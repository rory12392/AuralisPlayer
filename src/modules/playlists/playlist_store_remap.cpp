// PlaylistStore 的重新對應部分（T-52）：改項目的路徑與 missing、path_remaps 的讀寫；
// 整張 CUE 展開（T-56）。和 playlist_store.cpp 分開放是因為單檔行數上限（boundary.file_length）。
#include "playlist_store.h"

#include <auralis/library_query.h>
#include <auralis/module_database.h>

#include <QDateTime>

#include <algorithm>

namespace auralis::mod::pl {

namespace {

/// 交易裡發生的錯誤：work 回傳 false 讓交易還原，錯誤留在這裡給呼叫端回傳（同
/// playlist_store.cpp）。
struct TxError {
    std::optional<Error> error;
    bool fail(const Error& e) {
        error = e;
        return false;
    }
};

} // namespace

Result<int> PlaylistStore::updateItems(qint64 id, const QList<ItemUpdate>& updates) {
    TxError tx;
    int written = 0;
    const auto done = m_db.transaction([&] {
        for (const auto& update : updates) {
            const auto changed = m_db.execute(
                QStringLiteral("UPDATE playlist_items SET path = ?, path_key = ?, missing = ? "
                               "WHERE playlist_id = ? AND pos = ? AND path = ? AND subsong = ?"),
                {update.newPath, pathKey(update.newPath), update.missing ? 1 : 0, id, update.pos,
                 update.oldPath, static_cast<qint64>(update.subsong)});
            if (!changed) {
                return tx.fail(changed.error());
            }
            written += static_cast<int>(changed.value());
        }
        return written == 0 || touch(id) || tx.fail(Error{QStringLiteral("更新播放清單時間失敗")});
    });
    if (tx.error) {
        return *tx.error;
    }
    if (!done) {
        return done.error();
    }
    return written;
}

Result<int> PlaylistStore::expandItems(qint64 id, const QList<ItemExpansion>& expansions) {
    TxError tx;
    int expanded = 0;
    const auto done = m_db.transaction([&] {
        const auto current = items(id);
        if (!current) {
            return tx.fail(current.error());
        }
        QList<PlaylistItem> merged;
        for (const PlaylistItem& item : current.value()) {
            const auto match = std::find_if(
                expansions.begin(), expansions.end(), [&item](const ItemExpansion& expansion) {
                    return expansion.pos == item.pos && expansion.track.path == item.track.path &&
                           expansion.track.subsong == item.track.subsong;
                });
            if (match == expansions.end()) {
                merged.append(item);
                continue;
            }
            ++expanded;
            for (const TrackRef& track : match->tracks) {
                merged.append({0, track, match->missing});
            }
        }
        if (expanded == 0) {
            return true;
        }
        if (!rewriteItems(id, merged)) {
            return tx.fail(Error{QStringLiteral("寫入播放清單項目失敗")});
        }
        return touch(id) || tx.fail(Error{QStringLiteral("更新播放清單時間失敗")});
    });
    if (tx.error) {
        return *tx.error;
    }
    if (!done) {
        return done.error();
    }
    return expanded;
}

Result<QList<PathRemap>> PlaylistStore::remaps() const {
    const auto rows = m_db.query(QStringLiteral(
        "SELECT old_root, new_root FROM path_remaps ORDER BY created_at DESC, id DESC"));
    if (!rows) {
        return rows.error();
    }
    QList<PathRemap> result;
    result.reserve(rows.value().size());
    for (const auto& row : rows.value()) {
        result.append({row.at(0).toString(), row.at(1).toString()});
    }
    return result;
}

Result<bool> PlaylistStore::saveRemap(const PathRemap& remap) {
    TxError tx;
    const auto done = m_db.transaction([&] {
        const auto rows = m_db.query(QStringLiteral("SELECT id, old_root FROM path_remaps"));
        if (!rows) {
            return tx.fail(rows.error());
        }
        const QString key = pathKey(remap.oldRoot);
        for (const auto& row : rows.value()) {
            if (pathKey(row.at(1).toString()) == key) {
                const auto removed = m_db.execute(
                    QStringLiteral("DELETE FROM path_remaps WHERE id = ?"), {row.at(0)});
                if (!removed) {
                    return tx.fail(removed.error());
                }
            }
        }
        const auto inserted = m_db.execute(
            QStringLiteral(
                "INSERT INTO path_remaps(old_root, new_root, created_at) VALUES (?, ?, ?)"),
            {remap.oldRoot, remap.newRoot, QDateTime::currentMSecsSinceEpoch()});
        return inserted.hasValue() || tx.fail(inserted.error());
    });
    if (tx.error) {
        return *tx.error;
    }
    if (!done) {
        return done.error();
    }
    return true;
}

} // namespace auralis::mod::pl

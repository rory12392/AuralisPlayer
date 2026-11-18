#include "playlist_store.h"

#include <auralis/library_query.h>
#include <auralis/module_database.h>

#include <QDateTime>
#include <QSet>

#include <algorithm>

namespace auralis::mod::pl {

namespace {

qint64 now() {
    return QDateTime::currentMSecsSinceEpoch();
}

Error nameError() {
    return Error{QStringLiteral("播放清單名稱是空白的")};
}

/// 交易裡發生的錯誤：work 回傳 false 讓交易還原，錯誤留在這裡給呼叫端回傳。
struct TxError {
    std::optional<Error> error;
    bool fail(const Error& e) {
        error = e;
        return false;
    }
};

} // namespace

PlaylistStore::PlaylistStore(ModuleDatabase& db) : m_db(db) {}

Result<QList<PlaylistInfo>> PlaylistStore::list() const {
    const auto rows =
        m_db.query(QStringLiteral("SELECT p.id, p.name, (SELECT COUNT(*) FROM playlist_items i "
                                  "WHERE i.playlist_id = p.id) FROM playlists p "
                                  "ORDER BY p.sort_order, p.id"));
    if (!rows) {
        return rows.error();
    }
    QList<PlaylistInfo> result;
    result.reserve(rows.value().size());
    for (const auto& row : rows.value()) {
        result.append({row.at(0).toLongLong(), row.at(1).toString(), row.at(2).toInt()});
    }
    return result;
}

Result<std::optional<PlaylistInfo>> PlaylistStore::playlist(qint64 id) const {
    const auto rows = m_db.query(QStringLiteral("SELECT p.id, p.name, (SELECT COUNT(*) FROM "
                                                "playlist_items i WHERE i.playlist_id = p.id) "
                                                "FROM playlists p WHERE p.id = ?"),
                                 {id});
    if (!rows) {
        return rows.error();
    }
    if (rows.value().isEmpty()) {
        return std::optional<PlaylistInfo>();
    }
    const auto& row = rows.value().first();
    return std::optional<PlaylistInfo>(
        PlaylistInfo{row.at(0).toLongLong(), row.at(1).toString(), row.at(2).toInt()});
}

Result<QList<PlaylistItem>> PlaylistStore::items(qint64 id) const {
    const auto rows =
        m_db.query(QStringLiteral("SELECT pos, path, subsong, missing FROM "
                                  "playlist_items WHERE playlist_id = ? ORDER BY pos"),
                   {id});
    if (!rows) {
        return rows.error();
    }
    QList<PlaylistItem> result;
    result.reserve(rows.value().size());
    for (const auto& row : rows.value()) {
        result.append({row.at(0).toInt(),
                       {row.at(1).toString(), static_cast<quint32>(row.at(2).toLongLong())},
                       row.at(3).toBool()});
    }
    return result;
}

Result<qint64> PlaylistStore::create(const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return nameError();
    }
    TxError tx;
    qint64 id = 0;
    const auto done = m_db.transaction([&] {
        const auto order =
            m_db.query(QStringLiteral("SELECT COALESCE(MAX(sort_order), 0) + 1 FROM playlists"));
        if (!order) {
            return tx.fail(order.error());
        }
        const qint64 time = now();
        const auto inserted = m_db.execute(
            QStringLiteral("INSERT INTO playlists(name, sort_order, created_at, updated_at) "
                           "VALUES (?, ?, ?, ?)"),
            {trimmed, order.value().first().first(), time, time});
        if (!inserted) {
            return tx.fail(inserted.error());
        }
        const auto last = m_db.query(QStringLiteral("SELECT MAX(id) FROM playlists"));
        if (!last) {
            return tx.fail(last.error());
        }
        id = last.value().first().first().toLongLong();
        return true;
    });
    if (tx.error) {
        return *tx.error;
    }
    if (!done) {
        return done.error();
    }
    return id;
}

Result<PlaylistInfo> PlaylistStore::createImported(const QString& name,
                                                   const QList<PlaylistItem>& items) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return nameError();
    }
    TxError tx;
    PlaylistInfo created;
    const auto done = m_db.transaction([&] {
        const auto names = m_db.query(QStringLiteral("SELECT name FROM playlists"));
        if (!names) {
            return tx.fail(names.error());
        }
        QSet<QString> taken;
        for (const auto& row : names.value()) {
            taken.insert(row.first().toString());
        }
        QString unique = trimmed;
        for (int n = 2; taken.contains(unique); ++n) {
            unique = QStringLiteral("%1 (%2)").arg(trimmed).arg(n);
        }
        // create() 自己會開交易（介面沒說可以巢狀），所以新增那一列在這裡直接寫
        const auto order =
            m_db.query(QStringLiteral("SELECT COALESCE(MAX(sort_order), 0) + 1 FROM playlists"));
        if (!order) {
            return tx.fail(order.error());
        }
        const qint64 time = now();
        const auto inserted = m_db.execute(
            QStringLiteral("INSERT INTO playlists(name, sort_order, created_at, updated_at) "
                           "VALUES (?, ?, ?, ?)"),
            {unique, order.value().first().first(), time, time});
        if (!inserted) {
            return tx.fail(inserted.error());
        }
        const auto last = m_db.query(QStringLiteral("SELECT MAX(id) FROM playlists"));
        if (!last) {
            return tx.fail(last.error());
        }
        created = {last.value().first().first().toLongLong(), unique,
                   static_cast<int>(items.size())};
        if (!rewriteItems(created.id, items)) {
            return tx.fail(Error{QStringLiteral("寫入播放清單項目失敗")});
        }
        return true;
    });
    if (tx.error) {
        return *tx.error;
    }
    if (!done) {
        return done.error();
    }
    return created;
}

Result<bool> PlaylistStore::rename(qint64 id, const QString& name) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return nameError();
    }
    const auto changed =
        m_db.execute(QStringLiteral("UPDATE playlists SET name = ?, updated_at = ? WHERE id = ?"),
                     {trimmed, now(), id});
    if (!changed) {
        return changed.error();
    }
    return changed.value() > 0;
}

Result<bool> PlaylistStore::remove(qint64 id) {
    TxError tx;
    bool found = false;
    const auto done = m_db.transaction([&] {
        // 外鍵的 cascade 也會刪項目；這裡明寫，不依賴連線有沒有開外鍵
        const auto items =
            m_db.execute(QStringLiteral("DELETE FROM playlist_items WHERE playlist_id = ?"), {id});
        if (!items) {
            return tx.fail(items.error());
        }
        const auto removed =
            m_db.execute(QStringLiteral("DELETE FROM playlists WHERE id = ?"), {id});
        if (!removed) {
            return tx.fail(removed.error());
        }
        found = removed.value() > 0;
        return true;
    });
    if (tx.error) {
        return *tx.error;
    }
    if (!done) {
        return done.error();
    }
    return found;
}

bool PlaylistStore::touch(qint64 id) {
    return m_db
        .execute(QStringLiteral("UPDATE playlists SET updated_at = ? WHERE id = ?"), {now(), id})
        .hasValue();
}

bool PlaylistStore::rewriteItems(qint64 id, const QList<PlaylistItem>& items) {
    if (!m_db.execute(QStringLiteral("DELETE FROM playlist_items WHERE playlist_id = ?"), {id})) {
        return false;
    }
    int pos = 0;
    for (const auto& item : items) {
        const auto inserted = m_db.execute(
            QStringLiteral("INSERT INTO playlist_items(playlist_id, pos, path, path_key, subsong, "
                           "missing) VALUES (?, ?, ?, ?, ?, ?)"),
            {id, pos++, item.track.path, pathKey(item.track.path),
             static_cast<qint64>(item.track.subsong), item.missing ? 1 : 0});
        if (!inserted) {
            return false;
        }
    }
    return true;
}

Result<bool> PlaylistStore::addTracks(qint64 id, const QList<TrackRef>& tracks, int insertAt) {
    TxError tx;
    bool found = false;
    const auto done = m_db.transaction([&] {
        const auto info = playlist(id);
        if (!info) {
            return tx.fail(info.error());
        }
        if (!info.value()) {
            return true;
        }
        found = true;
        const int count = info.value()->trackCount;
        if (insertAt < 0 || insertAt >= count) {
            // 加在最後：只寫新的列
            int pos = count;
            for (const auto& track : tracks) {
                const auto inserted = m_db.execute(
                    QStringLiteral("INSERT INTO playlist_items(playlist_id, pos, path, path_key, "
                                   "subsong) VALUES (?, ?, ?, ?, ?)"),
                    {id, pos++, track.path, pathKey(track.path),
                     static_cast<qint64>(track.subsong)});
                if (!inserted) {
                    return tx.fail(inserted.error());
                }
            }
        } else {
            // 插在中間：pos 是主鍵的一部分，整份重寫比逐列往後挪簡單，而且在同一個交易裡
            auto current = items(id);
            if (!current) {
                return tx.fail(current.error());
            }
            QList<PlaylistItem> merged = current.value();
            QList<PlaylistItem> added;
            for (const auto& track : tracks) {
                added.append({0, track, false});
            }
            merged.insert(insertAt, added.size(), PlaylistItem{});
            std::copy(added.begin(), added.end(), merged.begin() + insertAt);
            if (!rewriteItems(id, merged)) {
                return tx.fail(Error{QStringLiteral("寫入播放清單項目失敗")});
            }
        }
        return touch(id) || tx.fail(Error{QStringLiteral("更新播放清單時間失敗")});
    });
    if (tx.error) {
        return *tx.error;
    }
    if (!done) {
        return done.error();
    }
    return found;
}

Result<bool> PlaylistStore::removeItems(qint64 id, const QList<int>& positions) {
    TxError tx;
    bool found = false;
    const auto done = m_db.transaction([&] {
        const auto info = playlist(id);
        if (!info) {
            return tx.fail(info.error());
        }
        if (!info.value()) {
            return true;
        }
        found = true;
        auto current = items(id);
        if (!current) {
            return tx.fail(current.error());
        }
        const QSet<int> drop(positions.begin(), positions.end());
        QList<PlaylistItem> kept;
        for (const auto& item : current.value()) {
            if (!drop.contains(item.pos)) {
                kept.append(item);
            }
        }
        if (kept.size() == current.value().size()) {
            return true;
        }
        if (!rewriteItems(id, kept)) {
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
    return found;
}

Result<bool> PlaylistStore::reorder(qint64 id, const QList<int>& order) {
    TxError tx;
    bool found = false;
    const auto done = m_db.transaction([&] {
        const auto info = playlist(id);
        if (!info) {
            return tx.fail(info.error());
        }
        if (!info.value()) {
            return true;
        }
        found = true;
        auto current = items(id);
        if (!current) {
            return tx.fail(current.error());
        }
        const auto& list = current.value();
        QList<int> sorted = order;
        std::sort(sorted.begin(), sorted.end());
        bool valid = sorted.size() == list.size();
        for (int i = 0; valid && i < sorted.size(); ++i) {
            valid = sorted.at(i) == i;
        }
        if (!valid) {
            return tx.fail(Error{QStringLiteral("重排的順序不是目前項目的一個排列")});
        }
        QList<PlaylistItem> reordered;
        reordered.reserve(list.size());
        for (const int from : order) {
            reordered.append(list.at(from));
        }
        if (!rewriteItems(id, reordered)) {
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
    return found;
}

} // namespace auralis::mod::pl

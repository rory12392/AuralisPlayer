#include "library/meta_backfill.h"

#include "library/cue_tracks.h"
#include "library/sqlite_connection.h"
#include "library/stream_bitrate.h"
#include "library/tag_reader.h"

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QVariant>

#include <optional>
#include <stdexcept>
#include <vector>

namespace auralis::core {

namespace {

[[noreturn]] void fail(const Error& error) {
    throw std::runtime_error(
        QStringLiteral("補齊曲目資訊失敗（%1）：%2").arg(error.call, error.message).toStdString());
}

QList<QVariantList> rows(SqliteConnection& db, const QString& sql,
                         const QVariantList& params = {}) {
    auto result = db.query(sql, params);
    if (!result) {
        fail(result.error());
    }
    return std::move(result).value();
}

void exec(SqliteConnection& db, const QString& sql, const QVariantList& params = {}) {
    if (const auto result = db.execute(sql, params); !result) {
        fail(result.error());
    }
}

struct Pending {
    qint64 id = 0;
    QString path; ///< files.path：整檔曲目是音訊檔本身，CUE 曲目是 .cue
    quint32 subsong = 0;
    QString codec;
    qint64 durationMs = 0;
    qint64 fileBytes = 0;
};

struct Filled {
    qint64 id = 0;
    std::optional<int> bitrate;
    std::optional<qint64> addedAt;
};

/// 同一批裡同一個 .cue 的各軌、同一個整軌檔只讀一次。
class CueCache {
public:
    /// 外層 nullopt：.cue 現在找不到它指向的音訊檔（外接碟離線），留到下次；
    /// 內層是位元率，音訊檔在但讀不到時是 nullopt（確實未知）。
    std::optional<std::optional<int>> bitrate(const QString& cuePath, quint32 subsong) {
        auto found = m_cues.constFind(cuePath);
        if (found == m_cues.cend()) {
            auto loaded = loadCue(cuePath);
            std::optional<LoadedCue> cue;
            if (loaded) {
                cue = std::move(loaded).value();
            }
            found = m_cues.insert(cuePath, std::move(cue));
        }
        if (!found.value()) {
            // 讀不出 .cue 指向的音訊檔：掃描時讀得出才有這些曲目，多半是音訊檔暫時不在
            return std::nullopt;
        }
        const LoadedCue& cue = *found.value();
        for (const auto& track : cue.sheet.tracks) {
            if (track.number == static_cast<int>(subsong)) {
                return audioBitrate(cue.audioPaths[static_cast<size_t>(track.fileIndex)]);
            }
        }
        return std::optional<int>();
    }

private:
    std::optional<int> audioBitrate(const QString& audioPath) {
        if (const auto it = m_audio.constFind(audioPath); it != m_audio.cend()) {
            return it.value();
        }
        std::optional<int> bitrate;
        try {
            // 和掃描同一條路：整軌檔的長度與大小（有損格式是 FFmpeg 的值）
            if (const auto tags = readTags(audioPath); tags) {
                bitrate = tags.value().bitrate;
            }
        } catch (...) {
            // 解析器的例外只讓這一首的位元率未知
        }
        m_audio.insert(audioPath, bitrate);
        return bitrate;
    }

    QHash<QString, std::optional<LoadedCue>> m_cues;
    QHash<QString, std::optional<int>> m_audio;
};

/// 一首的補值；檔案（CUE 曲目含它指向的音訊檔）不在時 nullopt（留到下次）。
std::optional<Filled> fill(const Pending& track, CueCache& cues) {
    const QFileInfo info(track.path);
    if (!info.exists()) {
        return std::nullopt;
    }
    Filled filled;
    filled.id = track.id;
    const QDateTime born = info.birthTime();
    if (born.isValid()) {
        filled.addedAt = born.toMSecsSinceEpoch();
    }
    if (isCuePath(track.path)) {
        const auto bitrate = cues.bitrate(track.path, track.subsong);
        if (!bitrate) {
            return std::nullopt;
        }
        filled.bitrate = *bitrate;
    } else {
        filled.bitrate =
            audioFileBitrateKbps(track.path, track.codec, track.fileBytes, track.durationMs);
    }
    return filled;
}

void write(SqliteConnection& db, const std::vector<Filled>& batch, BackfillResult& result) {
    if (batch.empty()) {
        return;
    }
    exec(db, QStringLiteral("BEGIN IMMEDIATE"));
    try {
        for (const auto& filled : batch) {
            // 期間被掃描刪掉的列更新不到，不算；added_at 已有值時（理論上不會）不覆寫
            const auto changed = db.execute(
                QStringLiteral("UPDATE tracks SET bitrate = ?, added_at = coalesce(added_at, ?), "
                               "meta_backfill = 0 WHERE id = ? AND meta_backfill = 1"),
                {filled.bitrate ? QVariant(*filled.bitrate) : QVariant(),
                 filled.addedAt ? QVariant(*filled.addedAt) : QVariant(), filled.id});
            if (!changed) {
                fail(changed.error());
            }
            result.updated += static_cast<int>(changed.value());
        }
        exec(db, QStringLiteral("COMMIT"));
    } catch (...) {
        (void)db.execute(QStringLiteral("ROLLBACK"));
        throw;
    }
}

} // namespace

int pendingBackfillCount(SqliteConnection& db) {
    return rows(db, QStringLiteral("SELECT count(*) FROM tracks WHERE meta_backfill = 1"))
        .first()
        .first()
        .toInt();
}

BackfillResult runMetaBackfill(SqliteConnection& db, const BackfillCallbacks& callbacks,
                               int batchSize) {
    BackfillResult result;
    result.total = pendingBackfillCount(db);
    int done = 0;
    qint64 lastId = 0; // 依 id 往前走：這次跳過的（檔案不在）不會在同一次裡再被選到
    while (result.total > 0) {
        const QString sql =
            QStringLiteral("SELECT t.id, f.path, t.subsong, t.codec, t.duration_ms, f.size "
                           "FROM tracks t JOIN files f ON f.id = t.file_id "
                           "WHERE t.meta_backfill = 1 AND t.id > ? ORDER BY t.id LIMIT ?");
        const auto found = rows(db, sql, {lastId, batchSize});
        if (found.isEmpty()) {
            break;
        }
        // 讀檔在交易外做，寫入的交易只有一批 UPDATE，不擋掃描
        std::vector<Filled> batch;
        CueCache cues;
        for (const auto& row : found) {
            if (callbacks.cancelled()) {
                result.cancelled = true;
                break;
            }
            Pending track;
            track.id = row[0].toLongLong();
            track.path = row[1].toString();
            track.subsong = static_cast<quint32>(row[2].toLongLong());
            track.codec = row[3].toString();
            track.durationMs = row[4].toLongLong();
            track.fileBytes = row[5].toLongLong();
            lastId = track.id;
            if (auto filled = fill(track, cues)) {
                batch.push_back(*filled);
            } else {
                ++result.deferred;
            }
            callbacks.progress(++done, result.total);
        }
        write(db, batch, result);
        if (result.cancelled) {
            break;
        }
    }
    return result;
}

} // namespace auralis::core

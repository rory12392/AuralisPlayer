#include "lyrics_cache.h"

#include <auralis/library_query.h>
#include <auralis/module_database.h>

#include <QDateTime>
#include <QFileInfo>
#include <QLoggingCategory>

namespace auralis::mod::lyr {

namespace {

Q_LOGGING_CATEGORY(lcCache, "auralis.lyr.cache")

QString statusName(LyricsDocument::Status status) {
    switch (status) {
    case LyricsDocument::Status::Found:
        return QStringLiteral("found");
    case LyricsDocument::Status::NotFound:
        return QStringLiteral("notfound");
    case LyricsDocument::Status::Error:
    case LyricsDocument::Status::Unavailable: // 線上暫時取不到：不是永久的沒找到（AC-16-2）
        break;
    }
    return QStringLiteral("error");
}

std::optional<LyricsSource> sourceFromName(const QString& name) {
    for (LyricsSource s : {LyricsSource::Lrc, LyricsSource::Embedded, LyricsSource::Lrclib}) {
        if (sourceName(s) == name) {
            return s;
        }
    }
    return std::nullopt;
}

/// 檔案的修改時間（毫秒）；檔案不存在時 std::nullopt。
std::optional<qint64> modifiedMs(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists()) {
        return std::nullopt;
    }
    return info.lastModified().toMSecsSinceEpoch();
}

/// 快取寫入後 .lrc 有沒有變化：多了 .lrc（快取不是 lrc）、.lrc 不見了、.lrc 比快取新。
bool lrcChanged(const LyricsTrack& track, LyricsSource cached, qint64 fetchedAt) {
    const QStringList candidates = lrcCandidates(track);
    // 有兩個候選（CUE 曲目）時，快取沒記是哪一個 .lrc：刪掉專屬的、或放進修改時間較舊的專屬
    // .lrc 都看不出來，所以來源是 lrc 時一律重讀（只是讀一個小檔）
    if (cached == LyricsSource::Lrc && candidates.size() > 1) {
        return true;
    }
    for (const QString& candidate : candidates) {
        if (const auto mtime = modifiedMs(candidate)) {
            return cached != LyricsSource::Lrc || *mtime > fetchedAt;
        }
    }
    return cached == LyricsSource::Lrc;
}

} // namespace

QString sourceName(LyricsSource source) {
    switch (source) {
    case LyricsSource::Lrc:
        return QStringLiteral("lrc");
    case LyricsSource::Embedded:
        return QStringLiteral("embedded");
    case LyricsSource::Lrclib:
        break;
    }
    return QStringLiteral("lrclib");
}

std::optional<LyricsDocument> LyricsCache::read(const LyricsTrack& track) const {
    const auto rows = m_database.query(
        QStringLiteral("SELECT source, synced, content, status, fetched_at FROM lyrics_cache "
                       "WHERE path_key = ? AND subsong = ?"),
        {pathKey(track.ref.path), static_cast<qint64>(track.ref.subsong)});
    if (!rows) {
        qCWarning(lcCache) << "讀不到歌詞快取" << rows.error().message;
        return std::nullopt;
    }
    if (rows.value().isEmpty()) {
        return std::nullopt;
    }
    const ModuleDatabase::Row& row = rows.value().first();
    const auto source = sourceFromName(row.value(0).toString());
    const QString status = row.value(3).toString();
    const qint64 fetchedAt = row.value(4).toLongLong();
    if (!source || status == QLatin1String("error")) {
        return std::nullopt;
    }
    // 曲目的檔案（CUE 曲目是整軌檔；取不到整軌檔時看 .cue 本身）
    const QString file = track.audioPath.isEmpty() ? track.ref.path : track.audioPath;
    const auto mtime = modifiedMs(file);
    if (!mtime || *mtime > fetchedAt || lrcChanged(track, *source, fetchedAt)) {
        return std::nullopt;
    }
    LyricsDocument doc;
    if (status == QLatin1String("found") && row.value(2).isNull()) {
        // 純音樂（T-67）：found 但沒有內容
        doc.status = LyricsDocument::Status::Found;
        doc.instrumental = true;
    } else if (status == QLatin1String("found")) {
        doc = parseLyrics(row.value(2).toString());
        if (doc.status != LyricsDocument::Status::Found) {
            return std::nullopt; // 內容壞掉：當成沒有快取，重找
        }
    }
    doc.source = *source;
    return doc;
}

bool LyricsCache::write(const LyricsTrack& track, const LyricsDocument& document) const {
    const bool found = document.status == LyricsDocument::Status::Found;
    // 純音樂存成 found＋content NULL（T-67）
    const bool hasContent = found && !document.instrumental;
    const auto written = m_database.execute(
        QStringLiteral("REPLACE INTO lyrics_cache "
                       "(path_key, subsong, source, synced, content, status, fetched_at) "
                       "VALUES (?, ?, ?, ?, ?, ?, ?)"),
        {pathKey(track.ref.path), static_cast<qint64>(track.ref.subsong),
         sourceName(document.source), hasContent && document.synced,
         hasContent ? QVariant(serializeLyrics(document)) : QVariant(), statusName(document.status),
         QDateTime::currentMSecsSinceEpoch()});
    if (!written) {
        qCWarning(lcCache) << "寫不進歌詞快取" << written.error().message;
        return false;
    }
    return true;
}

} // namespace auralis::mod::lyr

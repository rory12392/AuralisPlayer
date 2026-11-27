#include "library/library_writer.h"

#include "covers/cover_key.h"
#include "library/album_regroup.h"
#include "library/cue_scan.h"
#include "library/sqlite_connection.h"

#include <auralis/library_query.h>

#include <QDateTime>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <stdexcept>

namespace auralis::core {

namespace {

QVariant orNull(int value) {
    return value == 0 ? QVariant() : QVariant(value);
}

QVariant orNull(const std::optional<double>& value) {
    return value ? QVariant(*value) : QVariant();
}

[[noreturn]] void fail(const Error& error) {
    throw std::runtime_error(
        QStringLiteral("音樂庫寫入失敗（%1）：%2").arg(error.call, error.message).toStdString());
}

const QString kTrackColumns = QStringLiteral(
    "start_cdframe, end_cdframe, title, artist, album_artist, album, composer, genre, year, "
    "disc_no, track_no, duration_ms, "
    "codec, sample_rate, bits, channels, dsd_rate, rg_track_gain, rg_track_peak, rg_album_gain, "
    "rg_album_peak, has_embedded_lyrics, album_id, search_text, cover_key, bitrate");

} // namespace

bool isDiscFolderName(const QString& name) {
    static const QRegularExpression pattern(QStringLiteral("^(?:disc|disk|cd)[ ._-]*[0-9]+$"));
    return pattern.match(normalizeSearchText(name).trimmed()).hasMatch();
}

qint64 LibraryWriter::albumFolder(qint64 folderId) {
    if (const auto it = m_albumFolders.constFind(folderId); it != m_albumFolders.cend()) {
        return it.value();
    }
    const auto found =
        rows(QStringLiteral("SELECT name, parent_id FROM folders WHERE id = ?"), {folderId});
    qint64 albumFolder = folderId;
    if (!found.isEmpty() && !found.first()[1].isNull() &&
        isDiscFolderName(found.first()[0].toString())) {
        albumFolder = found.first()[1].toLongLong();
    }
    m_albumFolders.insert(folderId, albumFolder);
    return albumFolder;
}

void LibraryWriter::exec(const QString& sql, const QVariantList& params) {
    const auto result = m_db.execute(sql, params);
    if (!result) {
        fail(result.error());
    }
}

QList<QVariantList> LibraryWriter::rows(const QString& sql, const QVariantList& params) {
    auto result = m_db.query(sql, params);
    if (!result) {
        fail(result.error());
    }
    return std::move(result).value();
}

void LibraryWriter::begin() {
    exec(QStringLiteral("BEGIN IMMEDIATE"));
}

void LibraryWriter::commit() {
    exec(QStringLiteral("COMMIT"));
}

void LibraryWriter::rollback() noexcept {
    (void)m_db.execute(QStringLiteral("ROLLBACK"));
}

QHash<QString, qint64> LibraryWriter::knownFolders(qint64 rootId) {
    QHash<QString, qint64> folders;
    for (const auto& row :
         rows(QStringLiteral("SELECT path_key, id FROM folders WHERE root_id = ?"), {rootId})) {
        folders.insert(row[0].toString(), row[1].toLongLong());
    }
    return folders;
}

QHash<QString, KnownFile> LibraryWriter::knownFiles(qint64 rootId) {
    QHash<QString, KnownFile> files;
    const auto all =
        rows(QStringLiteral("SELECT f.path_key, f.id, f.size, f.mtime_ns, f.status = 'ok' "
                            "FROM files f JOIN folders d ON d.id = f.folder_id "
                            "WHERE d.root_id = ?"),
             {rootId});
    files.reserve(all.size());
    for (const auto& row : all) {
        files.insert(row[0].toString(), {row[1].toLongLong(), row[2].toLongLong(),
                                         row[3].toLongLong(), row[4].toBool()});
    }
    return files;
}

qint64 LibraryWriter::insertFolder(qint64 rootId, qint64 parentId, const EnumeratedFolder& folder) {
    const auto inserted =
        rows(QStringLiteral("INSERT INTO folders(root_id, parent_id, path, path_key, name) "
                            "VALUES (?, ?, ?, ?, ?) RETURNING id"),
             {rootId, parentId == 0 ? QVariant() : QVariant(parentId), folder.path,
              pathKey(folder.path), folder.name});
    return inserted.first().first().toLongLong();
}

qint64 LibraryWriter::upsertFile(qint64 folderId, const EnumeratedFile& file, const QString& status,
                                 const QString& error) {
    const auto result = rows(
        QStringLiteral(
            "INSERT INTO files(folder_id, path, path_key, size, mtime_ns, status, error, "
            "scanned_at) VALUES (?, ?, ?, ?, ?, ?, ?, ?) "
            "ON CONFLICT(path_key) DO UPDATE SET folder_id = excluded.folder_id, "
            "path = excluded.path, size = excluded.size, mtime_ns = excluded.mtime_ns, "
            "status = excluded.status, error = excluded.error, scanned_at = excluded.scanned_at "
            "RETURNING id"),
        {folderId, file.path, pathKey(file.path), file.size, file.mtimeNs, status,
         error.isEmpty() ? QVariant() : QVariant(error), QDateTime::currentMSecsSinceEpoch()});
    return result.first().first().toLongLong();
}

QVariant LibraryWriter::albumId(qint64 folderId, const TagData& tags) {
    if (tags.album.isEmpty()) {
        return {};
    }
    const QString albumArtist = tags.albumArtist.isEmpty() ? tags.artist : tags.albumArtist;
    folderId = albumFolder(folderId);
    const QString key =
        albumArtist + QChar(0x1f) + tags.album + QChar(0x1f) + QString::number(folderId);
    if (const auto it = m_albums.constFind(key); it != m_albums.cend()) {
        return it.value();
    }
    // 衝突時也要 DO UPDATE，RETURNING 才會交出既有那一列的 id
    const auto result =
        rows(QStringLiteral("INSERT INTO albums(album_artist, title, year, folder_id) "
                            "VALUES (?, ?, ?, ?) ON CONFLICT(album_artist, title, folder_id) "
                            "DO UPDATE SET year = coalesce(albums.year, excluded.year) "
                            "RETURNING id"),
             {albumArtist, tags.album, orNull(tags.year), folderId});
    const qint64 id = result.first().first().toLongLong();
    m_albums.insert(key, id);
    return id;
}

void LibraryWriter::writeTrack(qint64 folderId, const EnumeratedFile& file, const TagData& tags) {
    const qint64 fileId = upsertFile(folderId, file, QStringLiteral("ok"), {});
    QString cover;
    if (tags.hasEmbeddedCover) {
        const QString source = embeddedCoverSource(file.path);
        cover = coverKey(source, file.size, file.mtimeNs);
        insertCover(cover, source);
    }
    upsertTrack(fileId, folderId, 0, -1, -1, tags, QFileInfo(file.path).completeBaseName(), cover);
}

void LibraryWriter::insertCover(const QString& key, const QString& source) {
    exec(QStringLiteral("INSERT INTO covers(key, source, created_at) VALUES (?, ?, ?) "
                        "ON CONFLICT(key) DO NOTHING"),
         {key, source, QDateTime::currentMSecsSinceEpoch()});
}

void LibraryWriter::assignFolderCover(qint64 folderId, const EnumeratedFolder& folder) {
    QVariant key;
    if (!folder.coverImage.isEmpty()) {
        const QString source = fileCoverSource(folder.coverImage);
        key = coverKey(source, folder.coverSize, folder.coverMtimeNs);
        insertCover(key.toString(), source);
    }
    // 內嵌圖（e 開頭）優先；已經是同一個值的不寫，避免每次掃描都改動每一列
    exec(QStringLiteral("UPDATE tracks SET cover_key = ? "
                        "WHERE file_id IN (SELECT id FROM files WHERE folder_id = ?) "
                        "AND (cover_key IS NULL OR substr(cover_key, 1, 1) <> 'e') "
                        "AND cover_key IS NOT ?"),
         {key, folderId, key});
}

void LibraryWriter::writeCueTracks(qint64 folderId, const EnumeratedFile& cueFile,
                                   const std::vector<CueTrackRow>& rows) {
    const qint64 fileId = upsertFile(folderId, cueFile, QStringLiteral("ok"), {});
    QStringList kept;
    for (const auto& row : rows) {
        upsertTrack(fileId, folderId, row.subsong, row.startCdFrame, row.endCdFrame, row.tags,
                    row.tags.title);
        kept.append(QString::number(row.subsong));
    }
    // .cue 改過、軌變少時，刪掉已經不存在的軌
    exec(QStringLiteral("DELETE FROM tracks WHERE file_id = ? AND subsong NOT IN (%1)")
             .arg(kept.join(QLatin1Char(','))),
         {fileId});
}

void LibraryWriter::upsertTrack(qint64 fileId, qint64 folderId, quint32 subsong,
                                qint64 startCdFrame, qint64 endCdFrame, const TagData& tags,
                                const QString& fallbackTitle, const QString& coverKey) {
    const QString title = tags.title.isEmpty() ? fallbackTitle : tags.title;
    const QString searchText = normalizeSearchText(
        QStringList{title, tags.artist, tags.albumArtist, tags.album, tags.composer}.join(
            QLatin1Char(' ')));
    QString updates;
    for (const auto& column : kTrackColumns.split(QStringLiteral(", "))) {
        updates += (updates.isEmpty() ? QString() : QStringLiteral(", ")) + column +
                   QStringLiteral(" = excluded.") + column;
    }
    // 衝突時更新而不是刪掉重建：曲目 id 不變，搜尋索引只在 search_text 改變時重建。
    // added_at（第一次收錄的時間）與補值狀態只在新增時寫，重新掃描不覆寫（AC-10-5）
    exec(QStringLiteral("INSERT INTO tracks(file_id, subsong, %1, added_at, meta_backfill) "
                        "VALUES (?, ?, %2, ?, 0) ON CONFLICT(file_id, subsong) DO UPDATE SET %3")
             .arg(kTrackColumns, QStringLiteral("?, ").repeated(25) + QStringLiteral("?"), updates),
         {fileId,
          static_cast<qint64>(subsong),
          startCdFrame < 0 ? QVariant() : QVariant(startCdFrame),
          endCdFrame < 0 ? QVariant() : QVariant(endCdFrame),
          title,
          tags.artist,
          tags.albumArtist,
          tags.album,
          tags.composer,
          tags.genre,
          orNull(tags.year),
          orNull(tags.discNo),
          orNull(tags.trackNo),
          tags.durationMs,
          tags.codec,
          tags.sampleRate,
          tags.bits,
          tags.channels,
          tags.dsdRate,
          orNull(tags.rgTrackGain),
          orNull(tags.rgTrackPeak),
          orNull(tags.rgAlbumGain),
          orNull(tags.rgAlbumPeak),
          tags.hasEmbeddedLyrics,
          albumId(folderId, tags),
          searchText,
          coverKey.isEmpty() ? QVariant() : QVariant(coverKey),
          tags.bitrate ? QVariant(*tags.bitrate) : QVariant(),
          QDateTime::currentMSecsSinceEpoch()});
}

void LibraryWriter::writeUnreadable(qint64 folderId, const EnumeratedFile& file,
                                    const QString& error) {
    const qint64 fileId = upsertFile(folderId, file, QStringLiteral("unreadable"), error);
    exec(QStringLiteral("DELETE FROM tracks WHERE file_id = ?"), {fileId});
}

qint64 LibraryWriter::ensureRoot(const QString& path) {
    exec(QStringLiteral("INSERT INTO library_roots(path, path_key, added_at) VALUES (?, ?, ?) "
                        "ON CONFLICT(path_key) DO NOTHING"),
         {path, pathKey(path), QDateTime::currentMSecsSinceEpoch()});
    return rows(QStringLiteral("SELECT id FROM library_roots WHERE path_key = ?"), {pathKey(path)})
        .first()
        .first()
        .toLongLong();
}

QList<std::pair<qint64, QString>> LibraryWriter::roots() {
    QList<std::pair<qint64, QString>> result;
    for (const auto& row : rows(QStringLiteral("SELECT id, path FROM library_roots ORDER BY id"))) {
        result.append({row[0].toLongLong(), row[1].toString()});
    }
    return result;
}

void LibraryWriter::deleteRoot(const QString& path) {
    exec(QStringLiteral("DELETE FROM library_roots WHERE path_key = ?"), {pathKey(path)});
}

void LibraryWriter::deleteFile(qint64 id) {
    exec(QStringLiteral("DELETE FROM files WHERE id = ?"), {id});
}

void LibraryWriter::deleteFolder(qint64 id) {
    exec(QStringLiteral("DELETE FROM folders WHERE id = ?"), {id});
}

void LibraryWriter::refreshAlbums() {
    regroupAlbums(m_db);
    exec(QStringLiteral("UPDATE albums SET track_count = "
                        "(SELECT count(*) FROM tracks WHERE tracks.album_id = albums.id)"));
    exec(QStringLiteral("DELETE FROM albums WHERE track_count = 0"));
    exec(QStringLiteral("UPDATE albums SET cover_key = (SELECT t.cover_key FROM tracks t "
                        "WHERE t.album_id = albums.id AND t.cover_key IS NOT NULL "
                        "ORDER BY t.disc_no, t.track_no, t.id LIMIT 1)"));
    exec(QStringLiteral("DELETE FROM covers WHERE key NOT IN "
                        "(SELECT cover_key FROM tracks WHERE cover_key IS NOT NULL)"));
    m_albums.clear();
    m_albumFolders.clear();
}

qint64 LibraryWriter::insertReport(qint64 startedAt) {
    return rows(QStringLiteral("INSERT INTO scan_reports(started_at) VALUES (?) RETURNING id"),
                {startedAt})
        .first()
        .first()
        .toLongLong();
}

void LibraryWriter::finishReport(const ScanReport& report) {
    const auto toJson = [](const QList<ScanFailure>& list) {
        QJsonArray array;
        for (const auto& failure : list) {
            array.append(QJsonObject{{QStringLiteral("path"), failure.path},
                                     {QStringLiteral("error"), failure.error}});
        }
        return array;
    };
    const QJsonObject details{{QStringLiteral("cancelled"), report.cancelled},
                              {QStringLiteral("failures"), toJson(report.failures)},
                              {QStringLiteral("skippedLinks"), toJson(report.skippedLinks)}};
    exec(QStringLiteral("UPDATE scan_reports SET finished_at = ?, scanned = ?, added = ?, "
                        "updated = ?, removed = ?, failed = ?, details_json = ? WHERE id = ?"),
         {report.finishedAt, report.scanned, report.added, report.updated, report.removed,
          report.failed, QString::fromUtf8(QJsonDocument(details).toJson(QJsonDocument::Compact)),
          report.id});
}

ScanReport LibraryWriter::lastReport() {
    ScanReport report;
    const auto found = rows(QStringLiteral(
        "SELECT id, started_at, coalesce(finished_at, 0), scanned, added, updated, removed, "
        "failed, coalesce(details_json, '{}') FROM scan_reports ORDER BY id DESC LIMIT 1"));
    if (found.isEmpty()) {
        return report;
    }
    const auto& row = found.first();
    report.id = row[0].toLongLong();
    report.startedAt = row[1].toLongLong();
    report.finishedAt = row[2].toLongLong();
    report.scanned = row[3].toInt();
    report.added = row[4].toInt();
    report.updated = row[5].toInt();
    report.removed = row[6].toInt();
    report.failed = row[7].toInt();
    const QJsonObject details = QJsonDocument::fromJson(row[8].toString().toUtf8()).object();
    report.cancelled = details.value(QStringLiteral("cancelled")).toBool();
    const auto fromJson = [&details](const QString& key) {
        QList<ScanFailure> list;
        for (const auto& item : details.value(key).toArray()) {
            const QJsonObject failure = item.toObject();
            list.append({failure.value(QStringLiteral("path")).toString(),
                         failure.value(QStringLiteral("error")).toString()});
        }
        return list;
    };
    report.failures = fromJson(QStringLiteral("failures"));
    report.skippedLinks = fromJson(QStringLiteral("skippedLinks"));
    return report;
}

} // namespace auralis::core

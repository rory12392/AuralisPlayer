#include "library/library_query_impl.h"

#include "library/cue_tracks.h"
#include "library/sqlite_connection.h"
#include "library/tag_reader.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace auralis::core {

namespace {

const QString kTrackSelect = QStringLiteral(
    "SELECT t.id, f.path, t.subsong, t.album_id, f.folder_id, t.title, t.artist, "
    "t.album_artist, t.album, t.composer, t.genre, t.year, t.disc_no, t.track_no, "
    "t.duration_ms, t.codec, t.sample_rate, t.bits, t.channels, t.dsd_rate, t.rg_track_gain, "
    "t.rg_track_peak, t.rg_album_gain, t.rg_album_peak, t.has_embedded_lyrics, t.cover_key, "
    "t.start_cdframe, t.end_cdframe FROM tracks t JOIN files f ON f.id = t.file_id");

std::optional<double> optionalDouble(const QVariant& value) {
    return value.isValid() ? std::optional<double>(value.toDouble()) : std::nullopt;
}

TrackInfo toTrack(const QVariantList& row) {
    TrackInfo track;
    track.id = row[0].toLongLong();
    track.ref = {row[1].toString(), static_cast<quint32>(row[2].toLongLong())};
    track.albumId = row[3].toLongLong();
    track.folderId = row[4].toLongLong();
    track.title = row[5].toString();
    track.artist = row[6].toString();
    track.albumArtist = row[7].toString();
    track.album = row[8].toString();
    track.composer = row[9].toString();
    track.genre = row[10].toString();
    track.year = row[11].toInt();
    track.discNo = row[12].toInt();
    track.trackNo = row[13].toInt();
    track.durationMs = row[14].toLongLong();
    track.codec = row[15].toString();
    track.sampleRate = row[16].toInt();
    track.bits = row[17].toInt();
    track.channels = row[18].toInt();
    track.dsdRate = row[19].toInt();
    track.rgTrackGain = optionalDouble(row[20]);
    track.rgTrackPeak = optionalDouble(row[21]);
    track.rgAlbumGain = optionalDouble(row[22]);
    track.rgAlbumPeak = optionalDouble(row[23]);
    track.hasEmbeddedLyrics = row[24].toBool();
    track.coverKey = row[25].toString();
    track.startCdFrame = row[26].isValid() ? row[26].toLongLong() : -1;
    track.endCdFrame = row[27].isValid() ? row[27].toLongLong() : -1;
    return track;
}

/// 搜尋字串 → SQL 條件。3 個字元以上的詞用 FTS5 trigram，較短的用 LIKE（trigram 至少要 3 個字元）。
void addSearch(const QString& search, QString& where, QVariantList& params) {
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    QStringList ftsTerms;
    for (const QString& word : normalizeSearchText(search).split(spaces, Qt::SkipEmptyParts)) {
        if (word.size() >= 3) {
            // 以片語（雙引號）交給 FTS5，詞裡的雙引號要重複一次
            QString quoted = word;
            quoted.replace(QLatin1Char('"'), QLatin1String("\"\""));
            ftsTerms.append(QLatin1Char('"') + quoted + QLatin1Char('"'));
        } else {
            QString escaped = word;
            escaped.replace(QLatin1Char('\\'), QLatin1String("\\\\"))
                .replace(QLatin1Char('%'), QLatin1String("\\%"))
                .replace(QLatin1Char('_'), QLatin1String("\\_"));
            where += QStringLiteral(" AND t.search_text LIKE ? ESCAPE '\\'");
            params.append(QLatin1Char('%') + escaped + QLatin1Char('%'));
        }
    }
    if (!ftsTerms.isEmpty()) {
        where +=
            QStringLiteral(" AND t.id IN (SELECT rowid FROM tracks_fts WHERE tracks_fts MATCH ?)");
        params.append(ftsTerms.join(QLatin1Char(' ')));
    }
}

/// TrackFilter 的條件（不含 limit、offset）→ SQL 條件。tracks() 與 tracksForDragQuery() 共用。
void addFilter(const TrackFilter& filter, QString& where, QVariantList& params) {
    addSearch(filter.search, where, params);
    if (!filter.artist.isEmpty()) {
        where += QStringLiteral(" AND (t.artist = ? OR t.album_artist = ?)");
        params << filter.artist << filter.artist;
    }
    if (!filter.album.isEmpty()) {
        where += QStringLiteral(" AND t.album = ?");
        params << filter.album;
    }
    if (filter.year != 0) {
        where += QStringLiteral(" AND t.year = ?");
        params << filter.year;
    }
    if (!filter.codec.isEmpty()) {
        where += QStringLiteral(" AND t.codec = ?");
        params << filter.codec;
    }
    if (filter.sampleRate != 0) {
        where += QStringLiteral(" AND t.sample_rate = ?");
        params << filter.sampleRate;
    }
    if (filter.albumId != 0) {
        where += QStringLiteral(" AND t.album_id = ?");
        params << filter.albumId;
    }
    if (filter.folderId != 0) {
        where += QStringLiteral(" AND f.folder_id = ?");
        params << filter.folderId;
    }
}

const QString kDsd = QStringLiteral("DSD");
constexpr int kDsdBaseRate = 44100; // DSD64 = 64 × 44100

/// TrackDragPayload.query 讀出來的條件。解析與格式鍵的換算照抄 LIB 的 search_criteria
/// （parseDragQuery、toTrackFilter），核心不 include 模組。
struct DragQuery {
    TrackFilter filter;      ///< text 與五種篩選；folderId、albumId 不用
    bool impossible = false; ///< 格式的 DSD 倍率和取樣率互相矛盾：結果一定是 0 首
    qint64 folderId = 0;     ///< 含子資料夾
    QList<qint64> albumIds; ///< 依給的順序，已去掉重複
};

/// 「DSD64」→ 64；不是 DSD 倍率的鍵回傳 0。
int dsdMultiplier(const QString& key) {
    if (!key.startsWith(kDsd)) {
        return 0;
    }
    bool ok = false;
    const int multiplier = key.mid(kDsd.size()).toInt(&ok);
    return ok && multiplier > 0 ? multiplier : 0;
}

std::optional<DragQuery> parseDragQuery(const QString& json) {
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isObject() || document.object().value(QStringLiteral("lib")).toInt() != 1) {
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    DragQuery query;
    TrackFilter& filter = query.filter;
    filter.search = root.value(QStringLiteral("text")).toString();
    filter.artist = root.value(QStringLiteral("artist")).toString();
    filter.album = root.value(QStringLiteral("album")).toString();
    filter.year = root.value(QStringLiteral("year")).toInt();
    filter.sampleRate = root.value(QStringLiteral("sampleRate")).toInt();
    // 格式鍵：PCM 就是 codec；DSD64、DSD128… 同時限定 codec 與取樣率（DSD 的取樣率就是 DSD 率）
    const QString format = root.value(QStringLiteral("format")).toString();
    const int multiplier = dsdMultiplier(format);
    if (multiplier > 0) {
        const int dsdRate = multiplier * kDsdBaseRate;
        query.impossible = filter.sampleRate != 0 && filter.sampleRate != dsdRate;
        filter.codec = kDsd;
        filter.sampleRate = dsdRate;
    } else {
        filter.codec = format;
    }
    query.folderId = root.value(QStringLiteral("folderId")).toInteger();
    for (const QJsonValue& id : root.value(QStringLiteral("albumIds")).toArray()) {
        if (!query.albumIds.contains(id.toInteger())) {
            query.albumIds.append(id.toInteger());
        }
    }
    return query;
}

} // namespace

LibraryQueryImpl::LibraryQueryImpl(const QString& databasePath) : m_databasePath(databasePath) {}

LibraryQueryImpl::~LibraryQueryImpl() = default;

Result<SqliteConnection*> LibraryQueryImpl::connection() const {
    QMutexLocker lock(&m_mutex);
    if (!m_db) {
        if (m_databasePath.isEmpty()) {
            return Error{QStringLiteral("沒有音樂庫資料庫"), 0, QStringLiteral("LibraryQuery")};
        }
        auto opened = SqliteConnection::open(m_databasePath);
        if (!opened) {
            return opened.error();
        }
        m_db = std::move(opened).value();
    }
    return m_db.get();
}

Result<std::optional<TrackInfo>> LibraryQueryImpl::findByIdentity(const QString& path,
                                                                  quint32 subsong) const {
    const auto db = connection();
    if (!db) {
        return db.error();
    }
    const auto rows =
        db.value()->query(kTrackSelect + QStringLiteral(" WHERE f.path_key = ? AND t.subsong = ?"),
                          {pathKey(path), static_cast<qint64>(subsong)});
    if (!rows) {
        return rows.error();
    }
    if (rows.value().isEmpty()) {
        return std::optional<TrackInfo>();
    }
    return std::optional<TrackInfo>(toTrack(rows.value().first()));
}

Result<QList<TrackInfo>> LibraryQueryImpl::tracks(const TrackFilter& filter) const {
    const auto db = connection();
    if (!db) {
        return db.error();
    }
    QString where = QStringLiteral(" WHERE 1 = 1");
    QVariantList params;
    addFilter(filter, where, params);
    const QString order = QStringLiteral(
        " ORDER BY t.album_artist COLLATE NOCASE, t.album COLLATE NOCASE, t.disc_no, t.track_no, "
        "f.path_key, t.subsong LIMIT ? OFFSET ?");
    params << filter.limit << filter.offset;
    const auto rows = db.value()->query(kTrackSelect + where + order, params);
    if (!rows) {
        return rows.error();
    }
    QList<TrackInfo> result;
    result.reserve(rows.value().size());
    for (const auto& row : rows.value()) {
        result.append(toTrack(row));
    }
    return result;
}

Result<QList<AlbumInfo>> LibraryQueryImpl::albums() const {
    const auto db = connection();
    if (!db) {
        return db.error();
    }
    const auto rows = db.value()->query(
        QStringLiteral("SELECT id, album_artist, title, year, folder_id, cover_key, track_count "
                       "FROM albums ORDER BY album_artist COLLATE NOCASE, title COLLATE NOCASE"));
    if (!rows) {
        return rows.error();
    }
    QList<AlbumInfo> result;
    for (const auto& row : rows.value()) {
        result.append({row[0].toLongLong(), row[1].toString(), row[2].toString(), row[3].toInt(),
                       row[4].toLongLong(), row[5].toString(), row[6].toInt()});
    }
    return result;
}

Result<QStringList> LibraryQueryImpl::artists() const {
    const auto db = connection();
    if (!db) {
        return db.error();
    }
    const auto rows = db.value()->query(
        QStringLiteral("SELECT name FROM (SELECT artist AS name FROM tracks UNION SELECT "
                       "album_artist FROM tracks) "
                       "WHERE name IS NOT NULL AND name <> '' ORDER BY name COLLATE NOCASE"));
    if (!rows) {
        return rows.error();
    }
    QStringList result;
    for (const auto& row : rows.value()) {
        result.append(row[0].toString());
    }
    return result;
}

Result<QList<FolderInfo>> LibraryQueryImpl::folders(qint64 parentId) const {
    const auto db = connection();
    if (!db) {
        return db.error();
    }
    const QString sql =
        QStringLiteral("SELECT id, parent_id, path, name FROM folders WHERE ") +
        (parentId == 0 ? QStringLiteral("parent_id IS NULL") : QStringLiteral("parent_id = ?")) +
        QStringLiteral(" ORDER BY name COLLATE NOCASE");
    const auto rows =
        db.value()->query(sql, parentId == 0 ? QVariantList{} : QVariantList{parentId});
    if (!rows) {
        return rows.error();
    }
    QList<FolderInfo> result;
    for (const auto& row : rows.value()) {
        result.append(
            {row[0].toLongLong(), row[1].toLongLong(), row[2].toString(), row[3].toString()});
    }
    return result;
}

Result<QString> LibraryQueryImpl::readEmbeddedLyrics(const TrackRef& track) const {
    if (track.subsong != 0) {
        return QString();
    }
    return core::readEmbeddedLyrics(track.path);
}

Result<QList<TrackInfo>> LibraryQueryImpl::tracksForDragQuery(const QString& query) const {
    const std::optional<DragQuery> parsed = parseDragQuery(query);
    if (!parsed) {
        return Error{QStringLiteral("不是音樂庫的查詢條件"), 0, QStringLiteral("LibraryQuery")};
    }
    if (parsed->impossible) {
        return QList<TrackInfo>();
    }
    const auto db = connection();
    if (!db) {
        return db.error();
    }
    // 參數依 SQL 裡出現的順序：WITH、JOIN、WHERE
    QString sql = kTrackSelect;
    QVariantList params;
    if (parsed->folderId != 0) {
        // 含子資料夾：沿 parent_id 往下走整棵子樹
        sql.prepend(QStringLiteral("WITH RECURSIVE subtree(id) AS (SELECT ? UNION ALL SELECT c.id "
                                   "FROM folders c JOIN subtree s ON c.parent_id = s.id) "));
        params << parsed->folderId;
    }
    QString order = QStringLiteral(
        " ORDER BY t.album_artist COLLATE NOCASE, t.album COLLATE NOCASE, t.disc_no, t.track_no, "
        "f.path_key, t.subsong");
    if (!parsed->albumIds.isEmpty()) {
        // 專輯依給的順序：json_each 的 key 是陣列位置，一個參數就涵蓋任意張數
        QJsonArray ids;
        for (const qint64 id : parsed->albumIds) {
            ids.append(id);
        }
        sql += QStringLiteral(" JOIN json_each(?) a ON a.value = t.album_id");
        params << QString::fromUtf8(QJsonDocument(ids).toJson(QJsonDocument::Compact));
        order = QStringLiteral(" ORDER BY a.key, t.disc_no, t.track_no, f.path_key, t.subsong");
    }
    QString where = QStringLiteral(" WHERE 1 = 1");
    if (parsed->folderId != 0) {
        where += QStringLiteral(" AND f.folder_id IN (SELECT id FROM subtree)");
    }
    addFilter(parsed->filter, where, params);
    const auto rows = db.value()->query(sql + where + order, params);
    if (!rows) {
        return rows.error();
    }
    QList<TrackInfo> result;
    result.reserve(rows.value().size());
    for (const auto& row : rows.value()) {
        result.append(toTrack(row));
    }
    return result;
}

Result<QString> LibraryQueryImpl::audioFilePath(const TrackRef& ref) const {
    const auto found = findByIdentity(ref.path, ref.subsong);
    if (!found) {
        return found.error();
    }
    if (!found.value().has_value()) {
        return Error{
            QStringLiteral("音樂庫裡沒有這首曲目：%1（第 %2 軌）").arg(ref.path).arg(ref.subsong),
            0, QStringLiteral("audioFilePath")};
    }
    if (!isCuePath(ref.path)) {
        return ref.path;
    }
    // 和播放（openTrack）同一個 loadCue：FILE 的解析與找檔規則只有一份
    auto cue = loadCue(ref.path);
    if (!cue) {
        return cue.error();
    }
    for (const auto& cueTrack : cue.value().sheet.tracks) {
        if (cueTrack.number == static_cast<int>(ref.subsong)) {
            return cue.value().audioPaths[static_cast<size_t>(cueTrack.fileIndex)];
        }
    }
    return Error{QStringLiteral("CUE 裡沒有第 %1 軌").arg(ref.subsong), 0,
                 QStringLiteral("audioFilePath")};
}

void LibraryQueryImpl::notifyScanFinished() {
    emit scanFinished();
}

} // namespace auralis::core

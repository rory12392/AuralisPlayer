#include "library/album_regroup.h"

#include "library/library_writer.h"
#include "library/sqlite_connection.h"

#include <QHash>
#include <QList>
#include <QSet>
#include <QVariant>

#include <stdexcept>

namespace auralis::core {

namespace {

QList<QVariantList> query(SqliteConnection& db, const QString& sql,
                          const QVariantList& params = {}) {
    auto result = db.query(sql, params);
    if (!result) {
        throw std::runtime_error(QStringLiteral("專輯合併失敗（%1）：%2")
                                     .arg(result.error().call, result.error().message)
                                     .toStdString());
    }
    return std::move(result).value();
}

/// 上一層資料夾的同名專輯（沒有時建立），回傳 id。
qint64 albumAt(SqliteConnection& db, const QVariant& artist, const QVariant& title,
               const QVariant& year, const QVariant& folderId) {
    return query(db,
                 QStringLiteral("INSERT INTO albums(album_artist, title, year, folder_id) "
                                "VALUES (?, ?, ?, ?) ON CONFLICT(album_artist, title, folder_id) "
                                "DO UPDATE SET year = coalesce(albums.year, excluded.year) "
                                "RETURNING id"),
                 {artist, title, year, folderId})
        .first()
        .first()
        .toLongLong();
}

void moveTracks(SqliteConnection& db, qint64 from, qint64 to) {
    query(db, QStringLiteral("UPDATE tracks SET album_id = ? WHERE album_id = ?"), {to, from});
}

void regroupDiscNames(SqliteConnection& db) {
    const auto albums =
        query(db, QStringLiteral("SELECT a.id, a.album_artist, a.title, a.year, f.name, "
                                 "f.parent_id FROM albums a JOIN folders f ON f.id = a.folder_id "
                                 "WHERE f.parent_id IS NOT NULL"));
    for (const auto& album : albums) {
        if (isDiscFolderName(album[4].toString())) {
            moveTracks(db, album[0].toLongLong(),
                       albumAt(db, album[1], album[2], album[3], album[5]));
        }
    }
}

/// 一個資料夾裡同一張專輯（藝人＋專輯名）的曲目。資料夾是 Disc 資料夾時算上一層。
struct Unit {
    QVariant parent; ///< 這個資料夾的上一層；NULL＝音樂資料夾本身
    QVariant year;
    QSet<qint64> discs;
    QSet<qint64> albums; ///< 這些曲目目前所在的專輯
    bool missingDisc = false;
};

QString join(const QVariant& a, const QVariant& b, const QVariant& c) {
    return a.toString() + QChar(0x1f) + b.toString() + QChar(0x1f) + c.toString();
}

void regroupDiscNumbers(SqliteConnection& db) {
    // 同名專輯只有一列（已合併或本來就一張）時沒有事要做，只看有兩列以上的
    const auto tracks =
        query(db, QStringLiteral(
                      "WITH multi AS (SELECT album_artist, title FROM albums "
                      "GROUP BY album_artist, title HAVING count(*) >= 2) "
                      "SELECT a.album_artist, a.title, a.year, a.id, t.disc_no, f.id, f.name, "
                      "f.parent_id, p.parent_id FROM tracks t JOIN albums a ON a.id = t.album_id "
                      "JOIN multi m ON m.album_artist = a.album_artist AND m.title = a.title "
                      "JOIN files fi ON fi.id = t.file_id JOIN folders f ON f.id = fi.folder_id "
                      "LEFT JOIN folders p ON p.id = f.parent_id"));
    QHash<QString, Unit> units;               // 資料夾＋藝人＋專輯名
    QHash<QString, QList<QVariant>> children; // 上一層＋藝人＋專輯名 → 子資料夾
    for (const auto& row : tracks) {
        const bool disc = !row[7].isNull() && isDiscFolderName(row[6].toString());
        const QVariant folder = disc ? row[7] : row[5];
        const QString key = join(folder, row[0], row[1]);
        const bool added = !units.contains(key);
        Unit& unit = units[key];
        unit.parent = disc ? row[8] : row[7];
        unit.year = unit.year.isNull() ? row[2] : unit.year;
        unit.albums.insert(row[3].toLongLong());
        if (row[4].isNull()) {
            unit.missingDisc = true;
        } else {
            unit.discs.insert(row[4].toLongLong());
        }
        if (added && !unit.parent.isNull()) {
            children[join(unit.parent, row[0], row[1])].append(folder);
        }
    }
    for (auto it = children.cbegin(); it != children.cend(); ++it) {
        if (it->size() < 2) {
            continue;
        }
        const QStringList parts = it.key().split(QChar(0x1f));
        QList<const Unit*> group;
        for (const QVariant& folder : *it) {
            group.append(&units[join(folder, parts[1], parts[2])]);
        }
        // 曲目直接放在上一層資料夾的同名專輯，碟號也要不重疊
        if (const auto self = units.constFind(it.key()); self != units.cend()) {
            group.append(&self.value());
        }
        QSet<qint64> all;
        qsizetype count = 0;
        bool ok = true;
        QVariant year;
        for (const Unit* unit : group) {
            ok = ok && !unit->missingDisc;
            all.unite(unit->discs);
            count += unit->discs.size();
            year = year.isNull() ? unit->year : year;
        }
        if (!ok || all.size() != count) {
            continue; // 碟號重疊或缺碟號：不同版本，照舊分開
        }
        const qint64 target = albumAt(db, parts[1], parts[2], year, group.first()->parent);
        for (const Unit* unit : group) {
            for (const qint64 album : unit->albums) {
                if (album != target) {
                    moveTracks(db, album, target);
                }
            }
        }
    }
}

} // namespace

void regroupAlbums(SqliteConnection& db) {
    regroupDiscNames(db);
    regroupDiscNumbers(db);
}

} // namespace auralis::core

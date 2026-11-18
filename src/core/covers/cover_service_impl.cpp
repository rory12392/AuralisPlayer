#include "covers/cover_service_impl.h"

#include "covers/cover_image.h"
#include "covers/cover_key.h"
#include "library/sqlite_connection.h"
#include "library/tag_reader.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QLoggingCategory>
#include <QSaveFile>

Q_LOGGING_CATEGORY(lcCovers, "auralis.covers")

namespace auralis::core {

namespace {

constexpr int kThumbQuality = 90;

Error coverError(const QString& message) {
    return Error{message, 0, QStringLiteral("CoverService")};
}

/// 先寫暫存檔再改名：同一個封面被兩條執行緒同時建立時，讀的一方不會看到寫一半的檔案。
bool saveImage(const QImage& image, const QString& path, const char* format, int quality) {
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && image.save(&file, format, quality) && file.commit();
}

} // namespace

Result<QImage> decodeCoverSource(const QString& source, int maxSide) {
    bool embedded = false;
    const QString path = coverSourcePath(source, &embedded);
    if (path.isEmpty()) {
        return coverError(QStringLiteral("不認得的封面來源：%1").arg(source));
    }
    auto bytes = embedded ? readEmbeddedCover(path) : readUserFile(path, kMaxCoverFileBytes);
    if (!bytes) {
        return bytes.error();
    }
    if (bytes.value().isEmpty()) {
        return coverError(QStringLiteral("沒有內嵌圖：%1").arg(path));
    }
    return decodeCoverImage(bytes.value(), maxSide);
}

CoverServiceImpl::CoverServiceImpl(const QString& databasePath) : m_databasePath(databasePath) {}

CoverServiceImpl::~CoverServiceImpl() = default;

QString CoverServiceImpl::cacheDir() const {
    return m_databasePath.isEmpty()
               ? QString()
               : QFileInfo(m_databasePath).absolutePath() + QStringLiteral("/covers");
}

Result<SqliteConnection*> CoverServiceImpl::connection() {
    QMutexLocker lock(&m_mutex);
    if (!m_db) {
        if (m_databasePath.isEmpty()) {
            return coverError(QStringLiteral("沒有音樂庫資料庫"));
        }
        auto opened = SqliteConnection::open(m_databasePath);
        if (!opened) {
            return opened.error();
        }
        m_db = std::move(opened).value();
    }
    return m_db.get();
}

Result<CoverServiceImpl::Entry> CoverServiceImpl::lookup(const QString& key) {
    if (!isValidCoverKey(key)) {
        return coverError(QStringLiteral("封面 key 格式不對"));
    }
    const auto db = connection();
    if (!db) {
        return db.error();
    }
    const auto rows = db.value()->query(
        QStringLiteral("SELECT source, thumb_path, dominant_rgb FROM covers WHERE key = ?"), {key});
    if (!rows) {
        return rows.error();
    }
    if (rows.value().isEmpty()) {
        return coverError(QStringLiteral("沒有這個封面：%1").arg(key));
    }
    const auto& row = rows.value().first();
    Entry entry{row[0].toString(), row[1].toString(), std::nullopt};
    if (!row[2].isNull()) {
        entry.color = static_cast<quint32>(row[2].toLongLong());
    }
    return entry;
}

Result<QImage> CoverServiceImpl::thumbnail(const QString& key, Entry& entry) {
    const QString dir = cacheDir();
    if (!entry.thumbName.isEmpty()) {
        const QImage cached(dir + QLatin1Char('/') + entry.thumbName);
        if (!cached.isNull()) {
            if (!entry.color) {
                entry.color = dominantRgb(cached);
            }
            return cached;
        }
        // 快取被刪掉或損毀：重建
    }
    auto decoded = decodeCoverSource(entry.source, kThumbSize);
    if (!decoded) {
        return decoded.error();
    }
    const QImage image = std::move(decoded).value();
    entry.color = dominantRgb(image);
    entry.thumbName = key + QStringLiteral(".jpg");
    if (!QDir().mkpath(dir) ||
        !saveImage(image, dir + QLatin1Char('/') + entry.thumbName, "JPG", kThumbQuality)) {
        qCWarning(lcCovers).noquote() << "無法寫入封面縮圖：" << dir << entry.thumbName;
        entry.thumbName.clear();
    }
    const auto db = connection();
    const auto stored =
        db ? db.value()->execute(
                 QStringLiteral("UPDATE covers SET thumb_path = ?, dominant_rgb = ? WHERE key = ?"),
                 {entry.thumbName.isEmpty() ? QVariant() : QVariant(entry.thumbName),
                  static_cast<qint64>(*entry.color), key})
           : Result<qint64>(db.error());
    if (!stored) {
        qCWarning(lcCovers).noquote() << "無法記下封面縮圖：" << stored.error().message;
    }
    return image;
}

Result<QImage> CoverServiceImpl::image(const QString& key, int side) {
    auto entry = lookup(key);
    if (!entry) {
        return entry.error();
    }
    if (side <= kThumbSize) {
        return thumbnail(key, entry.value());
    }
    return decodeCoverSource(entry.value().source, side);
}

std::optional<quint32> CoverServiceImpl::dominantColor(const QString& key) {
    if (key.isEmpty()) {
        return std::nullopt;
    }
    auto entry = lookup(key);
    if (!entry) {
        qCWarning(lcCovers).noquote() << "封面主色：" << entry.error().message;
        return std::nullopt;
    }
    if (entry.value().color) {
        return entry.value().color;
    }
    const auto made = thumbnail(key, entry.value());
    if (!made) {
        qCWarning(lcCovers).noquote() << "封面主色：" << made.error().message;
        return std::nullopt;
    }
    return entry.value().color;
}

Result<QString> CoverServiceImpl::exportThumbnail(const QString& key, int size) {
    if (size < 1 || size > kMaxExportSize) {
        return coverError(QStringLiteral("輸出尺寸要在 1 到 %1 之間").arg(kMaxExportSize));
    }
    auto entry = lookup(key);
    if (!entry) {
        return entry.error();
    }
    const QString dir = cacheDir() + QStringLiteral("/export");
    const QString path = dir + QStringLiteral("/%1-%2.png").arg(key).arg(size);
    if (QFileInfo::exists(path)) {
        return QDir::toNativeSeparators(path);
    }
    auto image = size <= kThumbSize ? thumbnail(key, entry.value())
                                    : decodeCoverSource(entry.value().source, size);
    if (!image) {
        return image.error();
    }
    QImage scaled = image.value();
    if (scaled.width() > size || scaled.height() > size) {
        scaled = scaled.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (!QDir().mkpath(dir) || !saveImage(scaled, path, "PNG", -1)) {
        return coverError(QStringLiteral("無法寫入封面檔：%1").arg(QDir::toNativeSeparators(path)));
    }
    return QDir::toNativeSeparators(path);
}

} // namespace auralis::core

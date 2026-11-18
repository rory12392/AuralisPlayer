// 封面服務：掃描決定曲目與專輯的封面 key（內嵌圖優先、資料夾圖檔次之）、縮圖快取只寫在程式的
// 資料夾、主色、輸出檔（給系統媒體控制）、image://cover 影像提供者；使用者的檔案不被改動。
#include "covers/cover_image_provider.h"
#include "covers/cover_service_impl.h"
#include "library/library_query_impl.h"
#include "library/library_scanner.h"
#include "library/migrator.h"
#include "library/sqlite_connection.h"
#include "library_fixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>

#include <algorithm>

#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>

using namespace auralis;
using namespace auralis::core;
using namespace auralis::test;

namespace {

QByteArray encode(QColor color, const char* format, int side = 600) {
    QImage image(side, side, QImage::Format_RGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    REQUIRE(image.save(&buffer, format));
    return bytes;
}

/// 在剛產生的 FLAC 加一張內嵌圖（Front Cover）。
void embedPicture(const QString& path, const QByteArray& jpeg) {
    TagLib::FLAC::File file(path.toStdWString().c_str());
    REQUIRE(file.isValid());
    auto* picture = new TagLib::FLAC::Picture; // 交給 TagLib 擁有
    picture->setType(TagLib::FLAC::Picture::FrontCover);
    picture->setMimeType("image/jpeg");
    picture->setData(TagLib::ByteVector(jpeg.constData(), static_cast<unsigned>(jpeg.size())));
    file.addPicture(picture);
    REQUIRE(file.save());
}

bool near(quint32 color, int r, int g, int b) {
    return std::abs(static_cast<int>((color >> 16) & 0xFF) - r) <= 12 &&
           std::abs(static_cast<int>((color >> 8) & 0xFF) - g) <= 12 &&
           std::abs(static_cast<int>(color & 0xFF) - b) <= 12;
}

/// 資料夾裡每個檔案的 SHA-256 與修改時間（確認封面服務沒有改動使用者的檔案）。
QMap<QString, QString> snapshot(const QString& root) {
    QMap<QString, QString> files;
    QDirIterator it(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile file(path);
        REQUIRE(file.open(QIODevice::ReadOnly));
        files.insert(
            path,
            QString::fromLatin1(
                QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex()) +
                QString::number(QFileInfo(path).lastModified().toMSecsSinceEpoch()));
    }
    return files;
}

struct Library {
    QString dir;
    QString root;
    QString databasePath;
    std::unique_ptr<SqliteConnection> db;
    QList<LibraryRoot> roots;
    LibraryQueryImpl query{databasePath};
    CoverServiceImpl covers{databasePath};

    explicit Library(const QString& name) {
        dir = freshTestDir(name);
        root = dir + QStringLiteral("\\音樂");
        databasePath = dir + QStringLiteral("\\app\\library.db");
        QDir().mkpath(dir + QStringLiteral("\\app"));
        REQUIRE(migrateDatabase(databasePath).hasValue());
        auto opened = SqliteConnection::open(databasePath);
        REQUIRE(opened.hasValue());
        db = std::move(opened).value();
        LibraryWriter writer(*db);
        roots.append({writer.ensureRoot(root), root});
    }

    void scan() { scanLibrary(*db, roots, {}); }

    QString trackKey(const QString& relative) {
        const auto found = query.findByIdentity(root + QLatin1Char('\\') + relative, 0);
        REQUIRE(found.hasValue());
        REQUIRE(found.value().has_value());
        return found.value()->coverKey;
    }

    QString albumKey(const QString& title) {
        const auto albums = query.albums();
        REQUIRE(albums.hasValue());
        const auto it =
            std::find_if(albums.value().cbegin(), albums.value().cend(),
                         [&title](const AlbumInfo& album) { return album.title == title; });
        REQUIRE(it != albums.value().cend());
        return it->coverKey;
    }

    qint64 count(const QString& sql) {
        const auto rows = db->query(sql);
        REQUIRE(rows.hasValue());
        return rows.value().first().first().toLongLong();
    }
};

} // namespace

TEST_CASE("封面 key：內嵌圖優先、沒有時用資料夾圖檔，專輯用第一首有封面的曲目") {
    Library lib(QStringLiteral("t12-keys"));
    const QString a = lib.root + QStringLiteral("\\專輯 A");
    writeFlac(a + QStringLiteral("\\01.flac"),
              {{QStringLiteral("ALBUM"), QStringLiteral("A")},
               {QStringLiteral("TRACKNUMBER"), QStringLiteral("1")}});
    writeFlac(a + QStringLiteral("\\02.flac"),
              {{QStringLiteral("ALBUM"), QStringLiteral("A")},
               {QStringLiteral("TRACKNUMBER"), QStringLiteral("2")}});
    embedPicture(a + QStringLiteral("\\02.flac"), encode(Qt::green, "JPG"));
    writeBytes(a + QStringLiteral("\\cover.jpg"), encode(Qt::red, "JPG"));
    writeBytes(a + QStringLiteral("\\folder.png"), encode(Qt::blue, "PNG"));
    writeFlac(lib.root + QStringLiteral("\\沒有封面\\x.flac"),
              {{QStringLiteral("ALBUM"), QStringLiteral("B")}});
    lib.scan();

    const QString key1 = lib.trackKey(QStringLiteral("專輯 A\\01.flac"));
    const QString key2 = lib.trackKey(QStringLiteral("專輯 A\\02.flac"));
    CHECK(key1.startsWith(QLatin1Char('f'))); // cover.jpg 比 folder.png 優先
    CHECK(key2.startsWith(QLatin1Char('e')));
    CHECK(lib.trackKey(QStringLiteral("沒有封面\\x.flac")).isEmpty());
    CHECK(lib.albumKey(QStringLiteral("A")) == key1);
    CHECK(lib.albumKey(QStringLiteral("B")).isEmpty());
    CHECK(near(*lib.covers.dominantColor(key1), 255, 0, 0));
    CHECK(near(*lib.covers.dominantColor(key2), 0, 255, 0));

    SECTION("圖檔刪掉後重新掃描，改用下一個圖檔；都刪掉後沒有封面，用不到的封面列移除") {
        REQUIRE(QFile::remove(a + QStringLiteral("\\cover.jpg")));
        lib.scan();
        const QString blue = lib.trackKey(QStringLiteral("專輯 A\\01.flac"));
        CHECK(blue.startsWith(QLatin1Char('f')));
        CHECK(blue != key1);
        CHECK(near(*lib.covers.dominantColor(blue), 0, 0, 255));
        CHECK(lib.trackKey(QStringLiteral("專輯 A\\02.flac")) == key2); // 內嵌圖不被蓋掉

        REQUIRE(QFile::remove(a + QStringLiteral("\\folder.png")));
        lib.scan();
        CHECK(lib.trackKey(QStringLiteral("專輯 A\\01.flac")).isEmpty());
        CHECK(lib.albumKey(QStringLiteral("A")) == key2);
        CHECK(lib.count(QStringLiteral("SELECT count(*) FROM covers")) == 1);
    }
    SECTION("圖檔內容改了（大小或修改時間不同），key 跟著變，不沿用舊的縮圖與主色") {
        writeBytes(a + QStringLiteral("\\cover.jpg"), encode(Qt::yellow, "JPG", 300));
        lib.scan();
        const QString changed = lib.trackKey(QStringLiteral("專輯 A\\01.flac"));
        CHECK(changed != key1);
        CHECK(near(*lib.covers.dominantColor(changed), 255, 255, 0));
    }
}

TEST_CASE("縮圖快取與輸出檔只寫在程式的資料夾，使用者的音樂與圖檔不變") {
    Library lib(QStringLiteral("t12-cache"));
    const QString a = lib.root + QStringLiteral("\\A");
    writeFlac(a + QStringLiteral("\\01.flac"), {{QStringLiteral("ALBUM"), QStringLiteral("A")}});
    embedPicture(a + QStringLiteral("\\01.flac"), encode(Qt::magenta, "JPG", 1200));
    writeBytes(a + QStringLiteral("\\front.jpg"), encode(Qt::cyan, "JPG", 1200));
    const auto before = snapshot(lib.root);
    lib.scan();
    const QString key = lib.trackKey(QStringLiteral("A\\01.flac"));

    // 縮圖：長邊 kThumbSize，存成 covers\<key>.jpg，資料庫記下檔名與主色
    const auto thumb = lib.covers.image(key, 48);
    REQUIRE(thumb.hasValue());
    CHECK(thumb.value().width() == CoverServiceImpl::kThumbSize);
    CHECK(
        QFileInfo::exists(lib.covers.cacheDir() + QLatin1Char('/') + key + QStringLiteral(".jpg")));
    CHECK(lib.count(QStringLiteral("SELECT count(*) FROM covers WHERE thumb_path IS NOT NULL "
                                   "AND dominant_rgb IS NOT NULL AND key = '%1'")
                        .arg(key)) == 1);
    // 要求比縮圖大的（全螢幕）時從來源解碼
    const auto large = lib.covers.image(key, 800);
    REQUIRE(large.hasValue());
    CHECK(large.value().width() == 800);

    // 給系統媒體控制的檔案：PNG、長邊不超過要求、第二次直接回傳同一個檔
    const auto exported = lib.covers.exportThumbnail(key, 300);
    REQUIRE(exported.hasValue());
    CHECK(exported.value().startsWith(QDir::toNativeSeparators(lib.covers.cacheDir())));
    const QImage png(exported.value());
    CHECK(png.size() == QSize(300, 300));
    CHECK(lib.covers.exportThumbnail(key, 300).value() == exported.value());
    CHECK_FALSE(lib.covers.exportThumbnail(key, 0).hasValue());
    CHECK_FALSE(lib.covers.exportThumbnail(key, CoverService::kMaxExportSize + 1).hasValue());

    // 快取被刪掉時重建
    QDir(lib.covers.cacheDir()).removeRecursively();
    CHECK(lib.covers.image(key, 0).hasValue());

    // 使用者的資料夾：檔案內容與修改時間都沒變，也沒有多出檔案
    CHECK(snapshot(lib.root) == before);
}

TEST_CASE("損毀或讀不到的封面：沒有主色、輸出失敗、不會當掉；不存在或不合格的 key 也一樣") {
    Library lib(QStringLiteral("t12-broken"));
    const QString a = lib.root + QStringLiteral("\\A");
    writeFlac(a + QStringLiteral("\\01.flac"), {{QStringLiteral("ALBUM"), QStringLiteral("A")}});
    writeBytes(a + QStringLiteral("\\cover.jpg"), QByteArray("not a jpeg"));
    lib.scan();
    const QString key = lib.trackKey(QStringLiteral("A\\01.flac"));
    REQUIRE(key.startsWith(QLatin1Char('f')));

    CHECK_FALSE(lib.covers.dominantColor(key).has_value());
    CHECK_FALSE(lib.covers.image(key, 48).hasValue());
    CHECK_FALSE(lib.covers.exportThumbnail(key, 64).hasValue());
    CHECK_FALSE(lib.covers.dominantColor(QString()).has_value());
    CHECK_FALSE(
        lib.covers.dominantColor(QStringLiteral("f") + QString(40, QLatin1Char('0'))).has_value());
    CHECK_FALSE(lib.covers.image(QStringLiteral("../../library.db"), 48).hasValue());

    // 圖檔在掃描後被刪掉
    writeBytes(a + QStringLiteral("\\front.png"), encode(Qt::red, "PNG"));
    lib.scan();
    const QString front = lib.trackKey(QStringLiteral("A\\01.flac"));
    REQUIRE(QFile::remove(a + QStringLiteral("\\front.png")));
    CHECK_FALSE(lib.covers.image(front, 48).hasValue());

    // 沒有資料庫
    const QString none;
    CoverServiceImpl empty(none);
    CHECK_FALSE(empty.image(front, 48).hasValue());
    CHECK(empty.cacheDir().isEmpty());
}

TEST_CASE("AC-10-5 用的 image://cover：在背景執行緒讀圖，依 sourceSize 縮小；讀不到時回報錯誤") {
    Library lib(QStringLiteral("t12-provider"));
    writeFlac(lib.root + QStringLiteral("\\A\\01.flac"),
              {{QStringLiteral("ALBUM"), QStringLiteral("A")}});
    writeBytes(lib.root + QStringLiteral("\\A\\cover.png"), encode(Qt::red, "PNG"));
    lib.scan();
    const QString key = lib.trackKey(QStringLiteral("A\\01.flac"));
    CHECK(CoverService::imageUrl(key) == QStringLiteral("image://cover/") + key);
    CHECK(CoverService::imageUrl(QString()).isEmpty());

    // 材質工廠要有 QGuiApplication（ctest 每個測試案例是獨立的行程，這裡建立不影響別的測試）
    static int argc = 1;
    static char name[] = "auralis_core_tests";
    static char* argv[] = {name, nullptr};
    if (QCoreApplication::instance() == nullptr) {
        new QGuiApplication(argc, argv); // 整個測試行程共用，不釋放
    }
    REQUIRE(qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr);

    const auto load = [&lib](const QString& id, QSize size) {
        std::unique_ptr<QQuickImageResponse> response;
        {
            CoverImageProvider provider(lib.covers);
            response.reset(provider.requestImageResponse(id, size));
        } // 提供者解構時等執行緒池做完
        std::unique_ptr<QQuickTextureFactory> texture(response->textureFactory());
        return std::make_pair(texture ? texture->image() : QImage(), response->errorString());
    };
    const auto [image, error] = load(key, QSize(48, 48));
    CHECK(error.isEmpty());
    CHECK(image.size() == QSize(48, 48));
    CHECK(near(image.pixel(10, 10) & 0xFFFFFF, 255, 0, 0));

    const auto [missing, missingError] = load(QStringLiteral("..\\..\\x"), QSize(48, 48));
    CHECK(missing.isNull());
    CHECK_FALSE(missingError.isEmpty());
}

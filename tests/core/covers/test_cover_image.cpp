// 封面的 key 規則、解碼的防護（損毀、不是圖、超大圖不能讓程式當掉）與主色取樣。
#include "covers/cover_image.h"
#include "covers/cover_key.h"
#include "library_fixtures.h"

#include <catch2/catch_test_macros.hpp>

#include <QBuffer>
#include <QImage>
#include <QPainter>

using namespace auralis::core;
using namespace auralis::test;

namespace {

QByteArray encode(const QImage& image, const char* format) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    REQUIRE(image.save(&buffer, format));
    return bytes;
}

QImage solid(int width, int height, QColor color) {
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(color);
    return image;
}

/// 把 JPEG 的 SOF0 標頭改成宣稱 width×height（內容不變）：檔案很小，但宣稱的像素數極大。
QByteArray claimJpegSize(QByteArray jpeg, quint16 width, quint16 height) {
    const qsizetype sof = jpeg.indexOf(QByteArray("\xFF\xC0", 2));
    REQUIRE(sof > 0);
    jpeg[sof + 5] = static_cast<char>(height >> 8);
    jpeg[sof + 6] = static_cast<char>(height & 0xFF);
    jpeg[sof + 7] = static_cast<char>(width >> 8);
    jpeg[sof + 8] = static_cast<char>(width & 0xFF);
    return jpeg;
}

} // namespace

TEST_CASE("封面 key：內嵌與圖檔分得出來、檔案改了 key 就變、只接受合格的 key") {
    const QString embedded = embeddedCoverSource(QStringLiteral("D:\\音樂\\A\\01.flac"));
    const QString file = fileCoverSource(QStringLiteral("D:\\音樂\\A\\cover.jpg"));
    const QString e1 = coverKey(embedded, 100, 1);
    const QString f1 = coverKey(file, 100, 1);
    CHECK(e1.startsWith(QLatin1Char('e')));
    CHECK(f1.startsWith(QLatin1Char('f')));
    CHECK(isValidCoverKey(e1));
    CHECK(isValidCoverKey(f1));
    // 同一個檔案（路徑大小寫、分隔符不同也算）key 相同；大小或修改時間不同 key 就不同
    CHECK(coverKey(fileCoverSource(QStringLiteral("d:/音樂/a/COVER.JPG")), 100, 1) == f1);
    CHECK(coverKey(file, 101, 1) != f1);
    CHECK(coverKey(file, 100, 2) != f1);

    bool isEmbedded = false;
    CHECK(coverSourcePath(embedded, &isEmbedded) == QStringLiteral("D:\\音樂\\A\\01.flac"));
    CHECK(isEmbedded);
    CHECK(coverSourcePath(QStringLiteral("http://x"), &isEmbedded).isEmpty());

    // 影像網址的 key 會變成快取檔名：路徑字元、大寫、長度不對都拒絕
    CHECK_FALSE(isValidCoverKey(QString()));
    CHECK_FALSE(isValidCoverKey(QStringLiteral("..\\..\\windows\\win.ini")));
    CHECK_FALSE(isValidCoverKey(f1.toUpper()));
    CHECK_FALSE(isValidCoverKey(f1 + QLatin1Char('0')));
    CHECK_FALSE(isValidCoverKey(QLatin1Char('x') + f1.mid(1)));
    CHECK_FALSE(isValidCoverKey(QStringLiteral("f..\\..\\") + f1.mid(7)));
}

TEST_CASE("資料夾封面圖檔的優先順序：front、cover、folder、albumart，只收 jpg、jpeg、png") {
    CHECK(folderCoverRank(QStringLiteral("Front.JPG")) == 0);
    CHECK(folderCoverRank(QStringLiteral("cover.jpeg")) == 1);
    CHECK(folderCoverRank(QStringLiteral("folder.png")) == 2);
    CHECK(folderCoverRank(QStringLiteral("AlbumArt.jpg")) == 3);
    CHECK(folderCoverRank(QStringLiteral("cover.gif")) == -1);
    CHECK(folderCoverRank(QStringLiteral("back.jpg")) == -1);
    CHECK(folderCoverRank(QStringLiteral("cover")) == -1);
    CHECK(folderCoverRank(QStringLiteral(".jpg")) == -1);
}

TEST_CASE("解碼封面：JPEG 與 PNG、大圖縮到長邊上限、透明的部分疊在黑色上") {
    const auto jpeg = decodeCoverImage(encode(solid(2000, 1000, Qt::red), "JPG"), 400);
    REQUIRE(jpeg.hasValue());
    CHECK(jpeg.value().size() == QSize(400, 200));
    CHECK(jpeg.value().format() == QImage::Format_RGB32);

    // 小圖不放大
    const auto small = decodeCoverImage(encode(solid(50, 50, Qt::blue), "PNG"), 400);
    REQUIRE(small.hasValue());
    CHECK(small.value().size() == QSize(50, 50));

    QImage transparent(10, 10, QImage::Format_ARGB32);
    transparent.fill(Qt::transparent);
    const auto flattened = decodeCoverImage(encode(transparent, "PNG"), 0);
    REQUIRE(flattened.hasValue());
    CHECK(flattened.value().pixel(5, 5) == qRgb(0, 0, 0));
}

TEST_CASE("解碼封面：損毀、不是圖、空的、宣稱超大尺寸的圖回傳錯誤，不會當掉") {
    const QByteArray jpeg = encode(solid(64, 64, Qt::green), "JPG");
    CHECK_FALSE(decodeCoverImage(QByteArray(), 400).hasValue());
    CHECK_FALSE(decodeCoverImage(QByteArray("not an image at all"), 400).hasValue());
    // 只剩開頭一半：標頭讀得到，內容讀不完
    const auto truncated = decodeCoverImage(jpeg.left(jpeg.size() / 2), 400);
    if (truncated.hasValue()) {
        // libjpeg 對截斷的檔案會補灰色而不是失敗；重點是不當掉、尺寸正確
        CHECK(truncated.value().size() == QSize(64, 64));
    }
    // 宣稱 60000×60000（36 億像素）：解碼前就拒絕，不配置記憶體
    const auto huge = decodeCoverImage(claimJpegSize(jpeg, 60000, 60000), 400);
    REQUIRE_FALSE(huge.hasValue());
    CHECK(huge.error().message.contains(QStringLiteral("圖太大")));
    // 隨機位元組接在 PNG 標頭後面
    QByteArray garbage = encode(solid(8, 8, Qt::white), "PNG").left(16);
    garbage.append(QByteArray(4096, '\x5a'));
    CHECK_FALSE(decodeCoverImage(garbage, 400).hasValue());
}

TEST_CASE("讀使用者的圖檔：唯讀開啟、超過大小上限的不讀") {
    const QString dir = freshTestDir(QStringLiteral("t12-read"));
    const QString path = dir + QStringLiteral("\\cover.jpg");
    writeBytes(path, QByteArray(100, 'x'));
    const auto read = readUserFile(path, 1000);
    REQUIRE(read.hasValue());
    CHECK(read.value().size() == 100);
    const auto tooBig = readUserFile(path, 99);
    REQUIRE_FALSE(tooBig.hasValue());
    CHECK(tooBig.error().message.contains(QStringLiteral("太大")));
    CHECK_FALSE(readUserFile(dir + QStringLiteral("\\沒有這個檔.jpg"), 1000).hasValue());
}

TEST_CASE("AC-17-2 用的主色：取出現最多的顏色區間的平均色") {
    CHECK(dominantRgb(solid(40, 40, QColor(200, 30, 40))) == 0xC81E28u);

    // 七成深藍、三成紅：主色是深藍
    QImage mixed = solid(100, 100, QColor(20, 40, 160));
    QPainter painter(&mixed);
    painter.fillRect(0, 0, 100, 30, QColor(230, 20, 20));
    painter.end();
    const quint32 color = dominantRgb(mixed);
    const int r = static_cast<int>((color >> 16) & 0xFF);
    const int g = static_cast<int>((color >> 8) & 0xFF);
    const int b = static_cast<int>(color & 0xFF);
    CHECK(std::abs(r - 20) <= 8);
    CHECK(std::abs(g - 40) <= 8);
    CHECK(std::abs(b - 160) <= 8);
}

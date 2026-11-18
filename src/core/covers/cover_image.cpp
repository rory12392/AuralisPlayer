#include "covers/cover_image.h"

#include "library/file_enumerator.h"

#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>

#include <array>
#include <memory>

#include <windows.h>

namespace auralis::core {

namespace {

constexpr int kColorSampleSide = 32;
constexpr int kLevels = 16; ///< 每個顏色分幾階
constexpr int kLevelShift = 4;

} // namespace

Result<QByteArray> readUserFile(const QString& path, qint64 maxBytes) {
    // 使用者的圖檔一律唯讀、共用讀寫刪除：不擋別的程式，也不會改到它
    const std::wstring longPath =
        toLongPath(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
    HANDLE file = CreateFileW(longPath.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return hresultError(HRESULT_FROM_WIN32(GetLastError()), "CreateFileW");
    }
    const std::unique_ptr<void, decltype(&CloseHandle)> guard(file, &CloseHandle);
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        return hresultError(HRESULT_FROM_WIN32(GetLastError()), "GetFileSizeEx");
    }
    if (size.QuadPart > maxBytes) {
        return Error{QStringLiteral("圖檔太大（%1 位元組）").arg(size.QuadPart), 0,
                     QStringLiteral("readUserFile")};
    }
    QByteArray bytes(static_cast<qsizetype>(size.QuadPart), Qt::Uninitialized);
    qsizetype done = 0;
    while (done < bytes.size()) {
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + done, static_cast<DWORD>(bytes.size() - done), &read,
                      nullptr)) {
            return hresultError(HRESULT_FROM_WIN32(GetLastError()), "ReadFile");
        }
        if (read == 0) {
            break; // 讀的期間檔案變短
        }
        done += read;
    }
    bytes.truncate(done);
    return bytes;
}

Result<QImage> decodeCoverImage(const QByteArray& bytes, int maxSide) {
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const QSize size = reader.size();
    if (!size.isValid() || size.isEmpty()) {
        return Error{QStringLiteral("不是可以讀的圖檔（%1）").arg(reader.errorString()), 0,
                     QStringLiteral("QImageReader::size")};
    }
    if (static_cast<qint64>(size.width()) * size.height() > kMaxCoverPixels) {
        return Error{QStringLiteral("圖太大（%1×%2）").arg(size.width()).arg(size.height()), 0,
                     QStringLiteral("decodeCoverImage")};
    }
    const bool shrink = maxSide > 0 && (size.width() > maxSide || size.height() > maxSide);
    if (shrink) {
        // JPEG 可以直接以縮小的尺寸解碼，省記憶體與時間；不支援的格式下面再縮
        reader.setScaledSize(size.scaled(maxSide, maxSide, Qt::KeepAspectRatio));
    }
    QImage image = reader.read();
    if (image.isNull()) {
        return Error{QStringLiteral("圖檔損毀（%1）").arg(reader.errorString()), 0,
                     QStringLiteral("QImageReader::read")};
    }
    if (shrink && (image.width() > maxSide || image.height() > maxSide)) {
        image = image.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    if (image.hasAlphaChannel()) {
        // 預乘後丟掉 alpha，等於疊在黑色上
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    }
    return image.convertToFormat(QImage::Format_RGB32);
}

quint32 dominantRgb(const QImage& image) {
    const QImage sample = image
                              .scaled(kColorSampleSide, kColorSampleSide, Qt::IgnoreAspectRatio,
                                      Qt::SmoothTransformation)
                              .convertToFormat(QImage::Format_RGB32);
    struct Bin {
        int count = 0;
        qint64 r = 0, g = 0, b = 0;
    };
    std::array<Bin, kLevels * kLevels * kLevels> bins{};
    for (int y = 0; y < sample.height(); ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(sample.constScanLine(y));
        for (int x = 0; x < sample.width(); ++x) {
            const int r = qRed(line[x]);
            const int g = qGreen(line[x]);
            const int b = qBlue(line[x]);
            Bin& bin = bins[static_cast<size_t>(
                ((r >> kLevelShift) * kLevels + (g >> kLevelShift)) * kLevels +
                (b >> kLevelShift))];
            ++bin.count;
            bin.r += r;
            bin.g += g;
            bin.b += b;
        }
    }
    const Bin* best = &bins.front();
    for (const Bin& bin : bins) {
        if (bin.count > best->count) {
            best = &bin;
        }
    }
    if (best->count == 0) {
        return 0;
    }
    const auto average = [best](qint64 sum) {
        return static_cast<quint32>(sum / best->count);
    };
    return (average(best->r) << 16) | (average(best->g) << 8) | average(best->b);
}

} // namespace auralis::core

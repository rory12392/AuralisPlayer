#pragma once

#include <auralis/result.h>

#include <QByteArray>
#include <QImage>
#include <QString>

namespace auralis::core {

/// 封面圖檔大小的上限：超過的不讀（內嵌圖已經由 TagLib 讀進記憶體，只受像素上限限制）。
constexpr qint64 kMaxCoverFileBytes = 64LL * 1024 * 1024;
/// 解碼前先看圖的尺寸，像素數超過這個的不解碼（避免一個宣稱極大尺寸的小檔吃光記憶體）。
constexpr qint64 kMaxCoverPixels = 40LL * 1000 * 1000;

/// 唯讀開檔（GENERIC_READ，共用讀寫刪除）讀進整個檔案；超過 maxBytes 回傳 Error。
/// 執行緒：任何執行緒。
Result<QByteArray> readUserFile(const QString& path, qint64 maxBytes);

/// 解碼圖檔內容（JPEG、PNG；其他 Qt 認得的格式也接受），長邊縮到不超過 maxSide（不放大；
/// maxSide ≤ 0 時不縮）。結果一律是不透明的 RGB32：透明的部分疊在黑色上。
/// 不是圖、損毀、或尺寸超過 kMaxCoverPixels 時回傳 Error，不會讓程式當掉。
/// 執行緒：任何執行緒。
Result<QImage> decodeCoverImage(const QByteArray& bytes, int maxSide);

/// 主色：縮成 32×32 後，每個顏色依 RGB 各 16 階分區，取像素最多的區間的平均色（0xRRGGBB）。
/// image 不能是空的。執行緒：任何執行緒。
quint32 dominantRgb(const QImage& image);

} // namespace auralis::core

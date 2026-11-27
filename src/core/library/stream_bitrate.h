#pragma once

#include <QString>

#include <optional>

namespace auralis::core {

/// 有損格式（MP3、AAC、Opus、Vorbis）：位元率用 FFmpeg 回報的值，不用檔案大小換算。
bool isLossyCodec(const QString& codec);

/// FFmpeg 回報的位元率（kbps，四捨五入）：音訊串流的 bit_rate，沒有時用整個檔案的 bit_rate。
/// 只開本機檔案（協定與格式白名單同播放），唯讀。開不了或 FFmpeg 不知道時 nullopt。
/// 執行緒：任何執行緒，可以同時讀不同的檔案。
std::optional<int> ffmpegBitrateKbps(const QString& path);

/// 一個音訊檔的位元率（kbps，AC-10-5）：有損格式用 ffmpegBitrateKbps(path)；其他用
/// fileBytes × 8 ÷ durationMs（整數除法，和正在播放面板相同），長度或大小是 0 時 nullopt。
/// 執行緒：任何執行緒。
std::optional<int> audioFileBitrateKbps(const QString& path, const QString& codec, qint64 fileBytes,
                                        qint64 durationMs);

} // namespace auralis::core

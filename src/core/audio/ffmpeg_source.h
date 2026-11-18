#pragma once

#include <auralis/audio_source.h>

namespace auralis::core {

/// 核心的 FFmpeg 音源：FLAC、ALAC、WAV、AIFF、APE、WavPack、MP3、AAC、Opus、Vorbis。
/// 整數格式交出原始位元深度的整數樣本（不轉浮點、不重取樣、不抖動）；
/// 浮點來源（32 位元浮點 WAV、有損格式）交出 32 位元浮點。檔案以唯讀方式開啟。
/// 只開本機檔案：帶協定前綴的路徑直接拒絕，FFmpeg 只能用 file 協定與上述格式的 demuxer。
class FfmpegSourceFactory final : public ISourceFactory {
public:
    QString name() const override;
    QStringList extensions() const override;
    Result<std::unique_ptr<IAudioSource>> open(const QString& path) override;
};

} // namespace auralis::core

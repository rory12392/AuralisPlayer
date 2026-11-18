#pragma once

#include <auralis/audio_format.h>

#include <QByteArray>
#include <QString>

namespace auralis::test {

/// 產生可重現的隨機雜訊（滿刻度、每個樣本都不同），排列方式和 IAudioSource 交出的相同：
/// 交錯、整數左對齊在容器裡（低位元為 0）。無損格式的測試拿它逐位元比對。
QByteArray makeNoise(const AudioFormat& format, int frames, quint32 seed);

/// 用 FFmpeg 的編碼器把 pcm 編成檔案（測試時產生訊號，不把音檔放進 git）。
/// codec 是 FFmpeg 編碼器名稱（flac、pcm_s24le、alac…），muxer 是容器名稱（flac、wav、ipod…）。
/// 成功回傳空字串，失敗回傳原因。
QString encodeFile(const QString& path, const char* codec, const char* muxer,
                   const AudioFormat& format, const QByteArray& pcm);

} // namespace auralis::test

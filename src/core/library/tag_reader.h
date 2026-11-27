#pragma once

#include <auralis/result.h>

#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace auralis::core {

/// 一個音樂檔的標籤與音訊屬性。沒有的欄位是空字串或 0。
struct TagData {
    QString title, artist, albumArtist, album, composer, genre;
    int year = 0;
    int discNo = 0;
    int trackNo = 0;
    qint64 durationMs = 0;
    QString codec;
    int sampleRate = 0;
    int bits = 0;
    int channels = 0;
    int dsdRate = 0;
    std::optional<double> rgTrackGain, rgTrackPeak, rgAlbumGain, rgAlbumPeak;
    bool hasEmbeddedLyrics = false;
    bool hasEmbeddedCover = false; ///< 有內嵌圖（TagLib 的 PICTURE）
    /// 整個檔案的位元率（kbps，規則見 audioFileBitrateKbps）；未知時 nullopt。
    std::optional<int> bitrate;
};

/// 音樂庫收錄的副檔名（小寫、不含點）：核心 FFmpeg 音源的格式加 DSF、DFF。
const QStringList& audioExtensions();

/// 讀標籤與音訊屬性，檔案一律唯讀開啟。讀不到（權限、被鎖住、損毀）時回傳 Error，
/// message 是給掃描報告的原因。TagLib 不認得的格式（例：ADTS 的 .aac）回傳只有 codec 的
/// TagData，由呼叫端用檔名當曲名。
/// 執行緒：任何執行緒，可以同時讀不同的檔案。
Result<TagData> readTags(const QString& path);

/// 只能在 catch 區塊裡呼叫：把正在處理的例外轉成給掃描報告的 Error。TagLib 解析損毀的檔案時
/// 可能丟出例外（例：配置失敗），只讓那一個檔案讀不到，掃描繼續（AC-08-5）。
Error readTagsExceptionError();

/// 讀內嵌歌詞（TagLib 屬性對照的 LYRICS：ID3 USLT、Vorbis comment、APE、MP4 ©lyr）。
/// 帶 ID3v2 的格式有毫秒單位的 SYLT 歌詞框時優先回傳它，轉成 LRC 文字（`[mm:ss.xx]行`）。
/// 沒有歌詞時回傳空字串。執行緒：任何執行緒。
Result<QString> readEmbeddedLyrics(const QString& path);

/// 讀內嵌圖的原始位元組（TagLib 的 PICTURE：FLAC 的 PICTURE 區塊、ID3 APIC、MP4 covr 等）；
/// 有多張時取「Front Cover」，沒有時取第一張。沒有內嵌圖時回傳空的 QByteArray。
/// 檔案一律唯讀開啟。執行緒：任何執行緒。
Result<QByteArray> readEmbeddedCover(const QString& path);

/// 測試用的故障注入：設定後 readTags 開檔前先呼叫它（可以丟例外，模擬解析器丟出例外）。
/// 正式程式不設定；只能在沒有掃描進行時設定或清除（傳空的 std::function）。
void setReadTagsFaultForTest(std::function<void(const QString&)> fault);

} // namespace auralis::core

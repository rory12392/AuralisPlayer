#pragma once

#include <auralis/audio_source.h>
#include <auralis/result.h>
#include <auralis/track_ref.h>

#include <QList>
#include <QSet>
#include <QStringList>

#include <functional>
#include <memory>

namespace auralis {
class LibraryQuery;
class ModuleDatabase;
} // namespace auralis

namespace auralis::mod::out {

/// ReplayGain 2.0 的參考響度（LUFS）：增益 = 參考 − 量到的整合響度。
inline constexpr double kR128ReferenceLufs = -18.0;

/// 一首要算的曲目（由音樂庫的 TrackInfo 整理而來）。
struct R128Item {
    TrackRef ref;
    /// 0 表示沒有專輯（只算音軌增益）。
    qint64 albumId = 0;
};

/// 這次要算什麼：曲目依專輯排在一起；completeAlbums 是整張都在這次裡的專輯（才算專輯增益）。
struct R128Plan {
    QList<R128Item> items;
    QSet<qint64> completeAlbums;
    /// DSD 曲目不算（DSD 不經處理器鏈，播放時 ReplayGain 不作用），只計數給狀態說明。
    int skippedDsd = 0;
};

/// 選取的曲目（右鍵選單「計算 ReplayGain」）：查音樂庫取得專輯與 CUE 區段；音樂庫裡找不到的
/// 曲目略過。選取的曲目涵蓋整張專輯時才算那張的專輯增益。在工作執行緒呼叫。
Result<R128Plan> planR128(const LibraryQuery& library, const QList<TrackRef>& tracks);
/// 整個音樂庫（設定頁的「計算整個音樂庫的 ReplayGain」）：每張專輯都完整。在工作執行緒呼叫。
Result<R128Plan> planLibraryR128(const LibraryQuery& library);

/// 依曲目身分開啟音源（正式是核心的 SourceRegistry::openTrack：CUE 曲目回傳已切好這一軌範圍的
/// 音源；測試可以換成記憶體裡的音源）。
using R128Opener = std::function<Result<std::unique_ptr<IAudioSource>>(const TrackRef& track)>;

/// 執行結果（給狀態說明與通知）。
struct R128Summary {
    int computed = 0;
    int failed = 0;
    int skippedDsd = 0;
    int albums = 0;
    bool cancelled = false;
    /// 無法計算的曲目與原因（給日誌）。
    QStringList failures;
};

/// 量測與寫入（AC-06-3，規格 §1 情況 5）：每首算完就在一個交易裡寫進 rg_computed 的音軌增益與峰值
/// （不寫音樂檔）；整張專輯都算完才在一個交易裡寫專輯增益與峰值。中途取消時已算完的保留，
/// 正在算的那首與那張專輯的專輯值不寫。單首開不了或解碼失敗只略過那首。
/// cancelled 定期呼叫；progress 收到 0–1。在工作執行緒呼叫；資料庫錯誤丟 std::runtime_error
/// （由 JobRunner 的錯誤邊界標記這個工作失敗）。
R128Summary runR128(const R128Plan& plan, const R128Opener& open, ModuleDatabase& database,
                    const std::function<bool()>& cancelled,
                    const std::function<void(double)>& progress);

} // namespace auralis::mod::out

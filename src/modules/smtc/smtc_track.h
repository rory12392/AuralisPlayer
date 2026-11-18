#pragma once

#include <auralis/track_ref.h>

#include <QByteArray>
#include <QString>

#include <optional>

namespace auralis {
class CoverService;
class LibraryQuery;
struct TrackInfo;
} // namespace auralis

namespace auralis::mod::smtc {

/// 系統媒體控制顯示的曲目資訊（AC-18-2）。coverKey 空字串表示沒有封面。
struct SmtcTrackDisplay {
    QString title;
    QString artist;
    QString album;
    QString coverKey;
};

/// 系統音量面板的縮圖邊長（像素）。
inline constexpr int kThumbnailSize = 300;

/// 組出要顯示的曲目資訊：音樂庫有這首時用它的曲名、藝人、專輯、封面；沒有時曲名用不含副檔名的
/// 檔名、其他空白（和核心的佇列一樣）。任何執行緒。
SmtcTrackDisplay displayFor(const TrackRef& track, const std::optional<TrackInfo>& info);

/// 向音樂庫查這首再組出顯示的資訊；查詢失敗時記日誌並當成音樂庫沒有這首。
/// 執行緒：工作執行緒（會讀資料庫）。
SmtcTrackDisplay lookupDisplay(const LibraryQuery& library, const TrackRef& track);

/// 封面縮圖的 PNG 內容（kThumbnailSize）；key 空字串、輸出失敗或讀檔失敗時回傳空的
/// QByteArray（原因記日誌）。執行緒：工作執行緒（會讀檔與解碼）。
QByteArray readThumbnail(CoverService& covers, const QString& key);

/// 是不是同一首（路徑用 pathKey 比對，加上子曲目）。晚到的查詢結果和目前曲目不同時丟掉。
bool sameTrack(const TrackRef& a, const TrackRef& b);

/// 沒有封面時顯示的程式圖示（PNG）：DESIGN §7 的暫用幾何標記，深色底、1px 比例的圓線加中心
/// 實心點。第一次呼叫時畫好並留著。執行緒：UI 執行緒。
const QByteArray& appIconPng();

} // namespace auralis::mod::smtc

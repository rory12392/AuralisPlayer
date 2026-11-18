#pragma once

#include <auralis/result.h>

#include <QString>

#include <optional>

namespace auralis {

/// 封面服務。曲目與專輯的封面以 key 表示（LibraryQuery 的 TrackInfo、AlbumInfo 的 coverKey）。
/// key 由掃描決定：曲目有內嵌圖時用內嵌圖；沒有時用同資料夾的圖檔（front、cover、folder、
/// albumart，.jpg／.jpeg／.png，依這個順序）；都沒有時 coverKey 是空字串。專輯用它第一首有封面的
/// 曲目的 key。圖檔或音樂檔改了，重新掃描後 key 跟著變。
///
/// QML 顯示：Image 的 source 設成 imageUrl(key)（image://cover/<key>，非同步載入，依 sourceSize
/// 取對應大小）；沒有封面、讀不到或圖檔損毀時影像載入失敗，共用元件 Cover 會畫預設封面。
/// 使用者的圖檔與音樂檔一律唯讀；縮圖快取只寫在程式自己的資料夾。
/// 實例由核心擁有，模組經 ModuleHost::covers() 取得參考。
class CoverService {
public:
    virtual ~CoverService() = default;
    CoverService(const CoverService&) = delete;
    CoverService& operator=(const CoverService&) = delete;

    /// 影像提供者的名稱：網址是 image://cover/<key>。
    static constexpr const char* kImageProviderId = "cover";

    /// 給 QML Image 的網址；key 是空字串時回傳空字串（Cover 元件顯示預設封面）。任何執行緒。
    static QString imageUrl(const QString& key) {
        return key.isEmpty() ? QString() : QStringLiteral("image://cover/") + key;
    }

    /// 封面的主色（0xRRGGBB，出現最多的顏色區間的平均色；全螢幕背景再自己壓暗，AC-17-2）。
    /// 沒有這個封面、key 是空字串、或圖讀不到時回傳 std::nullopt（原因寫日誌）。
    /// 第一次會讀圖並建立縮圖，之後讀資料庫裡存的值。
    /// 執行緒：任何非 UI 執行緒（會讀檔與解碼），可以同時呼叫。
    virtual std::optional<quint32> dominantColor(const QString& key) = 0;

    /// 把封面縮成長邊不超過 size 像素（不放大）的 PNG 存檔，回傳完整路徑（給系統媒體控制顯示，
    /// AC-18-2）。檔案在程式自己的資料夾，呼叫端只讀；同 key、同 size 已輸出過時直接回傳那個檔。
    /// size 要在 1 到 kMaxExportSize 之間。沒有這個封面或讀不到時回傳 Error。
    /// 執行緒：任何非 UI 執行緒（會讀檔與解碼），可以同時呼叫。
    virtual Result<QString> exportThumbnail(const QString& key, int size) = 0;

    static constexpr int kMaxExportSize = 2048;

protected:
    CoverService() = default;
};

} // namespace auralis

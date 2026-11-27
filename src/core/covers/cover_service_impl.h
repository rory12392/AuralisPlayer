#pragma once

#include <auralis/cover_service.h>
#include <auralis/job_gate.h>

#include <QMutex>

#include <memory>
#include <optional>

// 這個標頭經 CoreServices 被核心各處 include，只有核心本身連結 Qt Gui，所以不 include <QImage>
class QImage;

namespace auralis::core {

class SqliteConnection;

/// CoverService 的實作。縮圖快取在資料庫旁的 covers 資料夾（AURALIS_DATA_DIR 或使用者的資料夾）：
/// 每個封面第一次被要求時讀來源、縮成長邊 kThumbSize 存成 <key>.jpg，主色同時算好，兩者寫回
/// covers 那一列（thumb_path 存檔名，相對於 covers 資料夾）。輸出檔在 covers\export。
/// 資料庫路徑在第一次使用時才讀（主程式在建立核心服務之後才設定路徑）；路徑是空字串時一律回傳錯誤。
/// 執行緒：除了建構與解構，任何非 UI 執行緒，可以同時呼叫。
class CoverServiceImpl final : public CoverService {
public:
    /// 縮圖的長邊。涵蓋清單縮圖、播放列與專輯牆（格寬最大 220，含 1.5 倍以上的螢幕縮放）；
    /// 要求更大的（全螢幕）時直接從來源解碼。
    static constexpr int kThumbSize = 400;

    /// databasePath 是核心服務的欄位，要活得比這個物件久。
    explicit CoverServiceImpl(const QString& databasePath);
    ~CoverServiceImpl() override;

    std::optional<quint32> dominantColor(const QString& key) override;
    Result<QString> exportThumbnail(const QString& key, int size) override;

    /// 影像提供者用：side ≤ kThumbSize（或 ≤ 0）時回傳快取的縮圖（長邊 kThumbSize 以內），
    /// 更大時從來源解碼成長邊不超過 side 的圖。key 不合格、不存在或讀不到時回傳 Error。
    Result<QImage> image(const QString& key, int side);

    /// 縮圖快取的資料夾；沒有資料庫時為空字串。
    QString cacheDir() const;

    /// 影像提供者的讀圖工作過這個閘門（工作開頭 Pass，沒過就不碰這個物件）。影像提供者由 QML 引擎
    /// 擁有，什麼時候解構不由核心決定，可能晚於核心服務；所以提供者與它的工作各持有一份閘門，
    /// 這個物件解構時關閉閘門並等進行中的讀圖做完，之後才關資料庫連線。任何執行緒。
    std::shared_ptr<JobGate> gate() const { return m_gate; }

    /// 關閉閘門：之後才開始的讀圖直接回傳錯誤，已經開始的最多等 timeoutMs 毫秒（逾時寫日誌）。
    /// 解構時會呼叫；可以重複呼叫。只在 UI 執行緒。
    void shutdown(int timeoutMs = kShutdownTimeoutMs);

    /// 和模組停止時等背景工作的上限相同。
    static constexpr int kShutdownTimeoutMs = 7000;

private:
    struct Entry {
        QString source;
        QString thumbName; ///< 空字串表示還沒有縮圖
        std::optional<quint32> color;
    };

    Result<Entry> lookup(const QString& key);
    Result<QImage> thumbnail(const QString& key, Entry& entry);
    Result<SqliteConnection*> connection();

    const QString& m_databasePath;
    QMutex m_mutex;
    std::unique_ptr<SqliteConnection> m_db;
    std::shared_ptr<JobGate> m_gate = std::make_shared<JobGate>();
};

/// 從來源（covers.source）讀圖並解碼，長邊不超過 maxSide。內嵌圖經 TagLib 唯讀讀出，
/// 圖檔唯讀開啟。執行緒：任何執行緒。
Result<QImage> decodeCoverSource(const QString& source, int maxSide);

} // namespace auralis::core

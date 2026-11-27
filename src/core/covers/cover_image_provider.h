#pragma once

#include <auralis/job_gate.h>

#include <QQuickAsyncImageProvider>
#include <QThreadPool>

#include <memory>

namespace auralis::core {

class CoverServiceImpl;

/// QML 的 image://cover/<key>：在自己的執行緒池讀縮圖或來源，不佔 UI 執行緒（AC-10-5 的延遲載入
/// 由畫面只對看得到的格子建立 Image 達成）。圖的長邊不超過 sourceSize 的長邊；沒有設 sourceSize
/// 時給快取的縮圖。key 不合格、沒有這個封面或讀不到時回報錯誤，Image 的狀態是 Error，
/// 共用元件 Cover 會畫預設封面。
/// 由 QQmlEngine::addImageProvider 註冊（引擎擁有它）。引擎與提供者可能比 covers 晚解構：
/// 每次讀圖先過 covers 的閘門，covers 解構後才開始的讀圖直接回報錯誤、不碰它（A6）。
class CoverImageProvider final : public QQuickAsyncImageProvider {
public:
    /// 同時讀幾個封面：讀檔與解碼都在這裡，太多會和掃描搶磁碟。
    static constexpr int kMaxThreads = 4;

    explicit CoverImageProvider(CoverServiceImpl& covers);
    ~CoverImageProvider() override;

    QQuickImageResponse* requestImageResponse(const QString& id,
                                              const QSize& requestedSize) override;

private:
    CoverServiceImpl& m_covers;
    std::shared_ptr<JobGate> m_gate;
    QThreadPool m_pool;
};

} // namespace auralis::core

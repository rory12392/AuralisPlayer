#include "covers/cover_image_provider.h"

#include "covers/cover_service_impl.h"

#include <QImage>
#include <QRunnable>

#include <algorithm>
#include <atomic>

namespace auralis::core {

namespace {

/// 一次讀圖的要求。引擎在 finished 之後刪除它，所以執行緒池不能自動刪（setAutoDelete(false)）。
class CoverResponse final : public QQuickImageResponse, public QRunnable {
public:
    CoverResponse(CoverServiceImpl& covers, std::shared_ptr<JobGate> gate, QString key,
                  const QSize& requestedSize)
        : m_covers(covers), m_gate(std::move(gate)), m_key(std::move(key)),
          m_requestedSize(requestedSize) {
        setAutoDelete(false);
    }

    void run() override {
        if (!m_cancelled) {
            load();
        }
        // finished 要在 response 所屬的執行緒（引擎的讀圖執行緒）送：引擎在那裡收到就刪掉它，
        // 在執行緒池直接送時，Qt 還在送訊號 response 就可能被刪（封面牆捲動中當掉）。
        // 排進那個執行緒的事件也保證引擎接上 finished 之後才送；之後不再碰 this
        QMetaObject::invokeMethod(this, [this] { emit finished(); }, Qt::QueuedConnection);
    }

    QQuickTextureFactory* textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override { return m_error; }

    void cancel() override { m_cancelled = true; }

private:
    void load() {
        // 引擎可能比封面服務晚解構：服務停止後不碰它（A6）。閘門只包住用到服務的部分，
        // 送出 finished 時已經離開，停止不必等引擎那一側
        const JobGate::Pass pass(*m_gate);
        if (!pass) {
            m_error = QStringLiteral("封面服務已停止");
            return;
        }
        const int side = std::max(m_requestedSize.width(), m_requestedSize.height());
        auto image = m_covers.image(m_key, side);
        if (image) {
            m_image = std::move(image).value();
            if (side > 0 && (m_image.width() > side || m_image.height() > side)) {
                m_image = m_image.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
        } else {
            m_error = image.error().message;
        }
    }

    CoverServiceImpl& m_covers;
    std::shared_ptr<JobGate> m_gate;
    QString m_key;
    QSize m_requestedSize;
    QImage m_image;
    QString m_error;
    std::atomic<bool> m_cancelled{false};
};

} // namespace

CoverImageProvider::CoverImageProvider(CoverServiceImpl& covers)
    : m_covers(covers), m_gate(covers.gate()) {
    m_pool.setMaxThreadCount(kMaxThreads);
}

CoverImageProvider::~CoverImageProvider() {
    m_pool.clear();
    m_pool.waitForDone();
}

QQuickImageResponse* CoverImageProvider::requestImageResponse(const QString& id,
                                                              const QSize& requestedSize) {
    auto* response = new CoverResponse(m_covers, m_gate, id, requestedSize);
    m_pool.start(response);
    return response;
}

} // namespace auralis::core

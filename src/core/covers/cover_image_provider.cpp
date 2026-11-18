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
    CoverResponse(CoverServiceImpl& covers, QString key, const QSize& requestedSize)
        : m_covers(covers), m_key(std::move(key)), m_requestedSize(requestedSize) {
        setAutoDelete(false);
    }

    void run() override {
        if (!m_cancelled) {
            const int side = std::max(m_requestedSize.width(), m_requestedSize.height());
            auto image = m_covers.image(m_key, side);
            if (image) {
                m_image = std::move(image).value();
                if (side > 0 && (m_image.width() > side || m_image.height() > side)) {
                    m_image =
                        m_image.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                }
            } else {
                m_error = image.error().message;
            }
        }
        emit finished();
    }

    QQuickTextureFactory* textureFactory() const override {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override { return m_error; }

    void cancel() override { m_cancelled = true; }

private:
    CoverServiceImpl& m_covers;
    QString m_key;
    QSize m_requestedSize;
    QImage m_image;
    QString m_error;
    std::atomic<bool> m_cancelled{false};
};

} // namespace

CoverImageProvider::CoverImageProvider(CoverServiceImpl& covers) : m_covers(covers) {
    m_pool.setMaxThreadCount(kMaxThreads);
}

CoverImageProvider::~CoverImageProvider() {
    m_pool.clear();
    m_pool.waitForDone();
}

QQuickImageResponse* CoverImageProvider::requestImageResponse(const QString& id,
                                                              const QSize& requestedSize) {
    auto* response = new CoverResponse(m_covers, id, requestedSize);
    m_pool.start(response);
    return response;
}

} // namespace auralis::core

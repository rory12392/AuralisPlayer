// image://cover 的讀圖回應和引擎讀圖執行緒之間的生命週期（P-61）
#include "covers/cover_image_provider.h"
#include "covers/cover_service_impl.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QGuiApplication>
#include <QQuickImageResponse>
#include <QThread>

#include <atomic>

using namespace auralis::core;

// 封面牆快速捲動時偶發存取違規（Qt 送訊號途中讀到已釋放的連線資料）：引擎在自己的讀圖執行緒
// 呼叫 requestImageResponse，response 屬於那個執行緒；引擎把 finished 接回那個執行緒，收到就
// deleteLater。finished 若在執行緒池直接送，Qt 還在送的時候 response 就可能被刪掉。
TEST_CASE("封面牆捲動中當掉（P-61）：image://cover 的 finished 在 response 所屬的執行緒送出") {
    static int argc = 1;
    static char name[] = "auralis_core_tests";
    static char* argv[] = {name, nullptr};
    if (QCoreApplication::instance() == nullptr) {
        new QGuiApplication(argc, argv); // 整個測試行程共用，不釋放
    }
    const QString noDatabase;
    CoverServiceImpl covers(noDatabase); // 沒有資料庫：每次讀圖很快就回報錯誤
    constexpr int kRequests = 200;
    std::atomic<int> wrongThread{0};
    std::atomic<int> deleted{0};
    QThread reader;
    reader.start();
    auto* engine = new QObject; // 模擬引擎在讀圖執行緒上的接收者
    engine->moveToThread(&reader);
    {
        CoverImageProvider provider(covers);
        for (int i = 0; i < kRequests; ++i) {
            QMetaObject::invokeMethod(engine, [&provider, &wrongThread, &deleted, engine] {
                QQuickImageResponse* response =
                    provider.requestImageResponse(QStringLiteral("x"), QSize(48, 48));
                QObject::connect(
                    response, &QQuickImageResponse::finished, response,
                    [&wrongThread, response] {
                        if (QThread::currentThread() != response->thread()) {
                            ++wrongThread;
                        }
                    },
                    Qt::DirectConnection);
                QObject::connect(response, &QQuickImageResponse::finished, engine,
                                 [response] { response->deleteLater(); });
                QObject::connect(response, &QObject::destroyed, [&deleted] { ++deleted; });
            });
        }
        // 太早送出（引擎還沒接上）的 finished 沒人收，response 就不會被刪：一樣算失敗
        const QDeadlineTimer deadline(10000);
        while (deleted.load() < kRequests && !deadline.hasExpired()) {
            QThread::msleep(5);
        }
    } // 提供者解構時等執行緒池做完
    QMetaObject::invokeMethod(engine, [engine] { delete engine; });
    reader.quit();
    reader.wait();
    CHECK(wrongThread.load() == 0);
    CHECK(deleted.load() == kRequests);
}

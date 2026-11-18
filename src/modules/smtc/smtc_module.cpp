// 系統媒體控制模組的進入點：主視窗註冊 SMTC，鍵盤媒體鍵在背景也能控制播放（AC-18-1、3），
// 播放狀態跟著顯示，曲名、藝人、專輯與封面顯示在系統音量面板（AC-18-2），程式結束時清除。
#include "smtc_mapping.h"
#include "smtc_refresh.h"
#include "smtc_session.h"

#include <auralis/job_runner.h>
#include <auralis/library_query.h>
#include <auralis/module.h>
#include <auralis/notifier.h>
#include <auralis/platform_window.h>
#include <auralis/playback_service.h>

#include <QCoreApplication>
#include <QObject>

#include <functional>
#include <memory>
#include <optional>

namespace auralis::mod::smtc {

namespace {

class SmtcModule final : public IModule {
public:
    ModuleInfo info() const override {
        return {QStringLiteral("SMTC"), QStringLiteral("系統媒體控制"), QStringLiteral("1")};
    }
    void start(ModuleHost& host) override {
        const quintptr hwnd = host.platformWindow().hwnd();
        if (hwnd == 0) {
            // 自我檢查與測試沒有主視窗：不註冊，模組照常啟動
            qCInfo(lcSmtc) << "沒有主視窗，不註冊系統媒體控制";
            return;
        }
        PlaybackService& playback = host.playback();
        // 按鈕的回呼在 WinRT 的執行緒：排進主執行緒的事件迴圈再呼叫播放控制；
        // 模組停止後才到的就丟掉
        auto alive = m_alive;
        auto onButton = [&playback, alive](SmtcButton button) {
            QCoreApplication* app = QCoreApplication::instance();
            if (app == nullptr) {
                return;
            }
            QMetaObject::invokeMethod(
                app,
                [&playback, alive, button] {
                    if (*alive) {
                        applyButton(button, playback);
                    }
                },
                Qt::QueuedConnection);
        };
        auto session = SmtcSession::create(hwnd, std::move(onButton));
        if (!session) {
            // 媒體鍵用不了不影響播放：記日誌、提示一次，模組照常啟動
            qCWarning(lcSmtc).noquote()
                << "系統媒體控制註冊失敗：" << session.error().call << session.error().message;
            host.notifier().notify(QCoreApplication::translate(
                "SmtcModule", "鍵盤媒體鍵無法使用：系統媒體控制註冊失敗。重新啟動程式可再試一次"));
            return;
        }
        m_session = std::move(session).value();
        m_session->setStatus(statusFor(playback.state()));
        // 每個連線都以 UI 執行緒的 context 物件接收：訊號從別的執行緒發出時（例：長時間播放測試
        // 在獨立執行緒播放）排回 UI 執行緒再處理（smtc_session.h 的執行緒約定）；
        // stop() 刪掉它，連線與還沒處理的排隊呼叫一起斷開
        m_context = std::make_unique<QObject>();
        QObject* context = m_context.get();
        QObject::connect(&playback, &PlaybackService::stateChanged, context,
                         [session = m_session.get()](PlaybackService::State state) {
                             session->setStatus(statusFor(state));
                         });
        // 曲目資訊與封面（AC-18-2）：換曲時更新；佇列裡的目前曲目被移掉時也要跟著換或清掉
        m_host = &host;
        m_refresher = std::make_unique<TrackRefresher>(
            host.library(), host.covers(), *m_session,
            [&jobs = host.jobs()](TrackRefresher::Job job) {
                jobs.submit(QStringLiteral("SMTC 曲目資訊"),
                            [job = std::move(job)](JobContext& jobContext) {
                                job([&jobContext] { return jobContext.isCancelled(); });
                            });
            },
            [](std::function<void()> function) {
                QCoreApplication* app = QCoreApplication::instance();
                if (app != nullptr) {
                    QMetaObject::invokeMethod(app, std::move(function), Qt::QueuedConnection);
                }
            });
        QObject::connect(&playback, &PlaybackService::trackChanged, context,
                         [this] { m_refresher->refresh(currentTrack()); });
        QObject::connect(&playback, &PlaybackService::queueChanged, context,
                         [this] { m_refresher->refresh(currentTrack()); });
        // 重新掃描後曲名或封面可能變了（或剛掃進音樂庫）：目前曲目重查一次
        QObject::connect(&host.library(), &LibraryQuery::scanFinished, context,
                         [this] { m_refresher->requery(currentTrack()); });
        m_refresher->refresh(currentTrack());
    }
    void stop() noexcept override {
        *m_alive = false;
        m_context.reset();
        // 還在路上的曲目資訊丟掉，之後才撤銷按鈕事件並清除顯示，系統的媒體控制不殘留「播放中」。
        // 核心在模組停止後先解構音樂庫與封面服務，最後才是 JobRunner，所以先等查詢工作結束
        if (m_refresher) {
            m_refresher->shutdown(kStopTimeoutMs);
        }
        m_refresher.reset();
        m_session.reset();
    }

private:
    /// 停止時等背景工作的上限（和其他模組相同）；逾時寫日誌、照常停止。
    static constexpr int kStopTimeoutMs = 7000;
    /// 佇列的目前曲目；沒有時 nullopt。UI 執行緒。
    std::optional<TrackRef> currentTrack() const {
        const auto entry = m_host->playback().currentEntry();
        if (!entry) {
            return std::nullopt;
        }
        return entry->track;
    }

    /// UI 執行緒讀寫：start() 之後為 true，stop() 設成 false。
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    std::unique_ptr<SmtcSession> m_session;
    /// 訊號連線的接收者（UI 執行緒建立）；刪掉時連線一起斷開。
    std::unique_ptr<QObject> m_context;
    std::unique_ptr<TrackRefresher> m_refresher;
    ModuleHost* m_host = nullptr;
};

} // namespace
} // namespace auralis::mod::smtc

AURALIS_REGISTER_MODULE(SMTC, [] {
    return std::unique_ptr<auralis::IModule>(std::make_unique<auralis::mod::smtc::SmtcModule>());
})

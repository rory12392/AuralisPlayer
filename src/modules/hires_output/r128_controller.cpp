#include "r128_controller.h"

#include <auralis/library_query.h>
#include <auralis/notifier.h>
#include <auralis/track_drag_payload.h>

#include <QJSEngine>
#include <QLoggingCategory>

#include <stdexcept>
#include <utility>

Q_LOGGING_CATEGORY(lcR128, "auralis.out.r128")

namespace auralis::mod::out {

namespace {

R128Controller* g_instance = nullptr;

/// 進度至少變這麼多才送一次（工作每 4096 個樣本框回報，不要每次都送訊號）。
constexpr double kProgressStep = 0.01;

} // namespace

QString r128SummaryText(const R128Summary& summary) {
    if (summary.computed == 0 && summary.failed == 0 && summary.skippedDsd == 0) {
        return R128Controller::tr("沒有可計算的曲目");
    }
    QStringList parts;
    if (summary.computed > 0 || summary.failed == 0) {
        parts.append(summary.albums > 0
                         ? R128Controller::tr("已計算 %1 首（含 %2 張專輯的專輯增益）")
                               .arg(summary.computed)
                               .arg(summary.albums)
                         : R128Controller::tr("已計算 %1 首").arg(summary.computed));
    }
    if (summary.failed > 0) {
        parts.append(R128Controller::tr("%1 首無法計算").arg(summary.failed));
    }
    if (summary.skippedDsd > 0) {
        parts.append(R128Controller::tr("DSD 曲目 %1 首不計算（DSD 播放時 ReplayGain 不作用）")
                         .arg(summary.skippedDsd));
    }
    return parts.join(R128Controller::tr("，"));
}

R128Controller::R128Controller(JobRunner& jobs, LibraryQuery& library, ModuleDatabase& database,
                               R128Opener open, Notifier* notifier, QObject* parent)
    : QObject(parent), m_jobs(jobs), m_library(library), m_database(database),
      m_open(std::move(open)), m_notifier(notifier) {
    connect(&m_jobs, &JobRunner::progress, this, &R128Controller::onProgress);
    connect(&m_jobs, &JobRunner::finished, this,
            [this](quint64 id, const QString&, JobRunner::Outcome outcome, const QString& error) {
                onFinished(id, outcome, error);
            });
}

R128Controller::~R128Controller() {
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

void R128Controller::computeLibrary() {
    start({}, true);
}

void R128Controller::computeTracks(const QString& payloadJson) {
    const auto payload = TrackDragPayload::fromJson(payloadJson.toUtf8());
    if (!payload || payload->tracks.isEmpty()) {
        return;
    }
    start(payload->tracks, false);
}

void R128Controller::cancel() {
    if (m_job != 0) {
        m_jobs.cancel(m_job);
    }
}

void R128Controller::shutdown(int timeoutMs) noexcept {
    // 模組停止後才到的結束訊號不再送通知
    m_notifier = nullptr;
    if (m_job != 0) {
        m_jobs.cancel(m_job);
    }
    // 工作每個區塊檢查一次取消，很快就會停；還在排隊的之後開始時直接結束，不碰音樂庫與資料庫
    if (!m_gate->close(timeoutMs)) {
        qCWarning(lcR128) << "停止時 ReplayGain 計算沒有在" << timeoutMs << "ms 內結束";
    }
}

void R128Controller::start(QList<TrackRef> tracks, bool wholeLibrary) {
    if (m_job != 0) {
        return;
    }
    auto shared = std::make_shared<Shared>();
    m_shared = shared;
    LibraryQuery& library = m_library;
    ModuleDatabase& database = m_database;
    m_job = m_jobs.submit(tr("計算 ReplayGain"), [shared, &library, &database, open = m_open,
                                                  tracks = std::move(tracks), gate = m_gate,
                                                  wholeLibrary](JobContext& context) {
        // 模組停止後才開始的工作不碰音樂庫與資料庫；不論成功、取消或丟例外，結束時都離開閘門
        // （shutdown 等的就是這個）
        const JobGate::Pass pass(*gate);
        if (!pass) {
            return;
        }
        auto plan = wholeLibrary ? planLibraryR128(library) : planR128(library, tracks);
        if (!plan) {
            throw std::runtime_error(plan.error().message.toStdString());
        }
        double reported = -1.0;
        shared->summary = runR128(
            plan.value(), open, database, [&context] { return context.isCancelled(); },
            [&context, &reported](double fraction) {
                if (fraction - reported >= kProgressStep || fraction >= 1.0) {
                    reported = fraction;
                    context.reportProgress(fraction);
                }
            });
    });
    m_progress = 0.0;
    setStatus(tr("正在計算 ReplayGain…"));
    emit changed();
}

void R128Controller::onProgress(quint64 id, double fraction) {
    if (id != m_job || m_job == 0) {
        return;
    }
    m_progress = fraction;
    emit changed();
}

void R128Controller::onFinished(quint64 id, JobRunner::Outcome outcome, const QString& error) {
    if (id != m_job || m_job == 0) {
        return;
    }
    m_job = 0;
    const R128Summary& summary = m_shared->summary;
    QString text;
    if (outcome == JobRunner::Outcome::Failed) {
        // 例外中斷時沒有完整的統計，只說已算完的會保留
        text = tr("ReplayGain 計算失敗（%1），已算完的曲目已保存").arg(error);
    } else if (outcome == JobRunner::Outcome::Cancelled || summary.cancelled) {
        text = tr("已取消 ReplayGain 計算，已算完的 %1 首已保存").arg(summary.computed);
    } else {
        text = r128SummaryText(summary);
    }
    // 畫面只寫「n 首無法計算」；是哪幾首、為什麼寫進日誌（取消時已記下的也寫）
    for (const QString& failure : summary.failures) {
        qCWarning(lcR128).noquote() << "ReplayGain 無法計算" << failure;
    }
    setStatus(text);
    emit changed();
    if (m_notifier != nullptr) {
        m_notifier->notify(text, outcome == JobRunner::Outcome::Failed ? Notifier::Kind::Error
                                                                       : Notifier::Kind::Info);
    }
}

void R128Controller::setStatus(const QString& status) {
    m_status = status;
}

void R128Controller::setInstance(R128Controller* instance) {
    g_instance = instance;
}

R128Controller* R128Controller::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "R128Controller", "模組要在載入區塊前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::mod::out

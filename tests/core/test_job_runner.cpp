// JobRunner：背景工作的錯誤邊界、取消與進度。
#include <auralis/job_runner.h>

#include <catch2/catch_test_macros.hpp>

#include <QMutex>
#include <QSemaphore>

#include <stdexcept>
#include <vector>

using auralis::JobContext;
using auralis::JobRunner;

namespace {

struct Recorder {
    QMutex mutex;
    std::vector<JobRunner::Outcome> outcomes;
    std::vector<QString> errors;
    std::vector<double> progress;

    void attach(JobRunner& runner) {
        // DirectConnection：測試沒有事件迴圈，直接在工作執行緒記錄
        QObject::connect(
            &runner, &JobRunner::finished, &runner,
            [this](quint64, const QString&, JobRunner::Outcome outcome, const QString& error) {
                QMutexLocker lock(&mutex);
                outcomes.push_back(outcome);
                errors.push_back(error);
            },
            Qt::DirectConnection);
        QObject::connect(
            &runner, &JobRunner::progress, &runner,
            [this](quint64, double fraction) {
                QMutexLocker lock(&mutex);
                progress.push_back(fraction);
            },
            Qt::DirectConnection);
    }
};

} // namespace

TEST_CASE("背景工作：成功時回報進度與成功") {
    JobRunner runner(2);
    Recorder recorder;
    recorder.attach(runner);
    const auto id = runner.submit(QStringLiteral("ok"), [](JobContext& context) {
        context.reportProgress(0.5);
        context.reportProgress(1.0);
    });
    CHECK(id == 1);
    REQUIRE(runner.waitForDone(5000));
    REQUIRE(recorder.outcomes.size() == 1);
    CHECK(recorder.outcomes[0] == JobRunner::Outcome::Succeeded);
    CHECK(recorder.progress == std::vector<double>{0.5, 1.0});
}

TEST_CASE("背景工作：例外被接住並標記失敗，不影響其他工作") {
    JobRunner runner(2);
    Recorder recorder;
    recorder.attach(runner);
    runner.submit(QStringLiteral("bad"),
                  [](JobContext&) { throw std::runtime_error("故障注入：工作失敗"); });
    REQUIRE(runner.waitForDone(5000));
    runner.submit(QStringLiteral("after"), [](JobContext&) {});
    REQUIRE(runner.waitForDone(5000));

    REQUIRE(recorder.outcomes.size() == 2);
    CHECK(recorder.outcomes[0] == JobRunner::Outcome::Failed);
    CHECK(recorder.errors[0].contains(QStringLiteral("故障注入")));
    CHECK(recorder.outcomes[1] == JobRunner::Outcome::Succeeded);
}

TEST_CASE("背景工作：取消後工作看到旗標並標記為已取消") {
    JobRunner runner(1);
    Recorder recorder;
    recorder.attach(runner);
    QSemaphore started;
    QSemaphore release;
    const auto id = runner.submit(QStringLiteral("long"), [&](JobContext& context) {
        started.release();
        release.acquire();
        CHECK(context.isCancelled());
    });
    started.acquire();
    runner.cancel(id);
    release.release();
    REQUIRE(runner.waitForDone(5000));
    REQUIRE(recorder.outcomes.size() == 1);
    CHECK(recorder.outcomes[0] == JobRunner::Outcome::Cancelled);
}

namespace {

/// 依序記下 submitted、progress、finished（狀態列靠這個順序知道有哪些工作）。
struct EventLog {
    QMutex mutex;
    std::vector<QString> events;

    void add(const QString& event) {
        QMutexLocker lock(&mutex);
        events.push_back(event);
    }

    void attach(JobRunner& runner) {
        QObject::connect(
            &runner, &JobRunner::submitted, &runner,
            [this](quint64 id, const QString& name) {
                add(QStringLiteral("submitted %1 %2").arg(id).arg(name));
            },
            Qt::DirectConnection);
        QObject::connect(
            &runner, &JobRunner::progress, &runner,
            [this](quint64 id, double fraction) {
                add(QStringLiteral("progress %1 %2").arg(id).arg(fraction));
            },
            Qt::DirectConnection);
        QObject::connect(
            &runner, &JobRunner::finished, &runner,
            [this](quint64 id, const QString& name, JobRunner::Outcome outcome, const QString&) {
                add(QStringLiteral("finished %1 %2 %3").arg(id).arg(name).arg(int(outcome)));
            },
            Qt::DirectConnection);
    }
};

} // namespace

TEST_CASE("T-93：submit 先發出 submitted（編號與名稱），再有進度與結束") {
    JobRunner runner(1);
    EventLog log;
    log.attach(runner);
    const auto id = runner.submit(QStringLiteral("查詢線上歌詞"), [](JobContext& context) {
        context.reportProgress(0.25);
        context.reportProgress(1.0);
    });
    // submitted 在 submit 返回前、在呼叫端的執行緒就發出
    REQUIRE(log.events.size() >= 1);
    CHECK(log.events[0] == QStringLiteral("submitted %1 查詢線上歌詞").arg(id));
    REQUIRE(runner.waitForDone(5000));
    CHECK(log.events ==
          std::vector<QString>{
              QStringLiteral("submitted 1 查詢線上歌詞"),
              QStringLiteral("progress 1 0.25"),
              QStringLiteral("progress 1 1"),
              QStringLiteral("finished 1 查詢線上歌詞 %1").arg(int(JobRunner::Outcome::Succeeded)),
          });
}

TEST_CASE("T-93：取消的工作也是 submitted 在前、以已取消結束") {
    JobRunner runner(1);
    EventLog log;
    log.attach(runner);
    QSemaphore started;
    QSemaphore release;
    const auto id = runner.submit(QStringLiteral("計算 ReplayGain"), [&](JobContext&) {
        started.release();
        release.acquire();
    });
    started.acquire();
    runner.cancel(id);
    release.release();
    REQUIRE(runner.waitForDone(5000));
    CHECK(
        log.events ==
        std::vector<QString>{
            QStringLiteral("submitted 1 計算 ReplayGain"),
            QStringLiteral("finished 1 計算 ReplayGain %1").arg(int(JobRunner::Outcome::Cancelled)),
        });
}

TEST_CASE("T-93：丟例外的工作也是 submitted 在前、以失敗結束") {
    JobRunner runner(1);
    EventLog log;
    log.attach(runner);
    runner.submit(QStringLiteral("壞掉"),
                  [](JobContext&) { throw std::runtime_error("故障注入：工作失敗"); });
    runner.submit(QStringLiteral("之後"), [](JobContext&) {});
    REQUIRE(runner.waitForDone(5000));
    REQUIRE(log.events.size() == 4);
    // 兩個 submitted 都在 submit 時發出，所以在任何 finished 之前
    CHECK(log.events[0] == QStringLiteral("submitted 1 壞掉"));
    CHECK(log.events[1] == QStringLiteral("submitted 2 之後"));
    CHECK(log.events[2] ==
          QStringLiteral("finished 1 壞掉 %1").arg(int(JobRunner::Outcome::Failed)));
    CHECK(log.events[3] ==
          QStringLiteral("finished 2 之後 %1").arg(int(JobRunner::Outcome::Succeeded)));
}

#include "lyrics_loader.h"

#include "lyrics_cache.h"

#include <auralis/job_runner.h>
#include <auralis/library_query.h>
#include <auralis/module_database.h>

#include <QLoggingCategory>
#include <QMetaObject>

#include <mutex>

namespace auralis::mod::lyr {

namespace {
Q_LOGGING_CATEGORY(lcLoader, "auralis.lyr.loader")
}

/// 工作執行緒送回結果的信箱：載入器刪掉時 receiver 設成 nullptr，之後的結果就不送。
/// 送出（排進事件佇列）與刪除都持有 mutex，所以不會送給刪到一半的物件；
/// 已經排進佇列、還沒處理的事件由 Qt 在物件刪除時一起丟掉。
struct LyricsLoader::Mailbox {
    std::mutex mutex;
    LyricsLoader* receiver = nullptr;
};

LyricsLoader::LyricsLoader(JobRunner& jobs, const LibraryQuery& library, ModuleDatabase& database,
                           QObject* parent)
    : QObject(parent), m_jobs(jobs), m_library(library), m_database(database),
      m_finder(std::make_shared<LyricsFinder>()), m_mailbox(std::make_shared<Mailbox>()),
      m_gate(std::make_shared<JobGate>()) {
    m_mailbox->receiver = this;
    // AC-15-1 的前三段；LRCLIB（T-67）由 appendSource 接在最後
    m_finder->append(std::make_unique<LrcFileStep>());
    m_finder->append(std::make_unique<EmbeddedStep>(m_library));
    connect(&m_jobs, &JobRunner::finished, this,
            [this](quint64 id, const QString&, JobRunner::Outcome outcome, const QString& error) {
                if (id != m_job || outcome != JobRunner::Outcome::Failed) {
                    return; // 成功的結果由 deliver 帶回；被取消的是舊的工作
                }
                qCWarning(lcLoader) << "找歌詞失敗" << m_track.path << error;
                LyricsDocument failed;
                failed.status = LyricsDocument::Status::Error;
                finish(failed);
            });
}

LyricsLoader::~LyricsLoader() {
    {
        std::lock_guard lock(m_mailbox->mutex);
        m_mailbox->receiver = nullptr;
    }
    if (m_job != 0) {
        m_jobs.cancel(m_job);
    }
}

void LyricsLoader::appendSource(std::unique_ptr<LyricsStep> step) {
    m_finder->append(std::move(step));
}

void LyricsLoader::load(const TrackRef& track) {
    if (m_job != 0) {
        m_jobs.cancel(m_job);
    }
    const quint64 generation = ++m_generation;
    m_track = track;
    m_document = LyricsDocument();
    std::shared_ptr<const LyricsFinder> finder = m_finder;
    std::shared_ptr<Mailbox> mailbox = m_mailbox;
    std::shared_ptr<JobGate> gate = m_gate;
    const LibraryQuery& library = m_library;
    ModuleDatabase& database = m_database;
    // 工作名稱顯示在狀態列（T-93），寫成給人看的動作；LRCLIB 是同一個工作裡的最後一段
    m_job = m_jobs.submit(tr("查詢歌詞"), [=, &library, &database](JobContext& context) {
        // 模組停止後才開始的工作不碰音樂庫與資料庫（它們可能已經解構，P-LYR-10）
        const JobGate::Pass pass(*gate);
        if (!pass) {
            return;
        }
        const LyricsTrack resolved = resolveTrack(library, track);
        if (context.isCancelled()) {
            return;
        }
        const LyricsCache cache(database);
        std::optional<LyricsDocument> doc = cache.read(resolved);
        // 線上抓取關閉時記下的 notfound，打開後要重查（T-67）
        if (doc && doc->status == LyricsDocument::Status::NotFound &&
            doc->source != finder->lastActiveKind()) {
            doc.reset();
        }
        if (!doc) {
            doc = finder->find(resolved, [&context] { return context.isCancelled(); });
            if (!doc || context.isCancelled()) {
                return; // 換歌了：結果可能不完整，不寫快取也不送回
            }
            if (doc->cacheable) {
                cache.write(resolved, *doc);
            }
        }
        std::lock_guard lock(mailbox->mutex);
        if (LyricsLoader* receiver = mailbox->receiver) {
            QMetaObject::invokeMethod(
                receiver,
                [receiver, generation, result = *doc] { receiver->deliver(generation, result); },
                Qt::QueuedConnection);
        }
    });
    emit changed();
}

void LyricsLoader::reload() {
    if (!m_track.path.isEmpty()) {
        load(m_track);
    }
}

void LyricsLoader::clear() {
    if (m_job != 0) {
        m_jobs.cancel(m_job);
        m_job = 0;
    }
    ++m_generation;
    m_track = TrackRef();
    m_document = LyricsDocument();
    emit changed();
}

void LyricsLoader::shutdown(int timeoutMs) noexcept {
    {
        std::lock_guard lock(m_mailbox->mutex);
        m_mailbox->receiver = nullptr;
    }
    // 換歌時舊的工作已經取消，這裡取消最後一個；LRCLIB 查詢取消後 50 ms 內 abort
    if (m_job != 0) {
        m_jobs.cancel(m_job);
    }
    if (!m_gate->close(timeoutMs)) {
        qCWarning(lcLoader) << "停止時查詢歌詞的工作沒有在" << timeoutMs << "ms 內結束";
    }
}

int LyricsLoader::lineIndexAt(qint64 positionMs) const {
    return lyr::lineIndexAt(m_document, positionMs);
}

void LyricsLoader::deliver(quint64 generation, const LyricsDocument& document) {
    if (generation != m_generation) {
        return; // 換歌前的結果
    }
    finish(document);
}

void LyricsLoader::finish(const LyricsDocument& document) {
    m_job = 0;
    m_document = document;
    emit changed();
}

} // namespace auralis::mod::lyr

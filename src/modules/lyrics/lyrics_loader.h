#pragma once

#include "lyrics_document.h"
#include "lyrics_finder.h"

#include <auralis/job_gate.h>
#include <auralis/track_ref.h>

#include <QObject>

#include <memory>

namespace auralis {
class JobRunner;
class LibraryQuery;
class ModuleDatabase;
} // namespace auralis

namespace auralis::mod::lyr {

/// 歌詞載入（給歌詞面板 T-66、全螢幕 T-68 用）：在 JobRunner 的工作裡依序查快取 → 來源鏈 →
/// 寫快取，結果回到 UI 執行緒。換歌時舊的工作取消，舊結果就算晚到也丟掉，不會錯置到別首。
/// 工作丟例外或失敗時 document() 的狀態是 Error（錯誤邊界在 JobRunner）。
/// 執行緒：只能在 UI 執行緒使用；JobRunner、LibraryQuery、ModuleDatabase 由核心擁有，要活得比它久。
class LyricsLoader final : public QObject {
    Q_OBJECT

public:
    LyricsLoader(JobRunner& jobs, const LibraryQuery& library, ModuleDatabase& database,
                 QObject* parent = nullptr);
    ~LyricsLoader() override;

    /// 在來源鏈最後加一段（T-67 的 LRCLIB）；只能在第一次 load 之前呼叫。
    void appendSource(std::unique_ptr<LyricsStep> step);

    /// 開始找這首的歌詞；前一個還沒結束的工作取消。
    void load(const TrackRef& track);
    /// 重找目前這首（「重試」、線上抓取的設定改了，T-67）；沒有曲目時不做事。
    void reload();
    /// 沒有在聽的曲目：取消工作，document() 變成 NotFound。
    void clear();
    /// 模組停止時（P-LYR-10）：取消並等送出的工作都結束（最多 timeoutMs 毫秒），之後不再碰
    /// 音樂庫與資料庫，也不再送結果；之後才開始的工作直接結束。之後只能解構。
    void shutdown(int timeoutMs) noexcept;

    const TrackRef& track() const { return m_track; }
    bool isLoading() const { return m_job != 0; }
    const LyricsDocument& document() const { return m_document; }
    /// 目前位置在第幾行（offset 已套用）；不是同步歌詞時 -1。
    int lineIndexAt(qint64 positionMs) const;

signals:
    /// document() 或 isLoading() 改變（換歌開始找、找完）。
    void changed();

private:
    void deliver(quint64 generation, const LyricsDocument& document);
    void finish(const LyricsDocument& document);

    struct Mailbox;
    JobRunner& m_jobs;
    const LibraryQuery& m_library;
    ModuleDatabase& m_database;
    std::shared_ptr<LyricsFinder> m_finder;
    std::shared_ptr<Mailbox> m_mailbox;
    std::shared_ptr<JobGate> m_gate;
    TrackRef m_track;
    LyricsDocument m_document;
    quint64 m_generation = 0;
    quint64 m_job = 0;
};

} // namespace auralis::mod::lyr

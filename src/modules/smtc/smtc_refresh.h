#pragma once

#include <auralis/job_gate.h>
#include <auralis/track_ref.h>

#include <QByteArray>
#include <QString>

#include <functional>
#include <memory>
#include <optional>

namespace auralis {
class CoverService;
class LibraryQuery;
} // namespace auralis

namespace auralis::mod::smtc {

/// 曲目資訊的顯示端（SmtcSession；測試用假的記錄呼叫）。只在 UI 執行緒呼叫。
class TrackSink {
public:
    virtual ~TrackSink() = default;
    virtual void setTrack(const QString& title, const QString& artist, const QString& album,
                          const QByteArray& thumbnailPng) noexcept = 0;
    virtual void setThumbnail(const QByteArray& thumbnailPng) noexcept = 0;
    virtual void clearTrack() noexcept = 0;
};

/// 目前曲目和顯示中的不同時重新查：曲目資訊在工作執行緒查音樂庫，查到就顯示（縮圖先放
/// 程式圖示，上一首的封面不會留著），有封面再輸出縮圖換上。晚到的結果和目前曲目不同就丟掉。
/// 佇列沒有目前曲目時清掉曲目資訊（停止但曲目還在時保留）（AC-18-2）。
/// 工作怎麼排、結果怎麼回 UI 執行緒由建構時給的函式決定（模組：JobRunner 與 queued 呼叫；
/// 測試：手動控制順序）。執行緒：除了 submit 交出去的工作，全部在 UI 執行緒。
class TrackRefresher {
public:
    /// 交給工作執行緒的工作；參數問是否已取消。
    using Job = std::function<void(const std::function<bool()>& isCancelled)>;
    using Submit = std::function<void(Job)>;
    /// 把函式排回 UI 執行緒執行；任何執行緒都會呼叫。
    using Post = std::function<void(std::function<void()>)>;

    TrackRefresher(LibraryQuery& library, CoverService& covers, TrackSink& sink, Submit submit,
                   Post post);
    /// 呼叫 invalidate()：還在路上的結果不會碰到已經不在的 sink。
    ~TrackRefresher();
    TrackRefresher(const TrackRefresher&) = delete;
    TrackRefresher& operator=(const TrackRefresher&) = delete;

    /// current 是佇列的目前曲目（沒有時 nullopt）。
    void refresh(const std::optional<TrackRef>& current);
    /// 重新掃描後：曲名或封面可能變了，目前曲目就算和顯示中的相同也重查。
    void requery(const std::optional<TrackRef>& current);
    /// 模組停止：之後才到的結果都丟掉，refresh() 也不再做事，不再碰 sink。
    void invalidate();
    /// 模組停止：invalidate()，之後才開始的工作直接結束，等已經開始的結束（最多 timeoutMs，
    /// 逾時寫日誌）。
    void shutdown(int timeoutMs) noexcept;

private:
    struct State {
        /// 顯示中（或正在查）的曲目。
        std::optional<TrackRef> shown;
        /// 查詢版本：每次開始新查詢、清掉曲目資訊、停止都加一。結果帶著查詢當時的版本回來，
        /// 不同就丟掉（同一首重查時 sameTrack 分不出新舊查詢，M-06 里程碑審查）。
        quint64 version = 0;
        /// invalidate() 後是 nullptr。
        TrackSink* sink = nullptr;
    };
    LibraryQuery& m_library;
    CoverService& m_covers;
    Submit m_submit;
    Post m_post;
    /// 查詢工作與模組停止之間的閘門；工作也持有一份。
    std::shared_ptr<JobGate> m_gate = std::make_shared<JobGate>();
    /// 排回 UI 執行緒的函式也拿著一份，UI 執行緒讀寫。
    std::shared_ptr<State> m_state = std::make_shared<State>();
};

} // namespace auralis::mod::smtc

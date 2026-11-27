#pragma once

#include <auralis/job_gate.h>
#include <auralis/track_ref.h>

#include <QObject>
#include <QString>

#include <memory>
#include <optional>

namespace auralis {
class CoverService;
class JobRunner;
class LibraryQuery;
} // namespace auralis

namespace auralis::mod::lyr {

/// 全螢幕正在播放（S-15）的底色（AC-17-2，DESIGN §2.3「全螢幕底色」）：封面主色（0xRRGGBB）轉 HSL，
/// 明度壓到 ≤ 0.10、飽和度壓到 ≤ 0.40（色相不變、已經夠暗的不提亮），回傳「#rrggbb」。
/// 沒有封面或取色失敗（nullopt）時是 #050505。純計算，任何執行緒。
QString backdropColor(std::optional<quint32> rgb);

/// 依目前曲目的封面算全螢幕底色。取主色（CoverService::dominantColor，會讀檔與解碼）在 JobRunner
/// 的工作「計算封面主色」裡做，結果回到 UI 執行緒才壓暗（AC-17-2）。換歌時舊的工作取消，晚到的
/// 結果丟掉（同 LyricsLoader 的 generation），所以快速連續切歌只有最後一首生效；結果回來之前
/// color() 維持目前的值，畫面上的過渡從目前顯示的顏色接續。
/// 執行緒：只能在 UI 執行緒使用；JobRunner、LibraryQuery、CoverService 由核心擁有，要活得比它久。
class BackdropLoader final : public QObject {
    Q_OBJECT

public:
    BackdropLoader(JobRunner& jobs, const LibraryQuery& library, CoverService& covers,
                   QObject* parent = nullptr);
    ~BackdropLoader() override;

    /// 開始算這首的底色；前一個還沒結束的工作取消。
    void load(const TrackRef& track);
    /// 沒有在聽的曲目：取消工作，底色回到 #050505。
    void clear();
    /// 模組停止時（P-LYR-10）：取消並等送出的工作都結束（最多 timeoutMs 毫秒），之後不再碰
    /// 音樂庫與封面服務，也不再送結果；之後才開始的工作直接結束。之後只能解構。
    void shutdown(int timeoutMs) noexcept;

    /// 目前的底色（「#rrggbb」）。
    QString color() const { return m_color; }
    /// 目前的封面主色原值（「#rrggbb」，沒壓暗；淺色全螢幕由核心提亮，AC-17-6）；沒有封面、
    /// 讀不到或沒有在聽的曲目時空字串。和 color() 同時更新。
    QString coverColor() const { return m_coverColor; }

signals:
    /// color() 或 coverColor() 改變。
    void changed();

private:
    void deliver(quint64 generation, std::optional<quint32> rgb);
    void setColor(std::optional<quint32> rgb);

    struct Mailbox;
    JobRunner& m_jobs;
    const LibraryQuery& m_library;
    CoverService& m_covers;
    std::shared_ptr<Mailbox> m_mailbox;
    std::shared_ptr<JobGate> m_gate;
    QString m_color;
    QString m_coverColor;
    quint64 m_generation = 0;
    quint64 m_job = 0;
};

} // namespace auralis::mod::lyr

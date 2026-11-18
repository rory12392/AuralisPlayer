#pragma once

#include "playlist_cue.h"
#include "playlist_fpl.h"
#include "playlist_parse.h"
#include "playlist_store.h"

#include <auralis/job_gate.h>

#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QQmlEngine;
class QJSEngine;

namespace auralis {
class Dialogs;
class JobRunner;
class Navigator;
class Notifier;
} // namespace auralis

namespace auralis::mod::pl {

class PlaylistsController;

/// 匯入 m3u／m3u8／pls（T-51，US-12 AC-1、2、4；畫面規格 S-09），QML 以單例 PlaylistImport 取用。
/// - 入口：chooseFiles()（側欄與空狀態的「匯入播放清單」，開檔對話框可多選）、
///   importFiles(paths)（拖進視窗與命令列，模組 start() 用 UiRegistry::addFileOpenHandler 接）。
/// - 一次一個檔：讀檔、解析與找不到的判斷（findMissing）在 JobRunner 的工作裡做，
///   回到 UI 執行緒才用 PlaylistStore::createImported 一次交易建立；讀不到時不建立。
/// - 全部找到：開新播放清單頁＋通知「已匯入「<名稱>」，共 <n> 首」，不開對話框，接著做下一個檔。
///   有找不到的、讀不到、不支援（.fpl 結構不符，T-53）：result 換成那一份結果（QML 開 S-09
///   對話框），使用者按「略過」／「關閉」（dismiss()）後才做下一個檔。
/// - result：空的＝沒有要顯示的；否則 {kind: "missing"|"unreadable"|"unsupported", fileName, name,
///   id, total, found, missing, missingItems: [{path, reason}]}。
/// - 重新對應（T-52，US-12 AC-3、US-13 AC-4）：kind 是 missing 時 result 另有
/// mode（"import"＝匯入結果、
///   "playlist"＝從播放清單頁開的）、oldRoot（預填：存過的對應規則的建議，沒有時找不到的路徑的共同資料夾）、
///   newRoot（建議的現在的資料夾，沒有時空的）、remapped（上一次套用找到幾首）。
///   applyRemap 在工作執行緒套用並重新檢查（playlist_remap.h），更新 result 的數字與清單；
///   openRemap(id) 給播放清單頁的「重新對應資料夾」開同一個對話框；chooseNewRoot 開資料夾選擇。
/// - recheckAll：音樂庫掃描完成時（模組 start() 接 scanFinished）在工作執行緒重新檢查所有播放清單的
/// missing。 執行緒：只在 UI 執行緒使用。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class PlaylistImporter : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(PlaylistImport)
    QML_SINGLETON
    Q_PROPERTY(QVariantMap result READ result NOTIFY resultChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    /// 「套用並重新檢查」進行中（按鈕的 busy）。
    Q_PROPERTY(bool remapping READ remapping NOTIFY remappingChanged)

public:
    /// jobs 沒給時在呼叫端的執行緒直接做（測試用）；navigator 沒給時不開頁。
    PlaylistImporter(PlaylistStore& store, PlaylistsController& playlists, Dialogs& dialogs,
                     Notifier& notifier, JobRunner* jobs, Navigator* navigator,
                     QObject* parent = nullptr);

    QVariantMap result() const { return m_result; }
    bool busy() const { return m_busy || !m_result.isEmpty(); }

    /// 開系統的開檔對話框（篩選「播放清單（*.m3u *.m3u8 *.pls *.fpl）」，可多選），選了就匯入。
    Q_INVOKABLE void chooseFiles();
    /// 依序匯入這些檔（排在正在做的後面）。
    Q_INVOKABLE void importFiles(const QStringList& paths);
    /// 關掉目前的結果（「略過」：找不到的項目已經保留在清單裡），接著做下一個檔。
    Q_INVOKABLE void dismiss();
    /// 模組停止：之後才開始的背景工作直接結束，等已經開始的結束（最多 timeoutMs，逾時寫日誌）。
    void shutdown(int timeoutMs) noexcept;

    bool remapping() const { return m_remapping; }

    /// 把 result 那個播放清單的找不到的項目從 oldRoot 對應到 newRoot 並重新檢查（oldRoot
    /// 空的＝只重新檢查）。
    Q_INVOKABLE void applyRemap(const QString& oldRoot, const QString& newRoot);
    /// 播放清單頁的「重新對應資料夾」：對整份播放清單開同一個對話框。有別的結果正在顯示時不做事。
    Q_INVOKABLE void openRemap(qint64 id);
    /// 開資料夾選擇（「現在的資料夾」旁的「瀏覽」），選了發 newRootChosen。
    Q_INVOKABLE void chooseNewRoot();
    /// 重新檢查所有播放清單的 missing（音樂庫掃描完成後），完成時發 rechecked。
    void recheckAll();

    /// 找不到的判斷用的檔案查詢（預設 contentPathExists）與找不到的原因（預設 linkReason：
    /// 經過符號連結或 junction 時寫明，S-1）；測試換成記錄查過哪些路徑的版本，原因不給就不查。
    void setExistsCheck(ExistsCheck exists, ReasonCheck reason = {});
    /// .fpl 內嵌 cue 的對應（T-53）：每次匯入在工作執行緒呼叫 make 取一個 lookup
    /// （模組給 libraryCueLookup）。沒設時內嵌 cue 的項目一律算找不到。
    using MakeCueLookup = std::function<CueLookup()>;
    void setCueLookup(MakeCueLookup make);
    /// m3u／pls 的整張 CUE 展開與重新檢查時的音樂庫查詢（T-56，playlist_cue.h）：
    /// 每次匯入、重新對應、重新檢查各取一份（模組給 libraryCheck）。
    /// 沒設時整張 CUE 算找不到、重新檢查只看磁碟。
    using MakeLibraryCheck = std::function<LibraryCheck()>;
    void setLibraryCheck(MakeLibraryCheck make) { m_libraryCheck = std::move(make); }

    static void setInstance(PlaylistImporter* instance);
    static PlaylistImporter* create(QQmlEngine*, QJSEngine*);

signals:
    void resultChanged();
    void busyChanged();
    /// 一個檔處理完（建立了 id，沒建立時 0）；測試與畫面用。
    void imported(const QString& file, qint64 id);
    void remappingChanged();
    void newRootChosen(const QString& folder);
    /// recheckAll 做完（changed：missing 有變的播放清單數）。
    void rechecked(int changed);

private:
    /// work 在工作執行緒（沒有 JobRunner 時在呼叫端）執行，回傳的函式回到 UI 執行緒執行；
    /// 工作被取消時改在 UI 執行緒執行 cancelled（清掉這個工作設的忙碌狀態）。
    void runJob(std::function<void(PlaylistImporter&)> cancelled,
                std::function<std::function<void()>()> work);
    /// 找不到的結果（S-09）：讀數、清單與重新對應區的預填值；reasons（路徑 → 原因）優先。
    QVariantMap missingResult(qint64 id, const QList<PlaylistItem>& items,
                              const QHash<QString, QString>& reasons = {}) const;
    void setRemapping(bool remapping);

    struct Outcome {
        QString file;
        std::optional<ParsedPlaylist> parsed;
        QList<bool> missing;
        QString error;
        /// 工作被取消：不建立清單，只清掉忙碌狀態、接著處理佇列。
        bool cancelled = false;
    };
    static Outcome load(const QString& file, const ExistsCheck& exists, const ReasonCheck& reason,
                        const MakeCueLookup& cueLookup, const LibraryCheck& library);
    LibraryCheck libraryCheck() const { return m_libraryCheck ? m_libraryCheck() : LibraryCheck(); }
    void next();
    void finish(const Outcome& outcome);
    void showResult(const QVariantMap& result);
    void setBusy(bool busy);

    PlaylistStore& m_store;
    PlaylistsController& m_playlists;
    Dialogs& m_dialogs;
    Notifier& m_notifier;
    JobRunner* m_jobs = nullptr;
    /// 匯入、重新對應、重新檢查的工作與模組停止之間的閘門（P-PL-24）；工作各持有一份。
    std::shared_ptr<JobGate> m_gate = std::make_shared<JobGate>();
    Navigator* m_navigator = nullptr;
    ExistsCheck m_exists;
    ReasonCheck m_reason;
    MakeCueLookup m_cueLookup;
    MakeLibraryCheck m_libraryCheck;
    QStringList m_queue;
    bool m_busy = false;
    bool m_remapping = false;
    QVariantMap m_result;
};

} // namespace auralis::mod::pl

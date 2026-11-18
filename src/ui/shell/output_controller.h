#pragma once

#include <auralis/notifier.h>
#include <auralis/playback_service.h>
#include <auralis/result.h>

#include <QHash>
#include <QList>
#include <QObject>
#include <QThreadPool>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>

class QQmlEngine;
class QJSEngine;

namespace auralis::ui {

/// 輸出面板的一列：輸出路徑＋裝置。
struct OutputOption {
    /// 輸出路徑（輸出後端工廠的名稱，例：WASAPI 獨占）。
    QString backend;
    QString deviceId;
    QString deviceName;
    /// 輸出路徑的能力說明（例：位元完美）；只在列舉時填。
    QString note;
};

/// 輸出面板接到核心的方式：主程式接 OutputRegistry 與 PlaybackServiceImpl，測試換成假的。
struct OutputHooks {
    /// 列出全部輸出路徑的裝置（工作執行緒呼叫，可能要問系統）。
    std::function<QList<OutputOption>()> list;
    /// 換到這個輸出，播放中時從目前位置繼續（UI 執行緒）；失敗時 Error 的 message 給使用者看。
    std::function<Result<bool>(const OutputOption&)> select;
    /// 目前選的輸出（還沒選過時是空的，第一次播放會用清單第一列）。
    std::function<OutputOption()> current;
};

/// 輸出按鈕（S-02）與輸出面板（S-14）的資料：QML 以單例 Output 取用。
/// 把 PlaybackService 的輸出狀態寫成畫面用的文字（路徑、裝置、取樣率／位元深度、位元完美或原因），
/// 管理輸出面板的裝置清單與切換；另外把播放錯誤交給通知列：開不了的曲目（AC-01-3）與
/// 裝置錯誤（AC-02-4，附「重試」＝從停下來的位置再播）。
/// 執行緒：只在 UI 執行緒使用；裝置列舉在自己的工作執行緒。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class OutputController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Output)
    QML_SINGLETON
    /// 輸出面板開著（Ctrl+O、輸出按鈕）。
    Q_PROPERTY(bool panelOpen READ panelOpen WRITE setPanelOpen NOTIFY panelOpenChanged)
    /// 有輸出狀態（播放中或暫停）。
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString pathName READ pathName NOTIFY changed)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY changed)
    /// 送到 DAC 的格式，例：「96 kHz / 24 bit」；沒有輸出狀態時是空字串。
    Q_PROPERTY(QString formatText READ formatText NOTIFY changed)
    /// 訊號路徑的「來源」，例：「FLAC 96 / 24」。
    Q_PROPERTY(QString sourceText READ sourceText NOTIFY changed)
    /// 訊號路徑的「處理」：「無」或改變訊號的處理（例：共享模式）。
    Q_PROPERTY(QString processingText READ processingText NOTIFY changed)
    Q_PROPERTY(bool bitPerfect READ bitPerfect NOTIFY changed)
    /// 狀態標籤的文字與樣式（amber 位元完美、neutral 原因、signal 錯誤、idle 沒有在播）。
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(QString statusVariant READ statusVariant NOTIFY changed)
    /// 輸出按鈕整段唸出來的名稱。
    Q_PROPERTY(QString accessibleText READ accessibleText NOTIFY changed)
    /// 輸出面板的裝置列：path、device、note、noteTone（muted、amber、signal）、selected。
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    /// 狀態列的說明（例：啟動時記住的輸出裝置不在、改用預設裝置）；使用者選了輸出後清掉。
    Q_PROPERTY(QString notice READ notice WRITE setNotice NOTIFY noticeChanged)

public:
    /// notifier 為 nullptr 時不送通知（測試用）。
    OutputController(PlaybackService& service, Notifier* notifier, OutputHooks hooks,
                     QObject* parent = nullptr);
    ~OutputController() override;

    bool panelOpen() const { return m_panelOpen; }
    void setPanelOpen(bool open);
    bool active() const { return !m_status.backend.isEmpty(); }
    QString pathName() const;
    QString deviceName() const;
    QString formatText() const;
    QString sourceText() const;
    QString processingText() const;
    bool bitPerfect() const { return active() && m_status.bitPerfect && m_status.error.isEmpty(); }
    QString statusText() const;
    QString statusVariant() const;
    QString accessibleText() const;
    QVariantList devices() const;
    QString notice() const { return m_notice; }
    void setNotice(const QString& notice);

    /// 重新列舉裝置（面板打開時；拔掉的裝置從清單消失）。
    Q_INVOKABLE void refreshDevices();
    /// 選第 row 列的輸出（點選、方向鍵或數字鍵）。被佔用時不切換，該列寫原因。
    Q_INVOKABLE void select(int row);

    /// 測試用：等裝置列舉做完並套用結果。
    void waitForDevices();

    static void setInstance(OutputController* instance);
    static OutputController* create(QQmlEngine*, QJSEngine*);

signals:
    void changed();
    void devicesChanged();
    void panelOpenChanged();
    void noticeChanged();

private:
    void onStatus(const OutputStatus& status);
    void onSkipped(const TrackRef& track, const QString& reason);
    void applyDevices(const QList<OutputOption>& options);
    /// 目前選的那一列（還沒選過時是第 0 列）；清單是空的時 -1。
    int selectedRow() const;

    PlaybackService& m_service;
    Notifier* m_notifier;
    OutputHooks m_hooks;
    OutputStatus m_status;
    bool m_panelOpen = false;
    QString m_notice;
    /// 切換中：服務換回原本的輸出前會短暫回報錯誤，這時不送通知。
    bool m_switching = false;
    QList<OutputOption> m_options;
    /// 這次面板打開後選不了的裝置（backend＋deviceId → 原因）。
    QHash<QString, QString> m_rowErrors;
    // 最後宣告、最先解構：解構時先等列舉結束，結果才不會送到已拆掉的物件
    QThreadPool m_pool;
};

} // namespace auralis::ui

#pragma once

#include <auralis/dialogs.h>
#include <auralis/notifier.h>
#include <auralis/platform_window.h>

#include <QObject>
#include <QVariant>
#include <QVariantMap>

#include <map>

namespace auralis::core {

/// Dialogs 的實作：把請求以 requested 訊號交給主視窗的 QML（檔案對話框、確認對話框），
/// QML 用 respond() 回覆後呼叫模組的回呼。只在 UI 執行緒使用。
class DialogsImpl final : public QObject, public Dialogs {
    Q_OBJECT

public:
    explicit DialogsImpl(QObject* parent = nullptr);

    void chooseFolder(const QString& title, PathsCallback done) override;
    void chooseFiles(const QString& title, const QStringList& nameFilters,
                     PathsCallback done) override;
    void chooseSaveFile(const QString& title, const QString& suggestedName,
                        const QStringList& nameFilters, PathsCallback done) override;
    void confirm(const ConfirmRequest& request, std::function<void(bool)> done) override;

    /// QML 回覆：檔案類是路徑或 file: 網址的清單（取消時空清單），確認類是 bool。
    Q_INVOKABLE void respond(int id, const QVariant& result);
    int pendingCount() const { return static_cast<int>(m_pending.size()); }

signals:
    /// kind：folder、files、saveFile、confirm；options 依 kind 帶 title、nameFilters、
    /// suggestedName、folder（saveFile 的建議檔名是完整路徑時：兩者都是 file: 網址）、
    /// message、confirmText、destructive。
    void requested(int id, const QString& kind, const QVariantMap& options);

private:
    int request(const QString& kind, const QVariantMap& options,
                std::function<void(const QVariant&)> done);

    int m_nextId = 1;
    std::map<int, std::function<void(const QVariant&)>> m_pending;
};

/// Notifier 的實作：目前的通知放在 current（{id, text, kind: info|error, actionText}），
/// 沒有通知時是空的 map。自動收起的計時在 QML。只在 UI 執行緒使用。
class NotifierImpl final : public QObject, public Notifier {
    Q_OBJECT
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)

public:
    explicit NotifierImpl(QObject* parent = nullptr);

    // 預設參數和介面相同（覆寫不會繼承預設參數）
    void notify(const QString& text, Kind kind = Kind::Info, const QString& actionText = {},
                std::function<void()> action = {}) override;
    QVariantMap current() const { return m_current; }

    /// 給 QML 用的簡化版（沒有動作按鈕）；kind 是 info 或 error。
    Q_INVOKABLE void post(const QString& text, const QString& kind = QStringLiteral("info"));
    /// 按下通知列的動作按鈕；id 不是目前的通知時不做事（通知已被取代）。
    Q_INVOKABLE void triggerAction(int id);
    Q_INVOKABLE void dismiss(int id);

signals:
    void currentChanged();

private:
    int m_nextId = 1;
    QVariantMap m_current;
    std::function<void()> m_action;
};

/// PlatformWindow 的實作；主程式建立主視窗後設定 HWND。
class PlatformWindowImpl final : public PlatformWindow {
public:
    quintptr hwnd() const override { return m_hwnd; }
    void setHwnd(quintptr hwnd) { m_hwnd = hwnd; }

private:
    quintptr m_hwnd = 0;
};

} // namespace auralis::core

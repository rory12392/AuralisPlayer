#pragma once

#include <QAbstractNativeEventFilter>
#include <QFont>
#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

#include <functional>

class QQmlEngine;
class QJSEngine;
class QStyleHints;

namespace auralis::ui {

/// 主題：在 QML 引擎建立前就算好深淺色（第一個畫面就是對的主題，不閃爍），
/// 跟隨系統時由 QStyleHints::colorSchemeChanged 更新；也提供字型與「減少動畫」。
/// 只在 UI 執行緒使用。QML 經單例 ThemeController 取用，Theme.qml 包成語意化 token。
class ThemeController final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    /// system（跟隨系統）、void（深色）、paper（淺色）；寫入時存進 settings.ini 的 ui/theme。
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
    /// 全螢幕頁開著時由主視窗殼設成 true（T-94）：這段期間 dark 一律是 true（Void），
    /// 不改 mode、不寫設定；設回 false 時照當下的 mode 與系統深淺色重算。
    Q_PROPERTY(bool forceVoid READ forceVoid WRITE setForceVoid NOTIFY forceVoidChanged)
    Q_PROPERTY(QFont sansFont READ sansFont CONSTANT)
    Q_PROPERTY(QFont monoFont READ monoFont CONSTANT)
    Q_PROPERTY(bool reduceMotion READ reduceMotion NOTIFY reduceMotionChanged)

public:
    /// 讀 Windows「顯示動畫」設定；回傳 true 表示動畫開著。測試可以換掉。
    using AnimationReader = std::function<bool()>;

    ThemeController(QString settingsPath, QStyleHints* hints,
                    AnimationReader readAnimation = systemAnimationEnabled,
                    QObject* parent = nullptr);
    ~ThemeController() override;

    QString mode() const { return m_mode; }
    void setMode(const QString& mode);
    /// 只影響這次執行、不寫進設定（--screenshot --theme 用）。
    Q_INVOKABLE void overrideMode(const QString& mode);
    bool dark() const { return m_dark; }
    bool forceVoid() const { return m_forceVoid; }
    void setForceVoid(bool force);
    QFont sansFont() const { return m_sans; }
    QFont monoFont() const { return m_mono; }
    bool reduceMotion() const { return m_reduceMotion; }

    /// 依字型角色建出完整的 QFont（保留退回順序的字族清單）。QML 不能在指定 font
    /// 之後再改 font.pixelSize，所以字級、字重、字距由 Theme.qml 的字型 token 一次給齊。
    Q_INVOKABLE QFont makeFont(bool mono, int pixelSize, int weight, qreal letterSpacing = 0,
                               bool uppercase = false) const;

    /// 重新讀「顯示動畫」設定（WM_SETTINGCHANGE 時呼叫）。
    void refreshReduceMotion();
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    static bool systemAnimationEnabled();
    /// 主程式在 QML 引擎建立前設定；QML 單例回傳這個實例。
    static void setInstance(ThemeController* instance);
    static ThemeController* create(QQmlEngine*, QJSEngine*);

signals:
    void modeChanged();
    void darkChanged();
    void forceVoidChanged();
    void reduceMotionChanged();

private:
    void updateDark();

    QString m_settingsPath;
    QStyleHints* m_hints;
    AnimationReader m_readAnimation;
    QString m_mode;
    bool m_dark = true;
    bool m_forceVoid = false;
    bool m_reduceMotion = false;
    QFont m_sans;
    QFont m_mono;
};

/// 載入隨程式附帶的字型（Jost、IBM Plex Mono），回傳載入成功的字族名稱。
QStringList loadBundledFonts();

} // namespace auralis::ui

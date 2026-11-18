#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace auralis::ui {

/// 主視窗的狀態：大小、位置、最大化、正在播放面板開合。啟動時從 settings.ini 的 ui/ 段讀，
/// 視窗關閉時 save() 寫回。沒有存過時用預設大小，視窗寬度 < 1200 時面板預設收合。
/// 只在 UI 執行緒使用。
class ShellState final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int windowX MEMBER m_x NOTIFY changed)
    Q_PROPERTY(int windowY MEMBER m_y NOTIFY changed)
    Q_PROPERTY(int windowWidth MEMBER m_width NOTIFY changed)
    Q_PROPERTY(int windowHeight MEMBER m_height NOTIFY changed)
    /// 有存過位置時才用 windowX／windowY，否則讓系統決定。
    Q_PROPERTY(bool hasPosition MEMBER m_hasPosition NOTIFY changed)
    Q_PROPERTY(bool maximized MEMBER m_maximized NOTIFY changed)
    Q_PROPERTY(bool panelOpen MEMBER m_panelOpen NOTIFY changed)

public:
    static constexpr int kDefaultWidth = 1440;
    static constexpr int kDefaultHeight = 900;
    static constexpr int kPanelAutoCollapseWidth = 1200;

    explicit ShellState(QString settingsPath, QObject* parent = nullptr);

    /// --screenshot 用：指定大小、不讀也不存使用者的視窗狀態。
    void useFixedSize(int width, int height);
    /// 寫回 settings.ini（固定大小模式時不寫）。
    Q_INVOKABLE void save() const;

    int width() const { return m_width; }
    int height() const { return m_height; }
    bool panelOpen() const { return m_panelOpen; }

    static void setInstance(ShellState* instance);
    static ShellState* create(QQmlEngine*, QJSEngine*);

signals:
    void changed();

private:
    QString m_settingsPath;
    bool m_persist = true;
    int m_x = 0;
    int m_y = 0;
    int m_width = kDefaultWidth;
    int m_height = kDefaultHeight;
    bool m_hasPosition = false;
    bool m_maximized = false;
    bool m_panelOpen = true;
};

} // namespace auralis::ui

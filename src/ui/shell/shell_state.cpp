#include "shell/shell_state.h"

#include <QJSEngine>
#include <QQmlEngine>
#include <QSettings>

namespace auralis::ui {

namespace {

ShellState* g_instance = nullptr;

} // namespace

ShellState::ShellState(QString settingsPath, QObject* parent)
    : QObject(parent), m_settingsPath(std::move(settingsPath)) {
    QSettings settings(m_settingsPath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("ui"));
    m_width = settings.value(QStringLiteral("windowWidth"), kDefaultWidth).toInt();
    m_height = settings.value(QStringLiteral("windowHeight"), kDefaultHeight).toInt();
    m_hasPosition = settings.contains(QStringLiteral("windowX"));
    m_x = settings.value(QStringLiteral("windowX"), 0).toInt();
    m_y = settings.value(QStringLiteral("windowY"), 0).toInt();
    m_maximized = settings.value(QStringLiteral("maximized"), false).toBool();
    m_panelOpen =
        settings.value(QStringLiteral("panelOpen"), m_width >= kPanelAutoCollapseWidth).toBool();
}

void ShellState::useFixedSize(int width, int height) {
    m_persist = false;
    m_width = width;
    m_height = height;
    m_hasPosition = false;
    m_maximized = false;
    m_panelOpen = width >= kPanelAutoCollapseWidth;
    emit changed();
}

void ShellState::save() const {
    if (!m_persist) {
        return;
    }
    QSettings settings(m_settingsPath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("ui"));
    settings.setValue(QStringLiteral("windowWidth"), m_width);
    settings.setValue(QStringLiteral("windowHeight"), m_height);
    settings.setValue(QStringLiteral("windowX"), m_x);
    settings.setValue(QStringLiteral("windowY"), m_y);
    settings.setValue(QStringLiteral("maximized"), m_maximized);
    settings.setValue(QStringLiteral("panelOpen"), m_panelOpen);
}

void ShellState::setInstance(ShellState* instance) {
    g_instance = instance;
}

ShellState* ShellState::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "ShellState", "主程式要在建立 QML 引擎前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::ui

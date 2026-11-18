#pragma once

// 逐格記錄主視窗的狀態（tst_fullscreen_transition，修正：進出全螢幕的跑版一格）：每一格在
// afterSynchronizing（場景圖剛同步完、這一格要畫的內容已經定下來，threaded 繪製迴圈時 GUI 執行緒
// 還停著）記下視窗的邏輯尺寸、原生視窗實際的工作區尺寸、目前顯示的是全螢幕頁還是一般頁、Theme.dark。
// 只給 windows 平台的 Quick Test 用；不截螢幕畫面、不送輸入。
#include <QMutex>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScreen>
#include <QVariantList>
#include <QVariantMap>

#include <qt_windows.h>

class FrameProbe : public QObject {
    Q_OBJECT

public:
    /// 讀 Theme.dark 用（測試的 ThemeController）。
    QObject* theme = nullptr;

    /// 開始記錄 window 的每一格；shell 是主視窗殼（AppShell），從它找全螢幕區與一般內容區。
    Q_INVOKABLE void attach(QQuickWindow* window, QQuickItem* shell) {
        detach();
        m_window = window;
        m_shell = shell;
        m_fullscreenArea = shell->findChild<QQuickItem*>(QStringLiteral("fullscreenArea"));
        m_cover = window->contentItem()->findChild<QQuickItem*>(QStringLiteral("windowStateCover"));
        m_sync = connect(
            window, &QQuickWindow::afterSynchronizing, this, [this] { record(); },
            Qt::DirectConnection);
        m_swap = connect(
            window, &QQuickWindow::frameSwapped, this, [this] { swapped(); }, Qt::DirectConnection);
    }
    Q_INVOKABLE void detach() {
        disconnect(m_sync);
        disconnect(m_swap);
    }
    /// 清掉紀錄（留下最後一格當第 0 格：視窗改大小時系統沿用的就是它），之後的格子都標上 label。
    Q_INVOKABLE void mark(const QString& label) {
        const QMutexLocker lock(&m_mutex);
        if (!m_frames.isEmpty()) {
            m_frames = {m_frames.constLast()};
        }
        m_label = label;
    }
    /// 已記錄的格子：label、sceneW/H（QQuickWindow 的邏輯尺寸，這一格的排版尺寸）、clientW/H（原生
    /// 視窗工作區，換成邏輯尺寸）、screenW/H、visibility、fullscreenPage（全螢幕頁顯示中）、
    /// covered（WindowStateKeeper 的不透明遮罩蓋著整個視窗）、dark、
    /// swapped（這一格之前已交換幾次畫面）。
    Q_INVOKABLE QVariantList frames() const {
        const QMutexLocker lock(&m_mutex);
        return m_frames;
    }

private:
    void record() {
        if (!m_window || !m_shell) {
            return;
        }
        QVariantMap frame;
        RECT rect{};
        GetClientRect(reinterpret_cast<HWND>(m_window->winId()), &rect);
        const qreal dpr = m_window->devicePixelRatio();
        frame.insert(QStringLiteral("sceneW"), m_window->width());
        frame.insert(QStringLiteral("sceneH"), m_window->height());
        frame.insert(QStringLiteral("clientW"), qRound((rect.right - rect.left) / dpr));
        frame.insert(QStringLiteral("clientH"), qRound((rect.bottom - rect.top) / dpr));
        const QRect screen = m_window->screen()->geometry();
        frame.insert(QStringLiteral("screenW"), screen.width());
        frame.insert(QStringLiteral("screenH"), screen.height());
        frame.insert(QStringLiteral("visibility"), static_cast<int>(m_window->visibility()));
        frame.insert(QStringLiteral("fullscreenPage"),
                     m_fullscreenArea && m_fullscreenArea->isVisible() &&
                         !m_fullscreenArea->childItems().isEmpty());
        frame.insert(QStringLiteral("covered"), m_cover && m_cover->isVisible());
        frame.insert(QStringLiteral("dark"), theme ? theme->property("dark").toBool() : false);
        const QMutexLocker lock(&m_mutex);
        frame.insert(QStringLiteral("label"), m_label);
        frame.insert(QStringLiteral("swapped"), m_swapped);
        m_frames.append(frame);
    }
    void swapped() {
        const QMutexLocker lock(&m_mutex);
        ++m_swapped;
    }

    mutable QMutex m_mutex;
    QVariantList m_frames;
    QString m_label;
    int m_swapped = 0;
    QPointer<QQuickWindow> m_window;
    QPointer<QQuickItem> m_shell;
    QPointer<QQuickItem> m_fullscreenArea;
    QPointer<QQuickItem> m_cover;
    QMetaObject::Connection m_sync;
    QMetaObject::Connection m_swap;
};

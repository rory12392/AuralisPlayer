#include "lyrics_view.h"

#include "backdrop.h"
#include "lrclib.h"
#include "lyrics_loader.h"

#include <auralis/module_settings.h>
#include <auralis/playback_service.h>

#include <QJSEngine>

namespace auralis::mod::lyr {

namespace {
LyricsView* g_instance = nullptr;

QString sourceKey(LyricsSource source) {
    switch (source) {
    case LyricsSource::Lrc:
        return QStringLiteral("lrc");
    case LyricsSource::Lrclib:
        return QStringLiteral("lrclib");
    case LyricsSource::Embedded:
        break;
    }
    return QStringLiteral("embedded");
}

/// 全螢幕主題的設定值（AC-17-6）：不認得的值當成深色（v1.0 的行為）。
QString validFullscreenTheme(const QString& theme) {
    if (theme == QStringLiteral("light") || theme == QStringLiteral("follow")) {
        return theme;
    }
    return QStringLiteral("dark");
}
} // namespace

LyricsView::LyricsView(LyricsLoader& loader, PlaybackService& playback, ModuleSettings* settings,
                       std::shared_ptr<LrclibSwitches> switches, QObject* parent)
    : QObject(parent), m_loader(&loader), m_playback(playback), m_settings(settings),
      m_switches(std::move(switches)) {
    connect(&loader, &LyricsLoader::changed, this, &LyricsView::reload);
    // 跳轉後引擎回報的就是目標位置，所以拖曳進度列後下一次通知就跳到對應行（AC-15-2）
    connect(&playback, &PlaybackService::positionChanged, this, &LyricsView::updateLine);
    // 全螢幕主題只在這裡讀一次：之後只有本程式自己改（設定頁、T 鍵、切換鈕）
    if (m_settings) {
        m_fullscreenTheme = validFullscreenTheme(
            m_settings->value(QStringLiteral("fullscreenTheme"), QString()).toString());
    }
    reload();
}

LyricsView::~LyricsView() {
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

void LyricsView::readSettings() {
    if (!m_settings) {
        return; // 測試：沒有設定檔時沿用目前的值（預設開、不另存）
    }
    m_onlineEnabled = m_settings->value(QStringLiteral("online"), true).toBool();
    m_saveLrc = m_settings->value(QStringLiteral("saveLrc"), false).toBool();
    if (m_switches) {
        m_switches->online.store(m_onlineEnabled);
        m_switches->saveLrc.store(m_saveLrc);
    }
}

void LyricsView::setOnlineEnabled(bool enabled) {
    if (m_settings) {
        m_settings->setValue(QStringLiteral("online"), enabled);
    }
    m_onlineEnabled = enabled;
    if (m_switches) {
        m_switches->online.store(enabled);
    }
    // 立刻生效：目前這首重找（關掉時進行中的查詢被取消，連線物件隨工作結束刪掉，AC-16-3）
    if (m_loader) {
        m_loader->reload();
    }
    emit changed();
}

void LyricsView::setSaveLrc(bool enabled) {
    if (m_settings) {
        m_settings->setValue(QStringLiteral("saveLrc"), enabled);
    }
    m_saveLrc = enabled;
    if (m_switches) {
        m_switches->saveLrc.store(enabled);
    }
    emit changed();
}

void LyricsView::setFullscreenTheme(const QString& theme) {
    const QString value = validFullscreenTheme(theme);
    if (m_settings) {
        m_settings->setValue(QStringLiteral("fullscreenTheme"), value);
    }
    if (value != m_fullscreenTheme) {
        m_fullscreenTheme = value;
        emit fullscreenThemeChanged();
    }
}

void LyricsView::retry() {
    if (m_loader) {
        m_loader->reload();
    }
}

void LyricsView::reload() {
    readSettings();
    m_lines.clear();
    m_times.clear();
    m_source.clear();
    if (!m_loader || m_loader->isLoading()) {
        m_state = m_loader ? QStringLiteral("loading") : QStringLiteral("none");
    } else if (const LyricsDocument& doc = m_loader->document();
               doc.status == LyricsDocument::Status::Unavailable) {
        m_state = QStringLiteral("unavailable"); // AC-16-2
    } else if (doc.status == LyricsDocument::Status::Found && doc.instrumental) {
        m_state = QStringLiteral("instrumental");
        m_source = sourceKey(doc.source);
    } else if (doc.status != LyricsDocument::Status::Found || doc.lines.isEmpty()) {
        // 讀取失敗也顯示「沒有歌詞」（細節在日誌），面板不留白（AC-15-5）
        m_state = QStringLiteral("none");
    } else {
        m_state = doc.synced ? QStringLiteral("synced") : QStringLiteral("plain");
        m_source = sourceKey(doc.source);
        for (const LyricLine& line : doc.lines) {
            m_lines.append(line.text);
            m_times.append(line.timeMs);
        }
    }
    emit changed();
    updateLine(m_playback.positionMs());
}

void LyricsView::updateLine(qint64 positionMs) {
    const int line =
        m_loader && m_state == QStringLiteral("synced") ? m_loader->lineIndexAt(positionMs) : -1;
    if (line != m_currentLine) {
        m_currentLine = line;
        emit currentLineChanged();
    }
}

QString LyricsView::fullscreenBackdrop() const {
    return m_backdrop ? m_backdrop->color() : backdropColor(std::nullopt);
}

QString LyricsView::fullscreenCoverColor() const {
    return m_backdrop ? m_backdrop->coverColor() : QString();
}

void LyricsView::setBackdrop(BackdropLoader* backdrop) {
    if (m_backdrop) {
        disconnect(m_backdrop, nullptr, this, nullptr);
    }
    m_backdrop = backdrop;
    if (backdrop) {
        connect(backdrop, &BackdropLoader::changed, this, &LyricsView::fullscreenBackdropChanged);
    }
    emit fullscreenBackdropChanged();
}

void LyricsView::seekToLine(int index) {
    if (m_state != QStringLiteral("synced") || index < 0 || index >= m_times.size()) {
        return;
    }
    m_playback.seek(m_times.at(index));
}

void LyricsView::setInstance(LyricsView* instance) {
    g_instance = instance;
}

LyricsView* LyricsView::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "LyricsView", "模組要在載入面板前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::mod::lyr

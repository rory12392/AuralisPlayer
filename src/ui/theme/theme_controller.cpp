#include "theme/theme_controller.h"

#include <QFontDatabase>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QQmlEngine>
#include <QSettings>
#include <QStyleHints>

#include <windows.h>

Q_LOGGING_CATEGORY(lcTheme, "auralis.ui")

namespace auralis::ui {

namespace {

ThemeController* g_instance = nullptr;

const QString kThemeKey = QStringLiteral("ui/theme");
const QString kSystem = QStringLiteral("system");
const QString kVoid = QStringLiteral("void");
const QString kPaper = QStringLiteral("paper");

QString validMode(const QString& mode) {
    return mode == kVoid || mode == kPaper ? mode : kSystem;
}

} // namespace

ThemeController::ThemeController(QString settingsPath, QStyleHints* hints,
                                 AnimationReader readAnimation, QObject* parent)
    : QObject(parent), m_settingsPath(std::move(settingsPath)), m_hints(hints),
      m_readAnimation(std::move(readAnimation)) {
    m_mode = validMode(
        QSettings(m_settingsPath, QSettings::IniFormat).value(kThemeKey, kSystem).toString());
    // 拉丁字與數字用附帶的字型；中日文由後面的系統字型補（字族清單就是退回順序）
    m_sans.setFamilies({QStringLiteral("Jost"), QStringLiteral("Microsoft JhengHei UI"),
                        QStringLiteral("Yu Gothic UI"), QStringLiteral("Segoe UI")});
    m_mono.setFamilies({QStringLiteral("IBM Plex Mono"), QStringLiteral("Microsoft JhengHei UI"),
                        QStringLiteral("Consolas")});
    updateDark();
    if (m_hints) {
        connect(m_hints, &QStyleHints::colorSchemeChanged, this, &ThemeController::updateDark);
    }
    m_reduceMotion = !m_readAnimation();
}

ThemeController::~ThemeController() {
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

void ThemeController::setMode(const QString& mode) {
    overrideMode(mode);
    QSettings(m_settingsPath, QSettings::IniFormat).setValue(kThemeKey, m_mode);
}

void ThemeController::overrideMode(const QString& mode) {
    const QString valid = validMode(mode);
    if (valid == m_mode) {
        return;
    }
    m_mode = valid;
    emit modeChanged();
    updateDark();
}

void ThemeController::setForceVoid(bool force) {
    if (force == m_forceVoid) {
        return;
    }
    m_forceVoid = force;
    emit forceVoidChanged();
    updateDark();
}

void ThemeController::updateDark() {
    bool dark = true;
    if (m_forceVoid) {
        // 全螢幕頁：一律 Void（DESIGN §2.3），使用者的 mode 不變
    } else if (m_mode == kPaper) {
        dark = false;
    } else if (m_mode == kSystem && m_hints) {
        // 系統沒有回報（Unknown）時用 Void，那是這套設計的主要外觀
        dark = m_hints->colorScheme() != Qt::ColorScheme::Light;
    }
    if (dark != m_dark) {
        m_dark = dark;
        qCInfo(lcTheme) << "主題：" << (m_dark ? "Void（深色）" : "Paper（淺色）");
        emit darkChanged();
    }
}

QFont ThemeController::makeFont(bool mono, int pixelSize, int weight, qreal letterSpacing,
                                bool uppercase) const {
    QFont font = mono ? m_mono : m_sans;
    font.setPixelSize(pixelSize);
    font.setWeight(static_cast<QFont::Weight>(weight));
    if (letterSpacing != 0) {
        font.setLetterSpacing(QFont::AbsoluteSpacing, letterSpacing);
    }
    if (uppercase) {
        font.setCapitalization(QFont::AllUppercase);
    }
    return font;
}

void ThemeController::refreshReduceMotion() {
    const bool reduce = !m_readAnimation();
    if (reduce != m_reduceMotion) {
        m_reduceMotion = reduce;
        emit reduceMotionChanged();
    }
}

bool ThemeController::nativeEventFilter(const QByteArray& eventType, void* message, qintptr*) {
    if (eventType == "windows_generic_MSG" && message != nullptr &&
        static_cast<const MSG*>(message)->message == WM_SETTINGCHANGE) {
        refreshReduceMotion();
    }
    return false;
}

bool ThemeController::systemAnimationEnabled() {
    BOOL enabled = TRUE;
    if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0)) {
        return true;
    }
    return enabled != FALSE;
}

void ThemeController::setInstance(ThemeController* instance) {
    g_instance = instance;
}

ThemeController* ThemeController::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "ThemeController", "主程式要在建立 QML 引擎前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

QStringList loadBundledFonts() {
    QStringList families;
    for (const char* file :
         {"Jost-VariableFont_wght.ttf", "IBMPlexMono-Light.ttf", "IBMPlexMono-Regular.ttf"}) {
        const QString path = QStringLiteral(":/qt/qml/Auralis/Ui/fonts/") + QLatin1String(file);
        const int id = QFontDatabase::addApplicationFont(path);
        if (id < 0) {
            qCWarning(lcTheme).noquote() << "字型載入失敗：" << path;
            continue;
        }
        families << QFontDatabase::applicationFontFamilies(id);
    }
    families.removeDuplicates();
    return families;
}

} // namespace auralis::ui

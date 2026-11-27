#include "theme/theme_controller.h"

#include <QFontDatabase>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QQmlEngine>
#include <QSettings>
#include <QStyleHints>

#include <windows.h>

#include <algorithm>
#include <cmath>

Q_LOGGING_CATEGORY(lcTheme, "auralis.ui")

namespace auralis::ui {

namespace {

ThemeController* g_instance = nullptr;

const QString kThemeKey = QStringLiteral("ui/theme");
const QString kSystem = QStringLiteral("system");
const QString kVoid = QStringLiteral("void");
const QString kPaper = QStringLiteral("paper");

// 全螢幕主題（AC-17-6）
const QString kDark = QStringLiteral("dark");
const QString kLight = QStringLiteral("light");
const QString kFollow = QStringLiteral("follow");

// 淺色全螢幕底色（DESIGN §2.3）：沒有封面時 Paper 的 bg
constexpr quint32 kNoCoverLight = 0xf4f2ee;
constexpr double kMinLightness = 0.92;
constexpr double kMaxLightSaturation = 0.30;

int toByte(double value) {
    return std::clamp(static_cast<int>(std::lround(value * 255.0)), 0, 255);
}

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

void ThemeController::setFullscreen(bool fullscreen) {
    if (fullscreen == m_fullscreen) {
        return;
    }
    m_fullscreen = fullscreen;
    emit fullscreenChanged();
    updateDark();
}

void ThemeController::setFullscreenTheme(const QString& theme) {
    const QString valid = theme == kLight || theme == kFollow ? theme : kDark;
    if (valid == m_fullscreenTheme) {
        return;
    }
    m_fullscreenTheme = valid;
    emit fullscreenThemeChanged();
    updateDark();
}

void ThemeController::updateDark() {
    bool dark = true;
    if (m_fullscreen && m_fullscreenTheme == kDark) {
        // 全螢幕頁強制 Void（DESIGN §2.3），使用者的 mode 不變
    } else if (m_fullscreen && m_fullscreenTheme == kLight) {
        dark = false; // 強制 Paper
    } else if (m_mode == kPaper) {
        dark = false;
    } else if (m_mode == kSystem && m_hints) {
        // 系統沒有回報（Unknown）時用 Void，那是這套設計的主要外觀
        dark = m_hints->colorScheme() != Qt::ColorScheme::Light;
    }
    if (dark != m_dark) {
        m_dark = dark;
        qCInfo(lcTheme) << "主題：" << (m_dark ? "Void（深色）" : "Paper（淺色）");
        // 全螢幕中換主題：顏色過渡關掉（進全螢幕也算；離開時主視窗照舊淡入，T-94）
        const bool instant = m_fullscreen;
        if (instant) {
            m_instantSwitch = true;
            emit instantThemeSwitchChanged();
        }
        emit darkChanged();
        if (instant) {
            m_instantSwitch = false;
            emit instantThemeSwitchChanged();
        }
    }
}

quint32 ThemeController::lightBackdropRgb(std::optional<quint32> rgb) {
    if (!rgb) {
        return kNoCoverLight;
    }
    const double r = ((*rgb >> 16) & 0xff) / 255.0;
    const double g = ((*rgb >> 8) & 0xff) / 255.0;
    const double b = (*rgb & 0xff) / 255.0;
    const double hi = std::max({r, g, b});
    const double lo = std::min({r, g, b});
    const double d = hi - lo;
    // RGB → HSL
    double lightness = (hi + lo) / 2;
    double saturation = d == 0 ? 0 : d / (1 - std::abs(2 * lightness - 1));
    double hue = 0; // 0–6（每 60 度一段）
    if (d > 0) {
        if (hi == r) {
            hue = std::fmod((g - b) / d + 6, 6);
        } else if (hi == g) {
            hue = (b - r) / d + 2;
        } else {
            hue = (r - g) / d + 4;
        }
    }
    lightness = std::max(lightness, kMinLightness);
    saturation = std::min(saturation, kMaxLightSaturation);
    // HSL → RGB
    const double chroma = (1 - std::abs(2 * lightness - 1)) * saturation;
    const double x = chroma * (1 - std::abs(std::fmod(hue, 2) - 1));
    const double m = lightness - chroma / 2;
    double rr = 0;
    double gg = 0;
    double bb = 0;
    switch (static_cast<int>(hue)) {
    case 0:
        rr = chroma, gg = x;
        break;
    case 1:
        rr = x, gg = chroma;
        break;
    case 2:
        gg = chroma, bb = x;
        break;
    case 3:
        gg = x, bb = chroma;
        break;
    case 4:
        rr = x, bb = chroma;
        break;
    default:
        rr = chroma, bb = x;
        break;
    }
    return (static_cast<quint32>(toByte(rr + m)) << 16) |
           (static_cast<quint32>(toByte(gg + m)) << 8) | static_cast<quint32>(toByte(bb + m));
}

QColor ThemeController::lightBackdrop(const QVariant& rgb) const {
    std::optional<quint32> input;
    if (rgb.isValid() && !rgb.isNull()) {
        const auto type = rgb.metaType().id();
        if (type == QMetaType::Int || type == QMetaType::UInt || type == QMetaType::Double ||
            type == QMetaType::LongLong || type == QMetaType::ULongLong) {
            input = static_cast<quint32>(rgb.toLongLong()) & 0xffffff;
        } else {
            const QColor color = rgb.value<QColor>();
            if (color.isValid()) {
                input = color.rgb() & 0xffffff;
            }
        }
    }
    return QColor::fromRgb(lightBackdropRgb(input));
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

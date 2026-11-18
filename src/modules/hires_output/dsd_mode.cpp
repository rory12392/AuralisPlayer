#include "dsd_mode.h"

#include <QCoreApplication>

namespace auralis::mod::out {

namespace {

constexpr quint32 kDefaultPcmRate = 176400;

QString tr(const char* text) {
    return QCoreApplication::translate("DsdMode", text);
}

} // namespace

QString dsdModeKey(DsdMode mode) {
    switch (mode) {
    case DsdMode::Native:
        return QStringLiteral("native");
    case DsdMode::Pcm:
        return QStringLiteral("pcm");
    case DsdMode::Dop:
        break;
    }
    return QStringLiteral("dop");
}

DsdMode dsdModeFromKey(const QString& key) {
    if (key == QLatin1String("native")) {
        return DsdMode::Native;
    }
    if (key == QLatin1String("pcm")) {
        return DsdMode::Pcm;
    }
    return DsdMode::Dop;
}

quint32 validPcmRate(qint64 rate) {
    return rate == 88200 || rate == 176400 || rate == 352800 ? static_cast<quint32>(rate)
                                                             : kDefaultPcmRate;
}

OutputKind outputKind(const QString& backendName) {
    if (backendName == QLatin1String("ASIO")) {
        return OutputKind::Asio;
    }
    if (backendName == QStringLiteral("WASAPI 共享")) {
        return OutputKind::Shared;
    }
    return OutputKind::Exclusive;
}

QString unavailableReason(DsdMode mode, OutputKind kind, bool nativeDsd) {
    if (mode == DsdMode::Native && !nativeDsd) {
        return tr("需要 ASIO 外掛");
    }
    if (mode == DsdMode::Native && kind != OutputKind::Asio) {
        return tr("需選 ASIO");
    }
    if (mode == DsdMode::Dop && kind == OutputKind::Shared) {
        return tr("共享模式不支援");
    }
    return {};
}

DsdMode effectiveMode(DsdMode chosen, OutputKind kind, bool nativeDsd) {
    if (unavailableReason(chosen, kind, nativeDsd).isEmpty()) {
        return chosen;
    }
    return kind == OutputKind::Shared ? DsdMode::Pcm : DsdMode::Dop;
}

QString dsdRouteLabel(const OutputStatus& status) {
    if (status.backend.isEmpty() || status.sourceFormat.dsdRate == 0) {
        return {};
    }
    if (status.deviceFormat.dsdRate > 0) {
        return tr("原生");
    }
    return status.reasons.contains(QStringLiteral("DSD 轉 PCM")) ? tr("轉 PCM") : tr("DoP");
}

QString unsupportedSuggestion(DsdMode effective) {
    switch (effective) {
    case DsdMode::Native:
        // 原生不行時後端已改送 DoP，DoP 的取樣率也不支援：只剩轉 PCM
        return tr("請改用轉 PCM");
    case DsdMode::Pcm:
        return tr("請在設定頁改用較低的轉 PCM 取樣率");
    case DsdMode::Dop:
        break;
    }
    return tr("請改用 ASIO 原生 DSD 或轉 PCM");
}

bool isDsdPath(const QString& path) {
    return path.endsWith(QLatin1String(".dsf"), Qt::CaseInsensitive) ||
           path.endsWith(QLatin1String(".dff"), Qt::CaseInsensitive);
}

} // namespace auralis::mod::out

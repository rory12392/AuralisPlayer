#include "replay_gain.h"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>

namespace auralis::mod::out {

namespace {

/// 記錄的上限：只需要目前這首與緩衝裡的下一首，留一點餘裕。
constexpr int kLogLimit = 16;

QString tr(const char* text) {
    return QCoreApplication::translate("ReplayGain", text);
}

/// 一種增益（音軌或專輯）：rg_computed 有就用它，否則用標籤；峰值跟著增益的來源。
RgChoice pick(const std::optional<double>& computedGain, const std::optional<double>& computedPeak,
              const std::optional<double>& tagGain, const std::optional<double>& tagPeak) {
    RgChoice choice;
    if (computedGain) {
        choice.gainDb = computedGain;
        choice.peak = computedPeak;
        choice.source = RgSource::Computed;
    } else if (tagGain) {
        choice.gainDb = tagGain;
        choice.peak = tagPeak;
        choice.source = RgSource::Tags;
    }
    return choice;
}

/// 「−6.2 dB」：負號用 U+2212（介面規範的讀數寫法），正值帶 +。
QString formatDb(double db) {
    const double rounded = std::round(db * 10.0) / 10.0;
    QString number = QString::number(std::abs(rounded), 'f', 1);
    if (rounded < 0) {
        number.prepend(QChar(0x2212));
    } else if (rounded > 0) {
        number.prepend(QLatin1Char('+'));
    }
    return number + QStringLiteral(" dB");
}

QString sourceText(RgSource source) {
    return source == RgSource::Computed ? tr("AuralisPlayer 計算") : tr("標籤");
}

} // namespace

QString rgModeKey(RgMode mode) {
    switch (mode) {
    case RgMode::Track:
        return QStringLiteral("track");
    case RgMode::Album:
        return QStringLiteral("album");
    case RgMode::Off:
        break;
    }
    return QStringLiteral("off");
}

RgMode rgModeFromKey(const QString& key) {
    if (key == QLatin1String("track")) {
        return RgMode::Track;
    }
    if (key == QLatin1String("album")) {
        return RgMode::Album;
    }
    return RgMode::Off;
}

RgTrackGain chooseGain(const RgValues& computed, const RgValues& tags) {
    RgTrackGain gain;
    gain.track = pick(computed.trackGain, computed.trackPeak, tags.trackGain, tags.trackPeak);
    gain.album = pick(computed.albumGain, computed.albumPeak, tags.albumGain, tags.albumPeak);
    if (!gain.album.gainDb && gain.track.gainDb) {
        // 專輯只有部分曲目算過、或標籤只有音軌增益：這首退回音軌增益（規格 §1 情況 4）
        gain.album = gain.track;
        gain.album.albumFallback = true;
    }
    return gain;
}

double appliedGainDb(const RgChoice& choice, bool preventClipping) {
    if (!choice.gainDb) {
        return 0.0;
    }
    double gain = *choice.gainDb;
    if (preventClipping) {
        const double limit =
            choice.peak && *choice.peak > 0.0 ? -20.0 * std::log10(*choice.peak) : 0.0;
        gain = std::min(gain, limit);
    }
    return gain;
}

double dbToLinear(double db) {
    return std::pow(10.0, db / 20.0);
}

QString RgAppliedLog::key(const TrackRef& track) {
    return pathKey(track.path) + QLatin1Char('|') + QString::number(track.subsong);
}

void RgAppliedLog::record(const TrackRef& track, const RgTrackGain& gain) {
    const std::lock_guard lock(m_mutex);
    if (m_entries.size() >= kLogLimit) {
        m_entries.clear();
    }
    m_entries.insert(key(track), gain);
}

std::optional<RgTrackGain> RgAppliedLog::find(const TrackRef& track) const {
    const std::lock_guard lock(m_mutex);
    const auto it = m_entries.constFind(key(track));
    if (it == m_entries.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

QString rgStatusText(RgMode mode, bool preventClipping, const QString& dsdRoute,
                     const std::optional<RgTrackGain>& gain) {
    if (mode == RgMode::Off) {
        return {};
    }
    if (!dsdRoute.isEmpty()) {
        // 核心的 DSD 路徑不經過處理器鏈（DoP 與原生改了就變成噪音；轉 PCM 目前也一樣旁通）
        return dsdRoute == QStringLiteral("轉 PCM") ? tr("DSD 轉 PCM 時不作用")
                                                    : tr("DSD 直通時不作用");
    }
    if (!gain) {
        return {};
    }
    const RgChoice& choice = gain->forMode(mode);
    if (!choice.gainDb) {
        return tr("這首沒有 ReplayGain 資訊，未調整音量");
    }
    const double applied = appliedGainDb(choice, preventClipping);
    QString text;
    if (choice.albumFallback) {
        text = tr("這首沒有專輯增益，改用音軌增益 %1（%2）")
                   .arg(formatDb(applied), sourceText(choice.source));
    } else {
        text = tr("目前這首 %1（%2，%3）")
                   .arg(formatDb(applied), mode == RgMode::Album ? tr("專輯增益") : tr("音軌增益"),
                        sourceText(choice.source));
    }
    if (applied < *choice.gainDb) {
        text += tr("；防削波已從 %1 降低").arg(formatDb(*choice.gainDb));
    }
    return text;
}

} // namespace auralis::mod::out

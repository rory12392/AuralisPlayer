#include "track_text.h"

#include "search_criteria.h"

#include <QDateTime>
#include <QLocale>

namespace auralis::mod::lib {

QString durationText(qint64 ms) {
    if (ms < 0) {
        return QStringLiteral("—");
    }
    const qint64 totalSeconds = ms / 1000;
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds / 60) % 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

QString qualityText(const TrackInfo& track) {
    if (track.dsdRate > 0) {
        return formatKey(track);
    }
    if (track.sampleRate <= 0) {
        return {};
    }
    const QString rate = sampleRateText(track.sampleRate);
    return track.bits > 0 ? QStringLiteral("%1 / %2 bit").arg(rate).arg(track.bits) : rate;
}

QString bitrateText(std::optional<int> kbps) {
    if (!kbps) {
        return {};
    }
    // 千分位固定用逗號（介面規範的數量寫法），不跟系統地區設定
    return QStringLiteral("%1 kbps").arg(
        QLocale(QLocale::English, QLocale::UnitedStates).toString(*kbps));
}

QString addedText(std::optional<qint64> utcMs) {
    if (!utcMs) {
        return {};
    }
    return QDateTime::fromMSecsSinceEpoch(*utcMs).toLocalTime().toString(
        QStringLiteral("yyyy-MM-dd"));
}

} // namespace auralis::mod::lib

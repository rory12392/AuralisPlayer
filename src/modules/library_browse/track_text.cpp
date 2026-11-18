#include "track_text.h"

#include "search_criteria.h"

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

} // namespace auralis::mod::lib

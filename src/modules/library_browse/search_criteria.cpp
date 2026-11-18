#include "search_criteria.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace auralis::mod::lib {

namespace {

const QString kDsd = QStringLiteral("DSD");
constexpr int kDsdBaseRate = 44100; // DSD64 = 64 × 44100

/// 「DSD64」→ 64；不是 DSD 倍率的鍵回傳 0。
int dsdMultiplier(const QString& key) {
    if (!key.startsWith(kDsd)) {
        return 0;
    }
    bool ok = false;
    const int multiplier = key.mid(kDsd.size()).toInt(&ok);
    return ok && multiplier > 0 ? multiplier : 0;
}

bool lessNoCase(const QString& a, const QString& b) {
    const int byCase = QString::compare(a, b, Qt::CaseInsensitive);
    return byCase != 0 ? byCase < 0 : a < b;
}

QStringList sortedNoCase(const QSet<QString>& values) {
    QStringList list(values.begin(), values.end());
    std::sort(list.begin(), list.end(), lessNoCase);
    return list;
}

} // namespace

QString formatKey(const QString& codec, int dsdRate) {
    if (codec == kDsd && dsdRate > 0) {
        return kDsd + QString::number(dsdRate / kDsdBaseRate);
    }
    return codec;
}

std::optional<TrackFilter> toTrackFilter(const SearchCriteria& criteria) {
    TrackFilter filter;
    filter.search = criteria.text;
    filter.artist = criteria.artist;
    filter.album = criteria.album;
    filter.year = criteria.year;
    filter.sampleRate = criteria.sampleRate;
    const int multiplier = dsdMultiplier(criteria.format);
    if (multiplier > 0) {
        const int dsdRate = multiplier * kDsdBaseRate;
        if (criteria.sampleRate != 0 && criteria.sampleRate != dsdRate) {
            return std::nullopt;
        }
        filter.codec = kDsd;
        filter.sampleRate = dsdRate;
    } else {
        filter.codec = criteria.format;
    }
    return filter;
}

QString sampleRateText(int rate) {
    if (rate >= 1000000) {
        return QString::number(rate / 1000000.0, 'g', 7) + QStringLiteral(" MHz");
    }
    return QString::number(rate / 1000.0, 'g', 7) + QStringLiteral(" kHz");
}

void FilterOptionsBuilder::add(const QList<TrackInfo>& tracks) {
    for (const TrackInfo& track : tracks) {
        if (!track.artist.isEmpty()) {
            m_artists.insert(track.artist);
        }
        if (!track.albumArtist.isEmpty()) {
            m_artists.insert(track.albumArtist);
        }
        if (!track.album.isEmpty()) {
            m_albums.insert(track.album);
        }
        if (track.year != 0) {
            m_years.insert(track.year);
        }
        const QString format = formatKey(track);
        if (!format.isEmpty()) {
            m_formats.insert(format);
        }
        if (track.sampleRate != 0) {
            m_sampleRates.insert(track.sampleRate);
        }
    }
}

FilterOptions FilterOptionsBuilder::finish() const {
    FilterOptions options;
    options.artists = sortedNoCase(m_artists);
    options.albums = sortedNoCase(m_albums);
    options.years = QList<int>(m_years.begin(), m_years.end());
    std::sort(options.years.begin(), options.years.end(), std::greater<>());
    options.formats = QStringList(m_formats.begin(), m_formats.end());
    std::sort(options.formats.begin(), options.formats.end(),
              [](const QString& a, const QString& b) {
                  const int ma = dsdMultiplier(a);
                  const int mb = dsdMultiplier(b);
                  if ((ma > 0) != (mb > 0)) {
                      return mb > 0; // DSD 排在 PCM 格式之後
                  }
                  return ma > 0 ? ma < mb : lessNoCase(a, b);
              });
    options.sampleRates = QList<int>(m_sampleRates.begin(), m_sampleRates.end());
    std::sort(options.sampleRates.begin(), options.sampleRates.end());
    return options;
}

QString toDragQuery(const DragQuery& query) {
    QJsonObject root{{QStringLiteral("lib"), 1}};
    const SearchCriteria& c = query.criteria;
    const auto putText = [&root](const char* key, const QString& value) {
        if (!value.isEmpty()) {
            root.insert(QLatin1String(key), value);
        }
    };
    putText("text", c.text);
    putText("artist", c.artist);
    putText("album", c.album);
    putText("format", c.format);
    if (c.year != 0) {
        root.insert(QStringLiteral("year"), c.year);
    }
    if (c.sampleRate != 0) {
        root.insert(QStringLiteral("sampleRate"), c.sampleRate);
    }
    if (query.folderId != 0) {
        root.insert(QStringLiteral("folderId"), query.folderId);
    }
    if (!query.albumIds.isEmpty()) {
        QJsonArray ids;
        for (const qint64 id : query.albumIds) {
            ids.append(id);
        }
        root.insert(QStringLiteral("albumIds"), ids);
    }
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

std::optional<DragQuery> parseDragQuery(const QString& json) {
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
    if (!document.isObject() || document.object().value(QStringLiteral("lib")).toInt() != 1) {
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    DragQuery query;
    query.criteria.text = root.value(QStringLiteral("text")).toString();
    query.criteria.artist = root.value(QStringLiteral("artist")).toString();
    query.criteria.album = root.value(QStringLiteral("album")).toString();
    query.criteria.format = root.value(QStringLiteral("format")).toString();
    query.criteria.year = root.value(QStringLiteral("year")).toInt();
    query.criteria.sampleRate = root.value(QStringLiteral("sampleRate")).toInt();
    query.folderId = root.value(QStringLiteral("folderId")).toInteger();
    for (const QJsonValue& id : root.value(QStringLiteral("albumIds")).toArray()) {
        query.albumIds.append(id.toInteger());
    }
    return query;
}

} // namespace auralis::mod::lib

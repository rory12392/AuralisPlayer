#include "shell/track_drag_codec.h"

#include <auralis/track_drag_payload.h>

namespace auralis::ui {

TrackDragCodec::TrackDragCodec(QObject* parent) : QObject(parent) {}

QString TrackDragCodec::mimeType() const {
    return QString::fromLatin1(TrackDragPayload::kMimeType);
}

int TrackDragCodec::maxTracks() const {
    return TrackDragPayload::kMaxTracks;
}

QString TrackDragCodec::encode(const QVariantList& tracks, const QString& query, int count) const {
    TrackDragPayload payload;
    payload.query = query;
    if (count <= TrackDragPayload::kMaxTracks || query.isEmpty()) {
        payload.tracks.reserve(tracks.size());
        for (const QVariant& item : tracks) {
            const QVariantMap track = item.toMap();
            payload.tracks.append({track.value(QStringLiteral("path")).toString(),
                                   track.value(QStringLiteral("subsong")).toUInt()});
        }
    }
    return QString::fromUtf8(payload.toJson());
}

QVariantMap TrackDragCodec::decode(const QString& json) const {
    const auto payload = TrackDragPayload::fromJson(json.toUtf8());
    if (!payload) {
        return {{QStringLiteral("valid"), false}};
    }
    QVariantList tracks;
    tracks.reserve(payload->tracks.size());
    for (const TrackRef& track : payload->tracks) {
        tracks.append(QVariantMap{{QStringLiteral("path"), track.path},
                                  {QStringLiteral("subsong"), track.subsong}});
    }
    return {{QStringLiteral("valid"), true},
            {QStringLiteral("tracks"), tracks},
            {QStringLiteral("query"), payload->query}};
}

} // namespace auralis::ui

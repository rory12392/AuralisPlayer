#pragma once

#include <auralis/services.h>

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>

#include <optional>

namespace auralis {

/// 跨模組拖放與曲目右鍵選單傳遞的曲目清單。拖放來源與目標不互相 include，只認這個型別。
/// 拖放時放在 MIME application/x-auralis-tracks，內容是 toJson() 的結果。
/// 曲目超過 kMaxTracks 首時不列出曲目，改帶查詢條件（query，格式由 LibraryQuery 定義），
/// 由接收端向 LibraryQuery 取，避免全選 4 萬首拖曳時卡住介面。
/// 執行緒：值型別。
struct TrackDragPayload {
    static constexpr const char* kMimeType = "application/x-auralis-tracks";
    static constexpr int kMaxTracks = 5000;

    QList<TrackRef> tracks;
    QString query;

    QByteArray toJson() const {
        QJsonArray items;
        for (const auto& track : tracks) {
            items.append(
                QJsonObject{{QStringLiteral("path"), track.path},
                            {QStringLiteral("subsong"), static_cast<qint64>(track.subsong)}});
        }
        QJsonObject root{{QStringLiteral("tracks"), items}};
        if (!query.isEmpty()) {
            root.insert(QStringLiteral("query"), query);
        }
        return QJsonDocument(root).toJson(QJsonDocument::Compact);
    }

    /// 內容不是這個格式時回傳 std::nullopt（拖進來的是別的東西）。
    static std::optional<TrackDragPayload> fromJson(const QByteArray& json) {
        const QJsonDocument document = QJsonDocument::fromJson(json);
        if (!document.isObject() || !document.object().value(QStringLiteral("tracks")).isArray()) {
            return std::nullopt;
        }
        TrackDragPayload payload;
        payload.query = document.object().value(QStringLiteral("query")).toString();
        for (const auto& item : document.object().value(QStringLiteral("tracks")).toArray()) {
            const QJsonObject object = item.toObject();
            const QString path = object.value(QStringLiteral("path")).toString();
            if (path.isEmpty()) {
                return std::nullopt;
            }
            payload.tracks.append(
                {path, static_cast<quint32>(object.value(QStringLiteral("subsong")).toInteger())});
        }
        return payload;
    }
};

} // namespace auralis

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

namespace auralis::ui {

/// 共用元件 TrackDragArea／TrackDropArea 用的拖放內容編解碼，格式就是 TrackDragPayload 的 JSON，
/// 接收端的 C++ 可以直接用 TrackDragPayload::fromJson 讀。只在 UI 執行緒使用。
// 由 QML 引擎建立（QML_SINGLETON），型別註冊會繼承它，所以不能標 final
class TrackDragCodec : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString mimeType READ mimeType CONSTANT)
    Q_PROPERTY(int maxTracks READ maxTracks CONSTANT)

public:
    explicit TrackDragCodec(QObject* parent = nullptr);

    QString mimeType() const;
    int maxTracks() const;

    /// tracks 是 [{path, subsong}]；count 是拖曳的曲目數（全選時可能比 tracks 多）。
    /// 曲目超過 maxTracks 而且有查詢條件時只帶查詢條件，避免全選 4 萬首時卡住介面。
    Q_INVOKABLE QString encode(const QVariantList& tracks, const QString& query, int count) const;
    /// 回傳 {valid, tracks: [{path, subsong}], query}；內容不是這個格式時 valid 為 false。
    Q_INVOKABLE QVariantMap decode(const QString& json) const;
};

} // namespace auralis::ui

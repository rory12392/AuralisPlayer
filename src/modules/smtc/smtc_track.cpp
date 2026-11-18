#include "smtc_track.h"

#include "smtc_session.h"

#include <auralis/cover_service.h>
#include <auralis/library_query.h>

#include <QBuffer>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPen>

namespace auralis::mod::smtc {

SmtcTrackDisplay displayFor(const TrackRef& track, const std::optional<TrackInfo>& info) {
    if (!info) {
        // 音樂庫沒有這首（例：拖進來的檔案）：和核心的佇列一樣，曲名用不含副檔名的檔名
        return {QFileInfo(track.path).completeBaseName(), {}, {}, {}};
    }
    return {info->title, info->artist, info->album, info->coverKey};
}

SmtcTrackDisplay lookupDisplay(const LibraryQuery& library, const TrackRef& track) {
    auto found = library.findByIdentity(track.path, track.subsong);
    if (!found) {
        qCWarning(lcSmtc).noquote() << "SMTC 查曲目資訊失敗：" << found.error().message;
        return displayFor(track, std::nullopt);
    }
    return displayFor(track, found.value());
}

QByteArray readThumbnail(CoverService& covers, const QString& key) {
    if (key.isEmpty()) {
        return {};
    }
    const auto exported = covers.exportThumbnail(key, kThumbnailSize);
    if (!exported) {
        qCWarning(lcSmtc).noquote() << "SMTC 縮圖輸出失敗：" << exported.error().message;
        return {};
    }
    QFile file(exported.value());
    if (!file.open(QIODevice::ReadOnly)) {
        qCWarning(lcSmtc).noquote()
            << "SMTC 縮圖讀取失敗：" << file.fileName() << file.errorString();
        return {};
    }
    return file.readAll();
}

bool sameTrack(const TrackRef& a, const TrackRef& b) {
    return a.subsong == b.subsong && pathKey(a.path) == pathKey(b.path);
}

const QByteArray& appIconPng() {
    static const QByteArray png = [] {
        // 程式圖示（和 tools/make_app_icon.py 產生的 .ico 同一個圖形）：
        // Void 的 bg 底、ink 圓線（直徑 0.60 邊長、線寬 邊長/40）、琥珀中心點（直徑 0.24 邊長）
        constexpr int size = kThumbnailSize;
        const QColor bg(0x05, 0x05, 0x05);
        const QColor ink(0xf2, 0xf0, 0xeb);
        const QColor amber(0xe0, 0xa3, 0x4a);
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
        image.fill(bg);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        const qreal ring = size * 0.60;
        const qreal line = size / 40.0;
        painter.setPen(QPen(ink, line));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(QPointF(size / 2.0, size / 2.0), ring / 2.0, ring / 2.0);
        const qreal dot = size * 0.24;
        painter.setPen(Qt::NoPen);
        painter.setBrush(amber);
        painter.drawEllipse(QPointF(size / 2.0, size / 2.0), dot / 2.0, dot / 2.0);
        painter.end();
        QByteArray bytes;
        QBuffer buffer(&bytes);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        return bytes;
    }();
    return png;
}

} // namespace auralis::mod::smtc

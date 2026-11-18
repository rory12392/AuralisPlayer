#include "album_detail_controller.h"

#include "albums_controller.h"
#include "track_text.h"

#include <auralis/cover_service.h>
#include <auralis/playback_service.h>

#include <QJSEngine>
#include <QSet>

#include <algorithm>
#include <utility>

namespace auralis::mod::lib {

namespace {
AlbumDetailController* g_instance = nullptr;
} // namespace

AlbumDetailController::AlbumDetailController(AlbumsController& albums, PlaybackService& playback,
                                             QObject* parent)
    : QObject(parent), m_source(albums), m_playback(playback) {
    connect(&m_source, &AlbumsController::libraryChanged, this, &AlbumDetailController::refresh);
    // loaded、error 轉自 LibraryAlbums
    connect(&m_source, &AlbumsController::changed, this, &AlbumDetailController::changed);
}

bool AlbumDetailController::loaded() const {
    return m_source.loaded();
}

QString AlbumDetailController::error() const {
    return m_source.error();
}

QString AlbumDetailController::summaryText() const {
    if (!m_found) {
        return {};
    }
    QStringList parts;
    if (m_entry.info.year > 0) {
        parts.append(QString::number(m_entry.info.year));
    }
    parts.append(tr("%1 首").arg(trackCount()));
    parts.append(durationText(m_entry.durationMs));
    if (!m_entry.formatText.isEmpty()) {
        parts.append(m_entry.formatText);
    }
    // 段與段之間用全形空格（S-04 的寫法）
    return parts.join(QChar(0x3000));
}

QString AlbumDetailController::coverUrl() const {
    return m_entry.info.coverKey.isEmpty() ? QString()
                                           : CoverService::imageUrl(m_entry.info.coverKey);
}

void AlbumDetailController::open(qint64 albumId) {
    if (albumId != m_albumId) {
        m_albumId = albumId;
        refresh();
    }
    m_source.load();
}

void AlbumDetailController::reload() {
    m_source.reload();
}

void AlbumDetailController::refresh() {
    const QList<AlbumEntry>& all = m_source.allAlbums();
    const auto it = std::find_if(all.cbegin(), all.cend(), [this](const AlbumEntry& a) {
        return m_albumId > 0 && a.info.id == m_albumId;
    });
    m_found = it != all.cend();
    m_entry = m_found ? *it : AlbumEntry{};
    QSet<int> discs;
    m_hasTrackArtists = false;
    for (const TrackInfo& t : std::as_const(m_entry.tracks)) {
        discs.insert(t.discNo);
        if (t.artist != m_entry.artist) {
            m_hasTrackArtists = true;
        }
    }
    m_discCount = static_cast<int>(discs.size());
    m_tracks.reset(m_entry.tracks);
    emit changed();
}

bool AlbumDetailController::play(int from) {
    // 和專輯頁的播放鍵、詳細檢視雙擊曲目同一個做法
    return m_found && m_source.playEntry(m_entry, from);
}

bool AlbumDetailController::enqueueAll() {
    if (!m_found || m_entry.tracks.isEmpty()) {
        return false;
    }
    QList<TrackRef> refs;
    refs.reserve(m_entry.tracks.size());
    for (const TrackInfo& t : std::as_const(m_entry.tracks)) {
        refs.append(t.ref);
    }
    m_playback.enqueueLast(refs);
    return true;
}

void AlbumDetailController::setInstance(AlbumDetailController* instance) {
    g_instance = instance;
}

AlbumDetailController* AlbumDetailController::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "AlbumDetailController", "模組要在載入頁面前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::mod::lib

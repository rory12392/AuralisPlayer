#include "albums_controller.h"

#include "search_controller.h"
#include "track_selection.h"

#include <auralis/library_query.h>
#include <auralis/module_settings.h>
#include <auralis/playback_service.h>
#include <auralis/track_drag_payload.h>

#include <QJSEngine>
#include <QLoggingCategory>

#include <algorithm>
#include <exception>

Q_DECLARE_LOGGING_CATEGORY(lcLib)

namespace auralis::mod::lib {

namespace {

AlbumsController* g_instance = nullptr;

const QString kViewKey = QStringLiteral("albumsView");
const QString kSortKey = QStringLiteral("albumsSort");

QString validView(const QString& view) {
    if (view == QLatin1String("list") || view == QLatin1String("detail")) {
        return view;
    }
    return QStringLiteral("grid");
}

/// 篩選條件去掉搜尋字串（專輯頁不受搜尋框影響）。
SearchCriteria filterOf(const SearchController& search) {
    SearchCriteria c;
    c.artist = search.artist();
    c.album = search.album();
    c.year = search.year();
    c.format = search.format();
    c.sampleRate = search.sampleRate();
    return c;
}

/// 工作執行緒：讀專輯與全部曲目並組起來；例外也轉成錯誤訊息（錯誤邊界）。
QString readAlbums(const LibraryQuery& library, QList<AlbumEntry>& out) {
    try {
        const auto albums = library.albums();
        if (!albums) {
            return albums.error().message;
        }
        const auto tracks = library.tracks(TrackFilter{});
        if (!tracks) {
            return tracks.error().message;
        }
        out = buildAlbums(albums.value(), tracks.value());
        return {};
    } catch (const std::exception& e) {
        return QString::fromUtf8(e.what());
    }
}

} // namespace

AlbumsController::AlbumsController(LibraryQuery& library, PlaybackService& playback,
                                   SearchController& search, ModuleSettings* settings,
                                   QObject* parent)
    : QObject(parent), m_library(library), m_playback(playback), m_search(search),
      m_settings(settings) {
    m_pool.setMaxThreadCount(1);
    m_view = validView(m_settings ? m_settings->value(kViewKey).toString() : QString());
    m_sort = albumSortFromName(m_settings ? m_settings->value(kSortKey).toString() : QString());
    m_filter = filterOf(m_search);
    connect(&m_search, &SearchController::criteriaChanged, this,
            &AlbumsController::onCriteriaChanged);
    connect(&m_library, &LibraryQuery::scanFinished, this, [this] {
        if (m_loaded || m_busy) {
            reload();
        }
    });
    connect(&m_playback, &PlaybackService::trackChanged, this,
            &AlbumsController::updateCurrentAlbum);
}

AlbumsController::~AlbumsController() {
    shutdown();
}

void AlbumsController::shutdown() {
    ++*m_generation;
    m_pool.clear();
    m_pool.waitForDone();
}

void AlbumsController::setView(const QString& view) {
    const QString valid = validView(view);
    if (valid == m_view) {
        return;
    }
    m_view = valid;
    if (m_settings) {
        m_settings->setValue(kViewKey, m_view);
    }
    emit viewChanged();
}

void AlbumsController::setSort(const QString& sort) {
    const AlbumSort value = albumSortFromName(sort);
    if (value == m_sort) {
        return;
    }
    m_sort = value;
    if (m_settings) {
        m_settings->setValue(kSortKey, albumSortName(m_sort));
    }
    emit sortChanged();
    apply();
}

void AlbumsController::load() {
    if (!m_loaded && !m_busy) {
        reload();
    }
}

void AlbumsController::reload() {
    if (m_busy) {
        m_reloadPending = true;
        return;
    }
    m_busy = true;
    m_reloadPending = false;
    emit changed();
    const quint64 generation = ++*m_generation;
    m_pool.start([this, generation] {
        if (generation != *m_generation) {
            return;
        }
        QList<AlbumEntry> albums;
        const QString error = readAlbums(m_library, albums);
        if (generation != *m_generation) {
            return;
        }
        QMetaObject::invokeMethod(
            this,
            [this, generation, albums = std::move(albums), error]() mutable {
                onLoaded(generation, std::move(albums), error);
            },
            Qt::QueuedConnection);
    });
}

void AlbumsController::onLoaded(quint64 generation, QList<AlbumEntry> albums,
                                const QString& error) {
    if (generation != *m_generation) {
        return;
    }
    m_busy = false;
    m_error = error;
    if (error.isEmpty()) {
        m_all = std::move(albums);
        m_loaded = true;
        apply();
        updateCurrentAlbum();
        emit libraryChanged();
    } else {
        qCWarning(lcLib) << "專輯讀取失敗：" << error;
    }
    emit changed();
    if (m_reloadPending) {
        reload();
    }
}

void AlbumsController::apply() {
    QList<AlbumEntry> shown;
    shown.reserve(m_all.size());
    for (const AlbumEntry& album : std::as_const(m_all)) {
        if (albumMatches(album, m_filter)) {
            shown.append(album);
        }
    }
    sortAlbums(shown, m_sort);
    m_model.update(std::move(shown));
    emit changed();
}

void AlbumsController::onCriteriaChanged() {
    const SearchCriteria filter = filterOf(m_search);
    if (filter == m_filter) {
        return; // 只改了搜尋字串
    }
    m_filter = filter;
    if (m_loaded) {
        apply();
    }
}

void AlbumsController::updateCurrentAlbum() {
    qint64 id = 0;
    if (const auto entry = m_playback.currentEntry()) {
        const TrackRef& ref = entry->track;
        for (const AlbumEntry& album : std::as_const(m_all)) {
            const bool found =
                std::any_of(album.tracks.cbegin(), album.tracks.cend(), [&ref](const TrackInfo& t) {
                    return t.ref.subsong == ref.subsong && t.ref.path == ref.path;
                });
            if (found) {
                id = album.info.id;
                break;
            }
        }
    }
    if (id != m_currentAlbumId) {
        m_currentAlbumId = id;
        emit currentAlbumChanged();
    }
}

qint64 AlbumsController::albumIdAt(int row) const {
    return row >= 0 && row < m_model.rowCount() ? m_model.album(row).info.id : 0;
}

QVariantMap AlbumsController::trackLocation(const QString& payloadJson) const {
    const auto payload = TrackDragPayload::fromJson(payloadJson.toUtf8());
    if (!payload || payload->tracks.size() != 1) {
        return {};
    }
    const TrackRef& ref = payload->tracks.first();
    for (const AlbumEntry& album : m_all) {
        for (const TrackInfo& track : album.tracks) {
            if (track.ref.path == ref.path && track.ref.subsong == ref.subsong) {
                return {{QStringLiteral("albumId"), album.info.id},
                        {QStringLiteral("artist"), album.artist}};
            }
        }
    }
    const TrackInfo& hint = TrackSelection::trackHint();
    if (hint.albumId > 0 && hint.ref.path == ref.path && hint.ref.subsong == ref.subsong) {
        return {{QStringLiteral("albumId"), hint.albumId},
                {QStringLiteral("artist"),
                 hint.albumArtist.isEmpty() ? hint.artist : hint.albumArtist}};
    }
    return {};
}

bool AlbumsController::playAlbum(int row, int from) {
    if (row < 0 || row >= m_model.rowCount()) {
        return false;
    }
    return playEntry(m_model.album(row), from);
}

bool AlbumsController::playEntry(const AlbumEntry& album, int from) {
    if (from < 0 || from >= album.tracks.size()) {
        return false;
    }
    QList<TrackRef> tracks;
    tracks.reserve(album.tracks.size());
    for (const TrackInfo& t : album.tracks) {
        tracks.append(t.ref);
    }
    m_playback.playFrom(tracks, from);
    return true;
}

void AlbumsController::setInstance(AlbumsController* instance) {
    g_instance = instance;
}

AlbumsController* AlbumsController::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "AlbumsController", "模組要在載入頁面前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::mod::lib

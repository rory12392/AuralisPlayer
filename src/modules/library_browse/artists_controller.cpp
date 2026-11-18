#include "artists_controller.h"

#include "albums_controller.h"

#include <QJSEngine>

namespace auralis::mod::lib {

namespace {
ArtistsController* g_instance = nullptr;
} // namespace

ArtistsController::ArtistsController(AlbumsController& albums, QObject* parent)
    : QObject(parent), m_source(albums) {
    connect(&m_source, &AlbumsController::libraryChanged, this, &ArtistsController::rebuild);
    connect(&m_source, &AlbumsController::sortChanged, this, &ArtistsController::applySelection);
    connect(&m_source, &AlbumsController::changed, this, &ArtistsController::changed);
    connect(&m_source, &AlbumsController::currentAlbumChanged, this,
            &ArtistsController::currentAlbumChanged);
    if (m_source.loaded()) {
        rebuild();
    }
}

int ArtistsController::selectedRow() const {
    return m_all ? 0 : m_artists.rowOf(m_selected);
}

bool ArtistsController::loaded() const {
    return m_source.loaded();
}

bool ArtistsController::busy() const {
    return m_source.busy();
}

QString ArtistsController::error() const {
    return m_source.error();
}

qint64 ArtistsController::currentAlbumId() const {
    return m_source.currentAlbumId();
}

void ArtistsController::load() {
    m_source.load();
}

void ArtistsController::reload() {
    m_source.reload();
}

void ArtistsController::rebuild() {
    const QList<AlbumEntry>& all = m_source.allAlbums();
    m_artists.update(groupArtists(all), static_cast<int>(all.size()));
    if (m_resolvePending) {
        // 讀完前收到的開頁參數 {artist}（T-25）：同名分組，或有這位曲目藝人的專輯的分組
        m_resolvePending = false;
        m_selected = resolveArtist(m_selected);
        m_all = m_selected.isEmpty();
    }
    if (!m_all && m_artists.rowOf(m_selected) < 0) {
        m_all = true; // 那位藝人不在了（例：重新掃描後改了標籤）
        m_selected.clear();
    }
    emit selectionChanged(); // 列號可能因新藝人插入而變
    applySelection();
}

void ArtistsController::applySelection() {
    QList<AlbumEntry> shown =
        m_all ? m_source.allAlbums() : albumsOfArtist(m_source.allAlbums(), m_selected);
    sortAlbums(shown, m_source.sortValue());
    m_albums.update(std::move(shown));
    emit changed();
}

void ArtistsController::select(int row) {
    if (row < 0 || row >= m_artists.rowCount()) {
        return;
    }
    const bool all = row == 0;
    const QString name = all ? QString() : m_artists.artist(row).name;
    if (all == m_all && name == m_selected) {
        return;
    }
    m_all = all;
    m_selected = name;
    emit selectionChanged();
    applySelection();
}

QString ArtistsController::resolveArtist(const QString& name) const {
    if (m_artists.rowOf(name) >= 0) {
        return name;
    }
    // 不是分組名稱（例：正在播放面板給的是曲目藝人）：第一張有這位曲目藝人的專輯所屬的分組
    for (const AlbumEntry& album : m_source.allAlbums()) {
        for (const TrackInfo& track : album.tracks) {
            if (track.artist == name) {
                return album.artist;
            }
        }
    }
    return {};
}

void ArtistsController::selectArtist(const QString& name) {
    QString group = name;
    if (loaded()) {
        group = name.isEmpty() ? QString() : resolveArtist(name);
        m_resolvePending = false;
    } else {
        m_resolvePending = !name.isEmpty(); // 讀完（rebuild）再對
    }
    const bool all = group.isEmpty();
    if (all == m_all && (all || group == m_selected)) {
        return;
    }
    m_all = all;
    m_selected = group;
    emit selectionChanged();
    applySelection();
}

bool ArtistsController::playAlbum(int row, int from) {
    if (row < 0 || row >= m_albums.rowCount()) {
        return false;
    }
    return m_source.playEntry(m_albums.album(row), from);
}

qint64 ArtistsController::albumIdAt(int row) const {
    return row >= 0 && row < m_albums.rowCount() ? m_albums.album(row).info.id : 0;
}

void ArtistsController::setInstance(ArtistsController* instance) {
    g_instance = instance;
}

ArtistsController* ArtistsController::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "ArtistsController", "模組要在載入頁面前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::mod::lib

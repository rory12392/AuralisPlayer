#include "playlist_tracks.h"

#include "playlist_parse.h"
#include "playlist_remap.h"
#include "playlists_controller.h"

#include <auralis/cover_service.h>
#include <auralis/playback_service.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QPointer>

Q_DECLARE_LOGGING_CATEGORY(lcPlaylists)

namespace auralis::mod::pl {

namespace {
PlaylistTracks* g_instance = nullptr;
constexpr int kDsdBaseRate = 44100; // DSD64 = 64 × 44100

QString durationText(qint64 ms) {
    if (ms < 0) {
        return QStringLiteral("—");
    }
    const qint64 total = ms / 1000;
    const qint64 hours = total / 3600;
    const qint64 minutes = (total / 60) % 60;
    const qint64 seconds = total % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes).arg(seconds, 2, 10, QLatin1Char('0'));
}

// 格式與取樣率的寫法同音樂庫清單（介面規範 §6.1：DSD64、96 kHz / 24 bit）
QString formatText(const TrackInfo& t) {
    if (t.codec == QStringLiteral("DSD") && t.dsdRate > 0) {
        return QStringLiteral("DSD%1").arg(t.dsdRate / kDsdBaseRate);
    }
    return t.codec;
}

QString qualityText(const TrackInfo& t) {
    if (t.dsdRate > 0) {
        return formatText(t);
    }
    if (t.sampleRate <= 0) {
        return {};
    }
    const QString rate =
        t.sampleRate >= 1000000
            ? QString::number(t.sampleRate / 1000000.0, 'g', 7) + QStringLiteral(" MHz")
            : QString::number(t.sampleRate / 1000.0, 'g', 7) + QStringLiteral(" kHz");
    return t.bits > 0 ? QStringLiteral("%1 / %2 bit").arg(rate).arg(t.bits) : rate;
}

TrackDisplay displayOf(const TrackRef& ref, const std::optional<TrackInfo>& found) {
    TrackDisplay d;
    if (!found) {
        d.title = QFileInfo(ref.path).completeBaseName();
        return d;
    }
    d.missing = false;
    d.title = found->title;
    d.artist = found->artist;
    d.album = found->album;
    d.format = formatText(*found);
    d.quality = qualityText(*found);
    d.coverKey = found->coverKey;
    d.durationMs = found->durationMs;
    return d;
}
} // namespace

/// 背景載入的結果。
struct PlaylistTracks::Loaded {
    QList<Row> rows;
    QHash<QString, TrackDisplay> resolved;
    QString error;
};

PlaylistTracks::PlaylistTracks(PlaylistStore& store, PlaylistsController& playlists,
                               LibraryQuery* library, PlaybackService* playback, QObject* parent)
    : QAbstractListModel(parent), m_store(store), m_playlists(playlists), m_library(library),
      m_playback(playback), m_exists(contentPathExists) {
    // 一次只載一個播放清單，新的載入讓舊的提早結束
    m_pool.setMaxThreadCount(1);
    connect(&m_playlists, &PlaylistsController::itemsChanged, this,
            &PlaylistTracks::onItemsChanged);
    if (m_library != nullptr) {
        connect(m_library, &LibraryQuery::scanFinished, this, [this] {
            m_cache.clear();
            if (m_playlistId > 0) {
                reload();
            }
        });
    }
    if (m_playback != nullptr) {
        connect(m_playback, &PlaybackService::trackChanged, this, &PlaylistTracks::updateCurrent);
        updateCurrent();
    }
}

PlaylistTracks::~PlaylistTracks() {
    m_generation->fetch_add(1);
    m_pool.clear();
    m_pool.waitForDone();
}

int PlaylistTracks::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant PlaylistTracks::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return {};
    }
    const Row& r = m_rows.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case TitleRole:
        return r.display.title;
    case ArtistRole:
        return r.display.artist;
    case AlbumRole:
        return r.display.album;
    case DurationTextRole:
        return r.display.missing ? QString() : durationText(r.display.durationMs);
    case FormatRole:
        return r.display.format;
    case QualityTextRole:
        return r.display.quality;
    case CoverUrlRole:
        return r.display.coverKey.isEmpty() ? QString()
                                            : CoverService::imageUrl(r.display.coverKey);
    case MissingRole:
        // 匯入時標的找不到（含被拒絕的）、或音樂庫沒有而且磁碟上也沒有（T-52）
        return r.item.missing || (r.display.missing && !r.display.onDisk);
    case NotInLibraryRole:
        return !r.item.missing && r.display.missing && r.display.onDisk;
    case PathRole:
        return r.item.track.path;
    case SubsongRole:
        return static_cast<int>(r.item.track.subsong);
    case KeyRole:
        return keyOf(r.item.track.path, static_cast<int>(r.item.track.subsong));
    default:
        return {};
    }
}

QHash<int, QByteArray> PlaylistTracks::roleNames() const {
    return {{TitleRole, "title"},       {ArtistRole, "artist"},
            {AlbumRole, "album"},       {DurationTextRole, "durationText"},
            {FormatRole, "format"},     {QualityTextRole, "qualityText"},
            {CoverUrlRole, "coverUrl"}, {MissingRole, "missing"},
            {PathRole, "path"},         {SubsongRole, "subsong"},
            {KeyRole, "key"},           {NotInLibraryRole, "notInLibrary"}};
}

int PlaylistTracks::missingCount() const {
    int missing = 0;
    for (const Row& r : m_rows) {
        if (r.item.missing || (r.display.missing && !r.display.onDisk)) {
            ++missing;
        }
    }
    return missing;
}

void PlaylistTracks::setExistsCheck(ExistsCheck exists) {
    m_exists = std::move(exists);
}

void PlaylistTracks::setPlaylistId(qint64 id) {
    if (id == m_playlistId) {
        return;
    }
    m_playlistId = id;
    emit playlistIdChanged();
    // 換清單時先清空，不要讓上一個清單的曲目留在畫面上
    beginResetModel();
    m_rows.clear();
    m_selected.clear();
    endResetModel();
    m_selectedCount = 0;
    m_anchor = -1;
    selectionDone();
    emit summaryChanged();
    reload();
}

QString PlaylistTracks::totalDurationText() const {
    qint64 total = 0;
    for (const Row& r : m_rows) {
        if (!r.display.missing && r.display.durationMs > 0) {
            total += r.display.durationMs;
        }
    }
    return durationText(total);
}

void PlaylistTracks::reload() {
    const quint64 generation = m_generation->fetch_add(1) + 1;
    if (m_rows.isEmpty() || m_status == QStringLiteral("error")) {
        setStatus(QStringLiteral("loading"));
    }
    if (m_playlistId <= 0) {
        return;
    }
    QPointer<PlaylistTracks> self(this);
    m_pool.start([self, generation, latest = m_generation, store = &m_store, library = m_library,
                  id = m_playlistId, cache = m_cache, exists = m_exists] {
        auto loaded = std::make_shared<Loaded>();
        // 音樂庫沒有的曲目查磁碟分「不在音樂庫」與「找不到檔案」（T-52）；對應過的路徑以當時的起點查
        const auto remaps = store->remaps();
        const QList<PathRemap> rules = remaps ? remaps.value() : QList<PathRemap>();
        const auto items = store->items(id);
        if (!items) {
            loaded->error = items.error().message;
        } else {
            loaded->rows.reserve(items.value().size());
            for (const PlaylistItem& item : items.value()) {
                if (latest->load() != generation) {
                    return; // 已經有新的載入
                }
                const QString key = keyOf(item.track.path, static_cast<int>(item.track.subsong));
                TrackDisplay display;
                if (const auto known = cache.constFind(key); known != cache.constEnd()) {
                    display = known.value();
                } else if (const auto again = loaded->resolved.constFind(key);
                           again != loaded->resolved.constEnd()) {
                    display = again.value(); // 同一首出現第二次
                } else {
                    std::optional<TrackInfo> found;
                    if (library != nullptr) {
                        const auto result =
                            library->findByIdentity(item.track.path, item.track.subsong);
                        if (!result) {
                            loaded->error = result.error().message;
                            break;
                        }
                        found = result.value();
                    }
                    display = displayOf(item.track, found);
                    if (display.missing) {
                        display.onDisk = existsOnDisk(item.track.path, rules, exists);
                    }
                    loaded->resolved.insert(key, display);
                }
                loaded->rows.append({item, display});
            }
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, generation, loaded] {
                if (self) {
                    self->applyLoaded(generation, loaded);
                }
            },
            Qt::QueuedConnection);
    });
}

void PlaylistTracks::applyLoaded(quint64 generation, const std::shared_ptr<Loaded>& loaded) {
    if (m_generation->load() != generation) {
        return;
    }
    if (!loaded->error.isEmpty()) {
        qCWarning(lcPlaylists) << "讀取播放清單的曲目失敗" << loaded->error;
        setStatus(QStringLiteral("error"));
        return;
    }
    for (auto it = loaded->resolved.cbegin(); it != loaded->resolved.cend(); ++it) {
        // 音樂庫沒有的不留：磁碟上有沒有會變（重新對應、重新檢查後重讀要再查，T-52）
        if (!it.value().missing) {
            m_cache.insert(it.key(), it.value());
        }
    }
    beginResetModel();
    m_rows = loaded->rows;
    m_selected.assign(static_cast<size_t>(m_rows.size()), 0);
    endResetModel();
    m_selectedCount = 0;
    m_anchor = -1;
    selectionDone();
    emit summaryChanged();
    setStatus(QStringLiteral("ready"));
}

void PlaylistTracks::onItemsChanged(qint64 id) {
    if (id == m_playlistId && !m_selfChange) {
        reload();
    }
}

void PlaylistTracks::setStatus(const QString& status) {
    if (status != m_status) {
        m_status = status;
        emit statusChanged();
    }
}

bool PlaylistTracks::playFrom(int row) {
    if (row < 0 || row >= m_rows.size()) {
        return false;
    }
    // 找不到的項目（被拒絕的路徑、經過連結的路徑）不交給播放：跳過它們，起點跟著往前算
    const auto blocked = [](const Row& r) {
        return blockedFromOpening(r.item.missing);
    };
    if (blocked(m_rows.at(row))) {
        return false;
    }
    QList<TrackRef> tracks;
    tracks.reserve(m_rows.size());
    int start = 0;
    for (int i = 0; i < m_rows.size(); ++i) {
        if (blocked(m_rows.at(i))) {
            continue;
        }
        if (i < row) {
            ++start;
        }
        tracks.append(m_rows.at(i).item.track);
    }
    return m_playlists.playFrom(tracks, start);
}

bool PlaylistTracks::playAll() {
    // 第一首找不到時從第一首找得到的開始（和側欄的「播放」一致；M-04 審查）
    for (int i = 0; i < m_rows.size(); ++i) {
        if (!blockedFromOpening(m_rows.at(i).item.missing)) {
            return playFrom(i);
        }
    }
    if (!m_rows.isEmpty()) {
        m_playlists.reportNothingPlayable();
    }
    return false;
}

int PlaylistTracks::dragCount(int row) const {
    if (row < 0 || row >= m_rows.size()) {
        return 0;
    }
    // 拖出去的是 selectedTracks()（不含找不到的）：小卡的數字照它算（M-04 審查）
    if (!isSelected(row)) {
        return blockedFromOpening(m_rows.at(row).item.missing) ? 0 : 1;
    }
    int count = 0;
    for (const int selected : selectedRows()) {
        if (!blockedFromOpening(m_rows.at(selected).item.missing)) {
            ++count;
        }
    }
    return count;
}

QString PlaylistTracks::keyOf(const QString& path, int subsong) {
    return pathKey(path) + QLatin1Char('|') + QString::number(subsong);
}

void PlaylistTracks::updateCurrent() {
    const auto entry = m_playback->currentEntry();
    const QString key =
        entry ? keyOf(entry->track.path, static_cast<int>(entry->track.subsong)) : QString();
    if (key != m_currentKey) {
        m_currentKey = key;
        emit currentKeyChanged();
    }
}

void PlaylistTracks::setInstance(PlaylistTracks* instance) {
    g_instance = instance;
}

PlaylistTracks* PlaylistTracks::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "PlaylistTracks", "模組要在載入頁面前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::mod::pl

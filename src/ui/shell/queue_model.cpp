#include "shell/queue_model.h"

#include <auralis/cover_service.h>
#include <auralis/track_drag_payload.h>

#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>

#include <algorithm>

namespace auralis::ui {

namespace {

QString metaKey(const TrackRef& track) {
    return track.path + QLatin1Char('\n') + QString::number(track.subsong);
}

QueueTrackMeta fallbackMeta(const TrackRef& track) {
    // 音樂庫還沒查到或沒有這首：曲名用不含副檔名的檔名（和音樂庫沒有曲名標籤時一樣）
    return {QFileInfo(track.path).completeBaseName(), {}, -1};
}

} // namespace

QueueModel::QueueModel(PlaybackService& service, Notifier* notifier, QueueMetaLookup lookup,
                       QObject* parent)
    : QAbstractListModel(parent), m_service(service), m_notifier(notifier),
      m_lookup(std::move(lookup)) {
    // 查詢依序做一批就好，不和掃描搶執行緒
    m_pool.setMaxThreadCount(1);
    m_resolvePool.setMaxThreadCount(1);
    connect(&m_service, &PlaybackService::queueChanged, this, &QueueModel::rebuild);
    // 目前曲目的長度在開始播放後才知道
    connect(&m_service, &PlaybackService::trackChanged, this, [this] {
        if (m_hasCurrent && !m_rows.isEmpty()) {
            emit dataChanged(index(0), index(0), {DurationMsRole});
        }
        emit changed();
    });
    rebuild();
}

QueueModel::~QueueModel() {
    m_resolvePool.waitForDone();
    m_pool.waitForDone();
}

int QueueModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant QueueModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_rows.size()) {
        return {};
    }
    const QueueEntry& entry = m_rows.at(index.row());
    const bool current = m_hasCurrent && index.row() == 0;
    switch (role) {
    case EntryIdRole:
        return entry.id;
    case TitleRole:
        return meta(entry.track).title;
    case ArtistRole:
        return meta(entry.track).artist;
    case DurationMsRole: {
        const qint64 known = meta(entry.track).durationMs;
        return current && known < 0 ? m_service.durationMs() : known;
    }
    case CurrentRole:
        return current;
    case UnplayableRole:
        return entry.unplayable;
    case CoverUrlRole:
        return CoverService::imageUrl(meta(entry.track).coverKey);
    default:
        return {};
    }
}

QHash<int, QByteArray> QueueModel::roleNames() const {
    return {{EntryIdRole, "entryId"},  {TitleRole, "title"},
            {ArtistRole, "artist"},    {DurationMsRole, "durationMs"},
            {CurrentRole, "current"},  {UnplayableRole, "unplayable"},
            {CoverUrlRole, "coverUrl"}};
}

qint64 QueueModel::totalDurationMs() const {
    qint64 total = 0;
    for (int row = 0; row < m_rows.size(); ++row) {
        const qint64 duration = data(index(row), DurationMsRole).toLongLong();
        total += std::max<qint64>(duration, 0);
    }
    return total;
}

QueueTrackMeta QueueModel::meta(const TrackRef& track) const {
    const auto found = m_cache.constFind(metaKey(track));
    return found != m_cache.constEnd() ? *found : fallbackMeta(track);
}

void QueueModel::playAt(int row) {
    if (row >= 0 && row < count()) {
        m_service.playAt(queueIndex(row));
    }
}

void QueueModel::removeAt(int row) {
    if (row >= 0 && row < count()) {
        m_service.remove({m_rows.at(row).id});
    }
}

void QueueModel::moveTo(int from, int to) {
    const int first = m_hasCurrent ? 1 : 0;
    if (from < first || from >= count()) {
        return;
    }
    to = std::clamp(to, first, count() - 1);
    if (to != from) {
        m_service.move(queueIndex(from), queueIndex(to));
    }
}

bool QueueModel::insertTracks(const QString& json, int row) {
    // 只帶查詢條件時曲目晚一點才到，佇列可能已經變了（刪除、重排、切歌）：記下放下位置的那一項，
    // 到時候重新找它在第幾列；放在最後的照樣放最後；那一項已經不在時退回加在最後（M-04 審查）
    row = std::clamp(row, 0, count());
    const quint64 anchor = row < count() ? m_rows.at(row).id : 0;
    return withTracks(json, this, [this, anchor](const QList<TrackRef>& tracks) {
        const auto found = std::find_if(m_rows.cbegin(), m_rows.cend(),
                                        [anchor](const QueueEntry& e) { return e.id == anchor; });
        placeTracks(tracks, static_cast<int>(found - m_rows.cbegin()));
    });
}

bool QueueModel::withTracks(const QString& json, QObject* context,
                            std::function<void(const QList<TrackRef>&)> apply) {
    const auto payload = TrackDragPayload::fromJson(json.toUtf8());
    if (!payload) {
        return false;
    }
    if (!payload->tracks.isEmpty()) {
        apply(payload->tracks);
        return true;
    }
    if (payload->query.isEmpty() || !m_resolver) {
        return false;
    }
    // 依查詢條件取曲目要讀資料庫（4 萬首約數百毫秒），不在 UI 執行緒做（Architecture §5）
    QPointer<QObject> target(context);
    m_resolvePool.start(
        [this, target, apply = std::move(apply), query = payload->query, resolver = m_resolver] {
            const Result<QList<TrackInfo>> found = resolver(query);
            QList<TrackRef> tracks;
            QString error;
            if (found) {
                tracks.reserve(found.value().size());
                for (const TrackInfo& info : found.value()) {
                    tracks.append(info.ref);
                }
            } else {
                error = found.error().message;
            }
            QMetaObject::invokeMethod(
                this,
                [this, target, apply, tracks = std::move(tracks), error, ok = found.hasValue()] {
                    if (!ok) {
                        if (m_notifier != nullptr) {
                            m_notifier->notify(tr("無法取得曲目：%1").arg(error),
                                               Notifier::Kind::Error);
                        }
                        return;
                    }
                    if (target && !tracks.isEmpty()) {
                        apply(tracks);
                    }
                },
                Qt::QueuedConnection);
        });
    return true;
}

void QueueModel::placeTracks(const QList<TrackRef>& tracks, int row) {
    // 不能插到「正在播放」前面（那是已播過的位置）
    row = std::clamp(row, m_hasCurrent ? 1 : 0, count());
    if (m_hasCurrent && row == 1) {
        m_service.enqueueNext(tracks);
        return;
    }
    const int oldSize = static_cast<int>(m_service.queue().size());
    const int target = queueIndex(row);
    m_service.enqueueLast(tracks);
    // 服務沒有「插在第 n 項」：加到最後再移動。每次移動都會重建清單，所以移比較少的那一邊：
    // 新曲目一首一首移到指到的位置，或原本在那之後的項目一項一項移到最後（4 萬首時是後者）
    const int tail = std::max(oldSize - target, 0);
    if (tail <= tracks.size()) {
        for (int k = 0; k < tail; ++k) {
            m_service.move(target, oldSize + static_cast<int>(tracks.size()) - 1);
        }
        return;
    }
    for (int k = 0; k < tracks.size(); ++k) {
        m_service.move(oldSize + k, target + k);
    }
}

void QueueModel::clearQueue() {
    QList<TrackRef> removed;
    for (int row = m_hasCurrent ? 1 : 0; row < m_rows.size(); ++row) {
        removed.append(m_rows.at(row).track);
    }
    m_service.clear();
    if (m_notifier == nullptr || removed.isEmpty()) {
        return;
    }
    QPointer<QueueModel> self(this);
    m_notifier->notify(tr("已清空佇列"), Notifier::Kind::Info, tr("復原"), [self, removed] {
        if (self) {
            self->m_service.enqueueLast(removed);
        }
    });
}

void QueueModel::waitForLookups() {
    m_resolvePool.waitForDone();
    QCoreApplication::sendPostedEvents(this);
    m_pool.waitForDone();
    QCoreApplication::sendPostedEvents(this);
}

void QueueModel::rebuild() {
    beginResetModel();
    const QList<QueueEntry> entries = m_service.queue();
    const int current = m_service.currentIndex();
    m_hasCurrent = current >= 0 && current < entries.size();
    m_start = m_hasCurrent ? current : 0;
    m_rows = entries.mid(m_start);
    endResetModel();
    emit changed();
    requestMeta();
}

void QueueModel::requestMeta() {
    if (!m_lookup) {
        return;
    }
    QList<TrackRef> missing;
    for (const QueueEntry& entry : std::as_const(m_rows)) {
        const QString key = metaKey(entry.track);
        if (!m_cache.contains(key) && !m_pending.contains(key)) {
            m_pending.insert(key);
            missing.append(entry.track);
        }
    }
    if (missing.isEmpty()) {
        return;
    }
    // 查音樂庫是 I/O，不在 UI 執行緒做（Architecture §5）
    m_pool.start([this, missing, lookup = m_lookup] {
        QHash<QString, QueueTrackMeta> found;
        for (const TrackRef& track : missing) {
            found.insert(metaKey(track), lookup(track).value_or(fallbackMeta(track)));
        }
        QMetaObject::invokeMethod(this, [this, found] { applyMeta(found); }, Qt::QueuedConnection);
    });
}

void QueueModel::applyMeta(const QHash<QString, QueueTrackMeta>& found) {
    for (auto it = found.constBegin(); it != found.constEnd(); ++it) {
        m_cache.insert(it.key(), it.value());
        m_pending.remove(it.key());
    }
    if (!m_rows.isEmpty()) {
        emit dataChanged(index(0), index(count() - 1),
                         {TitleRole, ArtistRole, DurationMsRole, CoverUrlRole});
    }
    emit changed();
    emit metaChanged();
}

} // namespace auralis::ui

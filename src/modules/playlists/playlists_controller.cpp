#include "playlists_controller.h"

#include "playlist_parse.h"

#include <auralis/dialogs.h>
#include <auralis/job_runner.h>
#include <auralis/notifier.h>
#include <auralis/playback_service.h>

#include <QCoreApplication>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QPointer>
#include <QSet>

Q_LOGGING_CATEGORY(lcPlaylists, "auralis.pl")

namespace auralis::mod::pl {

namespace {
PlaylistsController* g_instance = nullptr;

QString tr(const char* text) {
    return QCoreApplication::translate("PlaylistsController", text);
}
} // namespace

PlaylistsController::PlaylistsController(PlaylistStore& store, Dialogs& dialogs, Notifier& notifier,
                                         QueryResolver resolver, JobRunner* jobs, QObject* parent,
                                         PlaybackService* playback)
    : QObject(parent), m_store(store), m_dialogs(dialogs), m_notifier(notifier),
      m_resolver(std::move(resolver)), m_jobs(jobs), m_playback(playback) {
    reload();
}

void PlaylistsController::setEditingId(qint64 id) {
    if (id != m_editingId) {
        m_editingId = id;
        emit editingIdChanged();
    }
}

void PlaylistsController::reload() {
    const auto list = m_store.list();
    if (!list) {
        reportError(tr("無法讀取播放清單"), list.error());
        return;
    }
    QVariantList playlists;
    playlists.reserve(list.value().size());
    for (const auto& info : list.value()) {
        playlists.append(QVariantMap{{QStringLiteral("id"), info.id},
                                     {QStringLiteral("name"), info.name},
                                     {QStringLiteral("count"), info.trackCount}});
    }
    m_playlists = playlists;
    emit playlistsChanged();
}

qint64 PlaylistsController::createPlaylist() {
    const auto id = m_store.create(tr("新播放清單"));
    if (!id) {
        reportError(tr("無法新增播放清單"), id.error());
        return 0;
    }
    reload();
    setEditingId(id.value());
    return id.value();
}

qint64 PlaylistsController::createWithTracks(const QString& payloadJson) {
    const auto payload = parsePayload(payloadJson);
    if (!payload) {
        return 0;
    }
    if (payload->tracks.isEmpty()) {
        return resolveLater(payload->query,
                            [this](const QList<TrackRef>& tracks) { createFromTracks(tracks); })
                   ? -1
                   : 0;
    }
    return createFromTracks(payload->tracks);
}

qint64 PlaylistsController::createFromTracks(const QList<TrackRef>& tracks) {
    if (tracks.isEmpty()) {
        return 0;
    }
    const auto id = m_store.create(tr("新播放清單"));
    if (!id) {
        reportError(tr("無法新增播放清單"), id.error());
        return 0;
    }
    const auto added = m_store.addTracks(id.value(), tracks);
    if (!added) {
        // 不留下空的清單：加入失敗就把剛建的刪掉
        m_store.remove(id.value());
        reload();
        reportError(tr("無法新增播放清單"), added.error());
        return 0;
    }
    reload();
    setEditingId(id.value());
    return id.value();
}

bool PlaylistsController::addTracks(qint64 id, const QString& payloadJson, int insertAt) {
    const auto payload = parsePayload(payloadJson);
    if (!payload || nameOf(id).isNull()) {
        return false;
    }
    if (payload->tracks.isEmpty()) {
        return resolveLater(payload->query, [this, id, insertAt](const QList<TrackRef>& tracks) {
            appendTracks(id, tracks, insertAt);
        });
    }
    return appendTracks(id, payload->tracks, insertAt);
}

bool PlaylistsController::appendTracks(qint64 id, const QList<TrackRef>& tracks, int insertAt) {
    if (tracks.isEmpty()) {
        return false;
    }
    const auto added = m_store.addTracks(id, tracks, insertAt);
    if (!added) {
        reportError(tr("無法加入播放清單"), added.error());
        return false;
    }
    if (!added.value()) {
        return false;
    }
    reload();
    emit itemsChanged(id);
    // 側欄的曲目數會變，但看不出加到哪裡、加了幾首，所以通知（介面規範「操作成功」）
    m_notifier.notify(tr("已加入 %1 首到「%2」").arg(tracks.size()).arg(nameOf(id)));
    return true;
}

bool PlaylistsController::reorder(qint64 id, const QList<int>& order) {
    const auto done = m_store.reorder(id, order);
    if (!done) {
        reportError(tr("無法調整播放清單的順序"), done.error());
        return false;
    }
    if (done.value()) {
        emit itemsChanged(id);
    }
    return done.value();
}

bool PlaylistsController::removeItems(qint64 id, const QList<int>& positions) {
    // 先記下被移除的項目（含原本的位置），「復原」時放回去
    const auto current = m_store.items(id);
    if (!current) {
        reportError(tr("無法從播放清單移除曲目"), current.error());
        return false;
    }
    const QSet<int> drop(positions.begin(), positions.end());
    QList<PlaylistItem> removed;
    for (const auto& item : current.value()) {
        if (drop.contains(item.pos)) {
            removed.append(item);
        }
    }
    if (removed.isEmpty()) {
        return false;
    }
    const auto done = m_store.removeItems(id, positions);
    if (!done) {
        reportError(tr("無法從播放清單移除曲目"), done.error());
        return false;
    }
    reload();
    emit itemsChanged(id);
    // 介面規範「可還原的操作」：直接做，通知帶「復原」
    m_notifier.notify(tr("已移除 %1 首").arg(removed.size()), Notifier::Kind::Info, tr("復原"),
                      [self = QPointer<PlaylistsController>(this), id, removed] {
                          if (self) {
                              self->restoreItems(id, removed);
                          }
                      });
    return true;
}

void PlaylistsController::restoreItems(qint64 id, const QList<PlaylistItem>& removed) {
    // removed 依原本的位置由小到大；照順序插回原位置，前面的放好後後面的位置就對了。
    // 相連的一段一次寫入（全選移除後復原只寫一次）
    int i = 0;
    while (i < removed.size()) {
        int end = i + 1;
        while (end < removed.size() && removed.at(end).pos == removed.at(end - 1).pos + 1) {
            ++end;
        }
        QList<TrackRef> run;
        for (int k = i; k < end; ++k) {
            run.append(removed.at(k).track);
        }
        const auto added = m_store.addTracks(id, run, removed.at(i).pos);
        if (!added) {
            reportError(tr("無法復原移除的曲目"), added.error());
            break;
        }
        if (!added.value()) {
            break; // 播放清單已經刪掉了
        }
        i = end;
    }
    reload();
    emit itemsChanged(id);
}

bool PlaylistsController::play(qint64 id) {
    const auto items = m_store.items(id);
    if (!items) {
        reportError(tr("無法讀取播放清單"), items.error());
        return false;
    }
    QList<TrackRef> tracks;
    tracks.reserve(items.value().size());
    for (const auto& item : items.value()) {
        // 找不到的項目（被拒絕的路徑、經過連結的路徑）不交給播放
        if (!blockedFromOpening(item.missing)) {
            tracks.append(item.track);
        }
    }
    return playFrom(tracks, 0);
}

bool PlaylistsController::playFrom(const QList<TrackRef>& tracks, int index) {
    if (m_playback == nullptr || index < 0 || index >= tracks.size()) {
        return false;
    }
    m_playback->playFrom(tracks, index);
    return true;
}

void PlaylistsController::reportNothingPlayable() {
    m_notifier.notify(tr("這個播放清單的曲目都找不到"));
}

bool PlaylistsController::rename(qint64 id, const QString& name) {
    if (name.trimmed().isEmpty()) {
        return false;
    }
    const auto renamed = m_store.rename(id, name);
    if (!renamed) {
        reportError(tr("無法重新命名播放清單"), renamed.error());
        return false;
    }
    if (renamed.value()) {
        reload();
    }
    if (m_editingId == id) {
        setEditingId(0);
    }
    return renamed.value();
}

void PlaylistsController::requestRemove(qint64 id) {
    const QString name = nameOf(id);
    if (name.isNull()) {
        return;
    }
    ConfirmRequest request;
    request.title = tr("刪除播放清單「%1」").arg(name);
    request.message = tr("只刪除播放清單，不會刪除任何音樂檔。");
    request.confirmText = tr("刪除播放清單");
    request.destructive = true;
    m_dialogs.confirm(request, [this, id](bool confirmed) {
        if (confirmed) {
            remove(id);
        }
    });
}

bool PlaylistsController::remove(qint64 id) {
    const auto removed = m_store.remove(id);
    if (!removed) {
        reportError(tr("無法刪除播放清單"), removed.error());
        return false;
    }
    if (m_editingId == id) {
        setEditingId(0);
    }
    reload();
    if (removed.value()) {
        emit playlistRemoved(id);
    }
    return removed.value();
}

QVariantMap PlaylistsController::info(qint64 id) const {
    for (const auto& value : m_playlists) {
        const QVariantMap playlist = value.toMap();
        if (playlist.value(QStringLiteral("id")).toLongLong() == id) {
            QVariantMap result = playlist;
            result.insert(QStringLiteral("exists"), true);
            return result;
        }
    }
    return {{QStringLiteral("exists"), false}, {QStringLiteral("id"), id}};
}

std::optional<TrackDragPayload>
PlaylistsController::parsePayload(const QString& payloadJson) const {
    auto payload = TrackDragPayload::fromJson(payloadJson.toUtf8());
    if (!payload) {
        qCWarning(lcPlaylists) << "拖放或右鍵選單的內容不是曲目清單";
    }
    return payload;
}

bool PlaylistsController::resolveLater(const QString& query,
                                       std::function<void(const QList<TrackRef>&)> apply) {
    if (query.isEmpty() || !m_resolver || m_jobs == nullptr) {
        return false;
    }
    // 依查詢條件取曲目要讀資料庫（4 萬首約數百毫秒），在背景工作裡做；
    // 結果排回 UI 執行緒，控制器先被銷毀時（程式關閉）就丟掉
    QPointer<PlaylistsController> self(this);
    m_jobs->submit(tr("加入播放清單"), [self, query, apply = std::move(apply), gate = m_gate,
                                        resolver = m_resolver](JobContext& context) {
        // 模組停止後才開始的工作不碰音樂庫（它可能已經解構，P-PL-24）
        const JobGate::Pass pass(*gate);
        if (!pass) {
            return;
        }
        const Result<QList<TrackInfo>> found = resolver(query);
        if (context.isCancelled()) {
            return;
        }
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
            QCoreApplication::instance(),
            [self, apply, tracks = std::move(tracks), error, ok = found.hasValue()] {
                if (!self) {
                    return;
                }
                if (!ok) {
                    self->reportError(tr("無法取得曲目"), Error{error});
                    return;
                }
                apply(tracks);
            },
            Qt::QueuedConnection);
    });
    return true;
}

void PlaylistsController::shutdown(int timeoutMs) noexcept {
    // 取曲目的工作中途不檢查取消，所以不取消，只關閘門：還沒開始的直接結束，已經開始的等它
    if (!m_gate->close(timeoutMs)) {
        qCWarning(lcPlaylists) << "停止時加入播放清單的工作沒有在" << timeoutMs << "ms 內結束";
    }
}

QString PlaylistsController::nameOf(qint64 id) const {
    const QVariantMap playlist = info(id);
    return playlist.value(QStringLiteral("exists")).toBool()
               ? playlist.value(QStringLiteral("name")).toString()
               : QString();
}

void PlaylistsController::reportError(const QString& text, const Error& error) {
    qCWarning(lcPlaylists) << text << error.message;
    m_notifier.notify(text, Notifier::Kind::Error);
}

void PlaylistsController::setInstance(PlaylistsController* instance) {
    g_instance = instance;
}

PlaylistsController* PlaylistsController::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "PlaylistsController", "模組要在載入側欄前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::mod::pl

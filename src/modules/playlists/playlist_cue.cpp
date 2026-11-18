#include "playlist_cue.h"

#include <QCoreApplication>
#include <QHash>

#include <algorithm>
#include <memory>
#include <optional>

namespace auralis::mod::pl {

namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("PlaylistImport", text);
}

bool isCuePath(const QString& path) {
    return path.endsWith(QLatin1String(".cue"), Qt::CaseInsensitive);
}

} // namespace

bool isWholeCue(const TrackRef& ref) {
    return ref.subsong == 0 && isCuePath(ref.path);
}

bool isEmbeddedCueTrack(const TrackRef& ref) {
    return ref.subsong != 0 && !isCuePath(ref.path);
}

QString cueMissingReason(const TrackRef& ref) {
    if (isWholeCue(ref)) {
        return tr("音樂庫裡沒有這個 .cue 的曲目");
    }
    if (isEmbeddedCueTrack(ref)) {
        return tr("音樂庫裡沒有這一軌的 CUE 曲目");
    }
    return {};
}

void expandCueEntries(QList<ImportEntry>& entries, const LibraryCheck& library) {
    QList<ImportEntry> expanded;
    expanded.reserve(entries.size());
    for (const ImportEntry& entry : entries) {
        // 被拒絕的（path 空的）與 .fpl 的內嵌 cue 照原樣
        if (entry.path.isEmpty() || entry.embeddedCue || !isWholeCue({entry.path, entry.subsong})) {
            expanded.append(entry);
            continue;
        }
        const QList<quint32> tracks =
            library.cueTracks ? library.cueTracks(entry.path) : QList<quint32>();
        if (tracks.isEmpty()) {
            ImportEntry missing = entry;
            missing.unmatched = cueMissingReason({entry.path, 0});
            expanded.append(missing);
            continue;
        }
        for (const quint32 track : tracks) {
            ImportEntry one = entry;
            one.subsong = track;
            expanded.append(one);
        }
    }
    entries = std::move(expanded);
}

LibraryCheck libraryCheck(const LibraryQuery& library) {
    // pathKey(.cue) → 軌號（第一次用到時讀一次；每份自己一個，只在一個工作執行緒用）
    auto cues = std::make_shared<std::optional<QHash<QString, QList<quint32>>>>();
    LibraryCheck check;
    check.cueTracks = [&library, cues](const QString& cuePath) {
        if (!cues->has_value()) {
            QHash<QString, QList<quint32>> byCue;
            const Result<QList<TrackInfo>> all = library.tracks(TrackFilter{});
            if (all) {
                for (const TrackInfo& info : all.value()) {
                    if (isCuePath(info.ref.path) && info.ref.subsong != 0) {
                        byCue[pathKey(info.ref.path)].append(info.ref.subsong);
                    }
                }
            }
            // tracks() 依專輯、碟號、軌號標籤排序；展開照 .cue 裡的軌號
            for (QList<quint32>& tracks : byCue) {
                std::sort(tracks.begin(), tracks.end());
                tracks.erase(std::unique(tracks.begin(), tracks.end()), tracks.end());
            }
            *cues = std::move(byCue);
        }
        return (*cues)->value(pathKey(cuePath));
    };
    check.contains = [&library](const TrackRef& ref) {
        const Result<std::optional<TrackInfo>> found =
            library.findByIdentity(ref.path, ref.subsong);
        return found && found.value().has_value();
    };
    return check;
}

} // namespace auralis::mod::pl

#include "playlist_remap.h"

#include <auralis/library_query.h>

#include <QCoreApplication>
#include <QDir>

namespace auralis::mod::pl {

namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("PlaylistImport", text);
}

/// 路徑的各層（原文）。\\主機\共用 的第一層寫成「\\主機」，和 C: 一樣是一層，
/// 所以網路路徑和本機路徑不會被當成同一個資料夾。
QStringList splitParts(const QString& path) {
    QString native = QDir::toNativeSeparators(path.trimmed());
    const bool unc = native.startsWith(QLatin1String("\\\\"));
    QStringList parts = native.mid(unc ? 2 : 0).split(QLatin1Char('\\'), Qt::SkipEmptyParts);
    if (unc && !parts.isEmpty()) {
        parts.first().prepend(QLatin1String("\\\\"));
    }
    return parts;
}

bool samePart(const QString& a, const QString& b) {
    return pathKey(a) == pathKey(b);
}

/// 資料夾的寫法：各層用 \ 接起來；只有磁碟機代號時加 \（C:\）。
QString folderText(const QStringList& parts) {
    QString text = parts.join(QLatin1Char('\\'));
    if (parts.size() == 1 && text.endsWith(QLatin1Char(':'))) {
        text.append(QLatin1Char('\\'));
    }
    return text;
}

/// 使用者輸入的資料夾（現在的資料夾）：Windows 分隔符、去掉結尾的 \（C:\ 除外）。
QString cleanFolder(const QString& folder) {
    return folderText(splitParts(folder));
}

/// 整張 CUE 與內嵌 cue 的項目在不在音樂庫（T-56）；
/// std::nullopt＝一般項目（或 library 沒給），看磁碟。
std::optional<bool> inLibrary(const TrackRef& ref, const LibraryCheck& library) {
    if (isWholeCue(ref) && library.cueTracks) {
        return !library.cueTracks(ref.path).isEmpty();
    }
    if (isEmbeddedCueTrack(ref) && library.contains) {
        return library.contains(ref);
    }
    return std::nullopt;
}

/// 整張 CUE 的項目在音樂庫已經有這個 .cue 的曲目時展開成各軌（T-56）；各軌的 missing 看 .cue
/// 在不在磁碟上。回傳是否有展開。工作執行緒。
Result<bool> expandWholeCues(PlaylistStore& store, qint64 id, const QList<PathRemap>& remaps,
                             const ExistsCheck& exists, const LibraryCheck& library) {
    if (!library.cueTracks) {
        return false;
    }
    const auto items = store.items(id);
    if (!items) {
        return items.error();
    }
    QList<ItemExpansion> expansions;
    for (const PlaylistItem& item : items.value()) {
        const QString& path = item.track.path;
        if (!isWholeCue(item.track) || !resolveEntryPath(path, QString())) {
            continue;
        }
        const QList<quint32> tracks = library.cueTracks(path);
        if (tracks.isEmpty()) {
            continue;
        }
        ItemExpansion expansion{item.pos, item.track, {}, !existsOnDisk(path, remaps, exists)};
        for (const quint32 track : tracks) {
            expansion.tracks.append({path, track});
        }
        expansions.append(expansion);
    }
    if (expansions.isEmpty()) {
        return false;
    }
    const auto written = store.expandItems(id, expansions);
    if (!written) {
        return written.error();
    }
    return written.value() > 0;
}

} // namespace

QString commonFolder(const QStringList& paths) {
    if (paths.isEmpty()) {
        return {};
    }
    QStringList common = splitParts(paths.first());
    // 最後一層是檔名
    if (!common.isEmpty()) {
        common.removeLast();
    }
    for (qsizetype i = 1; i < paths.size() && !common.isEmpty(); ++i) {
        QStringList parts = splitParts(paths.at(i));
        if (!parts.isEmpty()) {
            parts.removeLast();
        }
        qsizetype same = 0;
        while (same < common.size() && same < parts.size() &&
               samePart(common.at(same), parts.at(same))) {
            ++same;
        }
        common = common.mid(0, same);
    }
    return folderText(common);
}

std::optional<QStringList> partsUnder(const QString& path, const QString& root) {
    const QStringList rootParts = splitParts(root);
    const QStringList parts = splitParts(path);
    if (rootParts.isEmpty() || parts.size() <= rootParts.size()) {
        return std::nullopt;
    }
    for (qsizetype i = 0; i < rootParts.size(); ++i) {
        if (!samePart(rootParts.at(i), parts.at(i))) {
            return std::nullopt;
        }
    }
    return parts.mid(rootParts.size());
}

std::optional<Result<QString>> remapPath(const QString& path, const QString& oldRoot,
                                         const QString& newRoot) {
    const auto rest = partsUnder(path, oldRoot);
    if (!rest) {
        return std::nullopt;
    }
    // 起點以下的各層不能跳出現在的資料夾（被拒絕的原文可能寫了 ..）
    for (const QString& part : *rest) {
        if (part == QLatin1String(".") || part == QLatin1String("..")) {
            return Result<QString>(Error{tr("跳出了現在的資料夾"), 0, QStringLiteral("remapPath")});
        }
    }
    QString root = cleanFolder(newRoot);
    if (!root.endsWith(QLatin1Char('\\'))) {
        root.append(QLatin1Char('\\'));
    }
    // 整條再照路徑規則檢查一次：現在的資料夾是網路路徑時一樣拒絕（不查詢、維持找不到）
    return resolveEntryPath(root + rest->join(QLatin1Char('\\')), QString());
}

QString missingReason(const QString& path) {
    const Result<QString> resolved = resolveEntryPath(path, QString());
    return resolved ? tr("找不到檔案") : resolved.error().message;
}

MissingView missingView(const QList<PlaylistItem>& items) {
    MissingView view;
    view.total = static_cast<int>(items.size());
    for (const PlaylistItem& item : items) {
        if (item.missing) {
            // 整張 CUE 與內嵌 cue 的項目寫音樂庫的原因（T-56）；被拒絕的照拒絕原因
            const QString cue = resolveEntryPath(item.track.path, QString())
                                    ? cueMissingReason(item.track)
                                    : QString();
            view.missing.append(
                {item.track.path, cue.isEmpty() ? missingReason(item.track.path) : cue});
        }
    }
    return view;
}

QHash<QString, QString> linkReasons(const QList<PlaylistItem>& items, const ReasonCheck& reason) {
    QHash<QString, QString> reasons;
    for (const PlaylistItem& item : items) {
        if (!reason || !item.missing || !resolveEntryPath(item.track.path, QString())) {
            continue;
        }
        const QString text = reason(item.track.path);
        if (!text.isEmpty()) {
            reasons.insert(item.track.path, text);
        }
    }
    return reasons;
}

Result<RemapOutcome> remapPlaylist(PlaylistStore& store, qint64 id, const QString& oldRoot,
                                   const QString& newRoot, const ExistsCheck& exists,
                                   const LibraryCheck& library) {
    const auto items = store.items(id);
    if (!items) {
        return items.error();
    }
    const QString start = cleanFolder(newRoot);
    const bool useRule = !oldRoot.trimmed().isEmpty() && !start.isEmpty();
    QList<ItemUpdate> updates;
    for (const PlaylistItem& item : items.value()) {
        if (!item.missing) {
            continue;
        }
        const QString& path = item.track.path;
        // 整張 CUE 與內嵌 cue 的項目看音樂庫（T-56），其他看磁碟
        const auto available = [&](const QString& candidate, const QString& from) {
            const std::optional<bool> found = inLibrary({candidate, item.track.subsong}, library);
            return found ? *found : exists(candidate, from);
        };
        if (useRule) {
            if (const auto mapped = remapPath(path, oldRoot, newRoot)) {
                // 對得上規則的只查新路徑；被拒絕或查不到的維持原路徑與 missing
                if (*mapped && available(mapped->value(), start)) {
                    updates.append({item.pos, path, item.track.subsong, mapped->value(), false});
                }
                continue;
            }
        }
        // 被路徑規則拒絕的原文不查詢（查網路路徑會對外連 SMB）
        if (resolveEntryPath(path, QString()) && available(path, QString())) {
            updates.append({item.pos, path, item.track.subsong, path, false});
        }
    }
    RemapOutcome outcome;
    if (!updates.isEmpty()) {
        const auto written = store.updateItems(id, updates);
        if (!written) {
            return written.error();
        }
    }
    for (const ItemUpdate& update : updates) {
        if (update.newPath != update.oldPath) {
            ++outcome.remapped;
        }
    }
    if (outcome.remapped > 0) {
        const auto saved = store.saveRemap({QDir::toNativeSeparators(oldRoot.trimmed()), start});
        if (!saved) {
            return saved.error();
        }
    }
    if (!updates.isEmpty()) {
        // 找到的整張 CUE 展開成各軌（T-56）
        const auto remaps = store.remaps();
        const auto expanded = remaps ? expandWholeCues(store, id, remaps.value(), exists, library)
                                     : Result<bool>(remaps.error());
        if (!expanded) {
            return expanded.error();
        }
    }
    const auto after = store.items(id);
    if (!after) {
        return after.error();
    }
    outcome.items = after.value();
    return outcome;
}

Result<QList<qint64>> recheckAll(PlaylistStore& store, const ExistsCheck& exists,
                                 const LibraryCheck& library) {
    const auto remaps = store.remaps();
    if (!remaps) {
        return remaps.error();
    }
    const auto playlists = store.list();
    if (!playlists) {
        return playlists.error();
    }
    QList<qint64> changed;
    for (const PlaylistInfo& info : playlists.value()) {
        const auto one = recheckPlaylist(store, info.id, remaps.value(), exists, library);
        if (!one) {
            return one.error();
        }
        if (one.value()) {
            changed.append(info.id);
        }
    }
    return changed;
}

Result<bool> recheckPlaylist(PlaylistStore& store, qint64 id, const QList<PathRemap>& remaps,
                             const ExistsCheck& exists, const LibraryCheck& library) {
    const auto items = store.items(id);
    if (!items) {
        return items.error();
    }
    QList<ItemUpdate> updates;
    for (const PlaylistItem& item : items.value()) {
        const QString& path = item.track.path;
        // 被拒絕的（網路路徑等）不查詢，維持原本的標記
        if (!resolveEntryPath(path, QString())) {
            continue;
        }
        // 整張 CUE 與內嵌 cue 的項目看音樂庫（T-56）；找到的整張 CUE 在下面展開
        const std::optional<bool> found = inLibrary(item.track, library);
        if (found && *found && isWholeCue(item.track)) {
            continue;
        }
        const bool missing = found ? !*found : !exists(path, startFor(path, remaps));
        if (missing != item.missing) {
            updates.append({item.pos, path, item.track.subsong, path, missing});
        }
    }
    bool changed = false;
    if (!updates.isEmpty()) {
        const auto written = store.updateItems(id, updates);
        if (!written) {
            return written.error();
        }
        changed = written.value() > 0;
    }
    const auto expanded = expandWholeCues(store, id, remaps, exists, library);
    if (!expanded) {
        return expanded.error();
    }
    return changed || expanded.value();
}

QString startFor(const QString& path, const QList<PathRemap>& remaps) {
    // 重新對應過的路徑以當時的「現在的資料夾」為起點（起點本身不檢查）
    for (const PathRemap& remap : remaps) {
        if (partsUnder(path, remap.newRoot)) {
            return cleanFolder(remap.newRoot);
        }
    }
    return {};
}

bool existsOnDisk(const QString& path, const QList<PathRemap>& remaps, const ExistsCheck& exists) {
    // 被拒絕的（網路路徑等）不查詢
    return resolveEntryPath(path, QString()) && exists(path, startFor(path, remaps));
}

std::optional<PathRemap> suggestRemap(const QList<PathRemap>& remaps, const QStringList& missing) {
    for (const PathRemap& remap : remaps) {
        for (const QString& path : missing) {
            if (partsUnder(path, remap.oldRoot)) {
                return remap;
            }
        }
    }
    return std::nullopt;
}

} // namespace auralis::mod::pl

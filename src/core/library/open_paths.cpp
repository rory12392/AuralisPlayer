#include "library/open_paths.h"

#include "library/cue_scan.h"
#include "library/cue_tracks.h"
#include "library/tag_reader.h"

#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <numeric>
#include <vector>

namespace auralis::core {

namespace {

Error pathError(const QString& message) {
    return Error{message, 0, QStringLiteral("checkUserPath")};
}

/// 和 FFmpeg 判斷協定的規則相同：英數字與 + - . 接著冒號；一個字母的是磁碟機代號。
bool hasSchemePrefix(const QString& path) {
    static const QRegularExpression scheme(QStringLiteral("^[A-Za-z0-9+.\\-]{2,}:"));
    return scheme.match(path).hasMatch();
}

bool isDriveAbsolute(const QString& native) {
    return native.size() >= 3 && native.at(0).isLetter() && native.at(1) == QLatin1Char(':') &&
           native.at(2) == QLatin1Char('\\');
}

/// 路徑依資料夾一層一層比，每一層照「自然排序」（數字照數值、不分大小寫）：
/// 「2 二」排在「06 CUE」前面，「9.flac」排在「10.flac」前面。
class NaturalPathOrder {
public:
    NaturalPathOrder() {
        m_collator.setNumericMode(true);
        m_collator.setCaseSensitivity(Qt::CaseInsensitive);
    }

    bool operator()(const QString& a, const QString& b) const {
        const QStringList left = a.split(QLatin1Char('\\'));
        const QStringList right = b.split(QLatin1Char('\\'));
        const qsizetype common = std::min(left.size(), right.size());
        for (qsizetype i = 0; i < common; ++i) {
            const int order = m_collator.compare(left.at(i), right.at(i));
            if (order != 0) {
                return order < 0;
            }
        }
        return left.size() < right.size();
    }

private:
    QCollator m_collator;
};

void appendCueTracks(const QString& cuePath, const LoadedCue& cue, OpenPathsResult& result) {
    for (const CueTrack& track : cue.sheet.tracks) {
        result.tracks.append({cuePath, static_cast<quint32>(track.number)});
    }
}

/// 資料夾：和音樂庫掃描同一套列舉與 CUE 規則（被 .cue 涵蓋的整軌檔不另外列）。
void expandFolder(const QString& folder, OpenPathsResult& result, QSet<QString>& covered) {
    const Enumeration listing = enumerateAudioFiles(folder, [] { return false; });
    for (const ScanFailure& failure : listing.unreadableFolders) {
        result.skipped.append(failure);
    }
    for (const ScanFailure& link : listing.skippedLinks) {
        result.skipped.append(link);
    }
    const CueScanPlan plan = planCues(listing);
    QHash<size_t, const CuePlan*> cueByFile;
    for (const CuePlan& cue : plan.cues) {
        cueByFile.insert(cue.fileIndex, &cue);
    }
    std::vector<size_t> order(listing.files.size());
    std::iota(order.begin(), order.end(), size_t{0});
    const NaturalPathOrder less;
    std::stable_sort(order.begin(), order.end(), [&listing, &less](size_t a, size_t b) {
        return less(listing.files[a].path, listing.files[b].path);
    });
    for (const size_t index : order) {
        const QString& path = listing.files[index].path;
        if (!isCuePath(path)) {
            if (!plan.coveredAudio.contains(pathKey(path))) {
                result.tracks.append({path, 0});
            }
            continue;
        }
        const CuePlan* cue = cueByFile.value(index);
        if (cue != nullptr && cue->cue && !cue->duplicate) {
            appendCueTracks(path, *cue->cue, result);
        } else if (cue != nullptr && !cue->error.isEmpty()) {
            result.skipped.append({path, cue->error});
        }
    }
    covered.unite(plan.coveredAudio);
}

} // namespace

Result<QString> checkUserPath(const QString& path) {
    if (path.isEmpty()) {
        return pathError(QStringLiteral("路徑是空的"));
    }
    if (hasSchemePrefix(path)) {
        return pathError(QStringLiteral("只開啟本機檔案，不開啟網址或協定路徑"));
    }
    const QString native = QDir::toNativeSeparators(path);
    if (native.startsWith(QLatin1String("\\\\?\\")) ||
        native.startsWith(QLatin1String("\\\\.\\"))) {
        return pathError(QStringLiteral("不是一般的檔案路徑"));
    }
    // 使用者自己給的網路路徑（\\主機\共用\…）可以開；本機的要是完整路徑（C:\…）
    const bool unc = native.startsWith(QLatin1String("\\\\"));
    if (!unc && !isDriveAbsolute(native)) {
        return pathError(QStringLiteral("不是完整的檔案路徑"));
    }
    // 磁碟機代號以外的冒號是替代資料流
    if (native.indexOf(QLatin1Char(':'), unc ? 0 : 2) >= 0) {
        return pathError(QStringLiteral("不是一般的檔案路徑"));
    }
    // CON、NUL、COM1 這類保留名稱（加任何副檔名也一樣）開的是裝置，不是檔案
    static const QRegularExpression device(
        QStringLiteral("(^|\\\\)(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])[ ]*(\\.[^\\\\]*)?($|\\\\)"),
        QRegularExpression::CaseInsensitiveOption);
    if (device.match(native).hasMatch()) {
        return pathError(QStringLiteral("是裝置名稱，不是檔案"));
    }
    return QDir::toNativeSeparators(QDir::cleanPath(QDir::fromNativeSeparators(native)));
}

QString absoluteUserPath(const QString& argument, const QString& baseDir) {
    const QString native = QDir::toNativeSeparators(argument);
    // 網址、網路路徑、帶磁碟機代號的原樣交給 checkUserPath（「C:a.flac」也不接到目前資料夾後面）
    if (hasSchemePrefix(argument) || native.startsWith(QLatin1String("\\\\")) ||
        (native.size() >= 2 && native.at(0).isLetter() && native.at(1) == QLatin1Char(':'))) {
        return native;
    }
    return QDir::toNativeSeparators(QDir::cleanPath(QDir(baseDir).absoluteFilePath(argument)));
}

OpenPathsResult expandOpenPaths(const QStringList& paths) {
    OpenPathsResult result;
    QSet<QString> covered;
    for (const QString& raw : paths) {
        const auto checked = checkUserPath(raw);
        if (!checked) {
            result.skipped.append({raw, checked.error().message});
            continue;
        }
        const QString& path = checked.value();
        const QFileInfo info(path);
        if (info.isDir()) {
            expandFolder(path, result, covered);
        } else if (!info.exists()) {
            result.skipped.append({path, QStringLiteral("找不到檔案或資料夾")});
        } else if (isCuePath(path)) {
            const auto cue = loadCue(path);
            if (!cue) {
                result.skipped.append({path, cue.error().message});
                continue;
            }
            appendCueTracks(path, cue.value(), result);
            for (const QString& audio : cue.value().audioPaths) {
                covered.insert(pathKey(audio));
            }
        } else if (audioExtensions().contains(info.suffix().toLower())) {
            result.tracks.append({path, 0});
        } else {
            result.skipped.append({path, QStringLiteral("不支援的格式")});
        }
    }
    // .cue 涵蓋的整軌檔不另外加入（AC-07-1 的規則，也涵蓋同時拖進整軌檔與 .cue 的情況）
    result.tracks.removeIf([&covered](const TrackRef& track) {
        return track.subsong == 0 && covered.contains(pathKey(track.path));
    });
    return result;
}

} // namespace auralis::core

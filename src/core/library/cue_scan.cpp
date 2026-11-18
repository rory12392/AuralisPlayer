#include "library/cue_scan.h"

#include <auralis/library_query.h>

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>

#include <algorithm>

namespace auralis::core {

namespace {

/// 「2004」「2004/05/01」「1/2」這類值開頭的整數；沒有時 0。
int leadingNumber(const QString& text) {
    static const QRegularExpression digits(QStringLiteral("^\\s*(\\d+)"));
    const auto match = digits.match(text);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

QString firstNonEmpty(std::initializer_list<QString> values) {
    for (const auto& value : values) {
        if (!value.isEmpty()) {
            return value;
        }
    }
    return {};
}

} // namespace

CueScanPlan planCues(const Enumeration& listing) {
    CueScanPlan plan;
    QHash<QString, size_t> filesByKey;
    for (size_t i = 0; i < listing.files.size(); ++i) {
        filesByKey.insert(pathKey(listing.files[i].path), i);
    }
    for (size_t i = 0; i < listing.files.size(); ++i) {
        const auto& file = listing.files[i];
        if (!isCuePath(file.path)) {
            continue;
        }
        CuePlan cuePlan;
        cuePlan.fileIndex = i;
        cuePlan.mtimeNs = file.mtimeNs;
        auto cue = loadCue(file.path);
        if (!cue) {
            cuePlan.error = cue.error().message;
            plan.cues.push_back(std::move(cuePlan));
            continue;
        }
        QStringList keys;
        for (const auto& audio : cue.value().audioPaths) {
            const QString key = pathKey(audio);
            keys.append(key);
            cuePlan.duplicate = cuePlan.duplicate || plan.coveredAudio.contains(key);
            if (const auto it = filesByKey.constFind(key); it != filesByKey.cend()) {
                cuePlan.mtimeNs = std::max(cuePlan.mtimeNs, listing.files[it.value()].mtimeNs);
            }
        }
        if (!cuePlan.duplicate) {
            for (const auto& key : keys) {
                plan.coveredAudio.insert(key);
            }
            cuePlan.cue = std::move(cue).value();
        }
        plan.cues.push_back(std::move(cuePlan));
    }
    return plan;
}

Result<std::vector<CueTrackRow>> readCueTracks(const QString& cuePath, const LoadedCue& cue) {
    std::vector<TagData> audioTags;
    for (const auto& audio : cue.audioPaths) {
        // 解析器丟出的例外只讓這個 .cue 讀不到，掃描繼續（AC-08-5）
        auto tags = [&audio]() -> Result<TagData> {
            try {
                return readTags(audio);
            } catch (...) {
                return readTagsExceptionError();
            }
        }();
        if (!tags) {
            return Error{QStringLiteral("讀不到 CUE 的音訊檔 %1：%2")
                             .arg(QFileInfo(audio).fileName(), tags.error().message),
                         0, QStringLiteral("readCueTracks")};
        }
        audioTags.push_back(std::move(tags).value());
    }
    const CueSheet& sheet = cue.sheet;
    const QString baseName = QFileInfo(cuePath).completeBaseName();
    std::vector<CueTrackRow> rows;
    for (const auto& track : sheet.tracks) {
        const TagData& audio = audioTags[static_cast<size_t>(track.fileIndex)];
        CueTrackRow row;
        row.subsong = static_cast<quint32>(track.number);
        row.startCdFrame = track.start;
        row.endCdFrame = track.end;
        TagData& tags = row.tags;
        // 沒有 TITLE 的軌用「.cue 主檔名 軌號」，避免整張專輯都叫同一個名字
        tags.title = firstNonEmpty(
            {track.title, baseName + QLatin1Char(' ') +
                              QStringLiteral("%1").arg(track.number, 2, 10, QChar('0'))});
        tags.artist = firstNonEmpty({track.performer, sheet.performer, audio.artist});
        tags.albumArtist = firstNonEmpty({sheet.performer, audio.albumArtist, audio.artist});
        tags.album = firstNonEmpty({sheet.title, audio.album});
        tags.composer = firstNonEmpty({track.songwriter, sheet.songwriter, audio.composer});
        tags.genre = firstNonEmpty({sheet.rem.value(QStringLiteral("GENRE")), audio.genre});
        tags.year = leadingNumber(sheet.rem.value(QStringLiteral("DATE")));
        tags.year = tags.year != 0 ? tags.year : audio.year;
        tags.discNo = leadingNumber(sheet.rem.value(QStringLiteral("DISCNUMBER")));
        tags.discNo = tags.discNo != 0 ? tags.discNo : audio.discNo;
        tags.trackNo = track.number;
        const qint64 startMs = track.start * 1000 / kCdFramesPerSecond;
        const qint64 endMs =
            track.end < 0 ? audio.durationMs : track.end * 1000 / kCdFramesPerSecond;
        tags.durationMs = std::max<qint64>(0, endMs - startMs);
        tags.codec = audio.codec;
        tags.sampleRate = audio.sampleRate;
        tags.bits = audio.bits;
        tags.channels = audio.channels;
        tags.dsdRate = audio.dsdRate;
        // 整軌檔的 ReplayGain 曲目值是整張的量測，只有專輯值對每一軌仍然成立
        tags.rgAlbumGain = audio.rgAlbumGain;
        tags.rgAlbumPeak = audio.rgAlbumPeak;
        rows.push_back(std::move(row));
    }
    return rows;
}

} // namespace auralis::core

#include "library/cue_tracks.h"

#include "library/file_enumerator.h"
#include "library/tag_reader.h"
#include "platform/content_path_check.h"

#include <auralis/text_decoder.h>

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace auralis::core {

namespace {

Error cueError(const QString& message) {
    return Error{message, 0, QStringLiteral("loadCue")};
}

std::function<bool(const QString&)>& cueProbeHook() {
    static std::function<bool(const QString&)> hook;
    return hook;
}

bool isLibraryAudio(const QFileInfo& info) {
    if (cueProbeHook() && !cueProbeHook()(info.filePath())) {
        return false;
    }
    // 符號連結先看目錄項目就排除，不去查它指到的地方（可能在網路上）
    if (isLinkPath(QDir::toNativeSeparators(info.absoluteFilePath()))) {
        return false;
    }
    return info.isFile() && audioExtensions().contains(info.suffix().toLower());
}

/// FILE 的寫法 → .cue 所在資料夾（含子資料夾）內的完整路徑。網路路徑、裝置路徑、跳出資料夾的
/// ..、資料夾外的絕對路徑一律拒絕：掃描（每次啟動都會）或播放時去查網路路徑，Windows 會對外建
/// SMB 連線並送出使用者的 NTLM 驗證，而本程式的對外連線只有歌詞服務。只做字串處理，所以被拒的
/// 路徑一次都不會被查詢或開啟。
Result<QString> confinedAudioPath(const QString& cueDir, const QString& name) {
    const QString native = QDir::toNativeSeparators(name);
    if (native.startsWith(QLatin1String("\\\\"))) {
        return cueError(QStringLiteral("CUE 的 FILE 是網路或裝置路徑，不開啟：%1").arg(name));
    }
    // 磁碟機代號只能出現在絕對路徑開頭（C:\…）；「C:a.flac」是那顆磁碟的目前資料夾，
    // 其他位置的冒號是替代資料流
    const bool driveAbsolute = native.size() >= 3 && native.at(0).isLetter() &&
                               native.at(1) == QLatin1Char(':') &&
                               native.at(2) == QLatin1Char('\\');
    if (native.indexOf(QLatin1Char(':'), driveAbsolute ? 2 : 0) >= 0) {
        return cueError(QStringLiteral("CUE 的 FILE 不是一般的檔案路徑，不開啟：%1").arg(name));
    }
    // CON、NUL、COM1 這類保留名稱（加任何副檔名也一樣）開的是裝置，不是檔案；Windows 11 也把
    // 上標數字的 COM¹–³、LPT¹–³ 與 CONIN$、CONOUT$ 當成裝置
    static const QRegularExpression device(
        QStringLiteral("(^|\\\\)(CON|PRN|AUX|NUL|COM[0-9¹²³]|LPT[0-9¹²³]|CONIN\\$|CONOUT\\$)[ ]*"
                       "(\\.[^\\\\]*)?($|\\\\)"),
        QRegularExpression::CaseInsensitiveOption);
    if (device.match(native).hasMatch()) {
        return cueError(QStringLiteral("CUE 的 FILE 是裝置名稱，不開啟：%1").arg(name));
    }
    const QString base = fullPathName(QDir::toNativeSeparators(cueDir));
    const QString full = fullPathName(
        driveAbsolute ? native : QDir::toNativeSeparators(cueDir) + QLatin1Char('\\') + native);
    const QString prefix = base.endsWith(QLatin1Char('\\')) ? base : base + QLatin1Char('\\');
    if (base.isEmpty() || full.size() <= prefix.size() ||
        !full.startsWith(prefix, Qt::CaseInsensitive)) {
        return cueError(
            QStringLiteral(
                "CUE 的 FILE 指到 .cue 所在資料夾以外（只接受同一資料夾或子資料夾內的檔案），"
                "不開啟：%1")
                .arg(name));
    }
    return full;
}

/// FILE 解析後的路徑（已確認在 .cue 所在資料夾內）→ 磁碟上的音訊檔；找不到時空字串。
QString resolveAudio(const QString& path) {
    const QFileInfo written(path);
    if (isLibraryAudio(written)) {
        return QDir::toNativeSeparators(written.absoluteFilePath());
    }
    const QDir dir = written.absoluteDir();
    for (const auto& extension : audioExtensions()) {
        const QFileInfo candidate(
            dir.filePath(written.completeBaseName() + QLatin1Char('.') + extension));
        if (isLibraryAudio(candidate)) {
            return QDir::toNativeSeparators(candidate.absoluteFilePath());
        }
    }
    return {};
}

} // namespace

void setCueProbeHookForTest(std::function<bool(const QString&)> hook) {
    cueProbeHook() = std::move(hook);
}

bool isCuePath(const QString& path) {
    return path.endsWith(QLatin1String(".cue"), Qt::CaseInsensitive);
}

Result<LoadedCue> loadCue(const QString& cuePath) {
    auto decoded = TextDecoder::decodeFile(cuePath);
    if (!decoded) {
        return decoded.error();
    }
    auto sheet = parseCueSheet(decoded.value().text);
    if (!sheet) {
        return sheet.error();
    }
    LoadedCue cue{std::move(sheet).value(), {}, decoded.value().encoding};
    const QString cueDir = QFileInfo(cuePath).absolutePath();
    for (const auto& name : cue.sheet.files) {
        auto confined = confinedAudioPath(cueDir, name);
        if (!confined) {
            return confined.error();
        }
        // 字串檢查看不出資料夾裡的連結指到哪裡（可以是網路位置），所以從 .cue 所在資料夾往下
        // 一層一層看目錄項目，查到連結就停：連結另一邊的路徑一次都不會被查詢
        const ContentPathCheck walked = walkContentPath(
            confined.value(), fullPathName(QDir::toNativeSeparators(cueDir)), cueProbeHook());
        if (walked.status == ContentPathStatus::LinkInPath) {
            return cueError(QStringLiteral("CUE 的 FILE 經過符號連結，不開啟：%1").arg(name));
        }
        const QString audio = resolveAudio(confined.value());
        if (audio.isEmpty()) {
            return cueError(QStringLiteral("找不到 CUE 指向的音訊檔：%1").arg(name));
        }
        cue.audioPaths.push_back(audio);
    }
    return cue;
}

qint64 cdFramesToSourceFrames(qint64 cdFrames, const AudioFormat& format) {
    const qint64 perSecond = format.rate != 0 ? format.rate : format.dsdRate / 32;
    return cdFrames * perSecond / kCdFramesPerSecond;
}

Result<EngineTrack> openTrack(const TrackRef& track, const SourceRegistry& sources, quint64 token) {
    if (!isCuePath(track.path)) {
        auto source = sources.open(track.path);
        if (!source) {
            return source.error();
        }
        return EngineTrack{std::move(source).value(), 0, -1, token, 0, track};
    }
    auto cue = loadCue(track.path);
    if (!cue) {
        return cue.error();
    }
    for (const auto& cueTrack : cue.value().sheet.tracks) {
        if (cueTrack.number != static_cast<int>(track.subsong)) {
            continue;
        }
        auto source = sources.open(cue.value().audioPaths[static_cast<size_t>(cueTrack.fileIndex)]);
        if (!source) {
            return source.error();
        }
        const AudioFormat format = source.value()->format();
        return EngineTrack{std::move(source).value(),
                           cdFramesToSourceFrames(cueTrack.start, format),
                           cueTrack.end < 0 ? -1 : cdFramesToSourceFrames(cueTrack.end, format),
                           token,
                           0,
                           track};
    }
    return cueError(QStringLiteral("CUE 裡沒有第 %1 軌").arg(track.subsong));
}

} // namespace auralis::core

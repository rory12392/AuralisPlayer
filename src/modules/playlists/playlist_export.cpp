#include "playlist_export.h"

#include "playlist_parse.h"

#include <auralis/library_query.h>

#include <QDir>
#include <QSaveFile>

namespace auralis::mod::pl {

namespace {

QString fileNameOf(const QString& path) {
    const QString native = QDir::toNativeSeparators(path);
    return native.mid(native.lastIndexOf(QLatin1Char('\\')) + 1);
}

/// #EXTINF 的標題寫在一行裡：標籤裡的換行換成空白。
QString oneLine(QString text) {
    text.replace(QStringLiteral("\r\n"), QStringLiteral(" "));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return text;
}

/// C:\… 的磁碟機代號（大寫）；不是磁碟機開頭（例：\\主機\共用）時空的。
QString driveOf(const QString& path) {
    if (path.size() >= 2 && path.at(1) == QLatin1Char(':') && path.at(0).isLetter()) {
        return path.left(1).toUpper();
    }
    return {};
}

bool isCueTrack(const TrackRef& ref) {
    return ref.subsong > 0 && ref.path.endsWith(QStringLiteral(".cue"), Qt::CaseInsensitive);
}

} // namespace

ExportContent exportLines(const QList<PlaylistItem>& items, const LibraryQuery* library) {
    ExportContent content;
    content.lines.reserve(items.size());
    for (const PlaylistItem& item : items) {
        ExportLine line{item.track.path, -1, fileNameOf(item.track.path), false};
        // 被拒絕、照原文存著的（網路路徑等）：只寫文字，不查音樂庫也不查檔案
        if (storedVerbatim(item.track.path, item.missing)) {
            line.verbatim = true;
            content.lines.append(line);
            continue;
        }
        if (library != nullptr) {
            const auto found = library->findByIdentity(item.track.path, item.track.subsong);
            if (found && found.value()) {
                const TrackInfo& info = *found.value();
                if (info.durationMs > 0) {
                    line.seconds = static_cast<int>((info.durationMs + 500) / 1000);
                }
                line.title = oneLine(info.artist.isEmpty()
                                         ? info.title
                                         : info.artist + QStringLiteral(" - ") + info.title);
                // m3u 無法表示區段：CUE 曲目寫整軌檔的路徑，#EXTINF 仍是這一軌的
                if (isCueTrack(item.track)) {
                    const auto audio = library->audioFilePath(item.track);
                    if (audio) {
                        line.path = audio.value();
                        content.hasCue = true;
                    }
                }
            }
        }
        content.lines.append(line);
    }
    for (const ExportLine& line : std::as_const(content.lines)) {
        if (hasControlChar(line.path)) {
            ++content.invalidPaths;
        }
    }
    return content;
}

QString exportPathFor(const QString& path, const QString& exportDir, bool relative) {
    const QString absolute = QDir::toNativeSeparators(path);
    if (!relative) {
        return absolute;
    }
    const QString drive = driveOf(absolute);
    if (drive.isEmpty() || drive != driveOf(QDir::toNativeSeparators(exportDir))) {
        return absolute;
    }
    const QString result =
        QDir::toNativeSeparators(QDir(QDir::fromNativeSeparators(exportDir))
                                     .relativeFilePath(QDir::fromNativeSeparators(path)));
    // 匯入時 # 開頭的行是註解、前後空白會被去掉：這種寫成相對的讀回來會不一樣
    if (result.startsWith(QLatin1Char('#')) || result != result.trimmed() ||
        !driveOf(result).isEmpty()) {
        return absolute;
    }
    return result;
}

QByteArray buildM3u8(const QList<ExportLine>& lines, const QString& exportDir, bool relative) {
    // 換行用 CRLF（Windows 的播放器與記事本都認得；匯入兩種都接受）
    QString text = QStringLiteral("#EXTM3U\r\n");
    for (const ExportLine& line : lines) {
        // 路徑含控制字元（例：.fpl 的路徑夾 CR LF）照寫會多出一行、可以夾帶 \\主機\… 給其他
        // 播放器開：不寫這一首，改寫一行註解，註解裡的控制字元也換掉（M-04 審查）
        if (hasControlChar(line.path)) {
            text += QStringLiteral("#路徑含無效字元，沒有匯出：%1\r\n")
                        .arg(replaceControlChars(line.path));
            continue;
        }
        text += QStringLiteral("#EXTINF:%1,%2\r\n").arg(line.seconds).arg(line.title);
        text += line.verbatim ? line.path : exportPathFor(line.path, exportDir, relative);
        text += QStringLiteral("\r\n");
    }
    return text.toUtf8();
}

Result<bool> writeExportFile(const QString& target, const QByteArray& bytes) {
    // QSaveFile：寫到同資料夾的暫存檔，commit 時才改名取代；失敗時刪掉暫存檔、原檔不動。
    // 不開「直接寫入」的退路（那會在原檔上直接寫）
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        return Error{file.errorString(), 0, QStringLiteral("QSaveFile::open")};
    }
    if (file.write(bytes) != bytes.size()) {
        const QString message = file.errorString();
        file.cancelWriting();
        return Error{message, 0, QStringLiteral("QSaveFile::write")};
    }
    if (!file.commit()) {
        return Error{file.errorString(), 0, QStringLiteral("QSaveFile::commit")};
    }
    return true;
}

} // namespace auralis::mod::pl

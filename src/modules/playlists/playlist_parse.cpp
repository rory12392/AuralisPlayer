#include "playlist_parse.h"

#include "playlist_fpl.h"

#include <auralis/content_path.h>
#include <auralis/text_decoder.h>

#include <QCoreApplication>
#include <QDir>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

namespace auralis::mod::pl {

namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("PlaylistImport", text);
}

Error rejectPath(const QString& reason) {
    return Error{reason, 0, QStringLiteral("resolveEntryPath")};
}

bool isDriveLetter(QChar c) {
    return c.unicode() < 128 && c.isLetter();
}

/// C:\…（完整的磁碟機路徑）。
bool isDriveAbsolute(const QString& native) {
    return native.size() >= 3 && isDriveLetter(native.at(0)) && native.at(1) == QLatin1Char(':') &&
           native.at(2) == QLatin1Char('\\');
}

/// root（C:\ 或 \\主機\共用\）接上 rest，處理 . 與 ..；跳出 root 時回傳空字串。
QString joinClean(const QString& root, const QString& rest) {
    QStringList parts;
    for (const QString& part : rest.split(QLatin1Char('\\'), Qt::SkipEmptyParts)) {
        if (part == QLatin1String(".")) {
            continue;
        }
        if (part == QLatin1String("..")) {
            if (parts.isEmpty()) {
                return {};
            }
            parts.removeLast();
            continue;
        }
        parts.append(part);
    }
    return root + parts.join(QLatin1Char('\\'));
}

/// \\主機\共用\…的根「\\主機\共用\」；寫不出主機與共用名稱時空字串。
QString uncRoot(const QString& native) {
    const qsizetype host = native.indexOf(QLatin1Char('\\'), 2);
    if (host <= 2) {
        return {};
    }
    const qsizetype share = native.indexOf(QLatin1Char('\\'), host + 1);
    if (share < 0) {
        return native.size() > host + 1 ? native + QLatin1Char('\\') : QString();
    }
    return share > host + 1 ? native.left(share + 1) : QString();
}

/// file: URI → 本機路徑（URL 解碼）；主機不是空的或 localhost 時拒絕。
Result<QString> localPathFromUri(const QString& uri) {
    const QUrl url(uri);
    if (!url.isValid()) {
        return rejectPath(tr("不是正確的 file:// 網址"));
    }
    const QString host = url.host();
    if (!host.isEmpty() && host.compare(QLatin1String("localhost"), Qt::CaseInsensitive) != 0) {
        return rejectPath(tr("網路上的路徑，不會開啟"));
    }
    QString path = url.path(QUrl::FullyDecoded);
    // file:///C:/… 的路徑是「/C:/…」
    if (path.size() >= 3 && path.at(0) == QLatin1Char('/') && isDriveLetter(path.at(1)) &&
        path.at(2) == QLatin1Char(':')) {
        path.remove(0, 1);
    }
    const QString native = QDir::toNativeSeparators(path);
    if (native.startsWith(QLatin1String("\\\\"))) {
        return rejectPath(tr("網路上的路徑，不會開啟"));
    }
    if (!isDriveAbsolute(native)) {
        return rejectPath(tr("不是完整的檔案路徑"));
    }
    return native;
}

/// 解析好的完整路徑最後再檢查一次：替代資料流、裝置名稱。
Result<QString> checkResolved(const QString& resolved) {
    if (resolved.isEmpty()) {
        return rejectPath(tr("跳出了磁碟機或共用資料夾的根目錄"));
    }
    // 磁碟機代號以外的冒號是替代資料流
    const bool unc = resolved.startsWith(QLatin1String("\\\\"));
    if (resolved.indexOf(QLatin1Char(':'), unc ? 0 : 2) >= 0) {
        return rejectPath(tr("不是一般的檔案路徑"));
    }
    // CON、NUL、COM1 這類保留名稱（加任何副檔名也一樣）開的是裝置，不是檔案；Windows 11 也把
    // 上標數字的 COM¹–³、LPT¹–³ 與 CONIN$、CONOUT$ 當成裝置（和核心的 .cue 規則相同）
    static const QRegularExpression device(
        QStringLiteral("(^|\\\\)(CON|PRN|AUX|NUL|COM[0-9¹²³]|LPT[0-9¹²³]|CONIN\\$|CONOUT\\$)[ ]*"
                       "(\\.[^\\\\]*)?($|\\\\)"),
        QRegularExpression::CaseInsensitiveOption);
    if (device.match(resolved).hasMatch()) {
        return rejectPath(tr("是裝置名稱，不是檔案"));
    }
    return resolved;
}

QStringList splitLines(const QString& text) {
    static const QRegularExpression lineBreak(QStringLiteral("\r\n|\n|\r"));
    return text.split(lineBreak);
}

ImportEntry makeEntry(const QString& raw, const QString& baseDir) {
    const Result<QString> path = resolveEntryPath(raw, baseDir);
    if (path) {
        return {raw, path.value(), QString()};
    }
    return {raw, QString(), path.error().message};
}

} // namespace

QList<bool> findMissing(const QList<ImportEntry>& entries, const ExistsCheck& exists) {
    QList<bool> missing;
    missing.reserve(entries.size());
    for (const ImportEntry& entry : entries) {
        // 被拒絕的不查詢：查網路路徑會對外連 SMB（安全底線）
        missing.append(entry.path.isEmpty() || !entry.unmatched.isEmpty() ||
                       !exists(entry.path, QString()));
    }
    return missing;
}

bool contentPathExists(const QString& path, const QString& start) {
    // 核心 T-63：一層一層看目錄項目，不跟隨符號連結與 junction（起點本身不檢查）
    return checkContentPath(path, start).usable();
}

Result<QString> resolveEntryPath(const QString& raw, const QString& baseDir) {
    const QString path = raw.trimmed();
    if (path.isEmpty()) {
        return rejectPath(tr("路徑是空的"));
    }
    if (hasControlChar(path)) {
        return rejectPath(tr("路徑含無效字元"));
    }
    if (path.startsWith(QLatin1String("file:"), Qt::CaseInsensitive)) {
        const Result<QString> local = localPathFromUri(path);
        if (!local) {
            return local;
        }
        return checkResolved(joinClean(local.value().left(3), local.value().mid(3)));
    }
    // 其他「名稱:」協定（http:、concat:…）；一個字母的是磁碟機代號
    static const QRegularExpression scheme(QStringLiteral("^[A-Za-z0-9+.\\-]{2,}:"));
    if (scheme.match(path).hasMatch()) {
        return rejectPath(tr("只接受本機檔案路徑，不接受網址或其他協定"));
    }
    const QString native = QDir::toNativeSeparators(path);
    if (native.startsWith(QLatin1String("\\\\"))) {
        return rejectPath(tr("網路上的路徑，不會開啟"));
    }
    if (isDriveAbsolute(native)) {
        return checkResolved(joinClean(native.left(3), native.mid(3)));
    }
    if (native.size() >= 2 && isDriveLetter(native.at(0)) && native.at(1) == QLatin1Char(':')) {
        return rejectPath(tr("不是完整的檔案路徑"));
    }
    // 相對路徑：接在清單檔所在的資料夾後面（使用者選的清單檔在網路上時，只到同一個共用資料夾）
    const QString base = QDir::toNativeSeparators(baseDir);
    QString root;
    if (base.startsWith(QLatin1String("\\\\"))) {
        root = uncRoot(base);
    } else if (isDriveAbsolute(base) || (base.size() == 2 && base.at(1) == QLatin1Char(':'))) {
        root = base.left(2) + QLatin1Char('\\');
    }
    if (root.isEmpty()) {
        return rejectPath(tr("不是完整的檔案路徑"));
    }
    if (native.startsWith(QLatin1Char('\\'))) {
        return checkResolved(joinClean(root, native.mid(1)));
    }
    return checkResolved(joinClean(root, base.mid(root.size()) + QLatin1Char('\\') + native));
}

bool hasControlChar(const QString& text) {
    for (const QChar c : text) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7f) {
            return true;
        }
    }
    return false;
}

QString replaceControlChars(QString text) {
    for (QChar& c : text) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7f) {
            c = QChar(QChar::ReplacementCharacter);
        }
    }
    return text;
}

bool blockedFromOpening(bool missing) {
    return missing;
}

bool storedVerbatim(const QString& path, bool missing) {
    // 完整的本機路徑在 resolveEntryPath 裡不需要 baseDir；被拒絕的原文照樣會被拒絕
    return missing && !resolveEntryPath(path, QString());
}

QString linkReason(const QString& path) {
    if (checkContentPath(path).status != ContentPathStatus::LinkInPath) {
        return {};
    }
    return tr("路徑經過符號連結或 junction，不會開啟");
}

void addMissingReasons(QList<ImportEntry>& entries, const QList<bool>& missing,
                       const ReasonCheck& reason) {
    for (qsizetype i = 0; reason && i < entries.size() && i < missing.size(); ++i) {
        ImportEntry& entry = entries[i];
        if (missing.at(i) && entry.rejected.isEmpty() && entry.unmatched.isEmpty()) {
            entry.rejected = reason(entry.path);
        }
    }
}

QList<ImportEntry> parseM3u(const QString& text, const QString& baseDir) {
    QList<ImportEntry> entries;
    for (const QString& line : splitLines(text)) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
            continue;
        }
        entries.append(makeEntry(trimmed, baseDir));
    }
    return entries;
}

QList<ImportEntry> parsePls(const QString& text, const QString& baseDir) {
    static const QRegularExpression fileKey(QStringLiteral("^file([0-9]+)$"),
                                            QRegularExpression::CaseInsensitiveOption);
    QMap<qint64, QString> files;
    for (const QString& line : splitLines(text)) {
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0) {
            continue;
        }
        const auto match = fileKey.match(line.left(equals).trimmed());
        if (!match.hasMatch()) {
            continue;
        }
        const qint64 number = match.captured(1).toLongLong();
        const QString value = line.mid(equals + 1).trimmed();
        if (!files.contains(number) && !value.isEmpty()) {
            files.insert(number, value);
        }
    }
    QList<ImportEntry> entries;
    entries.reserve(files.size());
    for (const QString& raw : std::as_const(files)) {
        entries.append(makeEntry(raw, baseDir));
    }
    return entries;
}

Result<ParsedPlaylist> readPlaylistFile(const QString& file) {
    const QString native = QDir::toNativeSeparators(file);
    const qsizetype slash = native.lastIndexOf(QLatin1Char('\\'));
    const QString fileName = native.mid(slash + 1);
    const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
    const QString suffix = dot < 0 ? QString() : fileName.mid(dot + 1).toLower();
    ParsedPlaylist result;
    result.name = dot <= 0 ? fileName : fileName.left(dot);
    if (suffix == QLatin1String("fpl")) {
        const Result<QByteArray> bytes = readFplFile(native);
        if (!bytes) {
            return bytes.error();
        }
        std::optional<QList<ImportEntry>> entries = parseFpl(bytes.value());
        result.unsupported = !entries;
        if (entries) {
            result.entries = std::move(*entries);
        }
        return result;
    }
    if (suffix != QLatin1String("m3u") && suffix != QLatin1String("m3u8") &&
        suffix != QLatin1String("pls")) {
        return Error{tr("不是支援的播放清單格式"), 0, QStringLiteral("readPlaylistFile")};
    }
    const Result<DecodedText> decoded = TextDecoder::decodeFile(native);
    if (!decoded) {
        return decoded.error();
    }
    const QString baseDir = slash < 0 ? QString() : native.left(slash);
    result.entries = suffix == QLatin1String("pls") ? parsePls(decoded.value().text, baseDir)
                                                    : parseM3u(decoded.value().text, baseDir);
    return result;
}

} // namespace auralis::mod::pl

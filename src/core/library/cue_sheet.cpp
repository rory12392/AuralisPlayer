#include "library/cue_sheet.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <optional>

namespace auralis::core {

namespace {

Error cueError(const QString& message) {
    return Error{message, 0, QStringLiteral("parseCueSheet")};
}

/// 一行切成指令與參數：雙引號內的空白不切；沒有引號的參數照空白切。
QStringList splitLine(const QString& line) {
    QStringList parts;
    QString current;
    bool quoted = false;
    bool has = false;
    for (const QChar c : line) {
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
            has = true;
        } else if (!quoted && c.isSpace()) {
            if (has) {
                parts.append(current);
                current.clear();
                has = false;
            }
        } else {
            current += c;
            has = true;
        }
    }
    if (has) {
        parts.append(current);
    }
    return parts;
}

/// 引號外的值：指令後面的整段（去掉一對外層引號）。TITLE 沒有加引號時可能含空白。
QString valueAfter(const QString& line, const QString& keyword) {
    QString value = line.trimmed().mid(keyword.size()).trimmed();
    if (value.size() >= 2 && value.startsWith(QLatin1Char('"')) &&
        value.endsWith(QLatin1Char('"'))) {
        value = value.mid(1, value.size() - 2);
    }
    return value;
}

/// MM:SS:FF → CD 影格；格式錯誤時 nullopt。分鐘可以超過 99（長的整軌檔）。
std::optional<qint64> parseTime(const QString& text) {
    // 分鐘最多 5 位數（約 69 天）：再大的值在換算成 CD 影格與樣本框時會溢位
    static const QRegularExpression pattern(QStringLiteral("^(\\d{1,5}):(\\d{1,2}):(\\d{1,2})$"));
    const auto match = pattern.match(text);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    const qint64 seconds = match.captured(2).toLongLong();
    const qint64 frames = match.captured(3).toLongLong();
    if (seconds >= 60 || frames >= kCdFramesPerSecond) {
        return std::nullopt;
    }
    return (match.captured(1).toLongLong() * 60 + seconds) * kCdFramesPerSecond + frames;
}

} // namespace

Result<CueSheet> parseCueSheet(const QString& text) {
    CueSheet sheet;
    // 目前所在的軌：-1 是在 FILE／TRACK 之前（整張的資料）；skip 是非 AUDIO 軌
    int current = -1;
    bool skipTrack = false;
    QSet<int> numbers;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\r\n|\r|\n")));
    for (qsizetype n = 0; n < lines.size(); ++n) {
        const QString& line = lines[n];
        const QStringList parts = splitLine(line);
        if (parts.isEmpty()) {
            continue;
        }
        const QString keyword = parts.first().toUpper();
        const QString where = QStringLiteral("第 %1 行").arg(n + 1);
        if (keyword == QLatin1String("FILE")) {
            if (parts.size() < 2) {
                return cueError(where + QStringLiteral("的 FILE 沒有檔名"));
            }
            sheet.files.push_back(parts[1]);
            // 一軌一檔的抓軌方式會把下一軌的 TRACK 與 INDEX 00（間隔）寫在上一個 FILE 裡、
            // INDEX 01 寫在新的 FILE：這一軌屬於新的檔案，間隔照區段規則算在上一軌的結尾
            if (current >= 0 && sheet.tracks[static_cast<size_t>(current)].start < 0) {
                sheet.tracks[static_cast<size_t>(current)].fileIndex =
                    static_cast<int>(sheet.files.size()) - 1;
            } else {
                current = -1;
            }
            skipTrack = false;
        } else if (keyword == QLatin1String("TRACK")) {
            if (sheet.files.empty()) {
                return cueError(where + QStringLiteral("的 TRACK 前面沒有 FILE"));
            }
            bool ok = false;
            const int number = parts.size() >= 2 ? parts[1].toInt(&ok) : 0;
            if (!ok || number <= 0) {
                return cueError(where + QStringLiteral("的軌號不正確"));
            }
            skipTrack = parts.size() >= 3 && parts[2].toUpper() != QLatin1String("AUDIO");
            if (skipTrack) {
                current = -1;
                continue;
            }
            if (numbers.contains(number)) {
                return cueError(where + QStringLiteral("的軌號 %1 重複").arg(number));
            }
            numbers.insert(number);
            CueTrack track;
            track.number = number;
            track.fileIndex = static_cast<int>(sheet.files.size()) - 1;
            track.start = -1;
            sheet.tracks.push_back(track);
            current = static_cast<int>(sheet.tracks.size()) - 1;
        } else if (keyword == QLatin1String("INDEX")) {
            if (current < 0 || skipTrack || parts.size() < 3) {
                continue;
            }
            const auto time = parseTime(parts[2]);
            if (!time) {
                return cueError(where + QStringLiteral("的時間格式不正確：%1").arg(parts[2]));
            }
            if (parts[1].toInt() == 1) {
                sheet.tracks[static_cast<size_t>(current)].start = *time;
            }
        } else if (keyword == QLatin1String("TITLE") || keyword == QLatin1String("PERFORMER") ||
                   keyword == QLatin1String("SONGWRITER")) {
            if (skipTrack) {
                continue;
            }
            const QString value = valueAfter(line, parts.first());
            if (current < 0) {
                (keyword == QLatin1String("TITLE")       ? sheet.title
                 : keyword == QLatin1String("PERFORMER") ? sheet.performer
                                                         : sheet.songwriter) = value;
            } else {
                auto& track = sheet.tracks[static_cast<size_t>(current)];
                (keyword == QLatin1String("TITLE")       ? track.title
                 : keyword == QLatin1String("PERFORMER") ? track.performer
                                                         : track.songwriter) = value;
            }
        } else if (keyword == QLatin1String("REM") && parts.size() >= 3 && sheet.files.empty()) {
            const QString key = parts[1];
            sheet.rem.insert(key.toUpper(), valueAfter(valueAfter(line, parts.first()), key));
        }
    }
    if (sheet.tracks.empty()) {
        return cueError(QStringLiteral("沒有任何音訊軌"));
    }
    for (size_t i = 0; i < sheet.tracks.size(); ++i) {
        auto& track = sheet.tracks[i];
        if (track.start < 0) {
            return cueError(QStringLiteral("第 %1 軌沒有 INDEX 01").arg(track.number));
        }
        if (i + 1 < sheet.tracks.size() && sheet.tracks[i + 1].fileIndex == track.fileIndex) {
            track.end = sheet.tracks[i + 1].start;
            if (track.end <= track.start) {
                return cueError(QStringLiteral("第 %1 軌的長度不是正數").arg(track.number));
            }
        }
    }
    return sheet;
}

} // namespace auralis::core

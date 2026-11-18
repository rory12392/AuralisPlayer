#include "lyrics_document.h"

#include <algorithm>

namespace auralis::mod::lyr {

namespace {

// [offset:] 的上限：一天。畸形檔寫了超大的數字時不讓時間溢位
constexpr qint64 kMaxOffsetMs = 24LL * 60 * 60 * 1000;

bool allDigits(QStringView s) {
    return !s.isEmpty() && std::all_of(s.begin(), s.end(), [](QChar c) { return c.isDigit(); });
}

/// 方括號裡的時間：mm:ss、mm:ss.x、mm:ss.xx、mm:ss.xxx（分鐘最多 5 位，秒 1–2 位且 < 60）。
bool parseTimeTag(QStringView tag, qint64& ms) {
    const qsizetype colon = tag.indexOf(u':');
    if (colon < 1 || colon > 5) {
        return false;
    }
    const QStringView minutes = tag.left(colon);
    QStringView seconds = tag.mid(colon + 1);
    QStringView fraction;
    const qsizetype dot = seconds.indexOf(u'.');
    if (dot >= 0) {
        fraction = seconds.mid(dot + 1);
        seconds = seconds.left(dot);
        if (!allDigits(fraction) || fraction.size() > 3) {
            return false;
        }
    }
    if (!allDigits(minutes) || !allDigits(seconds) || seconds.size() > 2) {
        return false;
    }
    const int sec = seconds.toInt();
    if (sec >= 60) {
        return false;
    }
    qint64 frac = 0;
    if (!fraction.isEmpty()) {
        frac = fraction.toInt();
        for (qsizetype i = fraction.size(); i < 3; ++i) {
            frac *= 10; // .5 是 500 ms、.05 是 50 ms
        }
    }
    ms = minutes.toLongLong() * 60'000 + sec * 1000 + frac;
    return true;
}

/// 整行只有一個 [key:value] 標頭（key 是英文字母）時回傳 true，key 與 value 由參數帶出。
bool parseHeader(QStringView line, QStringView& key, QStringView& value) {
    if (!line.startsWith(u'[') || !line.endsWith(u']')) {
        return false;
    }
    const QStringView inner = line.mid(1, line.size() - 2);
    const qsizetype colon = inner.indexOf(u':');
    if (colon < 1) {
        return false;
    }
    key = inner.left(colon);
    if (!std::all_of(key.begin(), key.end(),
                     [](QChar c) { return c.isLetter() && c.unicode() < 128; })) {
        return false;
    }
    value = inner.mid(colon + 1);
    return true;
}

QString formatTime(qint64 ms) {
    return QStringLiteral("[%1:%2.%3]")
        .arg(ms / 60'000, 2, 10, QLatin1Char('0'))
        .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

} // namespace

LyricsDocument parseLyrics(const QString& text) {
    LyricsDocument doc;
    QList<LyricLine> timed;
    QList<LyricLine> plain;
    qint64 offset = 0;
    const QStringView all(text);
    qsizetype pos = 0;
    while (pos <= all.size()) {
        qsizetype end = all.indexOf(u'\n', pos);
        if (end < 0) {
            end = all.size();
        }
        const QStringView line = all.mid(pos, end - pos).trimmed(); // trimmed 也去掉 \r
        pos = end + 1;

        QStringView rest = line;
        QList<qint64> times;
        while (rest.startsWith(u'[')) {
            const qsizetype close = rest.indexOf(u']');
            qint64 ms = 0;
            if (close < 0 || !parseTimeTag(rest.mid(1, close - 1), ms)) {
                break;
            }
            times.append(ms);
            rest = rest.mid(close + 1);
        }
        if (!times.isEmpty()) {
            const QString lineText = rest.trimmed().toString();
            for (qint64 t : times) {
                timed.append({t, lineText});
            }
            continue;
        }
        QStringView key;
        QStringView value;
        if (parseHeader(line, key, value)) {
            if (key.compare(u"offset", Qt::CaseInsensitive) == 0) {
                bool ok = false;
                const qint64 v = value.trimmed().toLongLong(&ok);
                if (ok) {
                    offset = std::clamp(v, -kMaxOffsetMs, kMaxOffsetMs);
                }
            }
            continue; // [ti:]、[ar:] 等標頭不顯示
        }
        plain.append({-1, line.toString()});
    }

    if (!timed.isEmpty()) {
        doc.synced = true;
        for (LyricLine& l : timed) {
            l.timeMs = std::max<qint64>(0, l.timeMs - offset); // 正值＝歌詞提前
        }
        // 時間不遞增或重複時依時間排序，同時間保留檔案裡的順序（模組規格 §1 沒涵蓋的情況 5）
        std::stable_sort(timed.begin(), timed.end(), [](const LyricLine& a, const LyricLine& b) {
            return a.timeMs < b.timeMs;
        });
        doc.lines = std::move(timed);
    } else {
        // 純文字：去掉頭尾的空行，中間的空行（段落）保留
        while (!plain.isEmpty() && plain.first().text.isEmpty()) {
            plain.removeFirst();
        }
        while (!plain.isEmpty() && plain.last().text.isEmpty()) {
            plain.removeLast();
        }
        doc.lines = std::move(plain);
    }
    doc.status =
        doc.lines.isEmpty() ? LyricsDocument::Status::NotFound : LyricsDocument::Status::Found;
    return doc;
}

int lineIndexAt(const LyricsDocument& document, qint64 positionMs) {
    if (!document.synced) {
        return -1;
    }
    const auto it = std::upper_bound(
        document.lines.cbegin(), document.lines.cend(), positionMs,
        [](qint64 position, const LyricLine& line) { return position < line.timeMs; });
    return static_cast<int>(it - document.lines.cbegin()) - 1;
}

QString serializeLyrics(const LyricsDocument& document) {
    QStringList out;
    out.reserve(document.lines.size());
    for (const LyricLine& line : document.lines) {
        out.append(document.synced ? formatTime(line.timeMs) + line.text : line.text);
    }
    return out.join(QLatin1Char('\n'));
}

LyricsDocument sliceLyrics(const LyricsDocument& document, qint64 startMs, qint64 endMs) {
    if (!document.synced) {
        return document;
    }
    LyricsDocument out = document;
    out.lines.clear();
    for (const LyricLine& line : document.lines) {
        if (line.timeMs >= startMs && (endMs < 0 || line.timeMs < endMs)) {
            out.lines.append({line.timeMs - startMs, line.text});
        }
    }
    out.status =
        out.lines.isEmpty() ? LyricsDocument::Status::NotFound : LyricsDocument::Status::Found;
    return out;
}

} // namespace auralis::mod::lyr

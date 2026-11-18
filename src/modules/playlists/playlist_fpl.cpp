#include "playlist_fpl.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QtEndian>

#include <memory>

#include <windows.h>

namespace auralis::mod::pl {

namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("PlaylistImport", text);
}

/// foobar2000 .fpl 檔的固定標頭。
constexpr unsigned char kHeader[16] = {0xe1, 0xa0, 0x9c, 0x91, 0xf8, 0x3c, 0x77, 0x42,
                                       0x85, 0x2c, 0x3b, 0xcc, 0x14, 0x01, 0xd3, 0xf2};
/// flags bit 0 的 meta 區塊：file_size、file_time（u64）、duration（f64）、ReplayGain（f32 ×4）。
constexpr quint64 kMetaBytes = 8 + 8 + 8 + 4 * 4;
/// 一筆最少的位元組數：flags、path_offset、subsong、entry_count 共 16，加 entries（E 至少 4）16。
constexpr qsizetype kMinEntryBytes = 32;
/// 標籤顯示文字的長度上限（字元）：多值欄位接起來時不無限長。
constexpr qsizetype kMaxLabelChars = 1024;

/// 只往前讀的游標：每次讀之前先和剩下的位元組數比。
class Cursor {
public:
    explicit Cursor(const QByteArray& bytes) : m_data(bytes.constData()), m_size(bytes.size()) {}
    qsizetype remaining() const { return m_size - m_pos; }
    const char* here() const { return m_data + m_pos; }
    bool u32(quint32& value) {
        if (remaining() < 4) {
            return false;
        }
        value = qFromLittleEndian<quint32>(here());
        m_pos += 4;
        return true;
    }
    bool skip(quint64 count) {
        if (count > static_cast<quint64>(remaining())) {
            return false;
        }
        m_pos += static_cast<qsizetype>(count);
        return true;
    }

private:
    const char* m_data;
    qsizetype m_size;
    qsizetype m_pos = 0;
};

/// 字串表：NUL 結尾的 UTF-8。位移有效＝它之後（含）還有 NUL（最後一個 NUL 的位置算一次）。
/// 解碼過的依位移留著（同一位移被很多筆引用時共用）；解碼的總量有上限，
/// 引用同一個長字串中間的大量不同位移不會讓記憶體或時間無限增加。
class StringTable {
public:
    StringTable(const char* data, quint32 size) : m_data(data), m_size(size) {
        for (qint64 i = static_cast<qint64>(size) - 1; i >= 0; --i) {
            if (m_data[i] == '\0') {
                m_lastNul = i;
                break;
            }
        }
        m_budget = 4 * static_cast<qint64>(size) + 1024 * 1024;
    }
    bool valid(quint32 offset) const { return static_cast<qint64>(offset) <= m_lastNul; }
    /// 不分大小寫比對 ASCII 的 name（offset 要先檢查過 valid）。
    bool equals(quint32 offset, const char* name) const {
        // 字串表裡 offset 之後一定有 NUL（valid），比到 NUL 或 name 結束就停
        const char* text = m_data + offset;
        const auto lower = [](char c) {
            return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c;
        };
        for (qsizetype i = 0;; ++i) {
            const char a = text[i];
            const char b = name[i];
            if (b == '\0') {
                return a == '\0';
            }
            if (lower(a) != lower(b)) {
                return false;
            }
        }
    }
    /// 解碼；超過解碼總量時 std::nullopt（offset 要先檢查過 valid）。
    std::optional<QString> text(quint32 offset) {
        const auto cached = m_cache.constFind(offset);
        if (cached != m_cache.cend()) {
            return *cached;
        }
        const qsizetype length = qstrnlen(m_data + offset, m_size - offset);
        m_budget -= length + 1;
        if (m_budget < 0) {
            return std::nullopt;
        }
        const QString value = QString::fromUtf8(m_data + offset, length);
        m_cache.insert(offset, value);
        return value;
    }

private:
    const char* m_data;
    quint32 m_size;
    qint64 m_lastNul = -1;
    qint64 m_budget = 0;
    QHash<quint32, QString> m_cache;
};

/// 一筆的 entries 陣列（長度 E 已經和剩下的位元組數比過）。
struct Entries {
    const char* data;
    quint64 size;
    quint32 at(quint64 index) const { return qFromLittleEndian<quint32>(data + 4 * index); }
};

/// 路徑字串 → 項目（去掉 file:// 後照 resolveEntryPath；raw 是去掉 file:// 的原文）。
ImportEntry entryFromPath(const QString& text) {
    QString raw = text.trimmed();
    if (raw.startsWith(QLatin1String("file://"), Qt::CaseInsensitive)) {
        raw.remove(0, 7);
    }
    ImportEntry entry;
    entry.raw = raw;
    const Result<QString> path = resolveEntryPath(raw, QString());
    if (path) {
        entry.path = path.value();
    } else {
        entry.rejected = path.error().message;
    }
    return entry;
}

/// 一個標籤的值（多值以「; 」接起來，最多 kMaxLabelChars 字）；越界時 std::nullopt。
/// 串接前先算剩餘額度、滿額就停：很多筆共用超長的多值標籤時，每筆只複製額度內的字。
std::optional<QString> tagValue(const Entries& entries, quint64 valuesAt, quint32 begin,
                                quint32 end, StringTable& strings) {
    QString value;
    for (quint32 i = begin; i < end && value.size() < kMaxLabelChars; ++i) {
        const std::optional<QString> part = strings.text(entries.at(valuesAt + i));
        if (!part) {
            return std::nullopt;
        }
        if (!value.isEmpty()) {
            value += QStringView(u"; ").left(kMaxLabelChars - value.size());
        }
        value += QStringView(*part).left(kMaxLabelChars - value.size());
    }
    return value;
}

/// 讀一筆；結構不符時 std::nullopt。paths 是路徑位移 → 解析好的項目（同一個檔案的多軌共用）。
std::optional<ImportEntry> readEntry(Cursor& cursor, StringTable& strings,
                                     QHash<quint32, ImportEntry>& paths) {
    quint32 flags = 0;
    quint32 pathOffset = 0;
    quint32 subsong = 0;
    if (!cursor.u32(flags) || !cursor.u32(pathOffset) || !cursor.u32(subsong)) {
        return std::nullopt;
    }
    if ((flags & 1U) != 0 && !cursor.skip(kMetaBytes)) {
        return std::nullopt;
    }
    quint32 count = 0;
    if (!cursor.u32(count) || count < 4 ||
        static_cast<quint64>(count) > static_cast<quint64>(cursor.remaining()) / 4) {
        return std::nullopt;
    }
    const Entries entries{cursor.here(), count};
    cursor.skip(4 * static_cast<quint64>(count));
    // [0] pk、[1] sk、[2] 未使用、[3 .. 3+2pk) 標籤、[3+2pk] nvals、值、次要欄位
    const quint64 pk = entries.at(0);
    const quint64 sk = entries.at(1);
    const quint64 nvalsAt = 3 + 2 * pk;
    if (nvalsAt >= entries.size) {
        return std::nullopt;
    }
    const quint64 nvals = entries.at(nvalsAt);
    if (4 + 2 * pk + nvals + 2 * sk != entries.size) {
        return std::nullopt;
    }
    const quint64 valuesAt = nvalsAt + 1;
    const quint64 secondaryAt = valuesAt + nvals;
    // 所有字串位移都要在字串表裡、而且後面有 NUL（不只用到的那幾個）
    if (!strings.valid(pathOffset)) {
        return std::nullopt;
    }
    for (quint64 j = 0; j < pk; ++j) {
        if (!strings.valid(entries.at(4 + 2 * j))) {
            return std::nullopt;
        }
    }
    for (quint64 i = valuesAt; i < entries.size; ++i) {
        if (!strings.valid(entries.at(i))) {
            return std::nullopt;
        }
    }
    // 標籤：第 j 個的值是 values[index_j .. index_{j+1})（最後一個到 nvals）
    QString artist;
    QString title;
    for (quint64 j = 0; j < pk; ++j) {
        const quint32 begin = entries.at(3 + 2 * j);
        const quint32 end = j + 1 < pk ? entries.at(3 + 2 * (j + 1)) : static_cast<quint32>(nvals);
        if (begin > nvals || end > nvals || end < begin) {
            return std::nullopt;
        }
        const quint32 key = entries.at(4 + 2 * j);
        QString* target = strings.equals(key, "artist")  ? &artist
                          : strings.equals(key, "title") ? &title
                                                         : nullptr;
        if (target == nullptr) {
            continue;
        }
        const std::optional<QString> value = tagValue(entries, valuesAt, begin, end, strings);
        if (!value) {
            return std::nullopt;
        }
        *target = *value;
    }
    bool cueEmbedded = false;
    for (quint64 j = 0; j < sk; ++j) {
        if (strings.equals(entries.at(secondaryAt + 2 * j), "cue_embedded") &&
            strings.equals(entries.at(secondaryAt + 2 * j + 1), "yes")) {
            cueEmbedded = true;
        }
    }

    auto known = paths.constFind(pathOffset);
    if (known == paths.cend()) {
        const std::optional<QString> text = strings.text(pathOffset);
        if (!text) {
            return std::nullopt;
        }
        known = paths.insert(pathOffset, entryFromPath(*text));
    }
    ImportEntry entry = *known;
    // subsong 只有 CUE 曲目用：.cue 本身，或內嵌 cue 的整軌檔（DSD 檔的 1 不代表第 1 軌）
    if (entry.raw.endsWith(QLatin1String(".cue"), Qt::CaseInsensitive)) {
        entry.subsong = subsong;
    } else if (cueEmbedded && subsong > 0) {
        entry.subsong = subsong;
        entry.embeddedCue = true;
    }
    if (!title.isEmpty()) {
        entry.label = artist.isEmpty() ? title : artist + QLatin1String(" - ") + title;
    }
    return entry;
}

/// 長路徑前綴（\\?\、\\?\UNC\）；path 是完整的 Windows 路徑。
std::wstring longPath(const QString& path) {
    QString full = path;
    if (full.startsWith(QLatin1String("\\\\?\\"))) {
        // 已經是長路徑
    } else if (full.startsWith(QLatin1String("\\\\"))) {
        full = QLatin1String("\\\\?\\UNC\\") + full.mid(2);
    } else {
        full = QLatin1String("\\\\?\\") + full;
    }
    return full.toStdWString();
}

} // namespace

Result<QByteArray> readFplFile(const QString& path) {
    // 唯讀、共用讀寫刪除：使用者的播放清單一律不改（AC-12-4）
    const std::wstring name =
        longPath(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
    HANDLE file = CreateFileW(name.c_str(), GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return Error{tr("無法開啟檔案"), HRESULT_FROM_WIN32(GetLastError()),
                     QStringLiteral("CreateFileW")};
    }
    const std::unique_ptr<void, decltype(&CloseHandle)> guard(file, &CloseHandle);
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        return Error{tr("無法讀取檔案"), HRESULT_FROM_WIN32(GetLastError()),
                     QStringLiteral("GetFileSizeEx")};
    }
    // 超過上限＝讀不到（Architecture §8），不整個讀進記憶體
    if (size.QuadPart > kMaxFplBytes) {
        return Error{tr("檔案太大（%1 位元組）").arg(size.QuadPart), 0,
                     QStringLiteral("readFplFile")};
    }
    QByteArray bytes(static_cast<qsizetype>(size.QuadPart), Qt::Uninitialized);
    qsizetype done = 0;
    while (done < bytes.size()) {
        DWORD read = 0;
        if (!ReadFile(file, bytes.data() + done, static_cast<DWORD>(bytes.size() - done), &read,
                      nullptr)) {
            return Error{tr("無法讀取檔案"), HRESULT_FROM_WIN32(GetLastError()),
                         QStringLiteral("ReadFile")};
        }
        if (read == 0) {
            break; // 讀的期間檔案變短
        }
        done += read;
    }
    bytes.truncate(done);
    return bytes;
}

std::optional<QList<ImportEntry>> parseFpl(const QByteArray& bytes) {
    Cursor cursor(bytes);
    if (cursor.remaining() < static_cast<qsizetype>(sizeof(kHeader)) ||
        memcmp(cursor.here(), kHeader, sizeof(kHeader)) != 0) {
        return std::nullopt;
    }
    cursor.skip(sizeof(kHeader));
    quint32 tableSize = 0;
    if (!cursor.u32(tableSize) || tableSize > static_cast<quint64>(cursor.remaining())) {
        return std::nullopt;
    }
    StringTable strings(cursor.here(), tableSize);
    cursor.skip(tableSize);
    quint32 count = 0;
    // 每一筆至少 kMinEntryBytes：超大的 count 在配置任何東西之前就擋掉；筆數另有上限（記憶體峰值）
    if (!cursor.u32(count) || count > static_cast<quint64>(kMaxFplEntries) ||
        static_cast<quint64>(count) > static_cast<quint64>(cursor.remaining()) / kMinEntryBytes) {
        return std::nullopt;
    }
    QList<ImportEntry> entries;
    entries.reserve(count);
    QHash<quint32, ImportEntry> paths;
    for (quint32 i = 0; i < count; ++i) {
        std::optional<ImportEntry> entry = readEntry(cursor, strings, paths);
        if (!entry) {
            return std::nullopt;
        }
        entries.append(std::move(*entry));
    }
    // 最後一筆要剛好到檔尾
    if (cursor.remaining() != 0) {
        return std::nullopt;
    }
    return entries;
}

void matchEmbeddedCue(QList<ImportEntry>& entries, const CueLookup& lookup) {
    for (ImportEntry& entry : entries) {
        if (!entry.embeddedCue || entry.path.isEmpty()) {
            continue;
        }
        const std::optional<TrackRef> ref =
            lookup ? lookup(entry.path, entry.subsong) : std::optional<TrackRef>();
        if (ref) {
            entry.path = ref->path;
            entry.subsong = ref->subsong;
            entry.embeddedCue = false;
        } else {
            entry.unmatched = tr("音樂庫裡沒有這一軌的 CUE 曲目");
        }
    }
}

CueLookup libraryCueLookup(const LibraryQuery& library) {
    // 音樂庫的 CUE 曲目（第一次用到時讀一次；每個 lookup 自己一份，只在一個工作執行緒用）
    auto cues = std::make_shared<std::optional<QList<TrackRef>>>();
    return [&library, cues](const QString& audioFile, quint32 track) -> std::optional<TrackRef> {
        if (!cues->has_value()) {
            QList<TrackRef> refs;
            const Result<QList<TrackInfo>> all = library.tracks(TrackFilter{});
            if (all) {
                for (const TrackInfo& info : all.value()) {
                    if (info.ref.path.endsWith(QLatin1String(".cue"), Qt::CaseInsensitive)) {
                        refs.append(info.ref);
                    }
                }
            }
            *cues = std::move(refs);
        }
        const QString audioKey = pathKey(audioFile);
        for (const TrackRef& ref : **cues) {
            if (ref.subsong != track) {
                continue;
            }
            // 整軌檔只會在 .cue 所在資料夾底下（核心的找檔規則），先用字串篩掉其他的
            const QString cueKey = pathKey(ref.path);
            const QString folder = cueKey.left(cueKey.lastIndexOf(QLatin1Char('\\')) + 1);
            if (folder.isEmpty() || !audioKey.startsWith(folder)) {
                continue;
            }
            const Result<QString> audio = library.audioFilePath(ref);
            if (audio && pathKey(audio.value()) == audioKey) {
                return ref;
            }
        }
        return std::nullopt;
    };
}

} // namespace auralis::mod::pl

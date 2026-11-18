#include "library/tag_reader.h"

#include "library/read_only_stream.h"

#include <QFileInfo>
#include <QRegularExpression>

#include <taglib/aifffile.h>
#include <taglib/apefile.h>
#include <taglib/dsdifffile.h>
#include <taglib/dsffile.h>
#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/mp4file.h>
#include <taglib/mpegfile.h>
#include <taglib/oggflacfile.h>
#include <taglib/opusfile.h>
#include <taglib/synchronizedlyricsframe.h>
#include <taglib/tpropertymap.h>
#include <taglib/vorbisfile.h>
#include <taglib/wavfile.h>
#include <taglib/wavpackfile.h>

namespace auralis::core {

namespace {

QString toQString(const TagLib::String& text) {
    return QString::fromStdWString(text.toWString());
}

QString first(const TagLib::PropertyMap& properties, const char* key) {
    const auto it = properties.find(key);
    if (it == properties.end() || it->second.isEmpty()) {
        return {};
    }
    return toQString(it->second.front()).trimmed();
}

/// 「2019-05-01」「3/12」這類值取開頭的整數；沒有時為 0。
int leadingInt(const QString& text) {
    static const QRegularExpression digits(QStringLiteral("^\\s*(\\d+)"));
    const auto match = digits.match(text);
    return match.hasMatch() ? match.captured(1).toInt() : 0;
}

/// ReplayGain 的值像「-6.50 dB」「0.988553」；不是數字時為 std::nullopt。
std::optional<double> leadingDouble(const QString& text) {
    static const QRegularExpression number(QStringLiteral("^\\s*([+-]?\\d+(?:\\.\\d+)?)"));
    const auto match = number.match(text);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    return match.captured(1).toDouble();
}

/// 依實際的檔案類型決定格式名稱與位元深度（音訊屬性的共同介面沒有位元深度）。
void fillFormat(TagLib::File* file, const QString& suffix, TagData& data) {
    data.codec = suffix.toUpper();
    if (auto* f = dynamic_cast<TagLib::FLAC::File*>(file)) {
        data.codec = QStringLiteral("FLAC");
        data.bits = f->audioProperties()->bitsPerSample();
    } else if (auto* w = dynamic_cast<TagLib::RIFF::WAV::File*>(file)) {
        data.codec = QStringLiteral("WAV");
        data.bits = w->audioProperties()->bitsPerSample();
    } else if (auto* a = dynamic_cast<TagLib::RIFF::AIFF::File*>(file)) {
        data.codec = QStringLiteral("AIFF");
        data.bits = a->audioProperties()->bitsPerSample();
    } else if (auto* ape = dynamic_cast<TagLib::APE::File*>(file)) {
        data.codec = QStringLiteral("APE");
        data.bits = ape->audioProperties()->bitsPerSample();
    } else if (auto* wv = dynamic_cast<TagLib::WavPack::File*>(file)) {
        data.codec = QStringLiteral("WV");
        data.bits = wv->audioProperties()->bitsPerSample();
    } else if (auto* mp4 = dynamic_cast<TagLib::MP4::File*>(file)) {
        const bool alac = mp4->audioProperties()->codec() == TagLib::MP4::Properties::ALAC;
        data.codec = alac ? QStringLiteral("ALAC") : QStringLiteral("AAC");
        data.bits = alac ? mp4->audioProperties()->bitsPerSample() : 0;
    } else if (dynamic_cast<TagLib::MPEG::File*>(file) != nullptr) {
        data.codec = QStringLiteral("MP3");
    } else if (dynamic_cast<TagLib::Ogg::Opus::File*>(file) != nullptr) {
        data.codec = QStringLiteral("Opus");
    } else if (dynamic_cast<TagLib::Ogg::Vorbis::File*>(file) != nullptr) {
        data.codec = QStringLiteral("Vorbis");
    } else if (auto* oggFlac = dynamic_cast<TagLib::Ogg::FLAC::File*>(file)) {
        data.codec = QStringLiteral("FLAC");
        data.bits = oggFlac->audioProperties()->bitsPerSample();
    } else if (auto* dsf = dynamic_cast<TagLib::DSF::File*>(file)) {
        data.codec = QStringLiteral("DSD");
        data.bits = dsf->audioProperties()->bitsPerSample();
        data.dsdRate = dsf->audioProperties()->sampleRate();
    } else if (auto* dff = dynamic_cast<TagLib::DSDIFF::File*>(file)) {
        data.codec = QStringLiteral("DSD");
        data.bits = dff->audioProperties()->bitsPerSample();
        data.dsdRate = dff->audioProperties()->sampleRate();
    }
}

bool taglibKnows(const QString& suffix) {
    static const TagLib::StringList known = TagLib::FileRef::defaultFileExtensions();
    return known.contains(TagLib::String(suffix.toStdWString()));
}

/// 唯讀開檔並交給 TagLib；失敗時回傳原因。stream 要活得比回傳的 FileRef 久。
Result<TagLib::FileRef> open(ReadOnlyStream& stream, bool readProperties) {
    if (!stream.isOpen()) {
        return Error{QStringLiteral("無法開啟檔案：%1").arg(stream.openError()), 0,
                     QStringLiteral("CreateFileW")};
    }
    return TagLib::FileRef(&stream, readProperties);
}

/// 帶 ID3v2 標籤的格式（MP3、AIFF、WAV 的 id3 chunk、DSF、DSDIFF）回傳它；其他為 nullptr。
TagLib::ID3v2::Tag* id3v2Tag(TagLib::File* file) {
    if (auto* mp3 = dynamic_cast<TagLib::MPEG::File*>(file)) {
        return mp3->ID3v2Tag();
    }
    if (auto* aiff = dynamic_cast<TagLib::RIFF::AIFF::File*>(file)) {
        return aiff->tag();
    }
    if (auto* wav = dynamic_cast<TagLib::RIFF::WAV::File*>(file)) {
        return wav->ID3v2Tag();
    }
    if (auto* dsf = dynamic_cast<TagLib::DSF::File*>(file)) {
        return dsf->tag();
    }
    if (auto* dff = dynamic_cast<TagLib::DSDIFF::File*>(file)) {
        return dff->ID3v2Tag();
    }
    return nullptr;
}

/// 第一個內容型別是歌詞、時間單位是毫秒、而且至少一段有文字的 SYLT 框轉成 LRC
/// （AC-15-1 的內嵌同步歌詞）；沒有時空字串。
/// MPEG 影格單位要音訊屬性才能換算，讀歌詞時不讀音訊屬性，這種框略過。
QString syltAsLrc(TagLib::ID3v2::Tag* tag) {
    if (tag == nullptr) {
        return {};
    }
    const auto& frames = tag->frameListMap();
    const auto it = frames.find("SYLT");
    if (it == frames.end()) {
        return {};
    }
    using TagLib::ID3v2::SynchronizedLyricsFrame;
    // 一個時間標籤只能佔一行：框裡的換行（有些軟體在每段開頭放換行）改成空白
    static const QRegularExpression lineBreaks(QStringLiteral("\\s*[\\r\\n]+\\s*"));
    for (const auto* frame : it->second) {
        const auto* sylt = dynamic_cast<const SynchronizedLyricsFrame*>(frame);
        if (sylt == nullptr || sylt->type() != SynchronizedLyricsFrame::Lyrics ||
            sylt->timestampFormat() != SynchronizedLyricsFrame::AbsoluteMilliseconds) {
            continue;
        }
        QStringList lines;
        // 整框都沒有文字時不採用，免得一串只有時間標籤的行遮住後面的框與 USLT；
        // 有文字的框裡的空白時間點照舊保留（清空顯示用）
        bool hasText = false;
        for (const auto& entry : sylt->synchedText()) {
            const unsigned int ms = entry.time;
            const QString text =
                toQString(entry.text).replace(lineBreaks, QStringLiteral(" ")).trimmed();
            hasText = hasText || !text.isEmpty();
            lines.append(QStringLiteral("[%1:%2.%3]")
                             .arg(ms / 60000, 2, 10, QLatin1Char('0'))
                             .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
                             .arg(ms % 1000 / 10, 2, 10, QLatin1Char('0')) +
                         text);
        }
        if (hasText) {
            return lines.join(QLatin1Char('\n'));
        }
    }
    return {};
}

std::function<void(const QString&)>& readTagsFault() {
    static std::function<void(const QString&)> fault;
    return fault;
}

} // namespace

void setReadTagsFaultForTest(std::function<void(const QString&)> fault) {
    readTagsFault() = std::move(fault);
}

const QStringList& audioExtensions() {
    static const QStringList extensions{
        QStringLiteral("flac"), QStringLiteral("wav"),  QStringLiteral("aif"),
        QStringLiteral("aiff"), QStringLiteral("aifc"), QStringLiteral("ape"),
        QStringLiteral("wv"),   QStringLiteral("mp3"),  QStringLiteral("aac"),
        QStringLiteral("m4a"),  QStringLiteral("mp4"),  QStringLiteral("opus"),
        QStringLiteral("ogg"),  QStringLiteral("oga"),  QStringLiteral("dsf"),
        QStringLiteral("dff")};
    return extensions;
}

Result<TagData> readTags(const QString& path) {
    if (readTagsFault()) {
        readTagsFault()(path);
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    ReadOnlyStream stream(path);
    auto ref = open(stream, true);
    if (!ref) {
        return ref.error();
    }
    if (stream.readLimitExceeded()) {
        return Error{QStringLiteral("檔案中的區塊超過讀取上限（%1 MB），可能已損毀")
                         .arg(ReadOnlyStream::kMaxReadBlock / (1024 * 1024)),
                     0, QStringLiteral("ReadOnlyStream::readBlock")};
    }
    TagData data;
    if (ref.value().isNull()) {
        if (!taglibKnows(suffix)) {
            data.codec = suffix.toUpper();
            return data;
        }
        return Error{QStringLiteral("無法解析檔案（可能已損毀或不是這個格式）"), 0,
                     QStringLiteral("TagLib::FileRef")};
    }

    TagLib::File* file = ref.value().file();
    const TagLib::PropertyMap properties = file->properties();
    data.title = first(properties, "TITLE");
    data.artist = first(properties, "ARTIST");
    data.albumArtist = first(properties, "ALBUMARTIST");
    data.album = first(properties, "ALBUM");
    data.composer = first(properties, "COMPOSER");
    data.genre = first(properties, "GENRE");
    data.year = leadingInt(first(properties, "DATE"));
    data.discNo = leadingInt(first(properties, "DISCNUMBER"));
    data.trackNo = leadingInt(first(properties, "TRACKNUMBER"));
    data.rgTrackGain = leadingDouble(first(properties, "REPLAYGAIN_TRACK_GAIN"));
    data.rgTrackPeak = leadingDouble(first(properties, "REPLAYGAIN_TRACK_PEAK"));
    data.rgAlbumGain = leadingDouble(first(properties, "REPLAYGAIN_ALBUM_GAIN"));
    data.rgAlbumPeak = leadingDouble(first(properties, "REPLAYGAIN_ALBUM_PEAK"));
    data.hasEmbeddedLyrics = !first(properties, "LYRICS").isEmpty();
    data.hasEmbeddedCover = file->complexPropertyKeys().contains("PICTURE");

    if (const auto* audio = file->audioProperties()) {
        data.durationMs = audio->lengthInMilliseconds();
        data.sampleRate = audio->sampleRate();
        data.channels = audio->channels();
    }
    if (file->audioProperties() != nullptr) {
        fillFormat(file, suffix, data); // 各格式的音訊屬性都經由它取得，沒有時不能呼叫
    } else {
        data.codec = suffix.toUpper();
    }
    return data;
}

Error readTagsExceptionError() {
    QString detail = QStringLiteral("未知的例外");
    try {
        throw;
    } catch (const std::exception& e) {
        detail = QString::fromUtf8(e.what());
    } catch (...) {
    }
    return Error{QStringLiteral("讀標籤時發生例外：%1").arg(detail), 0, QStringLiteral("readTags")};
}

Result<QString> readEmbeddedLyrics(const QString& path) {
    ReadOnlyStream stream(path);
    auto ref = open(stream, false);
    if (!ref) {
        return ref.error();
    }
    if (ref.value().isNull()) {
        return QString();
    }
    // 同一首同時有 SYLT 與 USLT 時優先同步歌詞
    const QString synced = syltAsLrc(id3v2Tag(ref.value().file()));
    if (!synced.isEmpty()) {
        return synced;
    }
    const TagLib::PropertyMap properties = ref.value().file()->properties();
    const auto it = properties.find("LYRICS");
    if (it == properties.end() || it->second.isEmpty()) {
        return QString();
    }
    return toQString(it->second.toString("\n"));
}

Result<QByteArray> readEmbeddedCover(const QString& path) {
    ReadOnlyStream stream(path);
    auto ref = open(stream, false);
    if (!ref) {
        return ref.error();
    }
    if (ref.value().isNull()) {
        return QByteArray();
    }
    const auto pictures = ref.value().file()->complexProperties("PICTURE");
    if (pictures.isEmpty()) {
        return QByteArray();
    }
    auto chosen = pictures.front();
    for (const auto& picture : pictures) {
        if (picture.value("pictureType").toString() == "Front Cover") {
            chosen = picture;
            break;
        }
    }
    const TagLib::ByteVector data = chosen.value("data").toByteVector();
    return QByteArray(data.data(), static_cast<qsizetype>(data.size()));
}

} // namespace auralis::core

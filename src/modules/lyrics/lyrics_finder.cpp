#include "lyrics_finder.h"

#include <auralis/library_query.h>
#include <auralis/text_decoder.h>

#include <QDir>
#include <QFileInfo>
#include <QLoggingCategory>

namespace auralis::mod::lyr {

namespace {

Q_LOGGING_CATEGORY(lcLyrics, "auralis.lyr")

/// CD 影格（1/75 秒）換成毫秒。
qint64 cdFrameToMs(qint64 frame) {
    return frame * 1000 / 75;
}

} // namespace

QStringList lrcCandidates(const LyricsTrack& track) {
    if (track.audioPath.isEmpty()) {
        return {};
    }
    const QFileInfo audio(track.audioPath);
    const QString base =
        QDir::toNativeSeparators(audio.path()) + QDir::separator() + audio.completeBaseName();
    QStringList out;
    if (track.cueTrack != 0) {
        out.append(
            QStringLiteral("%1 %2.lrc").arg(base).arg(track.cueTrack, 2, 10, QLatin1Char('0')));
    }
    out.append(base + QStringLiteral(".lrc"));
    return out;
}

std::optional<LyricsDocument> LrcFileStep::find(const LyricsTrack& track) {
    const QStringList candidates = lrcCandidates(track);
    for (qsizetype i = 0; i < candidates.size(); ++i) {
        const QFileInfo info(candidates[i]);
        if (!info.isFile()) {
            continue;
        }
        // 解碼一律經核心（BOM、UTF-8、uchardet 的 Big5／Shift-JIS，AC-15-4）；有大小上限
        const Result<DecodedText> decoded = TextDecoder::decodeFile(candidates[i]);
        if (!decoded) {
            qCWarning(lcLyrics) << "讀不到 .lrc" << candidates[i] << decoded.error().message;
            continue;
        }
        LyricsDocument doc = parseLyrics(decoded.value().text);
        // CUE 曲目借用整軌 .lrc（最後一個候選）：時間換成曲目內的位置
        const bool wholeFile = track.cueTrack != 0 && i == candidates.size() - 1;
        if (wholeFile) {
            doc = sliceLyrics(doc, track.startMs, track.endMs);
        }
        if (doc.status == LyricsDocument::Status::Found) {
            doc.source = LyricsSource::Lrc;
            return doc;
        }
    }
    return std::nullopt;
}

std::optional<LyricsDocument> EmbeddedStep::find(const LyricsTrack& track) {
    // CUE 曲目核心一律回傳空字串（內嵌歌詞屬於整個檔案）
    const Result<QString> text = m_library.readEmbeddedLyrics(track.ref);
    if (!text) {
        // 可能只是暫時讀不到（檔案被別的程式鎖住），不是「確定沒有」：
        // 整條鏈沒找到時不能記成 notfound
        qCWarning(lcLyrics) << "讀不到內嵌歌詞" << track.ref.path << text.error().message;
        LyricsDocument failed;
        failed.status = LyricsDocument::Status::Error;
        failed.source = LyricsSource::Embedded;
        return failed;
    }
    LyricsDocument doc = parseLyrics(text.value());
    if (doc.status != LyricsDocument::Status::Found) {
        return std::nullopt;
    }
    doc.source = LyricsSource::Embedded;
    return doc;
}

void LyricsFinder::append(std::unique_ptr<LyricsStep> step) {
    m_steps.push_back(std::move(step));
}

std::optional<LyricsDocument> LyricsFinder::find(const LyricsTrack& track,
                                                 const std::function<bool()>& cancelled) const {
    std::optional<LyricsSource> last;
    bool failed = false;
    for (const auto& step : m_steps) {
        if (cancelled()) {
            return std::nullopt;
        }
        if (!step->active()) {
            continue;
        }
        last = step->kind();
        if (auto doc = step->find(track, cancelled, failed)) {
            if (doc->status == LyricsDocument::Status::Error) {
                failed = true; // 這一段暫時讀不到：照樣往下找
                continue;
            }
            // 前面有段落讀取失敗：後段的結果照常顯示，但不寫快取（下次從頭找）
            doc->cacheable = !failed;
            return doc;
        }
    }
    LyricsDocument none;
    // 有一段讀取失敗時不是「確定沒有」：Error 不寫成可重用的快取，面板照樣顯示「沒有歌詞」
    if (failed) {
        none.status = LyricsDocument::Status::Error;
    }
    if (last) {
        none.source = *last;
    }
    return none;
}

LyricsSource LyricsFinder::lastActiveKind() const {
    for (auto it = m_steps.rbegin(); it != m_steps.rend(); ++it) {
        if ((*it)->active()) {
            return (*it)->kind();
        }
    }
    return LyricsDocument().source;
}

LyricsTrack resolveTrack(const LibraryQuery& library, const TrackRef& ref) {
    LyricsTrack track;
    track.ref = ref;
    // 查 LRCLIB 用的標籤（T-67）：不在音樂庫的檔案沒有，線上那段就不查
    const auto info = library.findByIdentity(ref.path, ref.subsong);
    if (info && info.value()) {
        const TrackInfo& t = *info.value();
        track.title = t.title;
        track.artist = t.artist;
        track.album = t.album;
        track.durationMs = t.durationMs;
        if (ref.subsong != 0) {
            track.startMs = t.startCdFrame > 0 ? cdFrameToMs(t.startCdFrame) : 0;
            track.endMs = t.endCdFrame >= 0 ? cdFrameToMs(t.endCdFrame) : -1;
        }
    }
    if (ref.subsong == 0) {
        track.audioPath = ref.path;
        return track;
    }
    track.cueTrack = ref.subsong;
    // 整軌檔（T-77）：取不到時沒有 .lrc 候選，快取看 .cue 本身的修改時間
    const Result<QString> audio = library.audioFilePath(ref);
    if (audio) {
        track.audioPath = audio.value();
    } else {
        qCWarning(lcLyrics) << "取不到 CUE 曲目的整軌檔" << ref.path << ref.subsong
                            << audio.error().message;
    }
    return track;
}

} // namespace auralis::mod::lyr

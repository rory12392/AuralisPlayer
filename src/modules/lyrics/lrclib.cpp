#include "lrclib.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimeZone>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>

namespace auralis::mod::lyr {

namespace {

Q_LOGGING_CATEGORY(lcLrclib, "auralis.lyr.lrclib")

/// 版本後綴的關鍵字（模組規格 §1「沒涵蓋的情況」第 1 條）。
constexpr auto kSuffixWords =
    "remaster(?:ed)?|version|mix|edit|live|mono|stereo|instrumental|acoustic|demo|bonus|deluxe";

/// 換歌與設定關閉的檢查間隔：取消後最多這麼久就 abort。
constexpr int kPollMs = 50;

/// 搜尋結果可以接受的時長差（秒）。
constexpr double kMaxDurationDiffSec = 3.0;

/// Retry-After 秒數的上限（一天）：比這更長的當成解析不了，避免乘 1000 溢位。
constexpr qint64 kMaxRetryAfterSec = 24 * 60 * 60;

/// 限流冷卻用的單調時鐘（毫秒）；改系統時間不影響。
qint64 steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

QString encode(const QString& value) {
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

QUrl apiUrl(const QString& baseUrl, const QString& path, const QStringList& pairs) {
    QUrl url(baseUrl + path);
    // 自己做百分比編碼：QUrlQuery 不編「+」，伺服器會當成空白
    url.setQuery(pairs.join(u'&'), QUrl::StrictMode);
    return url;
}

LyricsDocument unavailable() {
    LyricsDocument doc;
    doc.status = LyricsDocument::Status::Unavailable;
    doc.source = LyricsSource::Lrclib;
    return doc;
}

LyricsDocument notFound() {
    LyricsDocument doc;
    doc.source = LyricsSource::Lrclib;
    return doc;
}

/// 一筆 LRCLIB 紀錄：同步歌詞優先，再來純音樂、純文字；都沒有時 NotFound。
LyricsDocument fromRecord(const QJsonObject& record) {
    const LyricsDocument synced = parseLyrics(record.value(u"syncedLyrics").toString());
    if (synced.status == LyricsDocument::Status::Found && synced.synced) {
        LyricsDocument doc = synced;
        doc.source = LyricsSource::Lrclib;
        return doc;
    }
    if (record.value(u"instrumental").toBool()) {
        LyricsDocument doc = notFound();
        doc.status = LyricsDocument::Status::Found;
        doc.instrumental = true;
        return doc;
    }
    LyricsDocument plain = parseLyrics(record.value(u"plainLyrics").toString());
    if (plain.status == LyricsDocument::Status::Found) {
        plain.source = LyricsSource::Lrclib;
        return plain;
    }
    return notFound();
}

} // namespace

qint64 lrclibRetryAfterMs(const QByteArray& value, const QDateTime& nowUtc) {
    const QByteArray text = value.trimmed();
    if (text.isEmpty()) {
        return kLrclibDefaultRetryAfterMs;
    }
    // 秒數：只收非負整數（RFC 9110 的 delay-seconds）
    bool isNumber =
        std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; });
    if (isNumber) {
        const qint64 seconds = text.toLongLong(&isNumber);
        // 位數多到溢位時當成解析不了
        return isNumber && seconds <= kMaxRetryAfterSec ? seconds * 1000
                                                        : kLrclibDefaultRetryAfterMs;
    }
    // HTTP 日期（IMF-fixdate）：「Tue, 17 Nov 2026 12:01:30 GMT」
    QDateTime when = QLocale::c().toDateTime(QString::fromLatin1(text),
                                             QStringLiteral("ddd, dd MMM yyyy HH:mm:ss 'GMT'"));
    if (!when.isValid()) {
        return kLrclibDefaultRetryAfterMs;
    }
    when.setTimeZone(QTimeZone::UTC);
    return std::max<qint64>(0, nowUtc.msecsTo(when));
}

QString lrclibUserAgent(const QString& version) {
    return QStringLiteral("AuralisPlayer/%1 (https://github.com/rory12392/AuralisPlayer)")
        .arg(version);
}

QString stripVersionSuffix(const QString& title) {
    static const QRegularExpression dash(
        QStringLiteral(R"(\s+[-–—]\s+[^-–—]*\b(?:%1)\b[^-–—]*$)").arg(QLatin1String(kSuffixWords)),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression bracket(
        QStringLiteral(R"(\s*[(\[][^()\[\]]*\b(?:%1)\b[^()\[\]]*[)\]]\s*$)")
            .arg(QLatin1String(kSuffixWords)),
        QRegularExpression::CaseInsensitiveOption);
    QString out = title.trimmed();
    for (;;) {
        QString next = out;
        next.remove(dash);
        next.remove(bracket);
        next = next.trimmed();
        if (next == out || next.isEmpty()) {
            return out;
        }
        out = next;
    }
}

QUrl lrclibGetUrl(const QString& baseUrl, const LyricsTrack& track, const QString& title) {
    const qint64 seconds = std::llround(static_cast<double>(track.durationMs) / 1000.0);
    return apiUrl(baseUrl, QStringLiteral("/api/get"),
                  {QStringLiteral("track_name=") + encode(title),
                   QStringLiteral("artist_name=") + encode(track.artist),
                   QStringLiteral("album_name=") + encode(track.album),
                   QStringLiteral("duration=") + QString::number(seconds)});
}

QUrl lrclibSearchUrl(const QString& baseUrl, const LyricsTrack& track, const QString& title) {
    return apiUrl(baseUrl, QStringLiteral("/api/search"),
                  {QStringLiteral("track_name=") + encode(title),
                   QStringLiteral("artist_name=") + encode(track.artist)});
}

LyricsDocument parseLrclibRecord(const QByteArray& body) {
    const QJsonDocument json = QJsonDocument::fromJson(body);
    if (!json.isObject()) {
        return unavailable();
    }
    return fromRecord(json.object());
}

LyricsDocument pickLrclibSearch(const QByteArray& body, qint64 durationMs) {
    const QJsonDocument json = QJsonDocument::fromJson(body);
    if (!json.isArray()) {
        return unavailable();
    }
    const double seconds = static_cast<double>(durationMs) / 1000.0;
    for (const QJsonValue& value : json.array()) {
        const QJsonObject record = value.toObject();
        if (durationMs > 0 &&
            std::abs(record.value(u"duration").toDouble(-1e9) - seconds) > kMaxDurationDiffSec) {
            continue;
        }
        const LyricsDocument doc = parseLyrics(record.value(u"syncedLyrics").toString());
        if (doc.status == LyricsDocument::Status::Found && doc.synced) {
            LyricsDocument found = doc;
            found.source = LyricsSource::Lrclib;
            return found;
        }
    }
    return notFound();
}

Result<bool> saveLrcBeside(const LyricsTrack& track, const LyricsDocument& document) {
    const QStringList candidates = lrcCandidates(track);
    if (candidates.isEmpty() || document.status != LyricsDocument::Status::Found ||
        document.lines.isEmpty()) {
        return false;
    }
    // CUE 曲目是「整軌主檔名 兩位數軌號.lrc」，一般曲目是同名 .lrc（都是第一個候選）
    const QString target = candidates.first();
    if (QFileInfo::exists(target)) {
        return false; // 只新增，不覆寫使用者的 .lrc
    }
    const QString temp = target + QStringLiteral(".auralis-tmp");
    QFile file(temp);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        return Error{file.errorString(), 0, QStringLiteral("QFile::open")};
    }
    const QByteArray bytes = serializeLyrics(document).toUtf8();
    const bool written = file.write(bytes) == bytes.size() && file.flush();
    file.close();
    // 改名不覆寫：這段時間別人放了同名 .lrc 時改名失敗，暫存檔刪掉
    if (!written || !QFile::rename(temp, target)) {
        const QString reason = written ? QStringLiteral("改名失敗") : file.errorString();
        QFile::remove(temp);
        return Error{reason, 0, QStringLiteral("saveLrcBeside")};
    }
    return true;
}

struct LrclibStep::Reply {
    bool ok = false; ///< 有收到完整的 HTTP 回應
    int status = 0;
    QByteArray body;
};

LrclibStep::LrclibStep(LrclibConfig config, std::shared_ptr<const LrclibSwitches> switches,
                       SaveFailed saveFailed)
    : m_config(std::move(config)), m_switches(std::move(switches)),
      m_saveFailed(std::move(saveFailed)) {}

qint64 LrclibStep::cooldownRemainingMs() const {
    return std::max<qint64>(0, m_blockedUntilMs.load() - steadyNowMs());
}

bool LrclibStep::active() const {
    return m_switches->online.load();
}

std::optional<LyricsDocument> LrclibStep::find(const LyricsTrack& track) {
    return find(track, [] { return false; });
}

std::optional<LyricsDocument> LrclibStep::find(const LyricsTrack& track,
                                               const std::function<bool()>& cancelled) {
    return find(track, cancelled, false);
}

std::optional<LyricsDocument> LrclibStep::find(const LyricsTrack& track,
                                               const std::function<bool()>& cancelled,
                                               bool earlierFailed) {
    // 不在音樂庫的檔案沒有曲名與藝人，LRCLIB 也查不到
    if (track.title.isEmpty() || track.artist.isEmpty()) {
        return std::nullopt;
    }
    LyricsDocument doc = lookup(track, track.title, cancelled);
    const QString stripped = stripVersionSuffix(track.title);
    if (doc.status == LyricsDocument::Status::NotFound && stripped != track.title) {
        doc = lookup(track, stripped, cancelled);
    }
    if (doc.status == LyricsDocument::Status::NotFound) {
        return std::nullopt;
    }
    if (doc.status == LyricsDocument::Status::Found && !doc.instrumental &&
        m_switches->saveLrc.load()) {
        if (earlierFailed) {
            // 不是錯誤，不跳通知：前段恢復後要換回前段（AC-15-1 的來源順序）
            qCInfo(lcLrclib) << "前段讀取失敗，這次的線上歌詞不另存 .lrc" << track.audioPath;
            return doc;
        }
        // 寫入失敗只記日誌與通知列，歌詞照樣顯示（AC-16-4）
        const Result<bool> saved = saveLrcBeside(track, doc);
        if (!saved) {
            qCWarning(lcLrclib) << "另存 .lrc 失敗" << track.audioPath << saved.error().message;
            if (m_saveFailed) {
                m_saveFailed(saved.error().message);
            }
        }
    }
    return doc;
}

LyricsDocument LrclibStep::lookup(const LyricsTrack& track, const QString& title,
                                  const std::function<bool()>& cancelled) const {
    // 不知道長度時 /api/get 一定配不到，直接搜尋
    if (track.durationMs > 0) {
        const Reply got = fetch(lrclibGetUrl(m_config.baseUrl, track, title), cancelled);
        if (!got.ok || (got.status != 200 && got.status != 404)) {
            return unavailable();
        }
        if (got.status == 200) {
            const LyricsDocument doc = parseLrclibRecord(got.body);
            if (doc.status != LyricsDocument::Status::NotFound) {
                return doc;
            }
        }
    }
    const Reply found = fetch(lrclibSearchUrl(m_config.baseUrl, track, title), cancelled);
    if (!found.ok || found.status != 200) {
        return unavailable();
    }
    return pickLrclibSearch(found.body, track.durationMs);
}

LrclibStep::Reply LrclibStep::fetch(const QUrl& url, const std::function<bool()>& cancelled) const {
    Reply out;
    if (cancelled() || !active()) {
        return out;
    }
    // 被限流（429／503）後的冷卻中：整段不送請求，當成暫時取不到（T-99）
    if (const qint64 remaining = cooldownRemainingMs(); remaining > 0) {
        qCInfo(lcLrclib) << "LRCLIB 限流冷卻中，不送請求" << url.path() << "還剩" << remaining
                         << "ms";
        return out;
    }
    // 每次查詢在工作執行緒建一個、查完就刪：線上抓取關閉時不存在（AC-16-3）
    QNetworkAccessManager network;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, m_config.userAgent);
    // 不跟隨轉址：對外連線只有 LRCLIB 這一個網址
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::ManualRedirectPolicy);
    // 在 network 之後宣告，所以比它先刪
    const std::unique_ptr<QNetworkReply> reply(network.get(request));
    QEventLoop loop;
    bool tooLarge = false;
    bool aborted = false;
    QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(
        reply.get(), &QNetworkReply::downloadProgress, &loop, [&](qint64 received, qint64 total) {
            if (received > m_config.maxResponseBytes || total > m_config.maxResponseBytes) {
                tooLarge = true;
                reply->abort();
            }
        });
    QElapsedTimer elapsed;
    elapsed.start();
    QTimer poll;
    poll.setInterval(kPollMs);
    // 換歌、設定關閉、逾時都直接 abort（AC-16-2、模組規格「沒涵蓋的情況」第 3 條）
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
        if (cancelled() || !active() || elapsed.elapsed() >= m_config.timeoutMs) {
            aborted = true;
            reply->abort();
        }
    });
    poll.start();
    if (!reply->isFinished()) {
        loop.exec();
    }
    poll.stop();
    out.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    // 收到完整的 HTTP 回應才算數（404、5xx 由呼叫端看狀態碼）；連不上、逾時、中途斷線、
    // 被我們中斷（QNetworkReply 的錯誤碼 200 以下）都當成暫時取不到
    const QNetworkReply::NetworkError error = reply->error();
    out.ok = !tooLarge && !aborted && out.status != 0 &&
             (error == QNetworkReply::NoError || error >= QNetworkReply::ContentAccessDenied);
    if (out.ok && (out.status == 429 || out.status == 503)) {
        // LRCLIB 文件：429 與 503 都要照 Retry-After 等，忽略可能被暫時封鎖（T-99）。
        // 呼叫端看狀態碼當成暫時取不到；不自動重試
        const qint64 waitMs =
            lrclibRetryAfterMs(reply->rawHeader("Retry-After"), QDateTime::currentDateTimeUtc());
        const qint64 until = steadyNowMs() + waitMs;
        qint64 current = m_blockedUntilMs.load();
        // 同時有別的查詢也被限流時取較晚的
        while (current < until && !m_blockedUntilMs.compare_exchange_weak(current, until)) {
        }
        qCWarning(lcLrclib) << "LRCLIB 限流" << out.status << url.path() << "等" << waitMs
                            << "ms 再查";
    }
    if (out.ok) {
        out.body = reply->readAll();
    } else {
        qCInfo(lcLrclib) << "LRCLIB 暫時取不到" << url.path() << reply->errorString()
                         << (tooLarge ? "回應太大" : "") << elapsed.elapsed() << "ms";
    }
    return out;
}

} // namespace auralis::mod::lyr

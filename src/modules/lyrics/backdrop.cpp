#include "backdrop.h"

#include <auralis/cover_service.h>
#include <auralis/job_runner.h>
#include <auralis/library_query.h>

#include <QLoggingCategory>
#include <QMetaObject>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace auralis::mod::lyr {

namespace {
Q_LOGGING_CATEGORY(lcBackdrop, "auralis.lyr.backdrop")

/// 沒有封面時的底色（DESIGN §2.3，同 Void 的 bg）。
constexpr quint32 kNoCover = 0x050505;
constexpr double kMaxLightness = 0.10;
constexpr double kMaxSaturation = 0.40;

QString toHex(quint32 rgb) {
    return QStringLiteral("#%1").arg(rgb & 0xffffff, 6, 16, QLatin1Char('0'));
}

int toByte(double value) {
    return std::clamp(static_cast<int>(std::lround(value * 255.0)), 0, 255);
}
} // namespace

QString backdropColor(std::optional<quint32> rgb) {
    if (!rgb) {
        return toHex(kNoCover);
    }
    const double r = ((*rgb >> 16) & 0xff) / 255.0;
    const double g = ((*rgb >> 8) & 0xff) / 255.0;
    const double b = (*rgb & 0xff) / 255.0;
    const double hi = std::max({r, g, b});
    const double lo = std::min({r, g, b});
    const double d = hi - lo;
    // RGB → HSL
    double lightness = (hi + lo) / 2;
    double saturation = d == 0 ? 0 : d / (1 - std::abs(2 * lightness - 1));
    double hue = 0; // 0–6（每 60 度一段）
    if (d > 0) {
        if (hi == r) {
            hue = std::fmod((g - b) / d + 6, 6);
        } else if (hi == g) {
            hue = (b - r) / d + 2;
        } else {
            hue = (r - g) / d + 4;
        }
    }
    lightness = std::min(lightness, kMaxLightness);
    saturation = std::min(saturation, kMaxSaturation);
    // HSL → RGB
    const double chroma = (1 - std::abs(2 * lightness - 1)) * saturation;
    const double x = chroma * (1 - std::abs(std::fmod(hue, 2) - 1));
    const double m = lightness - chroma / 2;
    double rr = 0;
    double gg = 0;
    double bb = 0;
    switch (static_cast<int>(hue)) {
    case 0:
        rr = chroma, gg = x;
        break;
    case 1:
        rr = x, gg = chroma;
        break;
    case 2:
        gg = chroma, bb = x;
        break;
    case 3:
        gg = x, bb = chroma;
        break;
    case 4:
        rr = x, bb = chroma;
        break;
    default:
        rr = chroma, bb = x;
        break;
    }
    const quint32 out = (static_cast<quint32>(toByte(rr + m)) << 16) |
                        (static_cast<quint32>(toByte(gg + m)) << 8) |
                        static_cast<quint32>(toByte(bb + m));
    return toHex(out);
}

/// 工作執行緒送回結果的信箱：載入器刪掉時 receiver 設成 nullptr，之後的結果就不送
/// （同 LyricsLoader::Mailbox）。
struct BackdropLoader::Mailbox {
    std::mutex mutex;
    BackdropLoader* receiver = nullptr;
};

BackdropLoader::BackdropLoader(JobRunner& jobs, const LibraryQuery& library, CoverService& covers,
                               QObject* parent)
    : QObject(parent), m_jobs(jobs), m_library(library), m_covers(covers),
      m_mailbox(std::make_shared<Mailbox>()), m_gate(std::make_shared<JobGate>()),
      m_color(backdropColor(std::nullopt)) {
    m_mailbox->receiver = this;
    connect(&m_jobs, &JobRunner::finished, this,
            [this](quint64 id, const QString&, JobRunner::Outcome outcome, const QString& error) {
                if (id != m_job || outcome != JobRunner::Outcome::Failed) {
                    return; // 成功的結果由 deliver 帶回；被取消的是舊的工作
                }
                qCWarning(lcBackdrop) << "計算封面主色失敗" << error;
                m_job = 0;
                setColor(std::nullopt);
            });
}

BackdropLoader::~BackdropLoader() {
    {
        std::lock_guard lock(m_mailbox->mutex);
        m_mailbox->receiver = nullptr;
    }
    if (m_job != 0) {
        m_jobs.cancel(m_job);
    }
}

void BackdropLoader::load(const TrackRef& track) {
    if (m_job != 0) {
        m_jobs.cancel(m_job);
    }
    const quint64 generation = ++m_generation;
    std::shared_ptr<Mailbox> mailbox = m_mailbox;
    std::shared_ptr<JobGate> gate = m_gate;
    const LibraryQuery& library = m_library;
    CoverService& covers = m_covers;
    // 工作名稱顯示在狀態列（T-93）；通常不到 300 ms，不會出現
    m_job = m_jobs.submit(tr("計算封面主色"), [=, &library, &covers](JobContext& context) {
        // 模組停止後才開始的工作不碰音樂庫與封面服務（它們可能已經解構，P-LYR-10）
        const JobGate::Pass pass(*gate);
        if (!pass) {
            return;
        }
        std::optional<quint32> rgb;
        const auto info = library.findByIdentity(track.path, track.subsong);
        if (!info.hasValue()) {
            qCWarning(lcBackdrop) << "查曲目失敗" << track.path << info.error().message;
        } else if (const auto& found = info.value(); found && !found->coverKey.isEmpty()) {
            if (context.isCancelled()) {
                return;
            }
            rgb = covers.dominantColor(found->coverKey);
        }
        if (context.isCancelled()) {
            return; // 換歌了
        }
        std::lock_guard lock(mailbox->mutex);
        if (BackdropLoader* receiver = mailbox->receiver) {
            QMetaObject::invokeMethod(
                receiver, [receiver, generation, rgb] { receiver->deliver(generation, rgb); },
                Qt::QueuedConnection);
        }
    });
}

void BackdropLoader::clear() {
    if (m_job != 0) {
        m_jobs.cancel(m_job);
        m_job = 0;
    }
    ++m_generation;
    setColor(std::nullopt);
}

void BackdropLoader::shutdown(int timeoutMs) noexcept {
    {
        std::lock_guard lock(m_mailbox->mutex);
        m_mailbox->receiver = nullptr;
    }
    // 取色本身不能中途停（通常不到 300 ms），取消只讓它算完後不再送結果
    if (m_job != 0) {
        m_jobs.cancel(m_job);
    }
    if (!m_gate->close(timeoutMs)) {
        qCWarning(lcBackdrop) << "停止時計算封面主色的工作沒有在" << timeoutMs << "ms 內結束";
    }
}

void BackdropLoader::deliver(quint64 generation, std::optional<quint32> rgb) {
    if (generation != m_generation) {
        return; // 換歌前的結果
    }
    m_job = 0;
    // 壓暗在 UI 執行緒做（AC-17-2；只是幾個浮點運算）
    setColor(rgb);
}

void BackdropLoader::setColor(std::optional<quint32> rgb) {
    const QString color = backdropColor(rgb);
    // 壓暗後同色的兩張封面，淺色版可能不同，所以原值變了也通知
    const QString cover = rgb ? toHex(*rgb) : QString();
    if (color != m_color || cover != m_coverColor) {
        m_color = color;
        m_coverColor = cover;
        emit changed();
    }
}

} // namespace auralis::mod::lyr

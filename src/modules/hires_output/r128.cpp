#include "r128.h"

#include <auralis/library_query.h>
#include <auralis/module_database.h>

#include <ebur128.h>

#include <QDateTime>
#include <QHash>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace auralis::mod::out {

namespace {

/// 一次解碼的樣本框數（約 0.1 秒）。
constexpr qint64 kChunkFrames = 4096;

struct StateDeleter {
    void operator()(ebur128_state* state) const noexcept { ebur128_destroy(&state); }
};
using StatePtr = std::unique_ptr<ebur128_state, StateDeleter>;

/// 一首量完的結果：狀態留著給專輯合併。
struct Measured {
    StatePtr state;
    double loudness = 0.0;
    double peak = 0.0;
};

QString identityKey(const TrackRef& ref) {
    return pathKey(ref.path) + QLatin1Char('#') + QString::number(ref.subsong);
}

R128Item itemFrom(const TrackInfo& info) {
    return {info.ref, info.albumId};
}

/// 交給 libebur128：16 位元容器是 short，32 位元整數左對齊（滿刻度就是 int 的範圍），浮點是 float。
int addFrames(ebur128_state* state, const AudioFormat& format, const std::byte* data,
              qint64 frames) {
    const auto count = static_cast<std::size_t>(frames);
    if (format.isFloat) {
        return ebur128_add_frames_float(state, reinterpret_cast<const float*>(data), count);
    }
    if (format.container == 16) {
        return ebur128_add_frames_short(state, reinterpret_cast<const short*>(data), count);
    }
    return ebur128_add_frames_int(state, reinterpret_cast<const int*>(data), count);
}

/// 量一首（CUE 曲目的音源已由核心切好範圍，從頭讀到結尾就是這一軌）；取消時回傳空的 state、
/// error 也是空的。
Measured measure(IAudioSource& source, QString& error, const std::function<bool()>& cancelled,
                 const std::function<void(double)>& progress) {
    const AudioFormat format = source.format();
    if (format.dsdRate > 0 || !format.isValid() || format.rate == 0) {
        error = QStringLiteral("不是 PCM 音源");
        return {};
    }
    StatePtr state(
        ebur128_init(format.channels, format.rate,
                     EBUR128_MODE_I | EBUR128_MODE_SAMPLE_PEAK | EBUR128_MODE_HISTOGRAM));
    if (!state) {
        error = QStringLiteral("無法建立響度量測（%1 聲道、%2 Hz）")
                    .arg(format.channels)
                    .arg(format.rate);
        return {};
    }
    const qint64 total = source.lengthFrames();
    std::vector<std::byte> buffer(static_cast<std::size_t>(kChunkFrames * format.bytesPerFrame()));
    qint64 done = 0;
    for (;;) {
        if (cancelled()) {
            return {};
        }
        auto read = source.read(buffer.data(), kChunkFrames);
        if (!read) {
            error = read.error().message;
            return {};
        }
        if (read.value() == 0) {
            break;
        }
        if (addFrames(state.get(), format, buffer.data(), read.value()) != EBUR128_SUCCESS) {
            error = QStringLiteral("響度量測失敗");
            return {};
        }
        done += read.value();
        if (total > 0) {
            progress(std::min(1.0, static_cast<double>(done) / static_cast<double>(total)));
        }
    }
    Measured result;
    if (ebur128_loudness_global(state.get(), &result.loudness) != EBUR128_SUCCESS ||
        !std::isfinite(result.loudness)) {
        // 全是靜音（或太短）時整合響度是負無限大：沒有可用的增益
        error = QStringLiteral("量不到響度（靜音或太短）");
        return {};
    }
    for (unsigned int channel = 0; channel < format.channels; ++channel) {
        double peak = 0.0;
        if (ebur128_sample_peak(state.get(), channel, &peak) == EBUR128_SUCCESS) {
            result.peak = std::max(result.peak, peak);
        }
    }
    result.state = std::move(state);
    return result;
}

/// 資料庫錯誤：工作的錯誤邊界接住，只標記這個工作失敗。
void requireOk(const Result<bool>& result) {
    if (!result) {
        throw std::runtime_error(result.error().message.toStdString());
    }
}

/// 寫這首的音軌增益與峰值；專輯值不動（之前算過的整張專輯值仍然成立，同一份音訊）。
void writeTrack(ModuleDatabase& database, const TrackRef& ref, double gain, double peak) {
    const QString key = pathKey(ref.path);
    const auto subsong = static_cast<qint64>(ref.subsong);
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    requireOk(database.transaction([&] {
        auto updated = database.execute(
            QStringLiteral("UPDATE rg_computed SET track_gain = ?, track_peak = ?, "
                           "algorithm = 'ebur128', computed_at = ? WHERE path_key = ? AND "
                           "subsong = ?"),
            {gain, peak, now, key, subsong});
        if (!updated) {
            throw std::runtime_error(updated.error().message.toStdString());
        }
        if (updated.value() > 0) {
            return true;
        }
        auto inserted = database.execute(
            QStringLiteral("INSERT INTO rg_computed (path_key, subsong, track_gain, track_peak, "
                           "album_gain, album_peak, algorithm, computed_at) "
                           "VALUES (?, ?, ?, ?, NULL, NULL, 'ebur128', ?)"),
            {key, subsong, gain, peak, now});
        if (!inserted) {
            throw std::runtime_error(inserted.error().message.toStdString());
        }
        return true;
    }));
}

/// 整張專輯一次寫：全部成功或全部不寫。
void writeAlbum(ModuleDatabase& database, const QList<TrackRef>& tracks, double gain, double peak) {
    requireOk(database.transaction([&] {
        for (const TrackRef& ref : tracks) {
            auto updated = database.execute(
                QStringLiteral("UPDATE rg_computed SET album_gain = ?, album_peak = ? "
                               "WHERE path_key = ? AND subsong = ?"),
                {gain, peak, pathKey(ref.path), static_cast<qint64>(ref.subsong)});
            if (!updated) {
                throw std::runtime_error(updated.error().message.toStdString());
            }
        }
        return true;
    }));
}

} // namespace

Result<R128Plan> planR128(const LibraryQuery& library, const QList<TrackRef>& tracks) {
    R128Plan plan;
    QSet<QString> seen;
    // 專輯依第一次出現的順序，同一張的曲目排在一起
    QList<qint64> albumOrder;
    QHash<qint64, QList<R128Item>> byAlbum;
    for (const TrackRef& ref : tracks) {
        if (seen.contains(identityKey(ref))) {
            continue;
        }
        seen.insert(identityKey(ref));
        auto found = library.findByIdentity(ref.path, ref.subsong);
        if (!found) {
            return found.error();
        }
        if (!found.value()) {
            continue;
        }
        const TrackInfo& info = *found.value();
        if (info.dsdRate > 0) {
            ++plan.skippedDsd;
            continue;
        }
        if (!byAlbum.contains(info.albumId)) {
            albumOrder.append(info.albumId);
        }
        byAlbum[info.albumId].append(itemFrom(info));
    }
    for (const qint64 albumId : albumOrder) {
        const QList<R128Item>& selected = byAlbum.value(albumId);
        plan.items.append(selected);
        if (albumId == 0) {
            continue;
        }
        TrackFilter filter;
        filter.albumId = albumId;
        auto all = library.tracks(filter);
        if (!all) {
            return all.error();
        }
        const auto pcm = std::count_if(all.value().cbegin(), all.value().cend(),
                                       [](const TrackInfo& info) { return info.dsdRate == 0; });
        if (pcm == selected.size()) {
            plan.completeAlbums.insert(albumId);
        }
    }
    return plan;
}

Result<R128Plan> planLibraryR128(const LibraryQuery& library) {
    auto all = library.tracks(TrackFilter{});
    if (!all) {
        return all.error();
    }
    R128Plan plan;
    QList<qint64> albumOrder;
    QHash<qint64, QList<R128Item>> byAlbum;
    for (const TrackInfo& info : all.value()) {
        if (info.dsdRate > 0) {
            ++plan.skippedDsd;
            continue;
        }
        if (!byAlbum.contains(info.albumId)) {
            albumOrder.append(info.albumId);
        }
        byAlbum[info.albumId].append(itemFrom(info));
    }
    for (const qint64 albumId : albumOrder) {
        plan.items.append(byAlbum.value(albumId));
        if (albumId != 0) {
            plan.completeAlbums.insert(albumId);
        }
    }
    return plan;
}

R128Summary runR128(const R128Plan& plan, const R128Opener& open, ModuleDatabase& database,
                    const std::function<bool()>& cancelled,
                    const std::function<void(double)>& progress) {
    R128Summary summary;
    summary.skippedDsd = plan.skippedDsd;
    const qsizetype total = plan.items.size();
    qsizetype index = 0;
    while (index < total) {
        // 一張專輯（或連續的無專輯曲目）一組
        const qint64 albumId = plan.items.at(index).albumId;
        qsizetype groupEnd = index;
        while (groupEnd < total && plan.items.at(groupEnd).albumId == albumId) {
            ++groupEnd;
        }
        const bool wantAlbum = albumId != 0 && plan.completeAlbums.contains(albumId);
        std::vector<StatePtr> states;
        QList<TrackRef> albumTracks;
        double albumPeak = 0.0;
        bool albumOk = wantAlbum;
        for (; index < groupEnd; ++index) {
            if (cancelled()) {
                summary.cancelled = true;
                return summary;
            }
            const R128Item& item = plan.items.at(index);
            const auto trackProgress = [&](double fraction) {
                progress((static_cast<double>(index) + fraction) / static_cast<double>(total));
            };
            QString error;
            Measured measured;
            auto source = open(item.ref);
            if (!source) {
                error = source.error().message;
            } else {
                measured = measure(*source.value(), error, cancelled, trackProgress);
            }
            if (!measured.state) {
                if (error.isEmpty()) {
                    // 量到一半被取消：這首不寫
                    summary.cancelled = true;
                    return summary;
                }
                ++summary.failed;
                summary.failures.append(QStringLiteral("%1：%2").arg(item.ref.path, error));
                albumOk = false;
                continue;
            }
            writeTrack(database, item.ref, kR128ReferenceLufs - measured.loudness, measured.peak);
            ++summary.computed;
            progress(static_cast<double>(index + 1) / static_cast<double>(total));
            if (wantAlbum) {
                albumTracks.append(item.ref);
                albumPeak = std::max(albumPeak, measured.peak);
                states.push_back(std::move(measured.state));
            }
        }
        if (!albumOk || states.empty()) {
            continue;
        }
        std::vector<ebur128_state*> raw;
        raw.reserve(states.size());
        for (const StatePtr& state : states) {
            raw.push_back(state.get());
        }
        double loudness = 0.0;
        if (ebur128_loudness_global_multiple(raw.data(), raw.size(), &loudness) ==
                EBUR128_SUCCESS &&
            std::isfinite(loudness)) {
            writeAlbum(database, albumTracks, kR128ReferenceLufs - loudness, albumPeak);
            ++summary.albums;
        }
    }
    return summary;
}

} // namespace auralis::mod::out

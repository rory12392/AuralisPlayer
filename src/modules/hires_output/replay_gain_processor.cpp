#include "replay_gain_processor.h"

#include <auralis/library_query.h>
#include <auralis/module_database.h>

#include <QCoreApplication>

#include <utility>

namespace auralis::mod::out {

namespace {

std::optional<double> optionalReal(const QVariant& value) {
    if (value.isNull() || !value.isValid()) {
        return std::nullopt;
    }
    bool ok = false;
    const double number = value.toDouble(&ok);
    return ok ? std::optional<double>(number) : std::nullopt;
}

int modeSlot(RgMode mode) {
    return mode == RgMode::Album ? 1 : 0;
}

} // namespace

RgLookup libraryLookup(ModuleDatabase& database, LibraryQuery& library) {
    return [&database, &library](const TrackRef& track) {
        RgValues computed;
        const auto rows = database.query(
            QStringLiteral("SELECT track_gain, track_peak, album_gain, album_peak FROM rg_computed "
                           "WHERE path_key = ? AND subsong = ?"),
            {pathKey(track.path), static_cast<qint64>(track.subsong)});
        if (rows && !rows.value().isEmpty() && rows.value().front().size() == 4) {
            const ModuleDatabase::Row& row = rows.value().front();
            computed = {optionalReal(row[0]), optionalReal(row[1]), optionalReal(row[2]),
                        optionalReal(row[3])};
        }
        RgValues tags;
        const auto info = library.findByIdentity(track.path, track.subsong);
        if (info && info.value()) {
            const TrackInfo& found = *info.value();
            tags = {found.rgTrackGain, found.rgTrackPeak, found.rgAlbumGain, found.rgAlbumPeak};
        }
        return chooseGain(computed, tags);
    };
}

ReplayGainProcessor::ReplayGainProcessor(std::shared_ptr<const RgSettings> settings,
                                         RgLookup lookup, std::shared_ptr<RgAppliedLog> log)
    : m_settings(std::move(settings)), m_lookup(std::move(lookup)), m_log(std::move(log)) {}

void ReplayGainProcessor::prepare(const AudioFormat&) {}

void ReplayGainProcessor::prepareTrack(const AudioFormat& format, const TrackRef& track) {
    prepare(format);
    const RgTrackGain gain = m_lookup ? m_lookup(track) : RgTrackGain{};
    for (const RgMode mode : {RgMode::Track, RgMode::Album}) {
        const RgChoice& choice = gain.forMode(mode);
        const int slot = modeSlot(mode);
        m_has[slot] = choice.gainDb.has_value();
        m_factor[slot][0] = dbToLinear(appliedGainDb(choice, false));
        m_factor[slot][1] = dbToLinear(appliedGainDb(choice, true));
    }
    if (m_log) {
        m_log->record(track, gain);
    }
}

bool ReplayGainProcessor::isActive() const noexcept {
    const RgMode mode = m_settings->mode.load(std::memory_order_relaxed);
    return mode != RgMode::Off && m_has[modeSlot(mode)];
}

QString ReplayGainProcessor::reason() const {
    return QCoreApplication::translate("ReplayGain", "ReplayGain 已套用");
}

double ReplayGainProcessor::currentFactor() const noexcept {
    const RgMode mode = m_settings->mode.load(std::memory_order_relaxed);
    if (mode == RgMode::Off || !m_has[modeSlot(mode)]) {
        return 1.0;
    }
    const bool clip = m_settings->preventClipping.load(std::memory_order_relaxed);
    return m_factor[modeSlot(mode)][clip ? 1 : 0];
}

void ReplayGainProcessor::process(double* samples, qint64 frames, int channels) noexcept {
    const double factor = currentFactor();
    if (factor == 1.0) {
        return;
    }
    const qint64 count = frames * channels;
    for (qint64 i = 0; i < count; ++i) {
        samples[i] *= factor;
    }
}

ReplayGainProcessorFactory::ReplayGainProcessorFactory(std::shared_ptr<const RgSettings> settings,
                                                       RgLookup lookup,
                                                       std::shared_ptr<RgAppliedLog> log)
    : m_settings(std::move(settings)), m_lookup(std::move(lookup)), m_log(std::move(log)) {}

QString ReplayGainProcessorFactory::name() const {
    return QStringLiteral("ReplayGain");
}

std::unique_ptr<IProcessor> ReplayGainProcessorFactory::create() {
    return std::make_unique<ReplayGainProcessor>(m_settings, m_lookup, m_log);
}

} // namespace auralis::mod::out

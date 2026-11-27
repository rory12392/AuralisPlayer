#include "gui_hooks.h"

#include "modules/core_services.h"
#include "play_entry.h"
#include "settings/output_settings.h"

#include <QFileInfo>

namespace auralis::app {

std::optional<ui::QueueTrackMeta> lookupTrack(const core::LibraryQueryImpl& library,
                                              const TrackRef& track) {
    const auto found = library.findByIdentity(track.path, track.subsong);
    if (!found.hasValue() || !found.value()) {
        return cueTrackMeta(track);
    }
    const TrackInfo& info = *found.value();
    return ui::QueueTrackMeta{info.title, info.artist, info.durationMs, info.coverKey};
}

std::optional<ui::NowPlayingInfo> lookupNowPlaying(const core::LibraryQueryImpl& library,
                                                   const TrackRef& track) {
    const auto found = library.findByIdentity(track.path, track.subsong);
    if (!found.hasValue() || !found.value()) {
        return std::nullopt;
    }
    const TrackInfo& info = *found.value();
    int trackTotal = 0;
    if (info.albumId > 0) {
        TrackFilter filter;
        filter.albumId = info.albumId;
        const auto albumTracks = library.tracks(filter);
        trackTotal = albumTracks.hasValue() ? static_cast<int>(albumTracks.value().size()) : 0;
    }
    const qint64 fileBytes = info.startCdFrame >= 0 ? -1 : QFileInfo(track.path).size();
    return ui::nowPlayingFrom(info, trackTotal, fileBytes);
}

ui::OutputHooks outputHooks(core::CoreServices& services, const QString& settingsPath) {
    ui::OutputHooks hooks;
    hooks.list = [&outputs = services.outputs] {
        QList<ui::OutputOption> options;
        for (const auto& factory : outputs.factories()) {
            const QString note = factory->note();
            for (const OutputDevice& device : factory->devices()) {
                options.append({factory->name(), device.id, device.name, note});
            }
        }
        return options;
    };
    hooks.select = [&playback = services.playback, settingsPath](const ui::OutputOption& option) {
        return core::selectOutput(playback, settingsPath,
                                  {option.backend, option.deviceId, option.deviceName});
    };
    hooks.current = [&playback = services.playback] {
        const auto choice = playback.outputChoice();
        return ui::OutputOption{choice.backend, choice.deviceId, choice.deviceName, {}};
    };
    return hooks;
}

} // namespace auralis::app

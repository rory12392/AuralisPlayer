#include "smtc_refresh.h"

#include "smtc_track.h"

#include <QLoggingCategory>

namespace auralis::mod::smtc {

Q_DECLARE_LOGGING_CATEGORY(lcSmtc)

TrackRefresher::TrackRefresher(LibraryQuery& library, CoverService& covers, TrackSink& sink,
                               Submit submit, Post post)
    : m_library(library), m_covers(covers), m_submit(std::move(submit)), m_post(std::move(post)) {
    m_state->sink = &sink;
}

TrackRefresher::~TrackRefresher() {
    invalidate();
}

void TrackRefresher::refresh(const std::optional<TrackRef>& current) {
    State& state = *m_state;
    if (state.sink == nullptr) {
        return;
    }
    if (!current) {
        if (state.shown) {
            state.shown.reset();
            ++state.version;
            state.sink->clearTrack();
        }
        return;
    }
    if (state.shown && sameTrack(*state.shown, *current)) {
        return;
    }
    state.shown = *current;
    const quint64 version = ++state.version;
    const TrackRef track = *current;
    LibraryQuery& library = m_library;
    CoverService& covers = m_covers;
    auto deliver = [post = m_post, shared = m_state, version](auto apply) {
        post([shared, version, apply = std::move(apply)] {
            // 模組停了、換到別首、佇列清空或同一首已重查：版本不同，丟掉
            if (shared->version == version) {
                apply(*shared->sink);
            }
        });
    };
    m_submit([=, &library, &covers, gate = m_gate](const std::function<bool()>& isCancelled) {
        // 模組停止後才開始的工作不碰音樂庫與封面服務（它們可能已經解構）
        const JobGate::Pass pass(*gate);
        if (!pass) {
            return;
        }
        const SmtcTrackDisplay display = lookupDisplay(library, track);
        deliver([display](TrackSink& sink) {
            sink.setTrack(display.title, display.artist, display.album, appIconPng());
        });
        if (display.coverKey.isEmpty() || isCancelled()) {
            return;
        }
        QByteArray png = readThumbnail(covers, display.coverKey);
        if (!png.isEmpty()) {
            deliver([png = std::move(png)](TrackSink& sink) { sink.setThumbnail(png); });
        }
    });
}

void TrackRefresher::requery(const std::optional<TrackRef>& current) {
    m_state->shown.reset();
    refresh(current);
}

void TrackRefresher::shutdown(int timeoutMs) noexcept {
    invalidate();
    // 查詢不能中途停（查一首曲目、輸出一張縮圖），已經開始的等它；結果因為版本不同會丟掉
    if (!m_gate->close(timeoutMs)) {
        qCWarning(lcSmtc) << "停止時查詢曲目資訊的工作沒有在" << timeoutMs << "ms 內結束";
    }
}

void TrackRefresher::invalidate() {
    m_state->shown.reset();
    ++m_state->version;
    m_state->sink = nullptr;
}

} // namespace auralis::mod::smtc

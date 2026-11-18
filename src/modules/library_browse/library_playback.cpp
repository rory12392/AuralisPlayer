#include "library_playback.h"

#include "search_controller.h"

#include <auralis/playback_service.h>

#include <QJSEngine>

namespace auralis::mod::lib {

namespace {
LibraryPlayback* g_instance = nullptr;
} // namespace

LibraryPlayback::LibraryPlayback(PlaybackService& playback, SearchController& search,
                                 QObject* parent)
    : QObject(parent), m_playback(playback), m_search(search) {
    connect(&m_playback, &PlaybackService::trackChanged, this, &LibraryPlayback::updateCurrent);
    updateCurrent();
}

bool LibraryPlayback::playFrom(int row) {
    const SearchResultsModel& results = *m_search.results();
    const int count = results.rowCount();
    if (row < 0 || row >= count) {
        return false;
    }
    QList<TrackRef> tracks;
    tracks.reserve(count);
    for (int i = 0; i < count; ++i) {
        tracks.append(results.track(i).ref);
    }
    m_playback.playFrom(tracks, row);
    return true;
}

QString LibraryPlayback::keyOf(const QString& path, int subsong) {
    return path + QLatin1Char('\n') + QString::number(subsong);
}

void LibraryPlayback::updateCurrent() {
    const auto entry = m_playback.currentEntry();
    const QString key =
        entry ? keyOf(entry->track.path, static_cast<int>(entry->track.subsong)) : QString();
    if (key != m_currentKey) {
        m_currentKey = key;
        emit currentKeyChanged();
    }
}

void LibraryPlayback::setInstance(LibraryPlayback* instance) {
    g_instance = instance;
}

LibraryPlayback* LibraryPlayback::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "LibraryPlayback", "模組要在載入頁面前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::mod::lib

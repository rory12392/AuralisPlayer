#include "search_controller.h"

#include <auralis/library_query.h>

#include <QElapsedTimer>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QVariantMap>

#include <exception>

Q_LOGGING_CATEGORY(lcLib, "auralis.lib")

namespace auralis::mod::lib {

namespace {

SearchController* g_instance = nullptr;

// 同一條工作執行緒上的先後：新查詢的第一批最先，其餘批次其次，篩選列的選項最後
constexpr int kPriorityFirst = 2;
constexpr int kPriorityRest = 1;
constexpr int kPriorityOptions = 0;

/// 查一批；LibraryQuery 丟出的例外也轉成錯誤訊息（工作執行緒的錯誤邊界）。
Result<QList<TrackInfo>> queryTracks(const LibraryQuery& library, const TrackFilter& filter) {
    try {
        return library.tracks(filter);
    } catch (const std::exception& e) {
        return Error{QString::fromUtf8(e.what()), 0, QStringLiteral("LibraryQuery::tracks")};
    }
}

} // namespace

SearchController::SearchController(LibraryQuery& library, QObject* parent)
    : QObject(parent), m_library(library) {
    m_pool.setMaxThreadCount(1);
    m_debounce.setSingleShot(true);
    // 預設的粗略計時器在 Windows 可能晚到十幾毫秒，200 ms 的門檻裡去抖已佔 120 ms
    m_debounce.setTimerType(Qt::PreciseTimer);
    m_debounce.setInterval(kDebounceMs);
    connect(&m_debounce, &QTimer::timeout, this, &SearchController::startQuery);
    // 掃描完成後音樂庫變了：已經查過的條件重查、已經讀過的選項重讀
    connect(&m_library, &LibraryQuery::scanFinished, this, [this] {
        if (m_queried) {
            startQuery();
        }
        if (m_optionsRequested) {
            loadOptions();
        }
    });
}

SearchController::~SearchController() {
    shutdown();
}

void SearchController::shutdown() {
    m_debounce.stop();
    // 讓還在排隊或正在查的批次一回來就結束，也不再排下一批
    ++*m_generation;
    ++*m_optionsGeneration;
    m_pool.clear();
    m_pool.waitForDone();
}

void SearchController::setText(const QString& text) {
    if (m_criteria.text == text) {
        return;
    }
    m_criteria.text = text;
    emit criteriaChanged();
    ++*m_generation; // 舊字串還在查的結果不再顯示（去抖期間也一樣）
    setBusy(true);
    m_debounce.start(); // 每個按鍵都重新計時，停下 120 ms 才查
}

void SearchController::setArtist(const QString& artist) {
    if (m_criteria.artist != artist) {
        m_criteria.artist = artist;
        filterChanged();
    }
}

void SearchController::setAlbum(const QString& album) {
    if (m_criteria.album != album) {
        m_criteria.album = album;
        filterChanged();
    }
}

void SearchController::setYear(int year) {
    if (m_criteria.year != year) {
        m_criteria.year = year;
        filterChanged();
    }
}

void SearchController::setFormat(const QString& format) {
    if (m_criteria.format != format) {
        m_criteria.format = format;
        filterChanged();
    }
}

void SearchController::setSampleRate(int rate) {
    if (m_criteria.sampleRate != rate) {
        m_criteria.sampleRate = rate;
        filterChanged();
    }
}

bool SearchController::filtersActive() const {
    return !m_criteria.artist.isEmpty() || !m_criteria.album.isEmpty() || m_criteria.year != 0 ||
           !m_criteria.format.isEmpty() || m_criteria.sampleRate != 0;
}

QString SearchController::sort() const {
    switch (m_sort) {
    case TrackSort::Bitrate:
        return QStringLiteral("bitrate");
    case TrackSort::AddedAt:
        return QStringLiteral("addedAt");
    case TrackSort::Default:
        break;
    }
    return {};
}

void SearchController::sortBy(const QString& column, bool descending) {
    TrackSort sort = TrackSort::Default;
    if (column == QLatin1String("bitrate")) {
        sort = TrackSort::Bitrate;
    } else if (column == QLatin1String("addedAt")) {
        sort = TrackSort::AddedAt;
    }
    if (sort == TrackSort::Default) {
        descending = false;
    }
    if (sort == m_sort && descending == m_descending) {
        return;
    }
    m_sort = sort;
    m_descending = descending;
    emit sortChanged();
    startQuery();
}

QVariantList SearchController::yearOptions() const {
    QVariantList list;
    for (int year : m_options.years) {
        list.append(year);
    }
    return list;
}

QVariantList SearchController::sampleRateOptions() const {
    QVariantList list;
    for (int rate : m_options.sampleRates) {
        list.append(QVariantMap{{QStringLiteral("value"), rate},
                                {QStringLiteral("text"), sampleRateText(rate)}});
    }
    return list;
}

void SearchController::refresh() {
    startQuery();
}

void SearchController::clear() {
    if (m_criteria == SearchCriteria{}) {
        startQuery();
        return;
    }
    m_criteria = {};
    emit criteriaChanged();
    startQuery();
}

void SearchController::clearFilters() {
    SearchCriteria cleared;
    cleared.text = m_criteria.text;
    if (m_criteria == cleared) {
        startQuery();
        return;
    }
    m_criteria = cleared;
    emit criteriaChanged();
    startQuery();
}

void SearchController::filterChanged() {
    emit criteriaChanged();
    startQuery();
}

void SearchController::startQuery() {
    m_debounce.stop();
    m_queried = true;
    const quint64 generation = ++*m_generation;
    setBusy(true);
    auto filter = toTrackFilter(m_criteria);
    if (!filter) {
        // 條件互相矛盾，一定是 0 首；結果照樣在之後交回，和有查詢時的順序一致
        QMetaObject::invokeMethod(
            this, [this, generation] { onPage(generation, true, {}, 0, true, {}); },
            Qt::QueuedConnection);
        return;
    }
    // 排序由核心做（沒有值的排最後）；分批載入時每批用同一個排序
    filter->sort = m_sort;
    filter->descending = m_descending;
    schedulePage(generation, *filter, 0, kFirstPage, kPriorityFirst);
}

void SearchController::schedulePage(quint64 generation, const TrackFilter& filter, int offset,
                                    int limit, int priority) {
    m_pool.start(
        [this, generation, filter, offset, limit] {
            if (generation != *m_generation) {
                return; // 已有更新的查詢
            }
            TrackFilter page = filter;
            page.offset = offset;
            page.limit = limit;
            QElapsedTimer timer;
            timer.start();
            auto rows = queryTracks(m_library, page);
            const int elapsed = static_cast<int>(timer.elapsed());
            if (generation != *m_generation) {
                return;
            }
            const bool more = rows && rows.value().size() == limit;
            QList<TrackInfo> tracks = rows ? std::move(rows).value() : QList<TrackInfo>{};
            const QString error = rows ? QString() : rows.error().message;
            QMetaObject::invokeMethod(
                this,
                [this, generation, offset, tracks = std::move(tracks), elapsed, more,
                 error]() mutable {
                    onPage(generation, offset == 0, std::move(tracks), elapsed, !more, error);
                },
                Qt::QueuedConnection);
            if (more) {
                schedulePage(generation, filter, offset + limit, kChunk, kPriorityRest);
            }
        },
        priority);
}

void SearchController::onPage(quint64 generation, bool first, QList<TrackInfo> tracks,
                              int elapsedMs, bool done, const QString& error) {
    if (generation != *m_generation) {
        return;
    }
    if (!error.isEmpty()) {
        qCWarning(lcLib) << "搜尋失敗：" << error;
    }
    if (first) {
        m_results.reset(error.isEmpty() ? std::move(tracks) : QList<TrackInfo>{});
        m_elapsedMs = elapsedMs;
        m_error = error;
    } else if (error.isEmpty()) {
        m_results.append(tracks);
    } else {
        m_error = error;
    }
    m_complete = done || !error.isEmpty();
    emit resultsChanged();
    if (first) {
        setBusy(m_debounce.isActive()); // 第一批回來時使用者可能又打了字
        emit resultsReady();
    }
}

void SearchController::loadOptions() {
    m_optionsRequested = true;
    const quint64 generation = ++*m_optionsGeneration;
    scheduleOptions(generation, std::make_shared<FilterOptionsBuilder>(), 0);
}

void SearchController::scheduleOptions(quint64 generation,
                                       std::shared_ptr<FilterOptionsBuilder> builder, int offset) {
    // 整個音樂庫分批讀，每批之間讓新的搜尋先查（優先順序較高）
    m_pool.start(
        [this, generation, builder, offset] {
            if (generation != *m_optionsGeneration) {
                return;
            }
            TrackFilter page;
            page.offset = offset;
            page.limit = kChunk;
            const auto rows = queryTracks(m_library, page);
            if (!rows) {
                qCWarning(lcLib) << "篩選選項讀取失敗：" << rows.error().message;
                // 交回 UI 執行緒讓篩選列顯示失敗並能重試；被新的讀取取代時不顯示
                QMetaObject::invokeMethod(
                    this,
                    [this, generation] {
                        if (generation != *m_optionsGeneration) {
                            return;
                        }
                        m_optionsError = tr("無法讀取篩選選項，詳細原因已寫進日誌。");
                        emit optionsChanged();
                    },
                    Qt::QueuedConnection);
                return;
            }
            builder->add(rows.value());
            if (rows.value().size() == kChunk) {
                scheduleOptions(generation, builder, offset + kChunk);
                return;
            }
            QMetaObject::invokeMethod(
                this,
                [this, generation, options = builder->finish()] {
                    if (generation != *m_optionsGeneration) {
                        return;
                    }
                    m_options = options;
                    m_optionsLoaded = true;
                    m_optionsError.clear();
                    emit optionsChanged();
                },
                Qt::QueuedConnection);
        },
        kPriorityOptions);
}

void SearchController::setBusy(bool busy) {
    if (m_busy != busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

void SearchController::setInstance(SearchController* instance) {
    g_instance = instance;
}

SearchController* SearchController::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "SearchController", "模組要在載入頁面前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::mod::lib

#pragma once

#include "search_criteria.h"
#include "search_results_model.h"

#include <QObject>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <memory>

class QQmlEngine;
class QJSEngine;

namespace auralis {
class LibraryQuery;
} // namespace auralis

namespace auralis::mod::lib {

/// 搜尋與篩選（US-09；S-07 的搜尋框與篩選列、S-03 的篩選列），QML 以單例 LibrarySearch 取用。
/// - 搜尋字串改變後去抖 120 ms 才查；篩選改變立刻查。條件都空白時查的是全部曲目。
/// - 在自己的工作執行緒（一條）查 LibraryQuery：先查第一批（kFirstPage 首）馬上顯示，其餘每批
///   kChunk 首接在後面；搜尋字串一改或新查詢一開始，舊查詢還沒開始的批次直接丟掉、
///   正在查的那一批查完也不顯示。
/// - elapsedMs 是第一批的查詢耗時（搜尋框右側的「12 ms」）。
/// - 篩選列的選項（整個音樂庫出現過的值）要先呼叫 loadOptions()，之後掃描完成時自動重讀。
/// 執行緒：只在 UI 執行緒使用；結果與選項由工作執行緒以 queued 呼叫交回。
// 由 QML 引擎取用的單例，型別註冊會繼承它，所以不能標 final
class SearchController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(LibrarySearch)
    QML_SINGLETON
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY criteriaChanged)
    Q_PROPERTY(QString artist READ artist WRITE setArtist NOTIFY criteriaChanged)
    Q_PROPERTY(QString album READ album WRITE setAlbum NOTIFY criteriaChanged)
    /// 0 表示不限。
    Q_PROPERTY(int year READ year WRITE setYear NOTIFY criteriaChanged)
    /// formatOptions 裡的值（FLAC、WAV、DSD64…）；空字串表示不限。
    Q_PROPERTY(QString format READ format WRITE setFormat NOTIFY criteriaChanged)
    /// Hz，0 表示不限。
    Q_PROPERTY(int sampleRate READ sampleRate WRITE setSampleRate NOTIFY criteriaChanged)
    /// 有任何一個篩選（不含搜尋字串）。
    Q_PROPERTY(bool filtersActive READ filtersActive NOTIFY criteriaChanged)
    Q_PROPERTY(auralis::mod::lib::SearchResultsModel* results READ results CONSTANT)
    /// 目前已載入的結果筆數；complete 為 true 時就是符合的總數。
    Q_PROPERTY(int count READ count NOTIFY resultsChanged)
    /// 結果都載入了（false 時後面的批次還在查）。
    Q_PROPERTY(bool complete READ complete NOTIFY resultsChanged)
    /// 條件改了、第一批還沒回來（含去抖的 120 ms）。
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    /// 最近一次查詢第一批的耗時（毫秒）；還沒查過是 -1。
    Q_PROPERTY(int elapsedMs READ elapsedMs NOTIFY resultsChanged)
    /// 查詢失敗的原因（給錯誤狀態顯示）；成功時空字串。
    Q_PROPERTY(QString error READ error NOTIFY resultsChanged)
    Q_PROPERTY(bool optionsLoaded READ optionsLoaded NOTIFY optionsChanged)
    /// 篩選選項讀取失敗時給使用者看的原因（篩選列顯示並可重試 loadOptions）；成功後清掉。
    Q_PROPERTY(QString optionsError READ optionsError NOTIFY optionsChanged)
    Q_PROPERTY(QStringList artistOptions READ artistOptions NOTIFY optionsChanged)
    Q_PROPERTY(QStringList albumOptions READ albumOptions NOTIFY optionsChanged)
    /// 年份（int），由新到舊。
    Q_PROPERTY(QVariantList yearOptions READ yearOptions NOTIFY optionsChanged)
    Q_PROPERTY(QStringList formatOptions READ formatOptions NOTIFY optionsChanged)
    /// [{ value: Hz, text: "44.1 kHz" }]，由低到高。
    Q_PROPERTY(QVariantList sampleRateOptions READ sampleRateOptions NOTIFY optionsChanged)

public:
    static constexpr int kDebounceMs = 120;
    static constexpr int kFirstPage = 200;
    static constexpr int kChunk = 2000;

    explicit SearchController(LibraryQuery& library, QObject* parent = nullptr);
    /// 等工作執行緒結束（同 shutdown()）。
    ~SearchController() override;

    QString text() const { return m_criteria.text; }
    QString artist() const { return m_criteria.artist; }
    QString album() const { return m_criteria.album; }
    int year() const { return m_criteria.year; }
    QString format() const { return m_criteria.format; }
    int sampleRate() const { return m_criteria.sampleRate; }
    void setText(const QString& text);
    void setArtist(const QString& artist);
    void setAlbum(const QString& album);
    void setYear(int year);
    void setFormat(const QString& format);
    void setSampleRate(int rate);
    bool filtersActive() const;

    SearchResultsModel* results() { return &m_results; }
    int count() const { return m_results.rowCount(); }
    bool complete() const { return m_complete; }
    bool busy() const { return m_busy; }
    int elapsedMs() const { return m_elapsedMs; }
    QString error() const { return m_error; }

    bool optionsLoaded() const { return m_optionsLoaded; }
    QString optionsError() const { return m_optionsError; }
    QStringList artistOptions() const { return m_options.artists; }
    QStringList albumOptions() const { return m_options.albums; }
    QVariantList yearOptions() const;
    QStringList formatOptions() const { return m_options.formats; }
    QVariantList sampleRateOptions() const;

    /// 用目前的條件重查（例：頁面第一次顯示「所有曲目」）。
    Q_INVOKABLE void refresh();
    /// 清除搜尋字串與所有篩選並重查（「清除搜尋與篩選」）。
    Q_INVOKABLE void clear();
    /// 只清除篩選並重查（「清除篩選」）。
    Q_INVOKABLE void clearFilters();
    /// 在背景讀篩選列的選項；完成時 optionsChanged。
    Q_INVOKABLE void loadOptions();

    /// 模組停止時：丟掉排隊中的查詢、等正在查的那一批結束。之後不再發出結果。
    void shutdown();

    static void setInstance(SearchController* instance);
    static SearchController* create(QQmlEngine*, QJSEngine*);

signals:
    void criteriaChanged();
    void resultsChanged();
    void busyChanged();
    void optionsChanged();
    /// 一次查詢的第一批結果已放進 results（效能量測從最後一個按鍵量到這裡）。
    void resultsReady();

private:
    /// 篩選改變：立刻查。
    void filterChanged();
    void startQuery();
    /// 工作執行緒呼叫也安全：只碰 m_pool、m_library 與原子變數。
    void schedulePage(quint64 generation, const TrackFilter& filter, int offset, int limit,
                      int priority);
    void scheduleOptions(quint64 generation, std::shared_ptr<FilterOptionsBuilder> builder,
                         int offset);
    void onPage(quint64 generation, bool first, QList<TrackInfo> tracks, int elapsedMs, bool done,
                const QString& error);
    void setBusy(bool busy);

    LibraryQuery& m_library;
    SearchResultsModel m_results;
    SearchCriteria m_criteria;
    QTimer m_debounce;
    QThreadPool m_pool;
    /// 最新一次查詢的編號；工作執行緒比對它來丟掉舊查詢。
    std::shared_ptr<std::atomic<quint64>> m_generation = std::make_shared<std::atomic<quint64>>(0);
    std::shared_ptr<std::atomic<quint64>> m_optionsGeneration =
        std::make_shared<std::atomic<quint64>>(0);
    bool m_queried = false;
    bool m_optionsRequested = false;
    bool m_busy = false;
    bool m_complete = true;
    int m_elapsedMs = -1;
    QString m_error;
    bool m_optionsLoaded = false;
    QString m_optionsError;
    FilterOptions m_options;
};

} // namespace auralis::mod::lib

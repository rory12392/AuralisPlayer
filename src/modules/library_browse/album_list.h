#pragma once

#include "search_criteria.h"

#include <auralis/library_query.h>

#include <QList>
#include <QString>

namespace auralis::mod::lib {

/// 專輯頁（S-03）的一張專輯：核心的 AlbumInfo 加上由曲目算出的總長、格式摘要與曲目本身
/// （詳細檢視與「播放整張專輯」用）。同名專輯在不同資料夾是不同的 AlbumInfo（核心依專輯藝人、
/// 專輯名、資料夾分組），這裡不合併（模組規格 §1 情況 2）。
struct AlbumEntry {
    AlbumInfo info;
    /// 顯示用的專輯藝人：沒有專輯藝人時用第一首的藝人（情況 4 由 T-23 處理其餘部分）。
    QString artist;
    qint64 durationMs = 0;
    /// 封面牆的等寬格式摘要：「96/24」「44.1/16」「DSD64」，有損是 codec（「MP3」）；
    /// 同專輯不一致時取最高的（取樣率先比、再比位元深度）。
    QString formatText;
    /// 依碟號、軌號排序（LibraryQuery::tracks 的順序）。
    QList<TrackInfo> tracks;
};

/// S-03 的排序：加入時間（新到舊）、專輯名、專輯藝人、年份（新到舊）。
enum class AlbumSort { Added, Title, Artist, Year };

/// 設定值與 QML 用的名稱：added、title、artist、year；不認得的當成 added。
AlbumSort albumSortFromName(const QString& name);
QString albumSortName(AlbumSort sort);

/// 把專輯與整個音樂庫的曲目組起來（曲目以 albumId 對應；沒有專輯的曲目略過）。
/// 沒有曲目的專輯照樣保留（總長 0、沒有格式摘要）。任何執行緒。
QList<AlbumEntry> buildAlbums(const QList<AlbumInfo>& albums, const QList<TrackInfo>& tracks);

/// 格式摘要（見 AlbumEntry::formatText）；tracks 空時空字串。
QString albumFormatText(const QList<TrackInfo>& tracks);

/// 篩選列的條件（搜尋字串不算，專輯頁不受搜尋框影響）：專輯裡有一首同時符合全部條件就顯示，
/// 和曲目清單的 TrackFilter 規則一致（藝人比對 artist 或 album_artist）。
bool albumMatches(const AlbumEntry& album, const SearchCriteria& criteria);

/// 依 sort 排序（穩定；相同時依專輯名、id）。加入時間以專輯 id 代表：核心新增專輯時 id 遞增，
/// 重新掃描時原地更新、id 不變。
void sortAlbums(QList<AlbumEntry>& albums, AlbumSort sort);

} // namespace auralis::mod::lib

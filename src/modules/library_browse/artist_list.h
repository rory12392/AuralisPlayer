#pragma once

#include "album_list.h"

#include <QList>
#include <QString>

namespace auralis::mod::lib {

/// 藝人頁（S-05）左欄的一位藝人：名稱與專輯數。
struct ArtistEntry {
    QString name; ///< 空字串＝沒有藝人資料（頁面顯示「未知藝人」）
    int albumCount = 0;
};

/// 以專輯的顯示藝人（AlbumEntry::artist：專輯藝人，沒有時第一首的藝人）分組（模組規格 §1 情況 4）。
/// 只看專輯藝人、不看各曲的藝人，所以「Various Artists」的合輯整張留在「Various Artists」，
/// 不拆到各曲的藝人底下。依名稱排序（不分大小寫，相同時依原字串），空名稱排最後。任何執行緒。
QList<ArtistEntry> groupArtists(const QList<AlbumEntry>& albums);

/// groupArtists 的排序（a 排在 b 前面）。
bool artistLess(const QString& a, const QString& b);

/// 顯示藝人正好是 name 的專輯（保持原本的順序）。任何執行緒。
QList<AlbumEntry> albumsOfArtist(const QList<AlbumEntry>& albums, const QString& name);

} // namespace auralis::mod::lib

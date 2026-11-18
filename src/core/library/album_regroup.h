#pragma once

namespace auralis::core {

class SqliteConnection;

/// 多片專輯合併成一張（T-91；LibraryWriter::refreshAlbums 的第一步，在呼叫端的交易裡）：
/// 1. 專輯資料夾是 Disc／Disk／CD＋數字（isDiscFolderName）的，曲目改掛到上一層資料夾的專輯
///    （舊版依檔案所在資料夾分組，增量掃描不重讀沒變的檔案，既有音樂庫在這裡修正）；
/// 2. 同一個上一層資料夾底下，兩個以上子資料夾裡有同一張專輯（專輯藝人＋專輯名）的曲目、每首都有
///    碟號、而且各子資料夾的碟號互不重疊（曲目直接放在上一層的也算一份）時，合成上一層資料夾的一張；
///    碟號重疊或缺碟號＝不同版本，照舊分開。以曲目所在的資料夾判斷，所以改過的檔案重新掃描後
///    （先寫進自己資料夾的專輯）會歸回同一張。只往上一層合併一次。
/// 只動 albums 與 tracks.album_id（沒有曲目的舊列由 refreshAlbums 接著刪掉）。資料庫錯誤丟
/// std::runtime_error。
void regroupAlbums(SqliteConnection& db);

} // namespace auralis::core

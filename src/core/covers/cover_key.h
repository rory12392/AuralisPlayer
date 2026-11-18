#pragma once

#include <QString>

namespace auralis::core {

/// covers.source 的兩種寫法：音樂檔的內嵌圖、資料夾裡的圖檔。
QString embeddedCoverSource(const QString& audioPath);
QString fileCoverSource(const QString& imagePath);

/// 來源是內嵌圖時回傳音樂檔路徑、是圖檔時回傳圖檔路徑；isEmbedded 告訴呼叫端是哪一種。
/// 不認得的寫法回傳空字串。
QString coverSourcePath(const QString& source, bool* isEmbedded);

/// covers.key：「e」（內嵌）或「f」（圖檔）加上「來源的路徑比對鍵、檔案大小、修改時間」的 SHA-1
/// （小寫十六進位）。檔案改了 key 就跟著變，舊的縮圖與主色不會被沿用。
/// 掃描只靠開頭的字母分辨內嵌與圖檔（資料夾封面不能蓋掉曲目的內嵌圖）。
QString coverKey(const QString& source, qint64 size, qint64 mtimeNs);

/// key 只能是 e／f 加 40 個小寫十六進位字元：影像網址的 key 會成為快取檔名，不合格的一律拒絕。
bool isValidCoverKey(const QString& key);

/// 資料夾封面圖檔的優先順序（數字越小越優先）：front、cover、folder、albumart，
/// 副檔名 .jpg／.jpeg／.png，不分大小寫。不是封面圖檔時回傳 -1。
int folderCoverRank(const QString& fileName);

} // namespace auralis::core

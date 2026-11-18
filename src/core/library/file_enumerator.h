#pragma once

#include <QList>
#include <QString>

#include <functional>
#include <vector>

namespace auralis::core {

struct EnumeratedFolder {
    QString path;
    QString name;
    int parent = -1; ///< folders 裡的索引；-1 是音樂資料夾本身
    /// 資料夾封面圖檔（folderCoverRank 最優先的那個）；沒有時為空字串。
    QString coverImage;
    qint64 coverSize = 0;
    qint64 coverMtimeNs = 0;
};

struct EnumeratedFile {
    QString path;
    int folder = 0; ///< folders 裡的索引
    qint64 size = 0;
    qint64 mtimeNs = 0; ///< Unix 奈秒（檔案系統的最後修改時間）
};

struct ScanFailure {
    QString path;
    QString error;
};

struct Enumeration {
    /// 先序：父資料夾一定排在子資料夾前面。第 0 個是音樂資料夾本身。
    std::vector<EnumeratedFolder> folders;
    std::vector<EnumeratedFile> files;
    /// 讀不到的資料夾（權限、離線）；這些資料夾底下在資料庫裡的東西不能當成已刪除。
    QList<ScanFailure> unreadableFolders;
    /// 跳過的符號連結與 junction（資料夾，以及音樂檔、.cue、封面圖檔）；原因是給使用者看的文字。
    QList<ScanFailure> skippedLinks;
    bool cancelled = false;
};

/// 目錄項目是不是符號連結或 junction（掛載點）。attributes 與 reparseTag 是 WIN32_FIND_DATAW 的
/// dwFileAttributes 與 dwReserved0。只認這兩種標籤：雲端硬碟的佔位檔、重複資料刪除、壓縮這些
/// 也是重新解析點，但內容就是那個檔案自己，不會把讀取帶到別的路徑，照一般檔案處理。
bool isLinkEntry(unsigned long attributes, unsigned long reparseTag);

/// path 這個項目本身是不是符號連結或 junction。只讀它上一層的目錄項目，不開啟、不跟隨它；
/// 不存在時回傳 false。上一層有沒有經過連結由呼叫端從外往內一層一層查。
bool isLinkPath(const QString& path);

/// 跳過連結時寫進掃描報告的原因。
QString linkSkippedReason();

/// 列出 root 底下的音樂檔（副檔名在 audioExtensions()）與 .cue，並記下每個資料夾的封面圖檔。
/// 不開檔，大小與修改時間來自目錄項目。
/// 不進入重新解析點（junction、符號連結），避免迴圈與重複；檔案層級的符號連結也不收
/// （連結可以指到網路位置，讀它會對外連線）。跳過的連結記在 skippedLinks。root 本身是使用者
/// 選的，不檢查。cancelled 回傳 true 時盡快結束。
/// 執行緒：工作執行緒。
Enumeration enumerateAudioFiles(const QString& root, const std::function<bool()>& cancelled);

/// 給 Win32 API 的路徑：加上 \\?\（UNC 是 \\?\UNC\）以支援超過 260 字元的路徑。
std::wstring toLongPath(const QString& path);

/// 統一成 Windows 分隔符、去掉結尾的分隔符（磁碟根目錄 C:\ 保留）。
QString cleanFolderPath(const QString& path);

} // namespace auralis::core

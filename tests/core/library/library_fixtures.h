#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>

namespace auralis::test {

/// 測試音檔的標籤：TagLib 屬性名稱（TITLE、ARTIST、ALBUMARTIST、ALBUM、COMPOSER、DATE、
/// TRACKNUMBER、DISCNUMBER、LYRICS、REPLAYGAIN_TRACK_GAIN…）→ 值。
using Tags = QMap<QString, QString>;

/// 測試用的空資料夾：AURALIS_TEST_TMP/t-09/<name>，先清掉上一輪留下的。
QString freshTestDir(const QString& name);

/// 拿掉 folder 底下所有的符號連結與 junction（只拿掉連結本身，不進去、不動它指到的東西）。
/// 一般的遞迴刪除碰到資料夾連結會留下它，下一輪就建不起來。
void removeLinksUnder(const QString& folder);

/// 有連結的測試用的空資料夾（同 freshTestDir）：先拿掉上一輪留下的連結再清空；離開時也把連結拿掉。
struct LinkDir {
    QString path;
    explicit LinkDir(const QString& name);
    ~LinkDir();
    LinkDir(const LinkDir&) = delete;
    LinkDir& operator=(const LinkDir&) = delete;
};

/// 建立 junction（不需要權限）；target 要是已存在的資料夾。失敗算測試失敗。
void makeJunction(const QString& link, const QString& target);

/// 建立符號連結；沒有權限（沒開開發人員模式、也不是系統管理員）時回傳 false，
/// 其他原因的失敗算測試失敗。
bool makeSymlink(const QString& link, const QString& target, bool directory);

/// 在檔案上放一個不是連結的重新解析點（自訂標籤，像雲端佔位檔那樣只是標記）；做不到時 false。
bool setCustomReparsePoint(const QString& path);

/// 以下都在測試時產生檔案（不把音檔放進 git），標籤用 TagLib 寫進剛產生的檔案。
/// 失敗時丟 std::runtime_error。
void writeFlac(const QString& path, const Tags& tags, int rate = 44100, int bits = 16,
               int frames = 4410);
void writeWav(const QString& path, const Tags& tags, int rate = 96000, int frames = 9600);
/// DSF：DSD64、立體聲、0.5 秒的 DSD 靜音，加 ID3v2 標籤。
void writeDsf(const QString& path, const Tags& tags);
/// DSDIFF（.dff）：DSD64、立體聲、0.5 秒的 DSD 靜音，加 ID3 標籤。
void writeDff(const QString& path, const Tags& tags);
/// 副檔名是 .flac 但內容不是 FLAC（損毀檔）。
void writeCorrupt(const QString& path);
/// 寫入任意位元組（建立需要的資料夾）。
void writeBytes(const QString& path, const QByteArray& bytes);

/// 範例音樂庫（root 底下，中日文資料夾名、多層資料夾）：
///   專輯 A\01.flac（NieR：Automata，有歌詞）、專輯 A\02.flac、其他\深一層\x.wav、
///   DSD\d.dsf、DSD\e.dff、壞掉\壞掉.flac（損毀）、notes.txt 與 cover.jpg（不收錄）
/// 回傳音樂檔數（含損毀的那一個）。
int buildSampleLibrary(const QString& root);

} // namespace auralis::test

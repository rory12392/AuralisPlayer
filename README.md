# AuralisPlayer

Windows 桌面音樂播放器，給重視位元完美輸出、管理大型本機無損音樂庫，也想從 foobar2000 帶著播放清單過來的使用者。

<picture>
  <source media="(prefers-color-scheme: dark)" srcset="src/plugins/asio/logo/asio-compatible-logo-dark.svg">
  <img alt="ASIO Compatible" src="src/plugins/asio/logo/asio-compatible-logo-light.svg" height="72">
</picture>

## 功能

- **播放**：FLAC、ALAC、WAV、AIFF、APE、WavPack、MP3、AAC、Opus、Vorbis；CUE 分軌（含 Shift-JIS、Big5 編碼的 .cue）；同格式曲目無縫播放。
- **位元完美輸出**：WASAPI 獨占模式以來源的取樣率與位元深度開裝置、不重取樣；也可以選 WASAPI 共享。ASIO® 輸出由預先安裝的官方外掛提供。播放列顯示輸出路徑、格式與是否位元完美，不用進設定頁就能切換輸出。
- **DSD**：播放 DSF、DFF（DSD64／128／256），輸出方式可選 DoP、ASIO 原生 DSD、轉 PCM 176.4 kHz。
- **音量與 ReplayGain**：音量 100% 時維持位元完美；ReplayGain（音軌／專輯、防止削波），沒有標籤的曲目可以計算 R128 增益（只存在程式的資料庫，不寫音樂檔）。
- **音樂庫**：加入音樂資料夾後自動掃描（標籤、封面、CUE）並監看變動；專輯、藝人、資料夾、所有曲目四種瀏覽；即時搜尋與篩選。音樂檔與資料夾只讀不改。
- **播放清單**：建立與編輯播放清單；匯入 m3u／m3u8／pls 與 foobar2000 的 .fpl，找不到的檔案列出原因並可以重新對應資料夾；匯出 .m3u8。
- **歌詞與全螢幕**：同步歌詞（本機 .lrc、內嵌歌詞，或從 LRCLIB 自動查詢，可以關閉）；全螢幕正在播放畫面。
- **Windows 整合**：系統媒體控制（媒體鍵、音量面板的曲目資訊與封面）；深色與淺色主題，可以跟隨系統。
- **外掛**：設定頁的「外掛」可以檢視、停用、匯入外掛。這一版只開放官方的 ASIO 外掛。

## 需求

- Windows 11（x64）。Windows 10 22H2 應該可以執行，但還沒驗證。
- Microsoft Visual C++ 2015–2022 可轉散發套件（x64）：zip 與安裝檔都不附，沒裝過請先從 Microsoft 的下載頁安裝（https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist）。
- ASIO 輸出需要 DAC 廠商的 ASIO 驅動；沒有 ASIO 驅動時用 WASAPI 獨占即可。

## 安裝

從 Releases 頁下載其中一種：

- **安裝檔**（`AuralisPlayer-<版本>-win64-setup.exe`）：只裝給目前使用者，不需要系統管理員權限；資料放在 `%LOCALAPPDATA%\AuralisPlayer`，解除安裝不會刪除音樂庫資料。
- **免安裝 zip**（`AuralisPlayer-<版本>-win64.zip`）：解壓縮後執行 `AuralisPlayer.exe`；資料放在程式旁邊的 `data\`。

程式沒有數位簽章，第一次執行時 Windows SmartScreen 可能擋下：按「其他資訊」→「仍要執行」。

第一次開啟時加入音樂資料夾，掃描完就可以瀏覽與播放；也可以直接把音樂檔、資料夾或播放清單檔拖進視窗。

## 從原始碼建置

需要 Visual Studio 2022（C++ 桌面開發工作負載）、CMake 3.28 以上、Ninja、vcpkg、Qt 6.11.3（msvc2022_64）。在「Developer PowerShell for VS 2022」（x64）中：

```powershell
$env:VCPKG_ROOT = "<vcpkg 資料夾>"
$env:QTDIR = "<Qt 安裝資料夾>\6.11.3\msvc2022_64"
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug
```

開發版一定要指定資料夾，避免動到真實的設定與音樂庫：

```powershell
$env:AURALIS_DATA_DIR = "$PWD\.localdata\app"
.\build\windows-debug\AuralisPlayer.exe
```

發佈用的建置：`cmake --preset windows-release`，`--target package`（免安裝 zip）、`--target package_installer`（安裝檔，需要 NSIS 3）、`--target package_plugin_asio`（只有 ASIO 外掛的 zip）。發佈建置會下載 ASIO SDK（vcpkg 的 `asio` 特性），只編進外掛的 DLL。

自我檢查（輸出 JSON，結束碼 0 表示通過）：`AuralisPlayer.exe --self-check --data-dir <資料夾> | Out-String`。

## 已知限制

- 只在 Windows 11 上驗證過。
- 副檔名和實際格式不符的檔案無法播放；只開啟本機檔案，不開啟網址，播放清單裡的網路路徑不播放。
- .cue 和音檔放在不同資料夾時不會分軌（音檔照常以整檔收錄）。
- DSD 播放時（包括轉 PCM）不能調音量，ReplayGain 不作用。
- 對外連線只有歌詞查詢（LRCLIB），可以在設定關閉。
- 外掛只接受和程式同一版本的官方外掛；第三方外掛還沒開放。

更新紀錄見 [docs/development-log.md](docs/development-log.md)。

## 授權

本專案採用 GNU General Public License 第 3 版（GPL-3.0-only，全文在 `LICENSE`）。發佈的 zip 與安裝檔整體以 GPLv3 散布；第三方元件的授權見 `THIRD-PARTY-NOTICES.txt` 與 [docs/NOTICES.md](docs/NOTICES.md)。

ASIO 輸出外掛隨主程式預先安裝（程式資料夾的 `plugins\AuralisAsio\`，可以在設定頁的「外掛」停用）。外掛使用 Steinberg ASIO SDK 2.3.4 授權檔中的 GPLv3 選項；SDK 只編進外掛的 DLL，主程式本身不含。

`src/plugins/asio/logo/` 的 ASIO Compatible 標誌是 Steinberg Media Technologies GmbH 的商標圖檔（取自 ASIO SDK 2.3.4，未修改），**不屬於本專案的 GPLv3 授權**，只能照 Steinberg 的《ASIO Usage Guidelines》使用。

對應原始碼（和該版 binary 完全對應的本專案原始碼與建置腳本，以及每個第三方元件確切版本的原始碼與 vcpkg 補丁）放在 Releases 頁的附件、binary 下載處旁邊，該頁標示 binary 是從哪個 commit 建置的。

ASIO is a registered trademark of Steinberg Media Technologies GmbH.

# 第三方素材與程式碼

> 隨程式發佈的第三方素材與改編的開源程式碼：來源、授權、改了什麼。這份是中文索引；給使用者的完整聲明是 repo 根目錄的 `THIRD-PARTY-NOTICES.txt`（英文，打包時放進 zip 根目錄，授權全文放 zip 的 `licenses/`）。這裡有改動時同步改那份。

## 字型

| 項目 | 檔案 | 來源 | 授權 | 修改 |
|---|---|---|---|---|
| Jost（可變字重） | `src/ui/fonts/Jost-VariableFont_wght.ttf` | Google Fonts 倉庫 `ofl/jost/Jost[wght].ttf`（2026-11-11 下載）；作者 The Jost Project Authors | SIL Open Font License 1.1，全文 `src/ui/fonts/OFL-Jost.txt` | 無（只改檔名） |
| IBM Plex Mono Light、Regular | `src/ui/fonts/IBMPlexMono-Light.ttf`、`IBMPlexMono-Regular.ttf` | Google Fonts 倉庫 `ofl/ibmplexmono/`（2026-11-11 下載）；IBM Corp. | SIL Open Font License 1.1（保留字型名稱「Plex」），全文 `src/ui/fonts/OFL-IBMPlexMono.txt` | 無 |

字型以 Qt 資源編進程式，不單獨販售；OFL 要求散布時附上授權全文：打包時兩份 OFL 照原樣放進 zip 的 `licenses/`（T-98）。

## 散布條件

- 整個專案：**GPLv3**（`GPL-3.0-only`，根目錄 `LICENSE` 是 GPLv3 全文；PRD v1.8，2026-11-17 Rory 決定，T-105）。原本的 `COPYING`（同一份 GPLv3 全文）併進 `LICENSE`，不再有「原始碼 MIT、外掛另外 GPLv3」的分法。要不要改成「GPLv3 或更新版本」（`GPL-3.0-or-later`）由 Rory 之後決定；ASIO SDK 的授權檔只寫 GPLv3，沒有「或更新版本」。
- 主程式的發佈檔（zip、安裝檔）：**整體以 GPLv3 散布**，第三方元件各自的授權照舊。根目錄放 `LICENSE` 與 `THIRD-PARTY-NOTICES.txt`，`licenses/GPL-3.0.txt`（同 `LICENSE`，給 Qt 的 LGPLv3 引用）。官方 ASIO 外掛**預先安裝**在程式資料夾的 `plugins\AuralisAsio\`（`AuralisAsio.dll`、`plugin.json`、標誌、`ASIO-LOGO-README.txt`），`licenses/` 多 `ASIO-SDK.txt`、`ASIO-SDK-host.txt`。ASIO SDK 只編進外掛 DLL，exe 不連 SDK（打包後檢查）。
- ASIO 外掛包（`AuralisPlayer-<版本>-plugin-asio-win64.zip`，給重新安裝用，外掛資料夾的檔案和主程式包內建的相同）：**整體以 GPLv3 散布**（包裡的 `LICENSE`），附自己的 `THIRD-PARTY-NOTICES.txt`（原始檔 `src/plugins/asio/THIRD-PARTY-NOTICES.txt`）與 `licenses/ASIO-SDK.txt`、`licenses/ASIO-SDK-host.txt`。ASIO SDK 2.3.4 是 Steinberg 專有授權與 GPLv3 雙授權，本專案選 GPLv3，不簽 Steinberg 協議。
- **ASIO Compatible 標誌**（`src/plugins/asio/logo/`，T-105）：Steinberg Media Technologies GmbH 的商標圖檔，從 ASIO SDK 2.3.4 的 `Steinberg ASIO Logo Artwork\` 原樣複製（只改成 ASCII 檔名；同資料夾 `README.txt` 記原檔名與 SHA-256，`.gitattributes` 讓 git 不改行尾）。**不屬於本專案的 GPLv3 授權**，只能照《Steinberg ASIO Usage Guidelines》使用（照 Steinberg 提供的樣子，不改色、不裁切）。打包時隨外掛放進外掛資料夾；公開快照包含這個資料夾（README 引用）。SDK 的其他檔案仍不進 repo。
- 對應原始碼（T-102，提供位置 T-103 定案）：
  - 內容：**和這個 binary 完全對應**的本專案原始碼（Release 頁標示的 commit；含建置與打包腳本：`CMakeLists.txt`、`CMakePresets.json`、`vcpkg.json` 與 baseline、`cmake/`、安裝檔腳本）＋每個第三方元件**確切版本**的原始碼封存與 vcpkg 的 port 檔（portfile、補丁）。主程式包是 FFmpeg、TagLib、uchardet、SQLite、libebur128、zlib、ASIO SDK（內建外掛，T-105）與 Qt 三個模組；外掛包是 ASIO SDK 與 Qt 的 qtbase（外掛連結 Qt Core）。tag 只是管理方式，以 Release 頁標示的 commit 為準。
  - 提供位置：**正式 repo 的 Release 附件**（`https://github.com/rory12392/AuralisPlayer/releases`，binary 下載處旁邊；Rory 同意，2026-11-17）。發佈者要確保發佈 binary 期間一直取得得到，不能只指向上游；notices 寫的上游網址只供參考。
  - 事先留存：發佈時跑 `python tools/collect_sources.py`（先 `--dry-run`），收進 `.localdata/release-sources/<版本>/`，附 `MANIFEST.json`（`packages` 列出主程式包與外掛包各自的原始碼）與 `SHA256SUMS.txt`。Qt 原始碼封存（約 92 MB）預設不下載，要 `--download-qt`。

## 函式庫（隨程式發佈）

版本與授權從 vcpkg 的 `share/<port>/copyright`、`vcpkg.spdx.json` 與 Qt 安裝裡的 `sbom/*.spdx.json` 讀（2026-11-17，T-98）。

| 元件 | 版本 | 授權（本專案採用的） | 原始碼取得 | zip 裡的授權全文 |
|---|---|---|---|---|
| Qt（Core、Gui、OpenGL、Network、Svg、Qml、Quick、Quick Controls 2、Dialogs、Effects、Layouts、Shapes、Labs FolderListModel 等） | 6.11.3 | LGPLv3（沒有只限 GPL 的模組） | qtbase、qtdeclarative、qtsvg 的 `<模組>-everywhere-src-6.11.3.tar.xz`（download.qt.io 的 submodules 資料夾；commit 取自 Qt 安裝的 `sbom/*.spdx.json`；未修改） | `THIRD-PARTY-NOTICES.txt` 第 4 節（LGPLv3）、`licenses/GPL-3.0.txt` |
| Qt 內建的第三方程式（libjpeg-turbo、libpng、zlib、FreeType、HarfBuzz、PCRE2 等） | 見 notices | IJG＋BSD-3（qjpeg，要附「based in part on the work of the Independent JPEG Group」）、各自的寬鬆授權 | 隨 Qt 原始碼 | `THIRD-PARTY-NOTICES.txt` 3.1 節列出 |
| FFmpeg（avcodec、avformat、avutil、swresample） | 9.0.2 | LGPL 2.1 以上（沒有 gpl、nonfree） | vcpkg 下載的 GitHub tag `n9.0.2` 封存＋vcpkg baseline `e54ef0f` 的 port 補丁 | `licenses/FFmpeg.txt` |
| TagLib（含 UTF8-CPP 4.2.1） | 2.3.1 | LGPL 2.1（另可選 MPL 1.1）；UTF8-CPP 是 BSL 1.0 | GitHub taglib＋vcpkg port | `licenses/TagLib.txt`、`TagLib-MPL-1.1.txt`、`utfcpp.txt` |
| uchardet | 0.0.8 | LGPL 2.1（三選一：MPL 1.1／GPL 2／LGPL 2.1） | freedesktop.org＋vcpkg port | `licenses/uchardet.txt` |
| SQLite | 3.54.1 | 公有領域 | sqlite.org | `licenses/SQLite.txt` |
| libebur128 | 1.2.6 | MIT | GitHub＋vcpkg port | `licenses/libebur128.txt` |
| zlib（`z.dll`） | 1.3.2 | zlib | zlib.net | `licenses/zlib.txt` |
| `d3dcompiler_47.dll`、`dxcompiler.dll`、`dxil.dll`、`vc_redist.x64.exe` | — | **不隨發佈包散布**（T-103）：前三個程式不載入，`windeployqt` 加 `--no-system-d3d-compiler --no-system-dxc-compiler`；VC++ 執行階段由使用者自己安裝（README 寫明 Microsoft 的下載頁），部署加 `NO_COMPILER_RUNTIME` | 不適用 | `THIRD-PARTY-NOTICES.txt` 3.8 節寫明不包含 |
| ASIO SDK（只編進 ASIO 外掛 `AuralisAsio.dll`，主程式包內建、外掛包也有；SDK 原始檔不進 zip） | 2.3.4（`ASIO-SDK_2.3.4_2025-10-15.zip`，vcpkg port `asiosdk` 2.3.4#2） | SDK 的 `LICENSE.txt` 二選一，採 b) GPLv3；`host/` 檔案另有 BSD 式授權；標誌是 Steinberg 的商標圖檔，不屬於 GPLv3 | Steinberg 封存＋vcpkg port 補丁 `fix-new-delete-mismatch.patch` | 兩個包的 `licenses/ASIO-SDK.txt`、`licenses/ASIO-SDK-host.txt`、`LICENSE`；標誌說明 `plugins\AuralisAsio\ASIO-LOGO-README.txt` |

只在測試用的 Catch2 不隨程式發佈。

## Microsoft 元件（T-102 逐項核對，2026-11-18）

**T-103 起不隨發佈包散布**（Rory 同意拿掉）：主程式包與安裝檔都沒有下表四個檔案，打包後檢查（`cmake/AuralisNoAsioCheck.cmake`）有任何一個就失敗；使用者要先安裝 Microsoft Visual C++ 2015–2022 可轉散發套件（x64），README 寫明 Microsoft 的下載頁。下表留作當時核對的紀錄。

不再一概寫成「GPL 的 System Libraries」。每項分開寫來源、條款依據與結論；GPLv3 的 System Libraries 例外適不適用另外判斷，目前**待確認**，不當成結論。這不是法律意見。

| 檔案 | 版本、簽章 | 從哪裡來 | 條款依據 | 程式需不需要 | 結論 |
|---|---|---|---|---|---|
| `d3dcompiler_47.dll` | 10.0.22621.3233；Microsoft Corporation 簽章有效 | windeployqt 從 Windows SDK 10.0.22621.0 `Redist\D3D\x64` 帶入（SHA-256 相同）；Qt 安裝的 SBOM 沒列 | Windows SDK 授權（`Windows Kits\10\Licenses\10.0.22621.0\sdk_license.rtf`）§2.a「Distributable Code」＋ SDK redist 清單（learn.microsoft.com/en-us/legal/windows-sdk/redist，2024-10-21）列在 Binaries，限 Classic Windows 應用程式 | 預設 D3D11、D3D12、OpenGL 三種繪圖後端實際執行時都沒有載入（也沒載入系統的副本）；Qt 6.11.3 的 windeployqt 預設帶入，可用 `--no-system-d3d-compiler` 不帶 | 有依據可照條款再散佈；但 SDK 條款要求「散佈者與終端使用者同意保護程度不低於本協議的條款」並禁止讓它受 Excluded License 約束，和整包以 GPLv3 散布放在一起是否有衝突**待確認** |
| `dxcompiler.dll` | 1.6.2112.16；Microsoft Corporation 簽章有效 | 同上（Windows SDK `Redist\D3D\x64`） | 同上（SDK redist 清單有列）；這份是 Microsoft 以 SDK 條款授權的 binary，不是上游 DirectX Shader Compiler 的開源授權；SDK 附的第三方聲明（`sdk_third_party_notices.rtf` 的「Microsoft/DirectXShaderCompiler」段，LLVM 等）依 SDK 條款 §1.e 屬「僅供參考」 | 同上；windeployqt 可用 `--no-system-dxc-compiler` 不帶 | 同 `d3dcompiler_47.dll`，**待確認**；另外要確認保留時是否要附 DXC 的第三方聲明 |
| `dxil.dll` | 101.6.2112.13；Microsoft Windows 簽章有效 | 同上 | 同上（SDK redist 清單有列） | 同上 | 同上，**待確認** |
| `vc_redist.x64.exe` | 14.42.34433.0；Microsoft Corporation 簽章有效 | windeployqt 從 VS 2022 `VC\Redist\MSVC\14.42.34433\` 帶入（SHA-256 相同） | VS 2022 可再散佈清單（learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution）寫「`VC\redist` 資料夾與子資料夾的檔案可依授權條款隨程式散佈」，但沒有逐字列出這個檔名；VS 2022 Community 授權條款本文本機沒有、網頁版抓不到 | exe 實際載入的 `VCRUNTIME140.dll`、`MSVCP140*.dll` 都是 `C:\Windows\System32` 的副本，zip 本身不帶 VC 執行階段 DLL；`vc_redist.x64.exe` 是給沒裝 VC 執行階段的電腦用的安裝程式 | **待確認**：缺 VS 2022 授權條款本文的「Distributable Code」節 |

載入模組的證據：解開的 Release 包用 `--data-dir` 啟動 15 秒，`%LOCALAPPDATA%` 指到暫存資料夾（著色器快取是空的），`QSG_RHI_BACKEND` 分別不設（預設 D3D11）、`d3d11`、`d3d12`、`opengl`，用 `Get-Process -Module` 列出實際載入的 DLL 與路徑；四次都沒有 `d3dcompiler_47.dll`、`dxcompiler.dll`、`dxil.dll`（T-102 回報）。只看了啟動後的主畫面；本專案沒有自訂著色器或 `ShaderEffect`／`MultiEffect`。

## ASIO SDK（T-102 核對；T-103 起只在 ASIO 外掛）

- 版本：vcpkg 安裝的 `asiosdk` 2.3.4#2，封存 `ASIO-SDK_2.3.4_2025-10-15-57de2c0cd0df.zip`（SHA-256 `d5ebf0c20dd2c5f43771fd0c1418f4b361bf52434ee670097cfa6b3a335e2eca`）；外掛包的 `licenses/ASIO-SDK.txt` 和封存裡的 `LICENSE.txt` 位元組相同。GPLv3 選項只引用這個版本 `LICENSE.txt` 的文字（b) General Public License (GPL) Version 3）。
- SDK 只連結進 ASIO 外掛 `AuralisAsio.dll`（`auralis::asiosdk` 只給外掛、硬體探針與它們的測試；主程式、核心與模組連結時 `boundary.no_asio_sdk_links` 失敗）。SDK 的 `common/asio.cpp` 標頭指向 `LICENSE.txt` 的雙授權；`host/asiodrivers.*`、`host/pc/asiolist.*`、`host/ginclude.h` 的檔頭是 Steinberg 的 BSD 式授權（binary 散布要在文件附著作權聲明與條件），外掛打包時從 vcpkg 安裝的標頭照原文擷取成 `licenses/ASIO-SDK-host.txt`（靜態函式庫只連結用到的物件檔，實際編進 DLL 的 SDK 程式可能比這份清單少，聲明照樣附上）。
- 商標：SDK 的 README 寫 GPLv3 下用不用「ASIO」名稱或標誌是選擇性的，用了就要照《Steinberg ASIO Usage Guidelines》。本專案的介面、文件都有用「ASIO」字樣，Rory 選照規範放標誌（PRD v1.8）。條文摘錄與介面對照表在 T-102 回報；文件（README、notices）照第 15 條第一次出現加 ® 並附「ASIO is a registered trademark of Steinberg Media Technologies GmbH.」；介面的 ® 與標示由 T-104 做，標誌由 T-105 補上（外掛分區、設定頁輸出分區、輸出面板、「關於」）。
- 標誌檔：T-105 起放進 repo（`src/plugins/asio/logo/`，見上方「散布條件」；Rory 2026-11-17 同意，取代先前「不進 repo」）。README 用 repo 內的路徑顯示（深淺主題各一）；GitHub Release 頁的說明引用同一個檔的 raw 網址。

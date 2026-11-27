#pragma once

#include <QString>
#include <QVariantMap>

#include <cstdio>
#include <functional>

class QQuickWindow;

namespace auralis::core {
struct CoreServices;
}
namespace auralis::ui {
class LibraryController;
}

namespace auralis::app {

// 主視窗的截圖（--screenshot）與開發建置截圖前的示範內容。

/// 印一行 UTF-8 文字並馬上送出（截圖結果、找不到頁面等給測試讀的訊息）。
void printLine(FILE* stream, const QString& text);

/// 畫面穩定後截圖存檔並結束事件迴圈（結束碼 0 成功、1 失敗；30 秒沒有新的一格也算失敗）。
void scheduleScreenshot(QQuickWindow* window, const QString& outPath);

#ifdef AURALIS_DEV_UI
/// 開發建置的截圖用：--screenshot "shell?queue=<資料夾>[&state=paused][&output=open]"
/// （另有 &panel=closed，在 runGui 處理）把資料夾加進音樂庫、
/// 掃描完把裡面的音樂檔放進佇列從第一首播，讓播放列與佇列面板有內容可截。輸出用 Capture 後端，
/// 不開實體音訊裝置、不發聲。準備好（再等曲目資訊查好、進度走一點）時呼叫 ready。
void startDemoQueue(core::CoreServices& services, const QVariantMap& params,
                    std::function<void()> ready);

/// 開發建置的截圖用：--screenshot "welcome?library=<資料夾>" 或
/// "settings?library=<資料夾>[&report=open]"：和按「加入音樂資料夾」一樣經 Library 加入，
/// 掃描完（每個資料夾的曲目數也查好）再呼叫 ready。
void startDemoLibrary(core::CoreServices& services, ui::LibraryController& library,
                      const QString& folder, std::function<void()> ready);
#endif

} // namespace auralis::app

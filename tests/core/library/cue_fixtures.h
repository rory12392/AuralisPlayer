#pragma once

// CUE 測試共用：讀 tests/fixtures/cue 的樣本、轉成 Shift-JIS／Big5 位元組、產生整軌檔。

#include <QByteArray>
#include <QString>

namespace auralis::test {

/// tests/fixtures/cue/<name> 的原始位元組。
QByteArray cueFixture(const QString& name);

/// 把 UTF-8 文字轉成 Windows 字碼頁（932＝Shift-JIS、950＝Big5）的位元組。
/// 有任何字元無法表示時丟 std::runtime_error（樣本要能完整轉換，測試才有意義）。
QByteArray encodeCodePage(const QByteArray& utf8, unsigned codePage);

/// 樣本 .cue 的三軌區段（CD 影格）：0–150、150–262、262–檔尾（第 2 軌另有 INDEX 00 在 125，
/// 前置空白算在第 1 軌）；整軌檔長 5 秒。
constexpr int kSampleTrackStart[] = {0, 150, 262};
constexpr int kSampleAudioSeconds = 5;

} // namespace auralis::test

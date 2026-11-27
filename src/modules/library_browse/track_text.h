#pragma once

#include <auralis/library_query.h>

#include <QString>

namespace auralis::mod::lib {

/// 清單列的長度：「m:ss」，超過一小時「h:mm:ss」（介面規範 §6.1）；不知道（負數）時「—」。
QString durationText(qint64 ms);

/// 清單列的「取樣率／位元」：「96 kHz / 24 bit」；有損格式（bits 0）只寫取樣率；
/// DSD 寫倍率（「DSD64」，同 formatKey）；取樣率不知道時空字串。任何執行緒。
QString qualityText(const TrackInfo& track);

/// 清單列的「位元率」：千分位加單位（例 1,411 kbps，同正在播放面板）；
/// 不知道或還在補值時空字串。任何執行緒。
QString bitrateText(std::optional<int> kbps);

/// 清單列的「加入時間」：本地時區的日期「2026-11-26」；不知道或還在補值時空字串。任何執行緒。
QString addedText(std::optional<qint64> utcMs);

} // namespace auralis::mod::lib

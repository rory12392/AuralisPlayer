#pragma once

#include <auralis/library_query.h>

#include <QString>

namespace auralis::mod::lib {

/// 清單列的長度：「m:ss」，超過一小時「h:mm:ss」（介面規範 §6.1）；不知道（負數）時「—」。
QString durationText(qint64 ms);

/// 清單列的「取樣率／位元」：「96 kHz / 24 bit」；有損格式（bits 0）只寫取樣率；
/// DSD 寫倍率（「DSD64」，同 formatKey）；取樣率不知道時空字串。任何執行緒。
QString qualityText(const TrackInfo& track);

} // namespace auralis::mod::lib

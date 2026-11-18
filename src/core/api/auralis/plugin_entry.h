// 外掛 DLL 唯一匯出的 C 進入點與它回傳的結構（外掛的二進位介面，不含 Qt）。
#pragma once

#include <cstdint>

/// 外掛 ABI 版本：進入點回傳的結構或依類型的介面改變時加一。
/// 不符的外掛不使用（狀態「版本不符」），plugin.json 的 abi 也要等於它。
#define AURALIS_PLUGIN_ABI_VERSION 1u

/// 外掛 DLL 匯出的函式名稱（extern "C"，不加修飾）。
#define AURALIS_PLUGIN_ENTRY_NAME "auralis_plugin_entry"

extern "C" {

/// 進入點回傳的結構，指向外掛自己的靜態資料（外掛載入後到程式結束都有效，核心不釋放）。
struct AuralisPluginEntry {
    /// 外掛建置時的 AURALIS_PLUGIN_ABI_VERSION。
    std::uint32_t abiVersion;
    /// 外掛 id 與類型（UTF-8，和 plugin.json 的 id、type 相同）。
    const char* id;
    const char* type;
    /// 依類型的介面（output 類型由 OUT 模組定義，見 OUT 的 output_plugin_api.h）。
    void* api;
};

/// 進入點：成功時回傳結構；初始化失敗時回傳 nullptr（核心停用這個外掛、記下原因）。
/// 只在 UI 執行緒、程式啟動時呼叫一次。
using AuralisPluginEntryFn = const AuralisPluginEntry* (*)();
}

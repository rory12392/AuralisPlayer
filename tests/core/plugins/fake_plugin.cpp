// 測試用的假外掛 DLL：同一份原始碼依 FAKE_PLUGIN_MODE 建成幾個行為不同的 DLL（T-103）。
//   ok     回傳正確的結構，api 指向值為 42 的整數
//   throw  進入點丟出例外
//   null   進入點回傳空值（外掛自己判斷初始化失敗）
//   abi    回傳的 ABI 版本和核心不同
#include <auralis/plugin_entry.h>

#include <cstring>
#include <stdexcept>

namespace {

int g_api = 42;

constexpr const char* kMode = FAKE_PLUGIN_MODE;

[[noreturn]] void failEntry() {
    throw std::runtime_error("假外掛初始化失敗");
}

AuralisPluginEntry g_entry{AURALIS_PLUGIN_ABI_VERSION, FAKE_PLUGIN_ID, "output", &g_api};

} // namespace

extern "C" __declspec(dllexport) const AuralisPluginEntry* auralis_plugin_entry() {
    if (std::strcmp(kMode, "throw") == 0) {
        failEntry();
    }
    if (std::strcmp(kMode, "null") == 0) {
        return nullptr;
    }
    if (std::strcmp(kMode, "abi") == 0) {
        g_entry.abiVersion = AURALIS_PLUGIN_ABI_VERSION + 98;
    }
    return &g_entry;
}

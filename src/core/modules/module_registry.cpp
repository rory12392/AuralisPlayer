#include "modules/module_registry.h"

#include <algorithm>

namespace auralis {

namespace {
// 函式內的靜態變數：不受各編譯單元靜態初始化順序影響
std::vector<core::ModuleRegistration>& registry() {
    static std::vector<core::ModuleRegistration> modules;
    return modules;
}
} // namespace

bool registerModule(const char* code, ModuleFactory factory) noexcept {
    try {
        registry().push_back({QString::fromLatin1(code), factory});
        return true;
    } catch (...) {
        // 靜態初始化時不能丟例外；配置失敗時這個模組就不會出現
        return false;
    }
}

namespace core {

std::vector<ModuleRegistration> registeredModules() {
    auto modules = registry();
    std::sort(
        modules.begin(), modules.end(),
        [](const ModuleRegistration& a, const ModuleRegistration& b) { return a.code < b.code; });
    return modules;
}

} // namespace core
} // namespace auralis

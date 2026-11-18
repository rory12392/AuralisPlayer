#pragma once

#include <auralis/module.h>

#include <vector>

namespace auralis::core {

struct ModuleRegistration {
    QString code;
    ModuleFactory factory = nullptr;
};

/// 靜態註冊的全部模組，依代碼排序。只在 main() 開始之後呼叫（靜態初始化已完成）。
std::vector<ModuleRegistration> registeredModules();

} // namespace auralis::core

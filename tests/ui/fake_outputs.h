#pragma once

// 介面測試用的輸出清單：三個裝置（兩個 WASAPI 獨占、一個共享），可以指定被佔用的裝置；
// 選了哪個記在 selected，不碰實體裝置。

#include "shell/output_controller.h"

#include <QSet>
#include <QStringList>

namespace auralis::test {

struct FakeOutputs {
    QList<ui::OutputOption> options{
        {QStringLiteral("WASAPI 獨占"), QStringLiteral("ex1"), QStringLiteral("Spring 3"),
         QStringLiteral("位元完美")},
        {QStringLiteral("WASAPI 獨占"), QStringLiteral("ex2"), QStringLiteral("喇叭"),
         QStringLiteral("位元完美")},
        {QStringLiteral("WASAPI 共享"), QStringLiteral("sh1"), QStringLiteral("Spring 3"),
         QStringLiteral("經 Windows 混音，非位元完美")},
    };
    /// 被其他程式佔用的裝置 id：選它時回傳原因、不切換。
    QSet<QString> busy;
    ui::OutputOption current;
    QStringList selected;

    ui::OutputHooks hooks() {
        ui::OutputHooks hooks;
        hooks.list = [this] {
            return options;
        };
        hooks.select = [this](const ui::OutputOption& option) -> Result<bool> {
            selected.append(option.deviceId);
            if (busy.contains(option.deviceId)) {
                return Error{QStringLiteral("裝置被其他程式使用中"), 0, QStringLiteral("select")};
            }
            current = option;
            return true;
        };
        hooks.current = [this] {
            return current;
        };
        return hooks;
    }
};

} // namespace auralis::test

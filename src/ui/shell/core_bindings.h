#pragma once

#include "settings/ui_scale_controller.h"
#include "ui/dialogs_impl.h"
#include "ui/ui_registry_impl.h"

#include <QObject>
#include <QtQml/qqmlregistration.h>

class QQmlEngine;
class QJSEngine;

namespace auralis::core {
struct CoreServices;
}

namespace auralis::ui {

class JobsController;

/// 把核心的介面服務交給 QML：主程式在建立 QML 引擎前呼叫一次，之後 QML 以單例
/// UiRegistry、Navigator、Dialogs、Notifier 取用同一份實例（核心擁有，QML 不擁有）。
/// 同時建立狀態列的背景工作單例 Jobs（JobsController，掛在核心的 JobRunner 底下、跟它一起解構）。
void bindCoreServices(core::CoreServices& services);

namespace detail {
core::UiRegistryImpl* registry();
core::NavigatorImpl* navigator();
core::DialogsImpl* dialogs();
core::NotifierImpl* notifier();
JobsController* jobs();
} // namespace detail

// 核心的類別只依賴 Qt Core，以 QML_FOREIGN 在這個 QML 模組註冊成單例

struct UiRegistryForeign {
    Q_GADGET
    QML_FOREIGN(auralis::core::UiRegistryImpl)
    QML_NAMED_ELEMENT(UiRegistry)
    QML_SINGLETON
public:
    static core::UiRegistryImpl* create(QQmlEngine*, QJSEngine*);
};

struct NavigatorForeign {
    Q_GADGET
    QML_FOREIGN(auralis::core::NavigatorImpl)
    QML_NAMED_ELEMENT(Navigator)
    QML_SINGLETON
public:
    static core::NavigatorImpl* create(QQmlEngine*, QJSEngine*);
};

struct DialogsForeign {
    Q_GADGET
    QML_FOREIGN(auralis::core::DialogsImpl)
    QML_NAMED_ELEMENT(Dialogs)
    QML_SINGLETON
public:
    static core::DialogsImpl* create(QQmlEngine*, QJSEngine*);
};

struct NotifierForeign {
    Q_GADGET
    QML_FOREIGN(auralis::core::NotifierImpl)
    QML_NAMED_ELEMENT(Notifier)
    QML_SINGLETON
public:
    static core::NotifierImpl* create(QQmlEngine*, QJSEngine*);
};

/// 設定頁的介面縮放（AC-19-4）：主程式在建立 QML 引擎前用 UiScaleController::setInstance 放好實例。
struct UiScaleForeign {
    Q_GADGET
    QML_FOREIGN(auralis::core::UiScaleController)
    QML_NAMED_ELEMENT(UiScale)
    QML_SINGLETON
public:
    static core::UiScaleController* create(QQmlEngine*, QJSEngine*);
};

} // namespace auralis::ui

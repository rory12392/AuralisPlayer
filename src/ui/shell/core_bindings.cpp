#include "shell/core_bindings.h"

#include "modules/core_services.h"
#include "shell/jobs_controller.h"

#include <QJSEngine>
#include <QQmlEngine>

namespace auralis::ui {

namespace {

core::CoreServices* g_services = nullptr;

template <typename T> T* owned(T* object) {
    Q_ASSERT_X(object, "bindCoreServices", "主程式要在建立 QML 引擎前呼叫 bindCoreServices");
    // 核心擁有這些物件，QML 的垃圾回收不能刪它們
    QJSEngine::setObjectOwnership(object, QJSEngine::CppOwnership);
    return object;
}

} // namespace

void bindCoreServices(core::CoreServices& services) {
    g_services = &services;
    if (!services.jobs.findChild<JobsController*>(QString(), Qt::FindDirectChildrenOnly)) {
        new JobsController(services.jobs, &services.jobs);
    }
}

namespace detail {
core::UiRegistryImpl* registry() {
    return g_services ? &g_services->ui : nullptr;
}
core::NavigatorImpl* navigator() {
    return g_services ? &g_services->navigator : nullptr;
}
core::DialogsImpl* dialogs() {
    return g_services ? &g_services->dialogs : nullptr;
}
core::NotifierImpl* notifier() {
    return g_services ? &g_services->notifier : nullptr;
}
JobsController* jobs() {
    return g_services
               ? g_services->jobs.findChild<JobsController*>(QString(), Qt::FindDirectChildrenOnly)
               : nullptr;
}
} // namespace detail

core::UiRegistryImpl* UiRegistryForeign::create(QQmlEngine*, QJSEngine*) {
    return owned(detail::registry());
}
core::NavigatorImpl* NavigatorForeign::create(QQmlEngine*, QJSEngine*) {
    return owned(detail::navigator());
}
core::DialogsImpl* DialogsForeign::create(QQmlEngine*, QJSEngine*) {
    return owned(detail::dialogs());
}
core::NotifierImpl* NotifierForeign::create(QQmlEngine*, QJSEngine*) {
    return owned(detail::notifier());
}

} // namespace auralis::ui

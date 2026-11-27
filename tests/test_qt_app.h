#pragma once

// 測試共用：需要事件迴圈或 Qt 物件的測試，第一次用到時建立應用程式物件。

#include <QCoreApplication>

namespace auralis::test {

/// 還沒有應用程式物件時建一個 App（QCoreApplication 或 QGuiApplication）；
/// 整個測試行程共用、不釋放。name 是 argv[0]（字串常值，壽命到行程結束）。
template <typename App = QCoreApplication> void ensureQtApp(const char* name) {
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char* argv[] = {const_cast<char*>(name), nullptr};
        new App(argc, argv);
    }
}

} // namespace auralis::test

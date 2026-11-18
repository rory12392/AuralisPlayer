// 需要 QGuiApplication 的 Catch2 測試（QStyleHints、字型）用這個 main。
#include <catch2/catch_session.hpp>

#include <QGuiApplication>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}

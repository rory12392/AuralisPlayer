#include "cue_fixtures.h"

#include <QFile>

#include <stdexcept>
#include <string>

#include <windows.h>

namespace auralis::test {

QByteArray cueFixture(const QString& name) {
    QFile file(QString::fromUtf8(AURALIS_FIXTURES) + QStringLiteral("/cue/") + name);
    if (!file.open(QIODevice::ReadOnly)) {
        throw std::runtime_error("讀不到樣本：" + name.toStdString());
    }
    return file.readAll();
}

QByteArray encodeCodePage(const QByteArray& utf8, unsigned codePage) {
    const std::wstring wide = QString::fromUtf8(utf8).toStdWString();
    const auto size = static_cast<int>(wide.size());
    BOOL usedDefault = FALSE;
    const int length = WideCharToMultiByte(codePage, WC_NO_BEST_FIT_CHARS, wide.data(), size,
                                           nullptr, 0, nullptr, &usedDefault);
    QByteArray bytes(length, '\0');
    WideCharToMultiByte(codePage, WC_NO_BEST_FIT_CHARS, wide.data(), size, bytes.data(), length,
                        nullptr, &usedDefault);
    if (length == 0 || usedDefault) {
        throw std::runtime_error("樣本有字元無法轉成字碼頁 " + std::to_string(codePage));
    }
    return bytes;
}

} // namespace auralis::test

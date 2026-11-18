#pragma once

// DSD 音源測試共用：開檔、讀到結尾、測試自己的對照排列（不用被測程式算預期值）。

#include "dsd_source.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QList>

#include <memory>

namespace auralis::test {

inline Result<std::unique_ptr<IAudioSource>> openDsd(const QString& path) {
    mod::out::DsdSourceFactory factory;
    return factory.open(path);
}

inline std::unique_ptr<IAudioSource> openDsdOk(const QString& path) {
    auto opened = openDsd(path);
    if (!opened) {
        FAIL("開不了：" << opened.error().message.toStdString() << "（"
                        << opened.error().call.toStdString() << "）");
    }
    return std::move(opened).value();
}

/// 每次最多讀 step 個樣本框，讀到結尾；讀取回傳錯誤時測試失敗。
inline QByteArray readAll(IAudioSource& source, qint64 step = 777) {
    const int frameBytes = source.format().bytesPerFrame();
    QByteArray all;
    QByteArray buffer(static_cast<qsizetype>(step * frameBytes), '\0');
    for (;;) {
        const auto got = source.read(reinterpret_cast<std::byte*>(buffer.data()), step);
        REQUIRE(got.hasValue());
        REQUIRE(got.value() <= step);
        if (got.value() == 0) {
            return all;
        }
        all.append(buffer.constData(), static_cast<qsizetype>(got.value() * frameBytes));
    }
}

/// 位元順序反轉（逐位元搬，和被測程式的查表無關）。
inline char reverseBits(char value) {
    unsigned out = 0;
    for (int bit = 0; bit < 8; ++bit) {
        if ((static_cast<unsigned char>(value) >> bit) & 1u) {
            out |= 0x80u >> bit;
        }
    }
    return static_cast<char>(out);
}

/// 各聲道的位元組（時間順序）→ 核心約定的排列：每聲道 4 位元組一組、聲道交錯。
inline QByteArray interleaveFrames(const QList<QByteArray>& channels, qsizetype frames,
                                   bool reverse) {
    QByteArray out;
    for (qsizetype f = 0; f < frames; ++f) {
        for (const QByteArray& channel : channels) {
            for (int k = 0; k < 4; ++k) {
                const char byte = channel[f * 4 + k];
                out.append(reverse ? reverseBits(byte) : byte);
            }
        }
    }
    return out;
}

/// 各聲道的位元組 → DFF 檔案裡的排列（各聲道以位元組輪流）。
inline QByteArray dffInterleave(const QList<QByteArray>& channels) {
    QByteArray out;
    for (qsizetype i = 0; i < channels.first().size(); ++i) {
        for (const QByteArray& channel : channels) {
            out.append(channel[i]);
        }
    }
    return out;
}

} // namespace auralis::test

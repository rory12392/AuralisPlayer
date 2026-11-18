// 無鎖環形緩衝：容量、繞回、單一生產者與單一消費者同時跑時資料順序不亂。
#include "audio/ring_buffer.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <vector>

using auralis::core::RingBuffer;

TEST_CASE("環形緩衝：容量取 2 的冪次，滿了寫不進、空了讀不出") {
    RingBuffer ring(100);
    REQUIRE(ring.capacity() == 128);
    std::vector<std::byte> data(200, std::byte{7});
    CHECK(ring.write(data.data(), 200) == 128);
    CHECK(ring.writable() == 0);
    CHECK(ring.read(data.data(), 50) == 50);
    CHECK(ring.readable() == 78);
    CHECK(ring.read(data.data(), 200) == 78);
    CHECK(ring.read(data.data(), 1) == 0);
}

TEST_CASE("環形緩衝：跨過結尾繞回時內容不變") {
    RingBuffer ring(16);
    std::vector<std::byte> in(10), out(10);
    for (int round = 0; round < 7; ++round) {
        for (int i = 0; i < 10; ++i) {
            in[i] = std::byte(round * 10 + i);
        }
        REQUIRE(ring.write(in.data(), 10) == 10);
        REQUIRE(ring.read(out.data(), 10) == 10);
        REQUIRE(out == in);
    }
}

TEST_CASE("環形緩衝：生產者與消費者兩條執行緒同時跑，收到的位元組順序與送出的相同") {
    RingBuffer ring(4096);
    constexpr std::size_t kTotal = 8 * 1024 * 1024;
    std::atomic<bool> ok{true};
    std::thread consumer([&] {
        std::vector<std::byte> buffer(777);
        std::size_t received = 0;
        while (received < kTotal) {
            const std::size_t n = ring.read(buffer.data(), buffer.size());
            for (std::size_t i = 0; i < n; ++i) {
                if (buffer[i] != std::byte((received + i) % 251)) {
                    ok = false;
                }
            }
            received += n;
        }
    });
    std::vector<std::byte> chunk(1000);
    std::size_t sent = 0;
    while (sent < kTotal) {
        const std::size_t want = chunk.size() < kTotal - sent ? chunk.size() : kTotal - sent;
        for (std::size_t i = 0; i < want; ++i) {
            chunk[i] = std::byte((sent + i) % 251);
        }
        std::size_t done = 0;
        while (done < want) {
            done += ring.write(chunk.data() + done, want - done);
        }
        sent += want;
    }
    consumer.join();
    CHECK(ok);
}

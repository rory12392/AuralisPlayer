// 進階輸出的建置依賴（T-46）：只確認函式庫找得到、能編譯與連結，不測任何功能
#include <catch2/catch_test_macros.hpp>

#include <ebur128.h>

#if AURALIS_WITH_ASIO
// asio.h 依賴 asiosys.h 先定義平台巨集
#include <asiosys.h>

#include <asio.h>
#endif

TEST_CASE("T-46：libebur128 可以連結，建立狀態後釋放") {
    ebur128_state* state = ebur128_init(2, 44100, EBUR128_MODE_I);
    REQUIRE(state != nullptr);
    ebur128_destroy(&state);
    REQUIRE(state == nullptr);
}

#if AURALIS_WITH_ASIO
TEST_CASE("T-46：AURALIS_WITH_ASIO 開啟時 ASIO SDK 的標頭可以編譯") {
    ASIODriverInfo info{};
    REQUIRE(sizeof(info.name) > 0);
}
#endif

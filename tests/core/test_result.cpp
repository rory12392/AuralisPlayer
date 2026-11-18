// Result：暫存物件的 value() 回傳值，不回傳參照（T-96，P-43／P-PL-28）。
#include <auralis/result.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

using auralis::Result;

// 編譯期測試：暫存（右值）的 value() 是值型別，range-for 走它不會懸空；左值版仍回傳參照。
static_assert(
    std::is_same_v<decltype(std::declval<Result<std::vector<int>>>().value()), std::vector<int>>,
    "Result<T>::value() && 要回傳 T（值），不能回傳參照");
static_assert(
    std::is_same_v<decltype(std::declval<Result<std::vector<int>>&>().value()), std::vector<int>&>,
    "左值 Result<T>::value() 回傳 T&");
static_assert(std::is_same_v<decltype(std::declval<const Result<std::vector<int>>&>().value()),
                             const std::vector<int>&>,
              "const 左值 Result<T>::value() 回傳 const T&");

namespace {

Result<std::vector<int>> makeResult() {
    return std::vector<int>{1, 2, 3, 4};
}

} // namespace

TEST_CASE("T-96：range-for 直接走暫存 Result 的 value() 走得完、內容正確", "[result]") {
    std::vector<int> seen;
    for (int x : makeResult().value()) {
        seen.push_back(x);
    }
    CHECK(seen == std::vector<int>{1, 2, 3, 4});
}

TEST_CASE("T-96：std::move(r).value() 照常取出只能移動的值", "[result]") {
    Result<std::unique_ptr<int>> r(std::make_unique<int>(7));
    std::unique_ptr<int> p = std::move(r).value();
    REQUIRE(p);
    CHECK(*p == 7);
}

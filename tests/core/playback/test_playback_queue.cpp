// 播放佇列的順序規則（AC-11-2）：接著播放、加到最後、重複（單曲／全部）、隨機播放，
// 以及移出、排序、跳過開不了的曲目。
#include "playback/playback_queue.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using auralis::QueueEntry;
using auralis::TrackRef;
using auralis::core::PlaybackQueue;
using RepeatMode = auralis::PlaybackService::RepeatMode;

namespace {

QList<TrackRef> tracks(std::initializer_list<const char*> names) {
    QList<TrackRef> list;
    for (const char* name : names) {
        list.append(TrackRef{QString::fromLatin1(name), 0});
    }
    return list;
}

/// 佇列的路徑，依播放順序。
QStringList order(const PlaybackQueue& queue) {
    QStringList names;
    for (const QueueEntry& entry : queue.entries()) {
        names.append(entry.track.path);
    }
    return names;
}

QString path(const std::optional<QueueEntry>& entry) {
    return entry ? entry->track.path : QStringLiteral("（沒有）");
}

/// 從目前曲目開始，自動接續最多 count 首的路徑。
QStringList playOrder(const PlaybackQueue& queue, int count) {
    QStringList names;
    auto entry = queue.current();
    for (int i = 0; entry && i < count; ++i) {
        names.append(entry->track.path);
        entry = queue.nextAfter(entry->id, true);
    }
    return names;
}

} // namespace

TEST_CASE("AC-11-2：接著播放插在目前曲目之後，多首保持原本的順序") {
    PlaybackQueue queue(1);
    queue.replace(tracks({"a", "b", "c"}), 1);
    queue.insertNext(tracks({"x", "y"}));
    CHECK(order(queue) == QStringList{"a", "b", "x", "y", "c"});
    CHECK(path(queue.current()) == "b");
    CHECK(path(queue.nextAfter(queue.current()->id, true)) == "x");
}

TEST_CASE("AC-11-2：沒有目前曲目時接著播放插在最前面") {
    PlaybackQueue queue(1);
    queue.append(tracks({"a"}));
    queue.insertNext(tracks({"x"}));
    CHECK(order(queue) == QStringList{"x", "a"});
}

TEST_CASE("AC-11-2：加到最後放在佇列尾端") {
    PlaybackQueue queue(1);
    queue.replace(tracks({"a", "b"}), 0);
    queue.append(tracks({"z"}));
    CHECK(order(queue) == QStringList{"a", "b", "z"});
    CHECK(playOrder(queue, 10) == QStringList{"a", "b", "z"});
}

TEST_CASE("AC-11-2：重複關閉時最後一首之後沒有下一首") {
    PlaybackQueue queue(1);
    queue.replace(tracks({"a", "b"}), 1);
    CHECK_FALSE(queue.nextAfter(queue.current()->id, true));
    CHECK_FALSE(queue.nextAfter(queue.current()->id, false));
}

TEST_CASE("AC-11-2：重複全部時最後一首之後回到第一首，上一首也會繞回") {
    PlaybackQueue queue(1);
    queue.replace(tracks({"a", "b", "c"}), 0);
    queue.setRepeat(RepeatMode::All);
    CHECK(playOrder(queue, 7) == QStringList{"a", "b", "c", "a", "b", "c", "a"});
    CHECK(path(queue.previousBefore(queue.current()->id)) == "c");
}

TEST_CASE("AC-11-2：重複單曲時自動接續同一首，按下一首仍然換到下一首") {
    PlaybackQueue queue(1);
    queue.replace(tracks({"a", "b"}), 0);
    queue.setRepeat(RepeatMode::One);
    const quint64 current = queue.current()->id;
    CHECK(queue.nextAfter(current, true)->id == current);
    CHECK(path(queue.nextAfter(current, false)) == "b");
}

TEST_CASE("AC-11-2：隨機播放只打亂目前曲目之後的，關掉時回到原本的順序") {
    QList<TrackRef> many;
    QStringList names;
    for (int i = 0; i < 20; ++i) {
        names.append(QStringLiteral("t%1").arg(i, 2, 10, QLatin1Char('0')));
        many.append(TrackRef{names.last(), 0});
    }
    PlaybackQueue queue(7);
    queue.replace(many, 5);
    queue.setShuffle(true);
    const QStringList shuffled = order(queue);
    CHECK(shuffled.mid(0, 6) == names.mid(0, 6));
    CHECK(shuffled != names);
    QStringList sorted = shuffled;
    sorted.sort();
    CHECK(sorted == names);
    CHECK(path(queue.current()) == "t05");

    queue.setShuffle(false);
    CHECK(order(queue) == names);
    CHECK(path(queue.current()) == "t05");
}

TEST_CASE("AC-11-2：隨機播放時從選的那首開始，其餘打亂；同一個 seed 順序相同") {
    QList<TrackRef> many;
    for (int i = 0; i < 12; ++i) {
        many.append(TrackRef{QStringLiteral("t%1").arg(i), 0});
    }
    PlaybackQueue first(3);
    first.setShuffle(true);
    first.replace(many, 4);
    PlaybackQueue second(3);
    second.setShuffle(true);
    second.replace(many, 4);
    CHECK(order(first).first() == "t4");
    CHECK(first.currentIndex() == 0);
    CHECK(order(first) == order(second));
    CHECK(playOrder(first, 20).size() == 12);
}

TEST_CASE("AC-11-2：隨機播放中接著播放與加到最後，關掉隨機後仍在對的位置") {
    PlaybackQueue queue(11);
    queue.replace(tracks({"a", "b", "c", "d"}), 0);
    queue.setShuffle(true);
    queue.insertNext(tracks({"x"}));
    queue.append(tracks({"z"}));
    CHECK(path(queue.nextAfter(queue.current()->id, true)) == "x");
    CHECK(order(queue).last() == "z");
    queue.setShuffle(false);
    CHECK(order(queue) == QStringList{"a", "x", "b", "c", "d", "z"});
}

TEST_CASE("佇列：移出、排序與清空只留目前曲目") {
    PlaybackQueue queue(1);
    queue.replace(tracks({"a", "b", "c", "d"}), 1);
    const QList<QueueEntry> entries = queue.entries();
    CHECK(queue.remove({entries[2].id}) == 1);
    CHECK(order(queue) == QStringList{"a", "b", "d"});
    CHECK(queue.move(2, 0));
    CHECK(order(queue) == QStringList{"d", "a", "b"});
    CHECK(queue.move(0, 2));
    CHECK(order(queue) == QStringList{"a", "b", "d"});
    CHECK_FALSE(queue.move(0, 5));
    queue.clearExceptCurrent();
    CHECK(order(queue) == QStringList{"b"});
    CHECK(path(queue.current()) == "b");
    CHECK(queue.remove({queue.current()->id}) == 1);
    CHECK_FALSE(queue.current());
}

TEST_CASE("AC-01-3：自動接續跳過開不了的曲目；重複單曲的那首開不了時沒有下一首") {
    PlaybackQueue queue(1);
    queue.replace(tracks({"a", "bad", "c"}), 0);
    const QList<QueueEntry> entries = queue.entries();
    queue.setUnplayable(entries[1].id, true);
    CHECK(playOrder(queue, 5) == QStringList{"a", "c"});
    CHECK(path(queue.previousBefore(entries[2].id)) == "a");
    queue.setRepeat(RepeatMode::One);
    queue.setUnplayable(entries[0].id, true);
    CHECK_FALSE(queue.nextAfter(entries[0].id, true));
}

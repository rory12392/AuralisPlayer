// 播放清單儲存（T-50）：建立、改名、刪除與重新啟動後保留（AC-11-1）、刪除只影響播放清單（AC-11-3）、
// 同一首加兩次兩筆都保留（規格 §1 情況 1）、重排一次交易寫完（情況 5）。
#include "pl_test_helpers.h"
#include "playlist_store.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>

using auralis::TrackRef;
using auralis::mod::pl::PlaylistStore;
using auralis::test::PlDatabase;

namespace {

QList<TrackRef> refs(std::initializer_list<const char*> names) {
    QList<TrackRef> result;
    for (const char* name : names) {
        result.append({QStringLiteral("D:/音樂/%1.flac").arg(QString::fromUtf8(name)), 0});
    }
    return result;
}

QStringList paths(PlaylistStore& store, qint64 id) {
    const auto items = store.items(id);
    REQUIRE(items.hasValue());
    QStringList result;
    int expectedPos = 0;
    for (const auto& item : items.value()) {
        CHECK(item.pos == expectedPos++);
        result.append(item.track.path.section(QLatin1Char('/'), -1));
    }
    return result;
}

/// 把呼叫轉給真的資料庫，第 failAt 次 execute 起回傳錯誤（中途失敗的交易要整個還原）。
class FailingDatabase final : public auralis::ModuleDatabase {
public:
    explicit FailingDatabase(auralis::ModuleDatabase& inner) : m_inner(inner) {}
    int failAt = -1;
    int executed = 0;

    auralis::Result<qint64> execute(const QString& sql, const QVariantList& params) override {
        if (failAt >= 0 && ++executed >= failAt) {
            return auralis::Error{QStringLiteral("注入的寫入失敗")};
        }
        return m_inner.execute(sql, params);
    }
    auralis::Result<QList<Row>> query(const QString& sql, const QVariantList& params) override {
        return m_inner.query(sql, params);
    }
    auralis::Result<bool> transaction(const std::function<bool()>& work) override {
        return m_inner.transaction(work);
    }

private:
    auralis::ModuleDatabase& m_inner;
};

} // namespace

TEST_CASE("AC-11-1：建立、重新命名、刪除播放清單，重新啟動後保留") {
    PlDatabase db;
    {
        PlaylistStore store(*db.db);
        const auto commute = store.create(QStringLiteral("通勤"));
        const auto night = store.create(QStringLiteral("  深夜  "));
        const auto gone = store.create(QStringLiteral("要刪的"));
        REQUIRE(commute.hasValue());
        REQUIRE(night.hasValue());
        REQUIRE(gone.hasValue());
        REQUIRE(store.addTracks(commute.value(), refs({"a", "b"})).value());
        REQUIRE(store.rename(night.value(), QStringLiteral("深夜電台")).value());
        REQUIRE(store.remove(gone.value()).value());
    }
    db.reopen();
    PlaylistStore store(*db.db);
    const auto list = store.list();
    REQUIRE(list.hasValue());
    REQUIRE(list.value().size() == 2);
    CHECK(list.value().at(0).name == QStringLiteral("通勤"));
    CHECK(list.value().at(0).trackCount == 2);
    CHECK(list.value().at(1).name == QStringLiteral("深夜電台"));
    CHECK(list.value().at(1).trackCount == 0);
    CHECK(paths(store, list.value().at(0).id) == QStringList{"a.flac", "b.flac"});
}

TEST_CASE("AC-11-1：空白名稱不能建立也不能改名，不存在的播放清單回傳 false") {
    PlDatabase db;
    PlaylistStore store(*db.db);
    CHECK_FALSE(store.create(QStringLiteral("   ")).hasValue());
    const auto id = store.create(QStringLiteral("最愛"));
    REQUIRE(id.hasValue());
    CHECK_FALSE(store.rename(id.value(), QString()).hasValue());
    CHECK(store.playlist(id.value()).value()->name == QStringLiteral("最愛"));
    CHECK_FALSE(store.rename(id.value() + 100, QStringLiteral("x")).value());
    CHECK_FALSE(store.remove(id.value() + 100).value());
    CHECK_FALSE(store.addTracks(id.value() + 100, refs({"a"})).value());
    CHECK_FALSE(store.playlist(id.value() + 100).value().has_value());
}

TEST_CASE("AC-11-3：刪除播放清單只刪清單與它的項目，不動音樂檔與其他清單") {
    PlDatabase db;
    // 播放清單指到的音樂檔（測試暫存裡的假檔）：刪除前後內容與修改時間不變
    const QString music = db.dir.filePath(QStringLiteral("曲目.flac"));
    {
        QFile file(music);
        REQUIRE(file.open(QIODevice::WriteOnly));
        file.write("fLaC 測試內容");
    }
    const auto hash = [&] {
        QFile file(music);
        REQUIRE(file.open(QIODevice::ReadOnly));
        return QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256);
    };
    const QByteArray before = hash();
    const QDateTime modified = QFileInfo(music).lastModified();

    PlaylistStore store(*db.db);
    const qint64 a = store.create(QStringLiteral("A")).value();
    const qint64 b = store.create(QStringLiteral("B")).value();
    REQUIRE(store.addTracks(a, {{music, 0}, {music, 0}}).value());
    REQUIRE(store.addTracks(b, {{music, 0}}).value());

    REQUIRE(store.remove(a).value());
    CHECK(QFile::exists(music));
    CHECK(hash() == before);
    CHECK(QFileInfo(music).lastModified() == modified);
    CHECK_FALSE(store.playlist(a).value().has_value());
    CHECK(store.items(a).value().isEmpty());
    CHECK(store.items(b).value().size() == 1);
}

TEST_CASE("情況 1：同一首加兩次兩筆都保留，順序不變；身分與 path_key 照存") {
    PlDatabase db;
    PlaylistStore store(*db.db);
    const qint64 id = store.create(QStringLiteral("重複")).value();
    REQUIRE(store.addTracks(id, refs({"a", "b"})).value());
    REQUIRE(store.addTracks(id, refs({"a"})).value());
    CHECK(paths(store, id) == QStringList{"a.flac", "b.flac", "a.flac"});

    // CUE 曲目：同一個 .cue 的不同軌是不同的身分
    REQUIRE(store.addTracks(id, {{QStringLiteral("D:/音樂/專輯.cue"), 2}}).value());
    const auto items = store.items(id).value();
    CHECK(items.last().track.subsong == 2u);
    const auto keys = db.db->query(QStringLiteral("SELECT path_key FROM playlist_items WHERE "
                                                  "playlist_id = ? ORDER BY pos"),
                                   {id});
    REQUIRE(keys.hasValue());
    CHECK(keys.value().last().first().toString() == QStringLiteral("d:\\音樂\\專輯.cue"));
}

TEST_CASE("加入曲目可以指定插入位置；移除後 pos 維持連續") {
    PlDatabase db;
    PlaylistStore store(*db.db);
    const qint64 id = store.create(QStringLiteral("插入")).value();
    REQUIRE(store.addTracks(id, refs({"a", "d"})).value());
    REQUIRE(store.addTracks(id, refs({"b", "c"}), 1).value());
    CHECK(paths(store, id) == QStringList{"a.flac", "b.flac", "c.flac", "d.flac"});
    REQUIRE(store.addTracks(id, refs({"z"}), 0).value());
    CHECK(paths(store, id) == QStringList{"z.flac", "a.flac", "b.flac", "c.flac", "d.flac"});
    REQUIRE(store.addTracks(id, refs({"e"}), 99).value());
    CHECK(paths(store, id).last() == QStringLiteral("e.flac"));

    REQUIRE(store.removeItems(id, {0, 2, 2, 42}).value());
    CHECK(paths(store, id) == QStringList{"a.flac", "c.flac", "d.flac", "e.flac"});
}

TEST_CASE("情況 5：重排是一次交易，不合法的順序或中途失敗都不會留下半套") {
    PlDatabase db;
    PlaylistStore store(*db.db);
    const qint64 id = store.create(QStringLiteral("排序")).value();
    REQUIRE(store.addTracks(id, refs({"a", "b", "c", "d"})).value());

    REQUIRE(store.reorder(id, {3, 0, 1, 2}).value());
    CHECK(paths(store, id) == QStringList{"d.flac", "a.flac", "b.flac", "c.flac"});

    // 不是排列（重複、缺號、長度不對）：不改
    CHECK_FALSE(store.reorder(id, {0, 0, 1, 2}).hasValue());
    CHECK_FALSE(store.reorder(id, {0, 1, 2}).hasValue());
    CHECK(paths(store, id) == QStringList{"d.flac", "a.flac", "b.flac", "c.flac"});

    // 寫到第 3 句（刪除舊項目、寫入兩筆之後）失敗：整個交易還原，清單和重排前一樣
    FailingDatabase failing(*db.db);
    failing.failAt = 3;
    PlaylistStore broken(failing);
    CHECK_FALSE(broken.reorder(id, {1, 2, 3, 0}).hasValue());
    CHECK(paths(store, id) == QStringList{"d.flac", "a.flac", "b.flac", "c.flac"});

    // 加入到中間時中途失敗也一樣
    failing.executed = 0;
    CHECK_FALSE(broken.addTracks(id, refs({"x", "y"}), 1).hasValue());
    CHECK(paths(store, id) == QStringList{"d.flac", "a.flac", "b.flac", "c.flac"});
}

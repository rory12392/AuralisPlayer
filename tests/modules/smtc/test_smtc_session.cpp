// 系統媒體控制：真的向 Windows 取得 SMTC（隱藏的測試視窗），確認模組註冊、狀態、結束清除與
// 失敗時的錯誤邊界。WinRT 標頭要在 Qt 標頭之前（Qt 的 signals、emit 巨集）。
#include <unknwn.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>

#include <SystemMediaTransportControlsInterop.h>

#include "smtc_session.h"

#include "modules/core_services.h"
#include "modules/module_manager.h"

#include <catch2/catch_test_macros.hpp>

#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QSettings>

#include <algorithm>
#include <thread>

using auralis::core::CoreServices;
using auralis::core::ModuleManager;
using auralis::core::ModuleStatus;
using auralis::mod::smtc::SmtcSession;
using auralis::mod::smtc::SmtcStatus;

namespace wm = winrt::Windows::Media;

namespace {

/// 不顯示的頂層視窗，給 SMTC 一個 HWND（主程式用的是主視窗）。
class HiddenWindow {
public:
    HiddenWindow()
        : m_hwnd(CreateWindowExW(0, L"STATIC", L"AuralisSmtcTest", WS_OVERLAPPED, 0, 0, 100, 100,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr)) {
        REQUIRE(m_hwnd != nullptr);
    }
    ~HiddenWindow() { DestroyWindow(m_hwnd); }
    HiddenWindow(const HiddenWindow&) = delete;
    HiddenWindow& operator=(const HiddenWindow&) = delete;
    quintptr id() const { return reinterpret_cast<quintptr>(m_hwnd); }

private:
    HWND m_hwnd;
};

/// 從外面再取一次同一個視窗的 SMTC（系統對同一個視窗回傳同一份），讀模組設定的結果。
wm::SystemMediaTransportControls controlsOf(quintptr hwnd) {
    const auto interop = winrt::get_activation_factory<wm::SystemMediaTransportControls,
                                                       ISystemMediaTransportControlsInterop>();
    wm::SystemMediaTransportControls controls{nullptr};
    winrt::check_hresult(interop->GetForWindow(reinterpret_cast<HWND>(hwnd),
                                               winrt::guid_of<wm::SystemMediaTransportControls>(),
                                               winrt::put_abi(controls)));
    return controls;
}

QString freshDir(const QString& name) {
    const QString dir = QStringLiteral(AURALIS_TEST_TMP "/t-80/") + name;
    QDir(dir).removeRecursively();
    QDir().mkpath(dir);
    return dir;
}

ModuleStatus::State smtcState(const ModuleManager& manager) {
    const auto& statuses = manager.statuses();
    const auto it = std::find_if(statuses.begin(), statuses.end(), [](const ModuleStatus& s) {
        return s.code == QStringLiteral("SMTC");
    });
    REQUIRE(it != statuses.end());
    return it->state;
}

} // namespace

TEST_CASE("AC-18-1：模組在主視窗註冊 SMTC，啟用播放、暫停、上一首、下一首，狀態跟著播放") {
    HiddenWindow window;
    CoreServices services;
    services.platformWindow.setHwnd(window.id());
    ModuleManager manager(services, freshDir(QStringLiteral("register")) + "/settings.ini");
    manager.startAll();
    REQUIRE(smtcState(manager) == ModuleStatus::State::Started);

    const auto controls = controlsOf(window.id());
    CHECK(controls.IsEnabled());
    CHECK(controls.IsPlayEnabled());
    CHECK(controls.IsPauseEnabled());
    CHECK(controls.IsNextEnabled());
    CHECK(controls.IsPreviousEnabled());
    CHECK(controls.PlaybackStatus() == wm::MediaPlaybackStatus::Stopped);
    CHECK(services.notifier.current().isEmpty());
    manager.stopAll();
}

TEST_CASE("AC-18-1：SMTC 包裝類別設定的狀態就是系統顯示的狀態") {
    HiddenWindow window;
    auto session = SmtcSession::create(window.id(), [](auto) {});
    REQUIRE(session.hasValue());
    const auto controls = controlsOf(window.id());
    session.value()->setStatus(SmtcStatus::Playing);
    CHECK(controls.PlaybackStatus() == wm::MediaPlaybackStatus::Playing);
    session.value()->setStatus(SmtcStatus::Paused);
    CHECK(controls.PlaybackStatus() == wm::MediaPlaybackStatus::Paused);
    session.value()->setStatus(SmtcStatus::Stopped);
    CHECK(controls.PlaybackStatus() == wm::MediaPlaybackStatus::Stopped);
}

namespace {

/// 系統目前顯示的縮圖內容；沒有縮圖時是空的。
QByteArray thumbnailBytes(const wm::SystemMediaTransportControls& controls) {
    const auto reference = controls.DisplayUpdater().Thumbnail();
    if (!reference) {
        return {};
    }
    const auto stream = reference.OpenReadAsync().get();
    const auto size = static_cast<quint32>(stream.Size());
    winrt::Windows::Storage::Streams::Buffer buffer(size);
    const auto read =
        stream.ReadAsync(buffer, size, winrt::Windows::Storage::Streams::InputStreamOptions::None)
            .get();
    return QByteArray(reinterpret_cast<const char*>(read.data()), static_cast<int>(read.Length()));
}

QString text(const winrt::hstring& value) {
    return QString::fromWCharArray(value.c_str());
}

/// 真的 PNG（單色），尺寸不同才分得出是哪一張。
QByteArray pngOf(int width, int height, Qt::GlobalColor color) {
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    REQUIRE(image.save(&buffer, "PNG"));
    return bytes;
}

/// 經 SMTC 讀回的縮圖要能解碼成影像，回傳影像（解不開時是空的 QImage）。
QImage thumbnailImage(const wm::SystemMediaTransportControls& controls) {
    QImage image;
    image.loadFromData(thumbnailBytes(controls), "PNG");
    return image;
}

} // namespace

TEST_CASE("AC-18-2：SMTC 顯示曲名、藝人、專輯與縮圖，換曲時換成新的") {
    HiddenWindow window;
    auto session = SmtcSession::create(window.id(), [](auto) {});
    REQUIRE(session.hasValue());
    const auto controls = controlsOf(window.id());

    const QByteArray coverA = pngOf(40, 30, Qt::red);
    const QByteArray coverB = pngOf(20, 10, Qt::blue);
    session.value()->setTrack(QStringLiteral("晴天"), QStringLiteral("周杰倫"),
                              QStringLiteral("葉惠美"), coverA);
    const auto display = controls.DisplayUpdater();
    CHECK(display.Type() == wm::MediaPlaybackType::Music);
    CHECK(text(display.MusicProperties().Title()) == QStringLiteral("晴天"));
    CHECK(text(display.MusicProperties().Artist()) == QStringLiteral("周杰倫"));
    CHECK(text(display.MusicProperties().AlbumTitle()) == QStringLiteral("葉惠美"));
    CHECK(thumbnailBytes(controls) == coverA);
    // 讀回的是能解碼的影像，不只是位元組相同
    const QImage shownA = thumbnailImage(controls);
    CHECK(shownA.size() == QSize(40, 30));
    CHECK(shownA.pixelColor(5, 5) == QColor(Qt::red));
    // 系統每次讀縮圖都從頭讀
    CHECK(thumbnailImage(controls).size() == QSize(40, 30));

    session.value()->setThumbnail(coverB);
    const QImage shownB = thumbnailImage(controls);
    CHECK(shownB.size() == QSize(20, 10));
    CHECK(shownB.pixelColor(5, 5) == QColor(Qt::blue));
    CHECK(text(display.MusicProperties().Title()) == QStringLiteral("晴天"));
}

TEST_CASE("SMTC 規格情況 1：換到沒有縮圖的曲目時不留上一首的封面") {
    HiddenWindow window;
    auto session = SmtcSession::create(window.id(), [](auto) {});
    REQUIRE(session.hasValue());
    const auto controls = controlsOf(window.id());
    session.value()->setTrack(QStringLiteral("A"), {}, {}, QByteArray("COVER-A"));
    session.value()->setTrack(QStringLiteral("B"), {}, {}, QByteArray());
    CHECK(thumbnailBytes(controls).isEmpty());
    CHECK(text(controls.DisplayUpdater().MusicProperties().Title()) == QStringLiteral("B"));
}

TEST_CASE("佇列沒有曲目時清掉曲目資訊；關閉後再設不會出錯") {
    HiddenWindow window;
    auto session = SmtcSession::create(window.id(), [](auto) {});
    REQUIRE(session.hasValue());
    const auto controls = controlsOf(window.id());
    session.value()->setTrack(QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"),
                              QByteArray("COVER"));
    session.value()->clearTrack();
    // ClearAll 把類型清回 Unknown（這時讀 MusicProperties 會丟例外），曲目資訊整個不顯示
    CHECK(controls.DisplayUpdater().Type() == wm::MediaPlaybackType::Unknown);
    CHECK(thumbnailBytes(controls).isEmpty());

    session.value()->close();
    REQUIRE_NOTHROW(session.value()->setTrack(QStringLiteral("X"), {}, {}, QByteArray("Y")));
    REQUIRE_NOTHROW(session.value()->setThumbnail(QByteArray("Y")));
    REQUIRE_NOTHROW(session.value()->clearTrack());
}

TEST_CASE("SMTC 規格情況 2：程式結束時清除 SMTC，不殘留「播放中」") {
    HiddenWindow window;
    CoreServices services;
    services.platformWindow.setHwnd(window.id());
    ModuleManager manager(services, freshDir(QStringLiteral("close")) + "/settings.ini");
    manager.startAll();
    REQUIRE(smtcState(manager) == ModuleStatus::State::Started);
    const auto controls = controlsOf(window.id());
    controls.PlaybackStatus(wm::MediaPlaybackStatus::Playing);

    manager.stopAll();
    CHECK(controls.PlaybackStatus() == wm::MediaPlaybackStatus::Closed);
    CHECK_FALSE(controls.IsEnabled());
}

TEST_CASE("錯誤邊界：沒有主視窗時不註冊 SMTC，模組照常啟動、不提示") {
    CoreServices services;
    ModuleManager manager(services, freshDir(QStringLiteral("no-window")) + "/settings.ini");
    REQUIRE_NOTHROW(manager.startAll());
    CHECK(smtcState(manager) == ModuleStatus::State::Started);
    CHECK(services.notifier.current().isEmpty());
    manager.stopAll();
}

TEST_CASE("錯誤邊界：取得 SMTC 失敗時模組照常啟動，通知列提示媒體鍵無法使用") {
    // 已經關掉的視窗：GetForWindow 失敗
    quintptr gone = 0;
    {
        HiddenWindow window;
        gone = window.id();
    }
    CoreServices services;
    services.platformWindow.setHwnd(gone);
    ModuleManager manager(services, freshDir(QStringLiteral("fail")) + "/settings.ini");
    REQUIRE_NOTHROW(manager.startAll());
    CHECK(smtcState(manager) == ModuleStatus::State::Started);
    CHECK(services.notifier.current()
              .value(QStringLiteral("text"))
              .toString()
              .contains(QStringLiteral("媒體鍵無法使用")));
    manager.stopAll();
}

TEST_CASE("執行緒約定：別的執行緒發出播放狀態時，SMTC 回到 UI 執行緒才更新；停止後才到的丟掉") {
    if (QCoreApplication::instance() == nullptr) {
        static int argc = 1;
        static char name[] = "auralis_smtc_tests";
        static char* argv[] = {name, nullptr};
        static QCoreApplication app(argc, argv);
    }
    HiddenWindow window;
    CoreServices services;
    services.platformWindow.setHwnd(window.id());
    ModuleManager manager(services, freshDir(QStringLiteral("thread")) + "/settings.ini");
    manager.startAll();
    REQUIRE(smtcState(manager) == ModuleStatus::State::Started);
    const auto controls = controlsOf(window.id());
    REQUIRE(controls.PlaybackStatus() == wm::MediaPlaybackStatus::Stopped);

    // 例：長時間播放測試在獨立執行緒播放，狀態訊號從那個執行緒發出
    const auto emitFromWorker = [&](auralis::PlaybackService::State state) {
        std::thread([&services, state] { emit services.playback.stateChanged(state); }).join();
    };
    emitFromWorker(auralis::PlaybackService::State::Playing);
    CHECK(controls.PlaybackStatus() == wm::MediaPlaybackStatus::Stopped);
    QCoreApplication::processEvents();
    CHECK(controls.PlaybackStatus() == wm::MediaPlaybackStatus::Playing);

    emitFromWorker(auralis::PlaybackService::State::Paused);
    manager.stopAll();
    QCoreApplication::processEvents();
    CHECK(controls.PlaybackStatus() == wm::MediaPlaybackStatus::Closed);
}

TEST_CASE("錯誤邊界：取得 SMTC 失敗時，錯誤寫出失敗的是 GetForWindow 那一步") {
    quintptr gone = 0;
    {
        HiddenWindow window;
        gone = window.id();
    }
    auto session = SmtcSession::create(gone, [](auto) {});
    REQUIRE_FALSE(session.hasValue());
    CHECK(session.error().call ==
          QStringLiteral("ISystemMediaTransportControlsInterop::GetForWindow"));
}

TEST_CASE("停用：modules/SMTC/enabled=false 時不註冊 SMTC") {
    HiddenWindow window;
    const QString ini = freshDir(QStringLiteral("off")) + "/settings.ini";
    {
        QSettings settings(ini, QSettings::IniFormat);
        settings.setValue(QStringLiteral("modules/SMTC/enabled"), false);
    }
    CoreServices services;
    services.platformWindow.setHwnd(window.id());
    ModuleManager manager(services, ini);
    REQUIRE_NOTHROW(manager.startAll());
    CHECK(smtcState(manager) == ModuleStatus::State::Disabled);
    // 新取得的 SMTC 預設 IsEnabled 就是 true，所以看模組才會打開的播放按鈕
    CHECK_FALSE(controlsOf(window.id()).IsPlayEnabled());
}

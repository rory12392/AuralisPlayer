// 系統媒體傳輸控制（SMTC）的 WinRT 包裝。
//
// WinRT 標頭一定要在任何 Qt 標頭之前：Qt 的 signals、slots、emit 是巨集，先定義會改到 WinRT
// 標頭的內容。unknwn.h 要在 WinRT 之前，C++/WinRT 才能和傳統 COM 介面（GetForWindow 的
// interop 介面）互通。
#include <unknwn.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>

#include <SystemMediaTransportControlsInterop.h>
#include <shcore.h>
#include <shlwapi.h>

#include "smtc_session.h"

#include <QString>

namespace auralis::mod::smtc {

Q_LOGGING_CATEGORY(lcSmtc, "auralis.smtc")

namespace {

namespace wm = winrt::Windows::Media;

SmtcButton buttonFrom(wm::SystemMediaTransportControlsButton button) noexcept {
    switch (button) {
    case wm::SystemMediaTransportControlsButton::Play:
        return SmtcButton::Play;
    case wm::SystemMediaTransportControlsButton::Pause:
        return SmtcButton::Pause;
    case wm::SystemMediaTransportControlsButton::Stop:
        return SmtcButton::Stop;
    case wm::SystemMediaTransportControlsButton::Next:
        return SmtcButton::Next;
    case wm::SystemMediaTransportControlsButton::Previous:
        return SmtcButton::Previous;
    default:
        break;
    }
    return SmtcButton::Other;
}

wm::MediaPlaybackStatus playbackStatusFrom(SmtcStatus status) noexcept {
    switch (status) {
    case SmtcStatus::Playing:
        return wm::MediaPlaybackStatus::Playing;
    case SmtcStatus::Paused:
        return wm::MediaPlaybackStatus::Paused;
    case SmtcStatus::Stopped:
        return wm::MediaPlaybackStatus::Stopped;
    case SmtcStatus::Closed:
        break;
    }
    return wm::MediaPlaybackStatus::Closed;
}

QString messageOf(const winrt::hresult_error& error) {
    return QString::fromWCharArray(error.message().c_str());
}

winrt::hstring toHstring(const QString& text) {
    return winrt::hstring(reinterpret_cast<const wchar_t*>(text.utf16()),
                          static_cast<winrt::hstring::size_type>(text.size()));
}

/// PNG 內容 → 縮圖的串流參考。用記憶體串流同步建立：StorageFile 只能非同步取得，UI 執行緒
/// （STA）不能阻塞等它。內容已在記憶體，系統每次讀縮圖都從複製的串流開頭讀。
winrt::Windows::Storage::Streams::RandomAccessStreamReference thumbnailFrom(const QByteArray& png) {
    winrt::com_ptr<IStream> memory;
    memory.attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(png.constData()),
                                    static_cast<UINT>(png.size())));
    if (!memory) {
        winrt::throw_hresult(E_OUTOFMEMORY);
    }
    winrt::Windows::Storage::Streams::IRandomAccessStream stream{nullptr};
    winrt::check_hresult(CreateRandomAccessStreamOverStream(
        memory.get(), BSOS_DEFAULT, winrt::guid_of<decltype(stream)>(), winrt::put_abi(stream)));
    return winrt::Windows::Storage::Streams::RandomAccessStreamReference::CreateFromStream(stream);
}

} // namespace

struct SmtcSession::Impl {
    wm::SystemMediaTransportControls controls{nullptr};
    winrt::event_token buttonToken{};
    bool closed = false;
};

Result<std::unique_ptr<SmtcSession>> SmtcSession::create(std::uintptr_t hwnd,
                                                         ButtonHandler onButton) {
    auto impl = std::make_unique<Impl>();
    // 失敗時錯誤寫出是哪一步
    const char* step = "get_activation_factory<ISystemMediaTransportControlsInterop>";
    try {
        // Qt 已在 UI 執行緒初始化 COM（STA），這裡不呼叫 winrt::init_apartment
        const auto interop = winrt::get_activation_factory<wm::SystemMediaTransportControls,
                                                           ISystemMediaTransportControlsInterop>();
        step = "ISystemMediaTransportControlsInterop::GetForWindow";
        winrt::check_hresult(interop->GetForWindow(
            reinterpret_cast<HWND>(hwnd), winrt::guid_of<wm::SystemMediaTransportControls>(),
            winrt::put_abi(impl->controls)));
        wm::SystemMediaTransportControls& controls = impl->controls;
        step = "啟用按鈕與設定狀態（IsPlayEnabled…IsEnabled）";
        controls.IsPlayEnabled(true);
        controls.IsPauseEnabled(true);
        controls.IsStopEnabled(true);
        controls.IsNextEnabled(true);
        controls.IsPreviousEnabled(true);
        controls.PlaybackStatus(wm::MediaPlaybackStatus::Stopped);
        controls.IsEnabled(true);
        // 回呼在 WinRT 的執行緒；呼叫端的處理函式負責轉回 UI 執行緒
        step = "註冊 ButtonPressed";
        impl->buttonToken = controls.ButtonPressed(
            [onButton = std::move(onButton)](
                const wm::SystemMediaTransportControls&,
                const wm::SystemMediaTransportControlsButtonPressedEventArgs& args) {
                onButton(buttonFrom(args.Button()));
            });
    } catch (const winrt::hresult_error& error) {
        return Error{messageOf(error), static_cast<long>(error.code()), QString::fromUtf8(step)};
    }
    return std::unique_ptr<SmtcSession>(new SmtcSession(std::move(impl)));
}

SmtcSession::SmtcSession(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

SmtcSession::~SmtcSession() {
    close();
}

void SmtcSession::setStatus(SmtcStatus status) noexcept {
    if (m_impl->closed) {
        return;
    }
    try {
        m_impl->controls.PlaybackStatus(playbackStatusFrom(status));
    } catch (const winrt::hresult_error& error) {
        qCWarning(lcSmtc).noquote() << "SMTC 播放狀態更新失敗：" << messageOf(error);
    }
}

void SmtcSession::setTrack(const QString& title, const QString& artist, const QString& album,
                           const QByteArray& thumbnailPng) noexcept {
    if (m_impl->closed) {
        return;
    }
    try {
        wm::SystemMediaTransportControlsDisplayUpdater display = m_impl->controls.DisplayUpdater();
        display.Type(wm::MediaPlaybackType::Music);
        wm::MusicDisplayProperties music = display.MusicProperties();
        music.Title(toHstring(title));
        music.Artist(toHstring(artist));
        music.AlbumTitle(toHstring(album));
        // 縮圖先設成 nullptr 再放新的：新曲目沒有縮圖時也不留上一首的封面
        display.Thumbnail(nullptr);
        if (!thumbnailPng.isEmpty()) {
            try {
                display.Thumbnail(thumbnailFrom(thumbnailPng));
            } catch (const winrt::hresult_error& error) {
                // 縮圖放不上去時曲名照樣顯示
                qCWarning(lcSmtc).noquote() << "SMTC 縮圖設定失敗：" << messageOf(error);
            }
        }
        display.Update();
    } catch (const winrt::hresult_error& error) {
        qCWarning(lcSmtc).noquote() << "SMTC 曲目資訊更新失敗：" << messageOf(error);
    }
}

void SmtcSession::setThumbnail(const QByteArray& thumbnailPng) noexcept {
    if (m_impl->closed) {
        return;
    }
    try {
        wm::SystemMediaTransportControlsDisplayUpdater display = m_impl->controls.DisplayUpdater();
        display.Thumbnail(thumbnailPng.isEmpty() ? nullptr : thumbnailFrom(thumbnailPng));
        display.Update();
    } catch (const winrt::hresult_error& error) {
        qCWarning(lcSmtc).noquote() << "SMTC 縮圖更新失敗：" << messageOf(error);
    }
}

void SmtcSession::clearTrack() noexcept {
    if (m_impl->closed) {
        return;
    }
    try {
        wm::SystemMediaTransportControlsDisplayUpdater display = m_impl->controls.DisplayUpdater();
        display.ClearAll();
        display.Update();
    } catch (const winrt::hresult_error& error) {
        qCWarning(lcSmtc).noquote() << "SMTC 曲目資訊清除失敗：" << messageOf(error);
    }
}

void SmtcSession::close() noexcept {
    if (m_impl->closed) {
        return;
    }
    m_impl->closed = true;
    // 先撤銷事件（noexcept），之後的清除不會再引來按鈕回呼。
    // 之後每一步各自接例外：前一步失敗時後面照樣做，盡量不殘留「播放中」
    m_impl->controls.ButtonPressed(m_impl->buttonToken);
    try {
        m_impl->controls.PlaybackStatus(wm::MediaPlaybackStatus::Closed);
    } catch (const winrt::hresult_error& error) {
        qCWarning(lcSmtc).noquote()
            << "SMTC 清除失敗（PlaybackStatus=Closed）：" << messageOf(error);
    }
    try {
        wm::SystemMediaTransportControlsDisplayUpdater display = m_impl->controls.DisplayUpdater();
        display.ClearAll();
        display.Update();
    } catch (const winrt::hresult_error& error) {
        qCWarning(lcSmtc).noquote()
            << "SMTC 清除失敗（DisplayUpdater ClearAll+Update）：" << messageOf(error);
    }
    try {
        m_impl->controls.IsEnabled(false);
    } catch (const winrt::hresult_error& error) {
        qCWarning(lcSmtc).noquote() << "SMTC 清除失敗（IsEnabled=false）：" << messageOf(error);
    }
}

} // namespace auralis::mod::smtc

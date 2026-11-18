// 輸出路徑「ASIO」的工廠（US-03；只在 AURALIS_WITH_ASIO 時編譯）。
#pragma once

#include "asio_backend.h"
#include "asio_drivers.h"

#include <auralis/audio_output.h>

#include <memory>

namespace auralis::mod::out {

/// 每個安裝的 ASIO 驅動是一個裝置（id 是 CLSID）。沒有驅動時沒有裝置，
/// 輸出清單就不出現 ASIO，也不報錯（AC-03-1）。
class AsioBackendFactory final : public IOutputBackendFactory {
public:
    /// window：給驅動 init 的視窗（主視窗 HWND；0 時用桌面視窗）。
    /// notice：原生 DSD 不能用、改送 DoP 時的提示（交給每個建立的後端）。
    AsioBackendFactory(std::shared_ptr<AsioDrivers> drivers, quintptr window,
                       AsioNotice notice = {});

    QString name() const override;
    QList<OutputDevice> devices() override;
    Result<std::unique_ptr<IOutputBackend>> create(const QString& deviceId) override;
    QString note() const override;

private:
    std::shared_ptr<AsioDrivers> m_drivers;
    quintptr m_window;
    AsioNotice m_notice;
};

} // namespace auralis::mod::out

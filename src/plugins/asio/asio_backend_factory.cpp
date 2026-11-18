#include "asio_backend_factory.h"

#include <QCoreApplication>

namespace auralis::mod::out {

AsioBackendFactory::AsioBackendFactory(std::shared_ptr<AsioDrivers> drivers, quintptr window,
                                       AsioNotice notice)
    : m_drivers(std::move(drivers)), m_window(window), m_notice(std::move(notice)) {}

QString AsioBackendFactory::name() const {
    return QStringLiteral("ASIO");
}

QString AsioBackendFactory::note() const {
    // 驅動能不能原生 DSD 要開啟時切 kAsioSetIoFormat 才知道（T-30：同一個驅動偶爾會拒絕），
    // 所以這裡不寫；播 DSD 時由輸出狀態的裝置格式分出原生（DSD）或 DoP（PCM）
    return QCoreApplication::translate("AsioBackend", "位元完美");
}

QList<OutputDevice> AsioBackendFactory::devices() {
    QList<OutputDevice> devices;
    for (const AsioDriverEntry& entry : m_drivers->list()) {
        devices.append({entry.clsid, entry.name});
    }
    return devices;
}

Result<std::unique_ptr<IOutputBackend>> AsioBackendFactory::create(const QString& deviceId) {
    for (const AsioDriverEntry& entry : m_drivers->list()) {
        if (entry.clsid == deviceId) {
            return std::unique_ptr<IOutputBackend>(std::make_unique<AsioBackend>(
                m_drivers, entry, m_window, kAsioStallLimit, m_notice));
        }
    }
    return Error{QStringLiteral("找不到 ASIO 驅動：%1").arg(deviceId), 0,
                 QStringLiteral("AsioBackendFactory::create")};
}

} // namespace auralis::mod::out

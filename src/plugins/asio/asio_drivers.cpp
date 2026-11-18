#include "asio_drivers.h"

#include <windows.h>

#include <objbase.h>

#include <asiosys.h>

#include <asio.h>
#include <iasiodrv.h>

namespace auralis::mod::out {

namespace {

class SystemAsioDrivers final : public AsioDrivers {
public:
    QList<AsioDriverEntry> list() override {
        QList<AsioDriverEntry> drivers;
        HKEY root = nullptr;
        // SDK 的 asiolist 用 ANSI 版登錄 API，驅動名稱有非 ASCII 字元時會變亂碼，所以自己列舉
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\ASIO", 0, KEY_READ, &root) !=
            ERROR_SUCCESS) {
            return drivers; // 沒有安裝任何 ASIO 驅動
        }
        for (DWORD i = 0;; ++i) {
            wchar_t name[256];
            DWORD nameLength = 256;
            if (RegEnumKeyExW(root, i, name, &nameLength, nullptr, nullptr, nullptr, nullptr) !=
                ERROR_SUCCESS) {
                break;
            }
            wchar_t clsid[128];
            DWORD bytes = sizeof clsid;
            if (RegGetValueW(root, name, L"CLSID", RRF_RT_REG_SZ, nullptr, clsid, &bytes) !=
                ERROR_SUCCESS) {
                continue; // 殘缺的登錄項目：載入不了，不列出
            }
            drivers.append({QString::fromWCharArray(clsid), QString::fromWCharArray(name)});
        }
        RegCloseKey(root);
        return drivers;
    }

    Result<IASIO*> create(const QString& clsid) override {
        CLSID id{};
        const std::wstring text = clsid.toStdWString();
        AURALIS_HR_CHECK(CLSIDFromString(text.c_str(), &id));
        IASIO* driver = nullptr;
        // ASIO 的慣例：驅動的 CLSID 同時當介面 ID
        AURALIS_HR_CHECK(CoCreateInstance(id, nullptr, CLSCTX_INPROC_SERVER, id,
                                          reinterpret_cast<void**>(&driver)));
        return driver;
    }
};

} // namespace

std::shared_ptr<AsioDrivers> systemAsioDrivers() {
    return std::make_shared<SystemAsioDrivers>();
}

} // namespace auralis::mod::out

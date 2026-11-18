// ASIO 輸出外掛的進入點（T-103）：AuralisAsio.dll 唯一匯出的函式。
// 後端本體（asio_*.cpp）原本在 OUT 模組裡，只搬位置、不改行為；這個檔只把它包成 output 類型的外掛。
#include "asio_backend_factory.h"
#include "output_plugin_api.h"

#include <auralis/plugin_entry.h>

namespace auralis::mod::out {

namespace {

class AsioOutputPlugin final : public IOutputPlugin {
public:
    std::shared_ptr<IOutputBackendFactory>
    createFactory(quintptr window, std::function<void(const QString&)> notice) override {
        // 沒有安裝 ASIO 驅動時這個路徑沒有裝置，輸出清單就不出現 ASIO，也不報錯（AC-03-1）
        return std::make_shared<AsioBackendFactory>(systemAsioDrivers(), window, std::move(notice));
    }
    bool nativeDsd() const override { return true; }
};

AsioOutputPlugin g_plugin;

const AuralisPluginEntry g_entry{AURALIS_PLUGIN_ABI_VERSION, AURALIS_ASIO_PLUGIN_ID, "output",
                                 static_cast<IOutputPlugin*>(&g_plugin)};

} // namespace

} // namespace auralis::mod::out

extern "C" __declspec(dllexport) const AuralisPluginEntry* auralis_plugin_entry() {
    return &auralis::mod::out::g_entry;
}

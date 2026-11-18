#pragma once

#include <auralis/audio_output.h>

#include <QList>
#include <QString>

#include <windows.h>

#include <audioclient.h>
#include <wrl/client.h>

#include <memory>

namespace auralis::core {

/// WASAPI 後端取得裝置的地方：系統的 MMDevice，或測試用的假裝置（不開實體裝置也能測
/// 格式協商、拔除、被佔用的邏輯）。執行緒：非即時執行緒。
class AudioEndpoints {
public:
    virtual ~AudioEndpoints() = default;
    /// 目前啟用中的播放裝置。
    virtual Result<QList<OutputDevice>> list() = 0;
    /// 為 id 這個裝置建立一個新的（還沒初始化的）IAudioClient。裝置不存在或已停用時回傳
    /// Error，hresult 是 AUDCLNT_E_DEVICE_INVALIDATED 或 ERROR_NOT_FOUND（裝置不見了）。
    virtual Result<Microsoft::WRL::ComPtr<IAudioClient>> activate(const QString& id) = 0;
};

/// 系統的播放裝置（IMMDeviceEnumerator）。建立時讓這個行程保有 MTA，
/// 沒有初始化 COM 的執行緒（解碼、音訊執行緒）也能使用 WASAPI 物件。
std::shared_ptr<AudioEndpoints> systemEndpoints();

} // namespace auralis::core

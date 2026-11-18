#pragma once

#include <auralis/result.h>

#include <QString>

#include <memory>

namespace auralis::mod::out {

/// 唯讀開啟的 DSD 檔。只讀取、不修改使用者的音樂檔；開啟期間使用者照常可以搬移或刪除檔案。
/// 執行緒：一個實例只給一個執行緒用（解碼執行緒）。
class DsdFile {
public:
    /// 開不了時回傳給使用者看的原因（找不到檔案、被其他程式使用中…）。
    static Result<std::unique_ptr<DsdFile>> open(const QString& path);
    ~DsdFile();

    DsdFile(const DsdFile&) = delete;
    DsdFile& operator=(const DsdFile&) = delete;

    /// 開啟當下的檔案長度；解析器以它為上限，檔頭宣告的長度超過時不採信。
    qint64 size() const noexcept { return m_size; }

    /// 從 offset 起讀滿 length 個位元組到 dest；範圍超出檔案、讀不滿或 I/O 錯誤時回傳 false，
    /// 不會讀超過 length。
    bool readAt(qint64 offset, void* dest, qint64 length) noexcept;

private:
    DsdFile(void* handle, qint64 size) noexcept : m_handle(handle), m_size(size) {}

    void* m_handle;
    qint64 m_size;
};

} // namespace auralis::mod::out

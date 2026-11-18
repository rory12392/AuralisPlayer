#pragma once

#include <QString>

#include <taglib/tiostream.h>

namespace auralis::core {

/// 給 TagLib 的唯讀檔案串流：以 GENERIC_READ 開啟、共用讀寫刪除，寫入類的方法一律不做事。
/// 讀使用者的音樂檔一律經過它，保證掃描不會改到檔案（AC-08-4）；TagLib 內建的 FileStream
/// 會先試著以可寫入的方式開檔，所以不用它。
/// 執行緒：一個實例只在一個執行緒使用。
class ReadOnlyStream final : public TagLib::IOStream {
public:
    explicit ReadOnlyStream(const QString& path);
    ~ReadOnlyStream() override;

    ReadOnlyStream(const ReadOnlyStream&) = delete;
    ReadOnlyStream& operator=(const ReadOnlyStream&) = delete;

    /// 單次讀取的上限：比這大的區塊（損毀或惡意的檔頭宣告的長度）不讀、回傳空的。
    /// 正常的標籤區塊遠小於它（FLAC 圖片區塊最大 16 MB）。
    static constexpr size_t kMaxReadBlock = 64 * 1024 * 1024;

    /// 開檔失敗時的原因（系統訊息）；成功時為空字串。
    const QString& openError() const { return m_openError; }

    /// 有沒有被要求讀超過 kMaxReadBlock 的區塊（呼叫端把這個檔案列為讀不到）。
    bool readLimitExceeded() const { return m_readLimitExceeded; }

    TagLib::FileName name() const override;
    TagLib::ByteVector readBlock(size_t length) override;
    void writeBlock(const TagLib::ByteVector& data) override;
    void insert(const TagLib::ByteVector& data, TagLib::offset_t start = 0,
                size_t replace = 0) override;
    void removeBlock(TagLib::offset_t start = 0, size_t length = 0) override;
    bool readOnly() const override { return true; }
    bool isOpen() const override;
    void seek(TagLib::offset_t offset, Position p = Beginning) override;
    TagLib::offset_t tell() const override;
    TagLib::offset_t length() override;
    void truncate(TagLib::offset_t length) override;

private:
    std::wstring m_path;
    void* m_handle;
    QString m_openError;
    bool m_readLimitExceeded = false;
};

} // namespace auralis::core

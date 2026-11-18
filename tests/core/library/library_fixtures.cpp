#include "library_fixtures.h"

#include "../audio/signal_writer.h"

#include "library/file_enumerator.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QtEndian>

#include <taglib/fileref.h>
#include <taglib/tpropertymap.h>

#include <stdexcept>
#include <vector>

#include <windows.h>
#include <winioctl.h>

namespace auralis::test {

namespace {

constexpr quint32 kDsd64 = 2822400;
constexpr quint32 kDsdBytesPerChannel = kDsd64 / 8 / 2; // 0.5 秒
constexpr quint8 kDsdSilence = 0x69;

void check(const QString& error) {
    if (!error.isEmpty()) {
        throw std::runtime_error(error.toStdString());
    }
}

void applyTags(const QString& path, const Tags& tags) {
    if (tags.isEmpty()) {
        return;
    }
    TagLib::FileRef file(path.toStdWString().c_str());
    if (file.isNull()) {
        throw std::runtime_error("TagLib 開不了測試檔：" + path.toStdString());
    }
    TagLib::PropertyMap properties;
    for (auto it = tags.cbegin(); it != tags.cend(); ++it) {
        properties.insert(TagLib::String(it.key().toStdString()),
                          TagLib::StringList(TagLib::String(it.value().toStdWString())));
    }
    file.file()->setProperties(properties);
    if (!file.save()) {
        throw std::runtime_error("TagLib 寫不進標籤：" + path.toStdString());
    }
}

template <typename T> void putLe(QByteArray& out, T value) {
    char bytes[sizeof(T)];
    qToLittleEndian(value, bytes);
    out.append(bytes, sizeof(T));
}

template <typename T> void putBe(QByteArray& out, T value) {
    char bytes[sizeof(T)];
    qToBigEndian(value, bytes);
    out.append(bytes, sizeof(T));
}

} // namespace

QString freshTestDir(const QString& name) {
    const QString path = QString::fromUtf8(AURALIS_TEST_TMP) + QStringLiteral("/t-09/") + name;
    QDir(path).removeRecursively();
    if (!QDir().mkpath(path)) {
        throw std::runtime_error("無法建立測試資料夾：" + path.toStdString());
    }
    return QDir::toNativeSeparators(path);
}

void writeBytes(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
        throw std::runtime_error("無法寫入測試檔：" + path.toStdString());
    }
}

void writeFlac(const QString& path, const Tags& tags, int rate, int bits, int frames) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    AudioFormat format;
    format.rate = static_cast<quint32>(rate);
    format.bits = static_cast<quint16>(bits);
    format.container = bits == 16 ? 16 : 32;
    format.channels = 2;
    check(encodeFile(path, "flac", "flac", format, makeNoise(format, frames, 7)));
    applyTags(path, tags);
}

void writeWav(const QString& path, const Tags& tags, int rate, int frames) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    AudioFormat format;
    format.rate = static_cast<quint32>(rate);
    format.bits = 24;
    format.container = 32;
    format.channels = 2;
    check(encodeFile(path, "pcm_s24le", "wav", format, makeNoise(format, frames, 9)));
    applyTags(path, tags);
}

void writeDsf(const QString& path, const Tags& tags) {
    // DSF 規格：DSD 區塊、fmt 區塊（每聲道 4096 位元組一塊）、data 區塊；全部小端
    constexpr quint32 kBlock = 4096;
    const quint32 blocks = (kDsdBytesPerChannel + kBlock - 1) / kBlock;
    const quint64 dataBytes = quint64(blocks) * kBlock * 2;
    QByteArray out;
    out.append("DSD ", 4);
    putLe<quint64>(out, 28);
    putLe<quint64>(out, 28 + 52 + 12 + dataBytes); // 檔案總長
    putLe<quint64>(out, 0); // 中繼資料位置（TagLib 寫標籤時補上）
    out.append("fmt ", 4);
    putLe<quint64>(out, 52);
    putLe<quint32>(out, 1);                                // 格式版本
    putLe<quint32>(out, 0);                                // DSD raw
    putLe<quint32>(out, 2);                                // 聲道類型：立體聲
    putLe<quint32>(out, 2);                                // 聲道數
    putLe<quint32>(out, kDsd64);                           // 取樣率
    putLe<quint32>(out, 1);                                // 每樣本位元數
    putLe<quint64>(out, quint64(kDsdBytesPerChannel) * 8); // 每聲道樣本數
    putLe<quint32>(out, kBlock);
    putLe<quint32>(out, 0);
    out.append("data", 4);
    putLe<quint64>(out, 12 + dataBytes);
    out.append(QByteArray(static_cast<qsizetype>(dataBytes), char(kDsdSilence)));
    writeBytes(path, out);
    applyTags(path, tags);
}

void writeDff(const QString& path, const Tags& tags) {
    // DSDIFF 規格：FRM8 容器，FVER、PROP（FS、CHNL、CMPR）、DSD 區塊；全部大端
    const quint64 dataBytes = quint64(kDsdBytesPerChannel) * 2;
    const QByteArray compressionName("not compressed.", 15); // 長度湊成偶數，不用補位
    QByteArray prop;
    prop.append("SND ", 4);
    prop.append("FS  ", 4);
    putBe<quint64>(prop, 4);
    putBe<quint32>(prop, kDsd64);
    prop.append("CHNL", 4);
    putBe<quint64>(prop, 2 + 8);
    putBe<quint16>(prop, 2);
    prop.append("SLFTSRGT", 8);
    prop.append("CMPR", 4);
    putBe<quint64>(prop, 4 + 1 + compressionName.size());
    prop.append("DSD ", 4);
    prop.append(char(compressionName.size()));
    prop.append(compressionName);

    QByteArray body;
    body.append("DSD ", 4);
    body.append("FVER", 4);
    putBe<quint64>(body, 4);
    putBe<quint32>(body, 0x01050000);
    body.append("PROP", 4);
    putBe<quint64>(body, static_cast<quint64>(prop.size()));
    body.append(prop);
    body.append("DSD ", 4);
    putBe<quint64>(body, dataBytes);
    body.append(QByteArray(static_cast<qsizetype>(dataBytes), char(kDsdSilence)));

    QByteArray out("FRM8", 4);
    putBe<quint64>(out, static_cast<quint64>(body.size()));
    out.append(body);
    writeBytes(path, out);
    applyTags(path, tags);
}

int buildSampleLibrary(const QString& root) {
    const QString album = QStringLiteral("NieR：Automata Original Soundtrack");
    writeFlac(root + QStringLiteral("\\專輯 A\\01.flac"),
              {{QStringLiteral("TITLE"), QStringLiteral("ＮｉｅＲ：Automata")},
               {QStringLiteral("ARTIST"), QStringLiteral("岡部啓一")},
               {QStringLiteral("ALBUMARTIST"), QStringLiteral("MONACA")},
               {QStringLiteral("ALBUM"), album},
               {QStringLiteral("DATE"), QStringLiteral("2017")},
               {QStringLiteral("TRACKNUMBER"), QStringLiteral("1")},
               {QStringLiteral("LYRICS"), QStringLiteral("歌詞")}});
    writeFlac(root + QStringLiteral("\\專輯 A\\02.flac"),
              {{QStringLiteral("TITLE"), QStringLiteral("遺サレタ場所／斜光")},
               {QStringLiteral("ARTIST"), QStringLiteral("帆足圭吾")},
               {QStringLiteral("ALBUMARTIST"), QStringLiteral("MONACA")},
               {QStringLiteral("ALBUM"), album},
               {QStringLiteral("DATE"), QStringLiteral("2017")},
               {QStringLiteral("TRACKNUMBER"), QStringLiteral("2")}},
              96000, 24, 9600);
    writeWav(root + QStringLiteral("\\其他\\深一層\\x.wav"),
             {{QStringLiteral("TITLE"), QStringLiteral("Wave")},
              {QStringLiteral("ARTIST"), QStringLiteral("Tester")}});
    writeDsf(root + QStringLiteral("\\DSD\\d.dsf"),
             {{QStringLiteral("TITLE"), QStringLiteral("DSF 曲")},
              {QStringLiteral("ARTIST"), QStringLiteral("Tester")},
              {QStringLiteral("ALBUM"), QStringLiteral("DSD 測試")}});
    writeDff(root + QStringLiteral("\\DSD\\e.dff"), {});
    writeCorrupt(root + QStringLiteral("\\壞掉\\壞掉.flac"));
    writeBytes(root + QStringLiteral("\\notes.txt"), "not audio");
    writeBytes(root + QStringLiteral("\\專輯 A\\cover.jpg"), "not audio");
    return 6;
}

void writeCorrupt(const QString& path) {
    QByteArray junk;
    for (int i = 0; i < 4096; ++i) {
        junk.append(char((i * 37 + 11) & 0xff));
    }
    writeBytes(path, junk);
}

void removeLinksUnder(const QString& folder) {
    WIN32_FIND_DATAW data;
    HANDLE find = FindFirstFileW((folder + QStringLiteral("\\*")).toStdWString().c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return;
    }
    do {
        const QString name = QString::fromWCharArray(data.cFileName);
        if (name == QLatin1String(".") || name == QLatin1String("..")) {
            continue;
        }
        const std::wstring path = (folder + QLatin1Char('\\') + name).toStdWString();
        const bool directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (core::isLinkEntry(data.dwFileAttributes, data.dwReserved0)) {
            directory ? RemoveDirectoryW(path.c_str()) : DeleteFileW(path.c_str());
        } else if (directory) {
            removeLinksUnder(QString::fromStdWString(path));
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

LinkDir::LinkDir(const QString& name) {
    removeLinksUnder(QDir::toNativeSeparators(QString::fromUtf8(AURALIS_TEST_TMP) +
                                              QStringLiteral("/t-09/") + name));
    path = freshTestDir(name);
}

LinkDir::~LinkDir() {
    removeLinksUnder(path);
}

void makeJunction(const QString& link, const QString& target) {
    QDir().mkpath(QFileInfo(link).absolutePath());
    const int code =
        QProcess::execute(QStringLiteral("cmd"), {QStringLiteral("/c"), QStringLiteral("mklink"),
                                                  QStringLiteral("/J"), link, target});
    REQUIRE(code == 0);
}

bool makeSymlink(const QString& link, const QString& target, bool directory) {
    QDir().mkpath(QFileInfo(link).absolutePath());
    const DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE |
                        (directory ? SYMBOLIC_LINK_FLAG_DIRECTORY : 0);
    if (CreateSymbolicLinkW(link.toStdWString().c_str(), target.toStdWString().c_str(), flags) !=
        0) {
        return true;
    }
    const DWORD code = GetLastError();
    INFO("CreateSymbolicLinkW 錯誤碼 " << code);
    REQUIRE(code == ERROR_PRIVILEGE_NOT_HELD);
    return false;
}

bool setCustomReparsePoint(const QString& path) {
    HANDLE file = CreateFileW(path.toStdWString().c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                              FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    std::vector<char> buffer(REPARSE_GUID_DATA_BUFFER_HEADER_SIZE + 4, 0);
    auto* data = reinterpret_cast<REPARSE_GUID_DATA_BUFFER*>(buffer.data());
    data->ReparseTag = 0x00001A55; // 非微軟、非連結的測試用標籤
    data->ReparseDataLength = 4;
    data->ReparseGuid = {
        0x6a1f3c52, 0x2b7e, 0x4d1a, {0x9c, 0x10, 0x41, 0x55, 0x52, 0x41, 0x4c, 0x01}};
    DWORD returned = 0;
    const BOOL ok =
        DeviceIoControl(file, FSCTL_SET_REPARSE_POINT, buffer.data(),
                        static_cast<DWORD>(buffer.size()), nullptr, 0, &returned, nullptr);
    CloseHandle(file);
    return ok != 0;
}

} // namespace auralis::test

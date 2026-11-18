#include "single_instance.h"

#include <auralis/library_query.h>

#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QMetaObject>

#include <algorithm>

#include <windows.h>

namespace auralis::app {

namespace {

/// 訊息開頭，用來認出是本程式送的；後面一行一個路徑（UTF-8；Windows 的路徑不會有換行）。
constexpr char kHeader[] = "AURALIS-OPEN 1\n";

HANDLE handleOf(void* pipe) {
    return static_cast<HANDLE>(pipe);
}

} // namespace

SingleInstance::SingleInstance(const QString& key) {
    // 管道名稱用資料夾比對鍵的雜湊：路徑本身可能很長、含管道名稱不能用的字元
    const QByteArray digest =
        QCryptographicHash::hash(pathKey(key).toUtf8(), QCryptographicHash::Sha1).toHex().left(20);
    m_name = L"\\\\.\\pipe\\AuralisPlayer-" + QString::fromLatin1(digest).toStdWString();
    // FIRST_PIPE_INSTANCE：已有同名管道時失敗，誰是主要的執行個體由這一步一次決定（沒有競爭）。
    // 只收本機連線；只開放對方寫入（INBOUND）
    HANDLE pipe = CreateNamedPipeW(
        m_name.c_str(), PIPE_ACCESS_INBOUND | FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 0,
        64 * 1024, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        // 已有主要的執行個體時是 ERROR_ACCESS_DENIED，或（實例數已滿）ERROR_PIPE_BUSY；
        // 其他錯誤（不太會發生）照常自己開
        const DWORD error = GetLastError();
        m_primary = error != ERROR_ACCESS_DENIED && error != ERROR_PIPE_BUSY;
        return;
    }
    m_pipe = pipe;
}

SingleInstance::~SingleInstance() {
    stop();
    if (m_pipe != nullptr) {
        CloseHandle(handleOf(m_pipe));
    }
}

bool SingleInstance::forward(const QStringList& paths) const {
    // 讓主要的執行個體可以把自己的視窗叫到前面（Windows 預設只讓前景程式這樣做）
    AllowSetForegroundWindow(ASFW_ANY);
    QByteArray message(kHeader);
    message += paths.join(QLatin1Char('\n')).toUtf8();
    if (message.size() > kMaxMessageBytes) {
        return false;
    }
    const QDeadlineTimer deadline(kConnectTimeoutMs);
    HANDLE pipe = INVALID_HANDLE_VALUE;
    while (pipe == INVALID_HANDLE_VALUE) {
        pipe = CreateFileW(m_name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            break;
        }
        // 主要的執行個體正在處理別人的訊息：等它空出來
        if (GetLastError() != ERROR_PIPE_BUSY || deadline.hasExpired()) {
            return false;
        }
        WaitNamedPipeW(m_name.c_str(),
                       static_cast<DWORD>(std::max<qint64>(1, deadline.remainingTime())));
    }
    DWORD written = 0;
    const bool ok = WriteFile(pipe, message.constData(), static_cast<DWORD>(message.size()),
                              &written, nullptr) &&
                    written == static_cast<DWORD>(message.size());
    CloseHandle(pipe);
    return ok;
}

void SingleInstance::listen(QObject* receiver, std::function<void(const QStringList&)> handler) {
    if (m_pipe == nullptr || m_thread.joinable()) {
        return;
    }
    m_receiver = receiver;
    m_handler = std::move(handler);
    m_thread = std::thread([this] { serve(); });
}

void SingleInstance::stop() {
    if (!m_thread.joinable()) {
        return;
    }
    m_stopping = true;
    // 背景執行緒停在等連線：自己連一次叫醒它
    HANDLE wake = CreateFileW(m_name.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (wake != INVALID_HANDLE_VALUE) {
        CloseHandle(wake);
    } else {
        CancelSynchronousIo(m_thread.native_handle());
    }
    m_thread.join();
}

void SingleInstance::serve() {
    HANDLE pipe = handleOf(m_pipe);
    while (!m_stopping) {
        // 對方可能在這裡開始等之前就連上（ERROR_PIPE_CONNECTED），甚至已經寫完並關掉
        // （ERROR_NO_DATA）：兩種都照樣讀，寫進管道的內容還在
        if (!ConnectNamedPipe(pipe, nullptr)) {
            const DWORD error = GetLastError();
            if (m_stopping) {
                break;
            }
            if (error != ERROR_PIPE_CONNECTED && error != ERROR_NO_DATA) {
                DisconnectNamedPipe(pipe);
                continue;
            }
        }
        // 讀到對方關閉為止；超過上限的整則丟掉
        QByteArray message;
        char buffer[4096];
        DWORD read = 0;
        while (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) {
            if (message.size() + static_cast<qsizetype>(read) > kMaxMessageBytes) {
                message.clear();
                break;
            }
            message.append(buffer, static_cast<qsizetype>(read));
        }
        DisconnectNamedPipe(pipe);
        if (m_stopping || !message.startsWith(kHeader)) {
            continue;
        }
        const QString body = QString::fromUtf8(message.mid(qsizetype(sizeof(kHeader) - 1)));
        const QStringList paths =
            body.isEmpty() ? QStringList() : body.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QMetaObject::invokeMethod(
            m_receiver, [handler = m_handler, paths] { handler(paths); }, Qt::QueuedConnection);
    }
}

} // namespace auralis::app

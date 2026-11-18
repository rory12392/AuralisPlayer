#pragma once

// 本機的假 LRCLIB（測試不打真的 LRCLIB）：QTcpServer 在 127.0.0.1 的隨機埠，依路徑（/api/get、
// /api/search）回固定的狀態碼與本體，可以延遲回應；記下每個請求的路徑＋查詢字串與 User-Agent，
// 以及還沒回應就被用戶端關掉的連線數（換歌時 abort）。跑在建立它的執行緒的事件迴圈。
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <atomic>
#include <memory>

namespace auralis::test {

class FakeLrclib final : public QObject {
public:
    struct Response {
        int status = 404;
        QByteArray body = "{\"code\":404,\"message\":\"Not found\"}";
        int delayMs = 0;
        /// 額外的回應標頭（例：429 的 Retry-After，T-99）。
        QList<QPair<QByteArray, QByteArray>> headers;
    };
    struct Request {
        QString target; ///< 路徑＋查詢字串（百分比編碼原樣）
        QByteArray userAgent;
    };

    FakeLrclib() {
        m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* socket = m_server.nextPendingConnection()) {
                socket->setParent(this);
                QObject::connect(socket, &QTcpSocket::readyRead, this,
                                 [this, socket] { onRead(socket); });
            }
        });
    }

    QString baseUrl() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort());
    }
    /// path 是 "/api/get" 或 "/api/search"；沒設定的路徑回 404。
    void set(const QString& path, Response response) { m_responses.insert(path, response); }
    /// 某個 track_name（百分比編碼後）走另一個回應；給換歌與版本後綴的測試。
    void setFor(const QString& path, const QString& encodedTrack, Response response) {
        m_byTrack.insert(path + u'|' + encodedTrack, response);
    }
    QList<Request> requests() const { return m_requests; }
    int abortedBeforeReply() const { return m_aborted.load(); }

private:
    void onRead(QTcpSocket* socket) {
        QByteArray& buffer = m_buffers[socket];
        buffer += socket->readAll();
        const qsizetype end = buffer.indexOf("\r\n\r\n");
        if (end < 0) {
            return;
        }
        const QList<QByteArray> lines = buffer.left(end).split('\n');
        const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
        Request request{QString::fromLatin1(first.value(1)), {}};
        for (const QByteArray& line : lines) {
            if (line.toLower().startsWith("user-agent:")) {
                request.userAgent = line.mid(11).trimmed();
            }
        }
        m_requests.append(request);
        m_buffers.remove(socket);
        const QString path = request.target.section(u'?', 0, 0);
        Response response = m_responses.value(path, Response{});
        for (const QString& pair : request.target.section(u'?', 1).split(u'&')) {
            if (pair.startsWith(u"track_name=")) {
                const QString key = path + u'|' + pair.mid(11);
                if (m_byTrack.contains(key)) {
                    response = m_byTrack.value(key);
                }
            }
        }
        QPointer<QTcpSocket> guard(socket);
        auto replied = std::make_shared<bool>(false); // 只在這個執行緒用
        QObject::connect(socket, &QTcpSocket::disconnected, this, [this, replied, socket] {
            if (!*replied) {
                ++m_aborted;
            }
            socket->deleteLater();
        });
        QTimer::singleShot(response.delayMs, this, [guard, response, replied] {
            if (!guard || guard->state() != QAbstractSocket::ConnectedState) {
                return;
            }
            *replied = true;
            QByteArray head = "HTTP/1.1 " + QByteArray::number(response.status) + " X\r\n";
            head += "Content-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                    QByteArray::number(response.body.size()) + "\r\n";
            for (const auto& [name, value] : response.headers) {
                head += name + ": " + value + "\r\n";
            }
            head += "\r\n";
            guard->write(head + response.body);
            guard->disconnectFromHost();
        });
    }

    QTcpServer m_server;
    QHash<QString, Response> m_responses;
    QHash<QString, Response> m_byTrack;
    QHash<QTcpSocket*, QByteArray> m_buffers;
    QList<Request> m_requests;
    std::atomic<int> m_aborted{0};
};

} // namespace auralis::test

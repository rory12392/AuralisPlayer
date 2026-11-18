#include "logging/logger.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMutex>
#include <QThread>

#include <cstdio>
#include <memory>

namespace auralis::core {

namespace {

constexpr int kLogRetentionDays = 7;

QMutex g_mutex;
std::unique_ptr<QFile> g_file;
QtMessageHandler g_previous = nullptr;

const char* levelName(QtMsgType type) {
    switch (type) {
    case QtDebugMsg:
        return "DEBUG";
    case QtInfoMsg:
        return "INFO";
    case QtWarningMsg:
        return "WARN";
    case QtCriticalMsg:
        return "ERROR";
    case QtFatalMsg:
        return "FATAL";
    }
    return "?";
}

void handler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    const QByteArray line =
        QStringLiteral("%1 [%2] %3 %4: %5\n")
            .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                 QString::fromLatin1(levelName(type)),
                 QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId())),
                 QString::fromLatin1(context.category ? context.category : "default"), message)
            .toUtf8();
    QMutexLocker lock(&g_mutex);
    if (g_file) {
        g_file->write(line);
        g_file->flush();
    }
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stderr);
}

void applyLevel(const QString& level) {
    // Qt 的規則：比設定等級低的類別關掉
    QString rules;
    if (level == u"error") {
        rules = QStringLiteral("*.debug=false\n*.info=false\n*.warning=false");
    } else if (level == u"warning") {
        rules = QStringLiteral("*.debug=false\n*.info=false");
    } else if (level == u"debug") {
        rules = QStringLiteral("*.debug=true");
    } else {
        rules = QStringLiteral("*.debug=false\n*.info=true");
    }
    QLoggingCategory::setFilterRules(rules);
}

void removeOldLogs(const QDir& dir) {
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-kLogRetentionDays);
    const auto files = dir.entryInfoList({QStringLiteral("auralis-*.log")}, QDir::Files);
    for (const QFileInfo& info : files) {
        if (info.lastModified() < cutoff) {
            QFile::remove(info.absoluteFilePath());
        }
    }
}

} // namespace

Result<QString> installFileLogger(const QString& dataDir) {
    QDir dir(dataDir + QStringLiteral("/logs"));
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        return Error{QStringLiteral("無法建立日誌資料夾：") + dir.absolutePath(), 0,
                     QStringLiteral("QDir::mkpath")};
    }
    removeOldLogs(dir);

    const QString path = dir.absoluteFilePath(
        QStringLiteral("auralis-%1.log").arg(QDate::currentDate().toString(Qt::ISODate)));
    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::Append | QIODevice::Text)) {
        return Error{QStringLiteral("無法開啟日誌檔：") + path + QStringLiteral("（") +
                         file->errorString() + QStringLiteral("）"),
                     0, QStringLiteral("QFile::open")};
    }

    applyLevel(qEnvironmentVariable("AURALIS_LOG_LEVEL").trimmed().toLower());
    {
        QMutexLocker lock(&g_mutex);
        g_file = std::move(file);
    }
    g_previous = qInstallMessageHandler(handler);
    return path;
}

void uninstallFileLogger() {
    qInstallMessageHandler(g_previous);
    QMutexLocker lock(&g_mutex);
    g_file.reset();
}

} // namespace auralis::core

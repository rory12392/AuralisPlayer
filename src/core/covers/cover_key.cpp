#include "covers/cover_key.h"

#include <auralis/library_query.h>

#include <QCryptographicHash>
#include <QStringList>

namespace auralis::core {

namespace {

const QString kEmbeddedPrefix = QStringLiteral("embedded:");
const QString kFilePrefix = QStringLiteral("file:");
constexpr qsizetype kHashHexLength = 40;

} // namespace

QString embeddedCoverSource(const QString& audioPath) {
    return kEmbeddedPrefix + audioPath;
}

QString fileCoverSource(const QString& imagePath) {
    return kFilePrefix + imagePath;
}

QString coverSourcePath(const QString& source, bool* isEmbedded) {
    if (source.startsWith(kEmbeddedPrefix)) {
        *isEmbedded = true;
        return source.mid(kEmbeddedPrefix.size());
    }
    if (source.startsWith(kFilePrefix)) {
        *isEmbedded = false;
        return source.mid(kFilePrefix.size());
    }
    return {};
}

QString coverKey(const QString& source, qint64 size, qint64 mtimeNs) {
    bool embedded = false;
    const QString path = coverSourcePath(source, &embedded);
    const QString identity = QStringLiteral("%1|%2|%3").arg(pathKey(path)).arg(size).arg(mtimeNs);
    const QByteArray hash =
        QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha1).toHex();
    return (embedded ? QStringLiteral("e") : QStringLiteral("f")) + QString::fromLatin1(hash);
}

bool isValidCoverKey(const QString& key) {
    if (key.size() != kHashHexLength + 1 ||
        (key.front() != QLatin1Char('e') && key.front() != QLatin1Char('f'))) {
        return false;
    }
    for (qsizetype i = 1; i < key.size(); ++i) {
        const QChar c = key.at(i);
        const bool hex = (c >= QLatin1Char('0') && c <= QLatin1Char('9')) ||
                         (c >= QLatin1Char('a') && c <= QLatin1Char('f'));
        if (!hex) {
            return false;
        }
    }
    return true;
}

int folderCoverRank(const QString& fileName) {
    static const QStringList names{QStringLiteral("front"), QStringLiteral("cover"),
                                   QStringLiteral("folder"), QStringLiteral("albumart")};
    static const QStringList suffixes{QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                      QStringLiteral("png")};
    const qsizetype dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0 || !suffixes.contains(fileName.mid(dot + 1).toLower())) {
        return -1;
    }
    return static_cast<int>(names.indexOf(fileName.left(dot).toLower()));
}

} // namespace auralis::core

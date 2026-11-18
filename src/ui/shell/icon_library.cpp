#include "shell/icon_library.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QXmlStreamReader>

Q_LOGGING_CATEGORY(lcIcons, "auralis.ui.icons")

namespace auralis::ui {

namespace {

const QString kIconDir = QStringLiteral(":/qt/qml/Auralis/Ui/icons/");

QString number(double value) {
    return QString::number(value, 'g', 6);
}

/// 正圓轉成兩段半圓弧，PathSvg 不認 <circle>。
QString circlePath(double cx, double cy, double r) {
    return QStringLiteral("M %1 %2 A %3 %3 0 1 0 %4 %2 A %3 %3 0 1 0 %1 %2 Z")
        .arg(number(cx - r), number(cy), number(r), number(cx + r));
}

} // namespace

IconLibrary::IconLibrary(QObject* parent) : QObject(parent) {}

QString IconLibrary::path(const QString& name) {
    const auto cached = m_cache.constFind(name);
    if (cached != m_cache.constEnd()) {
        return *cached;
    }
    QFile file(kIconDir + name + QStringLiteral(".svg"));
    QString result;
    if (file.open(QIODevice::ReadOnly)) {
        result = pathFromSvg(file.readAll());
    }
    if (result.isEmpty()) {
        qCWarning(lcIcons).noquote() << "沒有這個圖示或格式不對：" << name;
    }
    m_cache.insert(name, result);
    return result;
}

QStringList IconLibrary::names() const {
    QStringList result;
    const QStringList files =
        QDir(kIconDir).entryList({QStringLiteral("*.svg")}, QDir::Files, QDir::Name);
    for (const QString& file : files) {
        result.append(file.chopped(4));
    }
    return result;
}

QString IconLibrary::pathFromSvg(const QByteArray& svg) {
    QXmlStreamReader reader(svg);
    QStringList parts;
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        const auto name = reader.name();
        const auto attributes = reader.attributes();
        if (name == QLatin1String("svg")) {
            continue;
        }
        if (name == QLatin1String("path")) {
            parts.append(attributes.value(QLatin1String("d")).toString());
        } else if (name == QLatin1String("circle")) {
            parts.append(circlePath(attributes.value(QLatin1String("cx")).toDouble(),
                                    attributes.value(QLatin1String("cy")).toDouble(),
                                    attributes.value(QLatin1String("r")).toDouble()));
        } else {
            return {};
        }
    }
    if (reader.hasError()) {
        return {};
    }
    return parts.join(QLatin1Char(' ')).trimmed();
}

} // namespace auralis::ui

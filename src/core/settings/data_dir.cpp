#include "settings/data_dir.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace auralis::core {

Result<QString> resolveDataDir(const DataDirInputs& inputs) {
    if (!inputs.commandLine.isEmpty()) {
        return QDir::cleanPath(QDir(inputs.commandLine).absolutePath());
    }
    if (!inputs.environment.isEmpty()) {
        return QDir::cleanPath(QDir(inputs.environment).absolutePath());
    }
    if (inputs.devBuild) {
        return Error{QStringLiteral("開發版必須指定資料夾：設定環境變數 AURALIS_DATA_DIR "
                                    "（例：<repo>\\.localdata\\app）或加上 --data-dir <資料夾>。"
                                    "這是為了避免開發與測試動到真實的設定與音樂庫。"),
                     0, QStringLiteral("resolveDataDir")};
    }
    if (inputs.portableFlag) {
        return QDir::cleanPath(inputs.exeDir + QStringLiteral("/data"));
    }
    if (inputs.localAppData.isEmpty()) {
        return Error{QStringLiteral("找不到 %LOCALAPPDATA%，無法決定資料夾"), 0,
                     QStringLiteral("resolveDataDir")};
    }
    return QDir::cleanPath(inputs.localAppData + QStringLiteral("/AuralisPlayer"));
}

DataDirInputs currentDataDirInputs(const QString& commandLine) {
    DataDirInputs inputs;
    inputs.commandLine = commandLine;
    inputs.environment = qEnvironmentVariable("AURALIS_DATA_DIR");
#ifdef AURALIS_DEV_BUILD
    inputs.devBuild = true;
#endif
    inputs.exeDir = QCoreApplication::applicationDirPath();
    inputs.portableFlag = QFileInfo::exists(inputs.exeDir + QStringLiteral("/portable.flag"));
    inputs.localAppData = qEnvironmentVariable("LOCALAPPDATA");
    return inputs;
}

} // namespace auralis::core

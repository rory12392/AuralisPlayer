#include "settings/data_dir.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <iterator>

#include <qt_windows.h>
#include <shellapi.h>

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

QString dataDirArgument(const QStringList& arguments) {
    const QString option = QStringLiteral("--data-dir");
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        const QString& argument = arguments[i];
        if (argument == option && i + 1 < arguments.size()) {
            return arguments[i + 1];
        }
        if (argument.startsWith(option + QLatin1Char('='))) {
            return argument.mid(option.size() + 1);
        }
    }
    return {};
}

DataDirInputs dataDirInputsBeforeApp() {
    QStringList arguments;
    int count = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int i = 1; i < count; ++i) {
            arguments.append(QString::fromWCharArray(argv[i]));
        }
        LocalFree(argv);
    }
    DataDirInputs inputs;
    inputs.commandLine = dataDirArgument(arguments);
    inputs.environment = qEnvironmentVariable("AURALIS_DATA_DIR");
#ifdef AURALIS_DEV_BUILD
    inputs.devBuild = true;
#endif
    wchar_t exePath[MAX_PATH * 4] = {};
    const DWORD length =
        GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
    inputs.exeDir =
        QFileInfo(QString::fromWCharArray(exePath, static_cast<int>(length))).absolutePath();
    inputs.portableFlag = QFileInfo::exists(inputs.exeDir + QStringLiteral("/portable.flag"));
    inputs.localAppData = qEnvironmentVariable("LOCALAPPDATA");
    return inputs;
}

} // namespace auralis::core

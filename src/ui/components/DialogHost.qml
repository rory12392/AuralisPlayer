import QtQuick
import QtQuick.Dialogs

// 接收核心 Dialogs 的請求，開系統的檔案對話框或確認對話框，結果回給 Dialogs.respond()。
Item {
    id: host

    property int pendingId: 0

    Connections {
        target: Dialogs
        function onRequested(id, kind, options) {
            host.pendingId = id
            if (kind === "folder") {
                folderDialog.title = options.title
                folderDialog.open()
            } else if (kind === "files" || kind === "saveFile") {
                fileDialog.title = options.title
                fileDialog.nameFilters = options.nameFilters
                fileDialog.fileMode = kind === "files" ? FileDialog.OpenFiles : FileDialog.SaveFile
                // 呼叫端指定的起始資料夾（建議檔名是完整路徑時）；沒有時用系統記得的
                if (options.folder !== undefined)
                    fileDialog.currentFolder = options.folder
                fileDialog.currentFile = kind === "saveFile" ? options.suggestedName : ""
                fileDialog.open()
            } else {
                confirmDialog.title = options.title
                confirmDialog.message = options.message
                confirmDialog.confirmText = options.confirmText
                confirmDialog.destructive = options.destructive
                confirmDialog.open()
            }
        }
    }

    FolderDialog {
        id: folderDialog
        onAccepted: Dialogs.respond(host.pendingId, [selectedFolder])
        onRejected: Dialogs.respond(host.pendingId, [])
    }

    FileDialog {
        id: fileDialog
        // 已存在的檔由呼叫端用 confirm() 問一次（AC-14-3），系統對話框不再問
        options: FileDialog.DontConfirmOverwrite
        onAccepted: Dialogs.respond(host.pendingId,
                                    fileMode === FileDialog.OpenFiles ? selectedFiles : [selectedFile])
        onRejected: Dialogs.respond(host.pendingId, [])
    }

    Dialog {
        id: confirmDialog
        variant: "confirm"
        onAccepted: Dialogs.respond(host.pendingId, true)
        onRejected: Dialogs.respond(host.pendingId, false)
    }
}

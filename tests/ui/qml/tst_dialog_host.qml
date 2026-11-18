import QtQuick
import QtQuick.Dialogs
import QtTest
import Auralis.Ui

// 核心 Dialogs 的 QML 端（M-04 驗證的 F-2）：另存新檔時系統的檔案對話框不問「已經存在，要取代嗎」，
// 覆蓋確認只由呼叫端用 confirm() 問一次（AC-14-3）。不開真的對話框，只看設定。
Item {
    id: root
    width: 400
    height: 300

    DialogHost {
        id: host
    }

    TestCase {
        name: "DialogHost"

        function fileDialog() {
            for (let i = 0; i < host.resources.length; ++i) {
                const item = host.resources[i]
                if (item.fileMode !== undefined && item.options !== undefined)
                    return item
            }
            return null
        }

        function test_save_dialog_does_not_confirm_overwrite() {
            const dialog = fileDialog()
            verify(dialog !== null, "找不到 DialogHost 的檔案對話框")
            verify((dialog.options & FileDialog.DontConfirmOverwrite) !== 0,
                   "系統對話框不該自己問覆蓋")
        }
    }
}

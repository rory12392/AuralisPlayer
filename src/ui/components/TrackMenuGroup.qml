import QtQml

// 曲目右鍵選單的一段裡要放好幾個項目時，區塊檔的根用這個（介面規範 §3、core-interfaces UiRegistry）：
// TrackMenuGroup { MenuItem { … } MenuItem { … } }。只有一個項目時根直接用 MenuItem，子選單用 Menu。
// payloadJson、trackCount 由 TrackMenu 在建立時給；組裡的項目用 parent 讀不到，請用這一層的 id。
QtObject {
    required property string payloadJson
    property int trackCount
    default property list<QtObject> items
}

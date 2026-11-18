#include "shell/output_controller.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJSEngine>
#include <QQmlEngine>
#include <QVariantMap>

namespace auralis::ui {

namespace {

OutputController* g_instance = nullptr;

QString rowKey(const OutputOption& option) {
    return option.backend + QLatin1Char('\n') + option.deviceId;
}

/// 取樣率的 kHz 數字（介面規範 §6.1）：44100 → 44.1、96000 → 96。
QString kiloHertz(quint32 rate) {
    return QString::number(rate / 1000.0, 'g', 6);
}

/// DSD 倍率的寫法：DSD64、DSD128…
QString dsdName(quint32 dsdRate) {
    return QStringLiteral("DSD%1").arg(dsdRate / 44100);
}

} // namespace

OutputController::OutputController(PlaybackService& service, Notifier* notifier, OutputHooks hooks,
                                   QObject* parent)
    : QObject(parent), m_service(service), m_notifier(notifier), m_hooks(std::move(hooks)),
      m_status(service.outputStatus()) {
    // 列舉一次就好，不用多條執行緒
    m_pool.setMaxThreadCount(1);
    connect(&m_service, &PlaybackService::outputStatusChanged, this, &OutputController::onStatus);
    connect(&m_service, &PlaybackService::trackSkipped, this, &OutputController::onSkipped);
    // 來源的格式名稱取自目前曲目
    connect(&m_service, &PlaybackService::trackChanged, this, &OutputController::changed);
    refreshDevices();
}

OutputController::~OutputController() {
    m_pool.waitForDone();
}

void OutputController::setPanelOpen(bool open) {
    if (open == m_panelOpen) {
        return;
    }
    m_panelOpen = open;
    if (open) {
        // 每次打開重新列舉：拔掉的裝置從清單消失，上次選不了的裝置可以再試
        m_rowErrors.clear();
        refreshDevices();
    }
    emit panelOpenChanged();
}

QString OutputController::pathName() const {
    const OutputOption current = m_hooks.current ? m_hooks.current() : OutputOption{};
    if (!current.backend.isEmpty()) {
        return current.backend;
    }
    if (active()) {
        return m_status.backend;
    }
    return m_options.isEmpty() ? QString() : m_options.constFirst().backend;
}

QString OutputController::deviceName() const {
    const OutputOption current = m_hooks.current ? m_hooks.current() : OutputOption{};
    if (!current.backend.isEmpty()) {
        return current.deviceName;
    }
    if (active()) {
        return {};
    }
    return m_options.isEmpty() ? QString() : m_options.constFirst().deviceName;
}

QString OutputController::formatText() const {
    if (!active()) {
        return {};
    }
    const AudioFormat& device = m_status.deviceFormat;
    if (m_status.sourceFormat.dsdRate > 0) {
        // DSD 曲目寫來源的倍率加輸出方式（畫面規格 S-02）：裝置格式是 DSD 是原生；
        // 轉 PCM 的包裝器把「DSD 轉 PCM」放進原因；其他（裝置格式是 PCM）是 DoP 封包
        QString route = tr("DoP");
        if (device.rate == 0 && device.dsdRate > 0) {
            route = tr("原生");
        } else if (m_status.reasons.contains(QStringLiteral("DSD 轉 PCM"))) {
            route = tr("轉 PCM");
        }
        return tr("%1（%2）").arg(dsdName(m_status.sourceFormat.dsdRate), route);
    }
    if (device.rate == 0 && device.dsdRate > 0) {
        return dsdName(device.dsdRate);
    }
    const QString bits =
        device.isFloat ? tr("%1 bit 浮點").arg(device.bits) : tr("%1 bit").arg(device.bits);
    return tr("%1 kHz / %2").arg(kiloHertz(device.rate), bits);
}

QString OutputController::sourceText() const {
    if (!active()) {
        return {};
    }
    // 格式名稱用副檔名（CUE 曲目的路徑是 .cue，不寫格式名稱）
    const auto current = m_service.currentEntry();
    QString codec = current ? QFileInfo(current->track.path).suffix().toUpper() : QString();
    if (codec == QLatin1String("CUE")) {
        codec.clear();
    }
    const AudioFormat& source = m_status.sourceFormat;
    QString format;
    if (source.dsdRate > 0) {
        format = dsdName(source.dsdRate);
    } else {
        format = QStringLiteral("%1 / %2").arg(kiloHertz(source.rate)).arg(source.bits);
        if (source.isFloat) {
            format = tr("%1 浮點").arg(format);
        }
    }
    return codec.isEmpty() ? format : codec + QLatin1Char(' ') + format;
}

QString OutputController::processingText() const {
    if (!active()) {
        return {};
    }
    return m_status.reasons.isEmpty() ? tr("無") : m_status.reasons.join(tr("、"));
}

QString OutputController::statusText() const {
    if (!m_status.error.isEmpty()) {
        return m_status.error;
    }
    if (!active()) {
        return tr("未在播放");
    }
    return m_status.bitPerfect ? tr("位元完美") : processingText();
}

QString OutputController::statusVariant() const {
    if (!m_status.error.isEmpty()) {
        return QStringLiteral("signal");
    }
    if (!active()) {
        return QStringLiteral("idle");
    }
    // 不是位元完美不是錯誤，用 neutral（介面規範 §6）
    return m_status.bitPerfect ? QStringLiteral("amber") : QStringLiteral("neutral");
}

QString OutputController::accessibleText() const {
    QString text = tr("輸出：%1").arg(pathName());
    if (!deviceName().isEmpty()) {
        text += QLatin1Char(' ') + deviceName();
    }
    if (!formatText().isEmpty()) {
        text += tr("，") + formatText();
    }
    return text + tr("，") + statusText();
}

int OutputController::selectedRow() const {
    if (m_options.isEmpty()) {
        return -1;
    }
    const OutputOption current = m_hooks.current ? m_hooks.current() : OutputOption{};
    if (current.backend.isEmpty()) {
        return 0; // 第一次播放時用的就是第一列
    }
    for (int row = 0; row < m_options.size(); ++row) {
        if (rowKey(m_options.at(row)) == rowKey(current)) {
            return row;
        }
    }
    return -1;
}

QVariantList OutputController::devices() const {
    QVariantList rows;
    const int selected = selectedRow();
    for (int row = 0; row < m_options.size(); ++row) {
        const OutputOption& option = m_options.at(row);
        QString note = option.note;
        QString tone = QStringLiteral("muted");
        if (m_rowErrors.contains(rowKey(option))) {
            note = m_rowErrors.value(rowKey(option));
            tone = QStringLiteral("signal");
        } else if (row == selected && !m_status.error.isEmpty()) {
            note = m_status.error;
            tone = QStringLiteral("signal");
        } else if (row == selected && bitPerfect()) {
            note = tr("位元完美");
            tone = QStringLiteral("amber");
        }
        rows.append(QVariantMap{{QStringLiteral("path"), option.backend},
                                {QStringLiteral("device"), option.deviceName},
                                {QStringLiteral("note"), note},
                                {QStringLiteral("noteTone"), tone},
                                {QStringLiteral("selected"), row == selected}});
    }
    return rows;
}

void OutputController::setNotice(const QString& notice) {
    if (notice == m_notice) {
        return;
    }
    m_notice = notice;
    emit noticeChanged();
}

void OutputController::refreshDevices() {
    m_pool.start([this, list = m_hooks.list] {
        const QList<OutputOption> options = list ? list() : QList<OutputOption>{};
        // 解構時會先等這裡做完；送出的呼叫在物件拆掉後由 Qt 丟掉
        QMetaObject::invokeMethod(
            this, [this, options] { applyDevices(options); }, Qt::QueuedConnection);
    });
}

void OutputController::applyDevices(const QList<OutputOption>& options) {
    m_options = options;
    emit devicesChanged();
    emit changed();
}

void OutputController::select(int row) {
    if (row < 0 || row >= m_options.size() || !m_hooks.select) {
        return;
    }
    const OutputOption option = m_options.at(row);
    m_switching = true;
    const Result<bool> switched = m_hooks.select(option);
    m_switching = false;
    if (switched) {
        m_rowErrors.remove(rowKey(option));
        // 使用者自己選了輸出，啟動時「改用預設裝置」的說明不再適用
        setNotice({});
    } else {
        // 選不了（例：被其他程式佔用）：不切換，原因寫在那一列（畫面規格 S-14）
        m_rowErrors.insert(rowKey(option), switched.error().message);
    }
    m_status = m_service.outputStatus();
    emit devicesChanged();
    emit changed();
}

void OutputController::onStatus(const OutputStatus& status) {
    const bool newError = !status.error.isEmpty() && status.error != m_status.error;
    m_status = status;
    emit changed();
    emit devicesChanged();
    if (newError && !m_switching && m_notifier != nullptr) {
        // 裝置錯誤（AC-02-4）：播放已停止；裝置恢復後按「重試」從停下來的位置繼續
        m_notifier->notify(tr("播放已停止：%1。裝置恢復後可以重試。").arg(status.error),
                           Notifier::Kind::Error, tr("重試"), [this] { m_service.play(); });
    }
}

void OutputController::onSkipped(const TrackRef& track, const QString& reason) {
    if (m_notifier != nullptr) {
        // AC-01-3：不中斷整個佇列，只告訴使用者哪一首、為什麼
        m_notifier->notify(
            tr("無法播放：%1（%2），已跳到下一首").arg(QFileInfo(track.path).fileName(), reason));
    }
}

void OutputController::waitForDevices() {
    m_pool.waitForDone();
    QCoreApplication::sendPostedEvents(this);
}

void OutputController::setInstance(OutputController* instance) {
    g_instance = instance;
}

OutputController* OutputController::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "OutputController", "主程式要在建立 QML 引擎前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::ui

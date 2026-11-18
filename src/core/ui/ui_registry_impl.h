#pragma once

#include <auralis/navigator.h>
#include <auralis/ui_registry.h>

#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <map>
#include <memory>
#include <vector>

namespace auralis::core {

/// UiRegistry 的實作。QML 讀屬性（每一項是 {id, title, source, order} 的 map，依 order 排序），
/// 所以模組在主視窗建立之後才登記也會立刻出現。只在 UI 執行緒使用。
class UiRegistryImpl final : public QObject, public UiRegistry {
    Q_OBJECT
    Q_PROPERTY(QVariantList sidebarPages READ sidebarPages NOTIFY changed)
    Q_PROPERTY(QVariantList sidebarSections READ sidebarSections NOTIFY changed)
    Q_PROPERTY(QVariantList panels READ panels NOTIFY changed)
    Q_PROPERTY(QVariantList settingsSections READ settingsSections NOTIFY changed)
    Q_PROPERTY(QVariantList outputPanelSections READ outputPanelSections NOTIFY changed)
    Q_PROPERTY(QVariantList trackMenuSections READ trackMenuSections NOTIFY changed)
    Q_PROPERTY(QVariantList toolbarItems READ toolbarItems NOTIFY changed)
    Q_PROPERTY(QVariantList nowPlayingSections READ nowPlayingSections NOTIFY changed)
    Q_PROPERTY(QStringList pageIds READ pageIds NOTIFY changed)

public:
    enum class Kind {
        SidebarPage,
        Page,
        SidebarSection,
        Panel,
        FullscreenPage,
        SettingsSection,
        OutputPanelSection,
        TrackMenuSection,
        ToolbarItem,
        NowPlayingSection,
    };

    explicit UiRegistryImpl(QObject* parent = nullptr);

    void addSidebarPage(const UiContribution& page) override;
    void addPage(const UiContribution& page) override;
    void addSidebarSection(const UiContribution& section) override;
    void addPanel(const UiContribution& panel) override;
    void addFullscreenPage(const UiContribution& page) override;
    void addSettingsSection(const UiContribution& section) override;
    void addOutputPanelSection(const UiContribution& section) override;
    void addTrackMenuSection(const UiContribution& section) override;
    void addToolbarItem(const UiContribution& item) override;
    void addNowPlayingSection(const UiContribution& section) override;
    void addFileOpenHandler(const QStringList& extensions, FileOpenHandler handler) override;

    /// 這個路徑的副檔名有登記時回傳它的處理函式（只看字串，不查檔案系統），沒有時 nullptr。
    /// 同一次登記的副檔名回傳同一個物件，呼叫端用它把同一批的路徑合併成一次呼叫。
    std::shared_ptr<const FileOpenHandler> fileOpenHandler(const QString& path) const;

    QVariantList sidebarPages() const { return list(Kind::SidebarPage); }
    QVariantList sidebarSections() const { return list(Kind::SidebarSection); }
    QVariantList panels() const { return list(Kind::Panel); }
    QVariantList settingsSections() const { return list(Kind::SettingsSection); }
    QVariantList outputPanelSections() const { return list(Kind::OutputPanelSection); }
    QVariantList trackMenuSections() const { return list(Kind::TrackMenuSection); }
    QVariantList toolbarItems() const { return list(Kind::ToolbarItem); }
    QVariantList nowPlayingSections() const { return list(Kind::NowPlayingSection); }

    /// 可以用 Navigator 開的頁面（側欄頁、其他頁、全螢幕頁）；找不到時回傳空的 QUrl。
    QUrl pageSource(const QString& pageId) const;
    bool isFullscreenPage(const QString& pageId) const;
    /// 主視窗的全螢幕頁：order 最小的全螢幕頁，沒有時空字串。
    QString fullscreenPageId() const;
    bool hasPanel(const QString& panelId) const;
    QStringList pageIds() const;

signals:
    void changed();

private:
    struct Entry {
        Kind kind;
        UiContribution contribution;
    };
    void add(Kind kind, const UiContribution& contribution);
    QVariantList list(Kind kind) const;
    const Entry* findPage(const QString& pageId) const;

    std::vector<Entry> m_entries;
    /// 小寫副檔名 → 處理函式
    std::map<QString, std::shared_ptr<const FileOpenHandler>> m_fileOpenHandlers;
};

/// Navigator 的實作：目前頁、參數與返回的歷史。QML 依 currentSource 載入頁面。
class NavigatorImpl final : public QObject, public Navigator {
    Q_OBJECT
    Q_PROPERTY(QString currentPage READ currentPage NOTIFY currentChanged)
    Q_PROPERTY(QUrl currentSource READ currentSource NOTIFY currentChanged)
    Q_PROPERTY(QVariantMap params READ params NOTIFY currentChanged)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY currentChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY currentChanged)
    Q_PROPERTY(QString fullscreenPage READ fullscreenPage NOTIFY currentChanged)

public:
    explicit NavigatorImpl(UiRegistryImpl& registry, QObject* parent = nullptr);

    Q_INVOKABLE bool open(const QString& pageId, const QVariantMap& params = {}) override;
    Q_INVOKABLE bool back() override;
    QString currentPage() const override { return m_current.pageId; }

    QUrl currentSource() const;
    QVariantMap params() const { return m_current.params; }
    bool fullscreen() const;
    bool canGoBack() const { return !m_history.empty(); }
    QString fullscreenPage() const { return m_registry.fullscreenPageId(); }

    /// 開主視窗的全螢幕頁；沒有登記時回傳 false。已經在全螢幕時不再記一筆歷史。
    Q_INVOKABLE bool enterFullscreen();
    /// 在全螢幕時回到原頁（沒有上一頁時開第一個側欄頁）；不在全螢幕時回傳 false、不做事。
    Q_INVOKABLE bool leaveFullscreen();
    /// F11：不在全螢幕時進入、在全螢幕時離開。
    Q_INVOKABLE bool toggleFullscreen();
    /// 右側面板切到這個頁籤（主視窗殼收到 panelRequested 後展開面板並切換）；
    /// 沒有登記這個面板時回傳 false、不做事。
    Q_INVOKABLE bool showPanel(const QString& panelId);

signals:
    void currentChanged();
    void panelRequested(const QString& panelId);

private:
    struct Location {
        QString pageId;
        QVariantMap params;
    };
    UiRegistryImpl& m_registry;
    Location m_current;
    std::vector<Location> m_history;
};

} // namespace auralis::core

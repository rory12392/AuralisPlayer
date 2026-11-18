#include "ui/ui_registry_impl.h"

#include <QLoggingCategory>
#include <QVariantMap>

#include <algorithm>

Q_LOGGING_CATEGORY(lcUi, "auralis.ui")

namespace auralis::core {

namespace {

bool isPageKind(UiRegistryImpl::Kind kind) {
    return kind == UiRegistryImpl::Kind::SidebarPage || kind == UiRegistryImpl::Kind::Page ||
           kind == UiRegistryImpl::Kind::FullscreenPage;
}

} // namespace

UiRegistryImpl::UiRegistryImpl(QObject* parent) : QObject(parent) {}

void UiRegistryImpl::add(Kind kind, const UiContribution& contribution) {
    // 同一種類（頁面類算同一種，因為共用頁面 id）重複的 id：後登記的取代先登記的
    const auto sameId = [&](const Entry& entry) {
        const bool sameKind = entry.kind == kind || (isPageKind(entry.kind) && isPageKind(kind));
        return sameKind && entry.contribution.id == contribution.id;
    };
    std::erase_if(m_entries, sameId);
    m_entries.push_back({kind, contribution});
    std::stable_sort(m_entries.begin(), m_entries.end(), [](const Entry& a, const Entry& b) {
        return a.contribution.order < b.contribution.order;
    });
    qCDebug(lcUi).noquote() << "登記介面：" << contribution.id << contribution.source.toString();
    emit changed();
}

void UiRegistryImpl::addSidebarPage(const UiContribution& page) {
    add(Kind::SidebarPage, page);
}
void UiRegistryImpl::addPage(const UiContribution& page) {
    add(Kind::Page, page);
}
void UiRegistryImpl::addSidebarSection(const UiContribution& section) {
    add(Kind::SidebarSection, section);
}
void UiRegistryImpl::addPanel(const UiContribution& panel) {
    add(Kind::Panel, panel);
}
void UiRegistryImpl::addFullscreenPage(const UiContribution& page) {
    add(Kind::FullscreenPage, page);
}
void UiRegistryImpl::addSettingsSection(const UiContribution& section) {
    add(Kind::SettingsSection, section);
}
void UiRegistryImpl::addOutputPanelSection(const UiContribution& section) {
    add(Kind::OutputPanelSection, section);
}
void UiRegistryImpl::addTrackMenuSection(const UiContribution& section) {
    add(Kind::TrackMenuSection, section);
}
void UiRegistryImpl::addToolbarItem(const UiContribution& item) {
    add(Kind::ToolbarItem, item);
}
void UiRegistryImpl::addNowPlayingSection(const UiContribution& section) {
    add(Kind::NowPlayingSection, section);
}

void UiRegistryImpl::addFileOpenHandler(const QStringList& extensions, FileOpenHandler handler) {
    auto shared = std::make_shared<const FileOpenHandler>(std::move(handler));
    for (const QString& extension : extensions) {
        m_fileOpenHandlers[extension.toLower()] = shared;
    }
    qCDebug(lcUi).noquote() << "登記開檔處理：" << extensions.join(QStringLiteral("、"));
}

std::shared_ptr<const FileOpenHandler> UiRegistryImpl::fileOpenHandler(const QString& path) const {
    // 只看字串：最後一個分隔符號之後、最後一個點之後的部分
    const qsizetype slash =
        std::max(path.lastIndexOf(QLatin1Char('\\')), path.lastIndexOf(QLatin1Char('/')));
    const qsizetype dot = path.lastIndexOf(QLatin1Char('.'));
    if (dot <= slash + 1) {
        return nullptr;
    }
    const auto it = m_fileOpenHandlers.find(path.mid(dot + 1).toLower());
    return it == m_fileOpenHandlers.end() ? nullptr : it->second;
}

QVariantList UiRegistryImpl::list(Kind kind) const {
    QVariantList items;
    for (const auto& entry : m_entries) {
        if (entry.kind == kind) {
            const auto& c = entry.contribution;
            items.append(QVariantMap{{QStringLiteral("id"), c.id},
                                     {QStringLiteral("title"), c.title},
                                     {QStringLiteral("source"), c.source},
                                     {QStringLiteral("order"), c.order}});
        }
    }
    return items;
}

const UiRegistryImpl::Entry* UiRegistryImpl::findPage(const QString& pageId) const {
    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [&](const Entry& entry) {
        return isPageKind(entry.kind) && entry.contribution.id == pageId;
    });
    return it == m_entries.end() ? nullptr : &*it;
}

QUrl UiRegistryImpl::pageSource(const QString& pageId) const {
    const Entry* entry = findPage(pageId);
    return entry ? entry->contribution.source : QUrl();
}

bool UiRegistryImpl::isFullscreenPage(const QString& pageId) const {
    const Entry* entry = findPage(pageId);
    return entry && entry->kind == Kind::FullscreenPage;
}

QString UiRegistryImpl::fullscreenPageId() const {
    // m_entries 依 order 排好，第一個就是 order 最小的
    const auto it = std::find_if(m_entries.begin(), m_entries.end(), [](const Entry& entry) {
        return entry.kind == Kind::FullscreenPage;
    });
    return it == m_entries.end() ? QString() : it->contribution.id;
}

bool UiRegistryImpl::hasPanel(const QString& panelId) const {
    return std::any_of(m_entries.begin(), m_entries.end(), [&](const Entry& entry) {
        return entry.kind == Kind::Panel && entry.contribution.id == panelId;
    });
}

QStringList UiRegistryImpl::pageIds() const {
    QStringList ids;
    for (const auto& entry : m_entries) {
        if (isPageKind(entry.kind)) {
            ids << entry.contribution.id;
        }
    }
    return ids;
}

NavigatorImpl::NavigatorImpl(UiRegistryImpl& registry, QObject* parent)
    : QObject(parent), m_registry(registry) {
    // 頁面晚一點才登記時（模組在主視窗之後啟動），目前頁的來源要跟著更新
    connect(&m_registry, &UiRegistryImpl::changed, this, &NavigatorImpl::currentChanged);
}

bool NavigatorImpl::open(const QString& pageId, const QVariantMap& params) {
    if (m_registry.pageSource(pageId).isEmpty()) {
        qCWarning(lcUi).noquote() << "沒有這個頁面：" << pageId;
        return false;
    }
    if (pageId == m_current.pageId && params == m_current.params) {
        return true;
    }
    if (!m_current.pageId.isEmpty()) {
        m_history.push_back(m_current);
    }
    m_current = {pageId, params};
    emit currentChanged();
    return true;
}

bool NavigatorImpl::back() {
    if (m_history.empty()) {
        return false;
    }
    m_current = m_history.back();
    m_history.pop_back();
    emit currentChanged();
    return true;
}

QUrl NavigatorImpl::currentSource() const {
    return m_registry.pageSource(m_current.pageId);
}

bool NavigatorImpl::fullscreen() const {
    return m_registry.isFullscreenPage(m_current.pageId);
}

bool NavigatorImpl::enterFullscreen() {
    const QString page = fullscreenPage();
    if (page.isEmpty()) {
        return false;
    }
    // 已經在某個全螢幕頁時不再記一筆歷史，離開時才回得到全螢幕之前的頁面
    return fullscreen() || open(page);
}

bool NavigatorImpl::leaveFullscreen() {
    if (!fullscreen()) {
        return false;
    }
    if (back()) {
        return true;
    }
    // 一開始就在全螢幕（命令列 --page、soak）：沒有上一頁，回到第一個側欄頁
    const QVariantList sidebar = m_registry.sidebarPages();
    m_current =
        sidebar.isEmpty()
            ? Location{}
            : Location{sidebar.constFirst().toMap().value(QStringLiteral("id")).toString(), {}};
    emit currentChanged();
    return true;
}

bool NavigatorImpl::toggleFullscreen() {
    return fullscreen() ? leaveFullscreen() : enterFullscreen();
}

bool NavigatorImpl::showPanel(const QString& panelId) {
    if (!m_registry.hasPanel(panelId)) {
        return false;
    }
    emit panelRequested(panelId);
    return true;
}

} // namespace auralis::core

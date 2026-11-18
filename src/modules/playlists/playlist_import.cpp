#include "playlist_import.h"

#include "playlist_remap.h"
#include "playlists_controller.h"

#include <auralis/dialogs.h>
#include <auralis/job_runner.h>
#include <auralis/navigator.h>
#include <auralis/notifier.h>

#include <QCoreApplication>
#include <QDir>
#include <QJSEngine>
#include <QLoggingCategory>
#include <QPointer>

Q_DECLARE_LOGGING_CATEGORY(lcPlaylists)

namespace auralis::mod::pl {

namespace {
PlaylistImporter* g_instance = nullptr;

QString tr(const char* text) {
    return QCoreApplication::translate("PlaylistImport", text);
}

QString fileNameOf(const QString& file) {
    const QString native = QDir::toNativeSeparators(file);
    return native.mid(native.lastIndexOf(QLatin1Char('\\')) + 1);
}
} // namespace

PlaylistImporter::PlaylistImporter(PlaylistStore& store, PlaylistsController& playlists,
                                   Dialogs& dialogs, Notifier& notifier, JobRunner* jobs,
                                   Navigator* navigator, QObject* parent)
    : QObject(parent), m_store(store), m_playlists(playlists), m_dialogs(dialogs),
      m_notifier(notifier), m_jobs(jobs), m_navigator(navigator), m_exists(contentPathExists),
      m_reason(linkReason) {}

void PlaylistImporter::setExistsCheck(ExistsCheck exists, ReasonCheck reason) {
    m_exists = std::move(exists);
    m_reason = std::move(reason);
}

void PlaylistImporter::chooseFiles() {
    QPointer<PlaylistImporter> self(this);
    m_dialogs.chooseFiles(tr("匯入播放清單"), {tr("播放清單 (*.m3u *.m3u8 *.pls *.fpl)")},
                          [self](const QStringList& paths) {
                              if (self) {
                                  self->importFiles(paths);
                              }
                          });
}

void PlaylistImporter::importFiles(const QStringList& paths) {
    m_queue.append(paths);
    next();
}

void PlaylistImporter::dismiss() {
    if (m_result.isEmpty()) {
        return;
    }
    m_result.clear();
    emit resultChanged();
    emit busyChanged();
    next();
}

void PlaylistImporter::setCueLookup(MakeCueLookup make) {
    m_cueLookup = std::move(make);
}

PlaylistImporter::Outcome PlaylistImporter::load(const QString& file, const ExistsCheck& exists,
                                                 const ReasonCheck& reason,
                                                 const MakeCueLookup& cueLookup,
                                                 const LibraryCheck& library) {
    Outcome outcome;
    outcome.file = file;
    Result<ParsedPlaylist> parsed = readPlaylistFile(file);
    if (!parsed) {
        outcome.error = parsed.error().message;
        return outcome;
    }
    // .fpl 的內嵌 cue 向音樂庫換成 CUE 曲目的身分（T-53；讀資料庫，所以也在工作執行緒）
    matchEmbeddedCue(parsed.value().entries, cueLookup ? cueLookup() : CueLookup());
    // m3u／pls 的一行 .cue 展開成音樂庫裡這個 .cue 的全部軌（T-56）
    expandCueEntries(parsed.value().entries, library);
    // 找不到的判斷會查磁碟（每一首一次），所以也在這裡（工作執行緒）做
    outcome.missing = findMissing(parsed.value().entries, exists);
    addMissingReasons(parsed.value().entries, outcome.missing, reason);
    outcome.parsed = std::move(parsed.value());
    return outcome;
}

void PlaylistImporter::next() {
    // 一次一個檔；有結果對話框開著時等使用者關掉
    if (m_busy || !m_result.isEmpty() || m_queue.isEmpty()) {
        return;
    }
    const QString file = m_queue.takeFirst();
    setBusy(true);
    if (m_jobs == nullptr) {
        finish(load(file, m_exists, m_reason, m_cueLookup, libraryCheck()));
        return;
    }
    QPointer<PlaylistImporter> self(this);
    m_jobs->submit(tr("匯入播放清單"),
                   [self, file, exists = m_exists, reason = m_reason, cueLookup = m_cueLookup,
                    library = libraryCheck(), gate = m_gate](JobContext& context) {
                       // 模組停止後才開始的工作不碰音樂庫（它可能已經解構，P-PL-24）
                       const JobGate::Pass pass(*gate);
                       if (!pass) {
                           return;
                       }
                       Outcome outcome = load(file, exists, reason, cueLookup, library);
                       // 取消時也要回 UI 執行緒清掉忙碌狀態、接著處理佇列（M-04 審查）
                       outcome.cancelled = context.isCancelled();
                       QMetaObject::invokeMethod(
                           QCoreApplication::instance(),
                           [self, outcome = std::move(outcome)] {
                               if (self) {
                                   self->finish(outcome);
                               }
                           },
                           Qt::QueuedConnection);
                   });
}

void PlaylistImporter::finish(const Outcome& outcome) {
    if (outcome.cancelled) {
        setBusy(false);
        emit imported(outcome.file, 0);
        next();
        return;
    }
    const QString fileName = fileNameOf(outcome.file);
    QVariantMap result{{QStringLiteral("fileName"), fileName}, {QStringLiteral("id"), 0}};
    qint64 id = 0;
    if (!outcome.parsed) {
        qCWarning(lcPlaylists) << "無法讀取播放清單" << outcome.file << outcome.error;
        result.insert(QStringLiteral("kind"), QStringLiteral("unreadable"));
        result.insert(QStringLiteral("message"),
                      tr("無法讀取 %1，請確認檔案存在而且沒有被其他程式鎖住。").arg(fileName));
    } else if (outcome.parsed->unsupported) {
        // .fpl 結構檢查沒過（AC-13-3）：不建立播放清單
        result.insert(QStringLiteral("kind"), QStringLiteral("unsupported"));
        result.insert(QStringLiteral("message"),
                      tr("不支援此 .fpl 版本，請在 foobar2000 另存為 .m3u8。"));
    } else {
        const auto& entries = outcome.parsed->entries;
        QList<PlaylistItem> items;
        QVariantList missingItems;
        items.reserve(entries.size());
        for (qsizetype i = 0; i < entries.size(); ++i) {
            const ImportEntry& entry = entries.at(i);
            const bool missing = outcome.missing.at(i);
            // 被拒絕的照原文存（T-52 重新對應時換成本機路徑）；它們標 missing，不會交給播放
            // 原文的控制字元換成 U+FFFD：資料庫裡不留含換行的路徑（M-04 審查）
            const QString path = entry.path.isEmpty() ? replaceControlChars(entry.raw) : entry.path;
            items.append({static_cast<int>(i), {path, entry.subsong}, missing});
            if (missing) {
                // .fpl 有標籤時顯示「藝人 - 曲名」（T-53，標籤不寫進音樂庫）
                const QString reason = !entry.rejected.isEmpty()    ? entry.rejected
                                       : !entry.unmatched.isEmpty() ? entry.unmatched
                                                                    : tr("找不到檔案");
                missingItems.append(QVariantMap{
                    {QStringLiteral("path"), entry.label.isEmpty() ? path : entry.label},
                    {QStringLiteral("reason"), reason}});
            }
        }
        const auto created = m_store.createImported(outcome.parsed->name, items);
        if (!created) {
            qCWarning(lcPlaylists) << "無法建立匯入的播放清單" << created.error().message;
            m_notifier.notify(tr("無法建立播放清單「%1」").arg(outcome.parsed->name),
                              Notifier::Kind::Error);
        } else {
            id = created.value().id;
            m_playlists.reload();
            const int total = static_cast<int>(items.size());
            const int missing = static_cast<int>(missingItems.size());
            if (missing == 0) {
                if (m_navigator != nullptr) {
                    m_navigator->open(QStringLiteral("playlist"), {{QStringLiteral("id"), id}});
                }
                m_notifier.notify(
                    tr("已匯入「%1」，共 %2 首").arg(created.value().name).arg(total));
            } else {
                // 讀數與重新對應區的預填值（T-52）；找不到的原因用匯入時的（相對路徑的拒絕原因比較準）
                result.insert(missingResult(id, items));
                result.insert(QStringLiteral("mode"), QStringLiteral("import"));
                result.insert(QStringLiteral("name"), created.value().name);
                result.insert(QStringLiteral("missingItems"), missingItems);
            }
        }
    }
    setBusy(false);
    if (result.contains(QStringLiteral("kind"))) {
        showResult(result);
    }
    emit imported(outcome.file, id);
    next();
}

QVariantMap PlaylistImporter::missingResult(qint64 id, const QList<PlaylistItem>& items,
                                            const QHash<QString, QString>& reasons) const {
    const MissingView view = missingView(items);
    QVariantList missingItems;
    QStringList paths;
    for (const auto& [path, reason] : view.missing) {
        missingItems.append(QVariantMap{{QStringLiteral("path"), path},
                                        {QStringLiteral("reason"), reasons.value(path, reason)}});
        paths.append(path);
    }
    // 存過的對應規則有對得上的就帶入建議，沒有時「原本的資料夾」預填共同的資料夾
    QString oldRoot = commonFolder(paths);
    QString newRoot;
    const auto remaps = m_store.remaps();
    if (remaps) {
        if (const auto suggested = suggestRemap(remaps.value(), paths)) {
            oldRoot = suggested->oldRoot;
            newRoot = suggested->newRoot;
        }
    } else {
        qCWarning(lcPlaylists) << "讀取重新對應規則失敗" << remaps.error().message;
    }
    const int total = view.total;
    const int missing = static_cast<int>(view.missing.size());
    return {{QStringLiteral("kind"), QStringLiteral("missing")},
            {QStringLiteral("id"), id},
            {QStringLiteral("total"), total},
            {QStringLiteral("found"), total - missing},
            {QStringLiteral("missing"), missing},
            {QStringLiteral("missingItems"), missingItems},
            {QStringLiteral("oldRoot"), oldRoot},
            {QStringLiteral("newRoot"), newRoot},
            {QStringLiteral("remapped"), -1}};
}

void PlaylistImporter::applyRemap(const QString& oldRoot, const QString& newRoot) {
    const qint64 id = m_result.value(QStringLiteral("id")).toLongLong();
    if (m_remapping || m_result.value(QStringLiteral("kind")) != QStringLiteral("missing") ||
        id <= 0) {
        return;
    }
    setRemapping(true);
    QPointer<PlaylistImporter> self(this);
    // 被取消時只清掉 remapping（M-04 審查）
    const auto undo = [](PlaylistImporter& importer) {
        importer.setRemapping(false);
    };
    runJob(undo, [self, store = &m_store, id, oldRoot, newRoot, exists = m_exists,
                  reason = m_reason, library = libraryCheck()] {
        Result<RemapOutcome> outcome = remapPlaylist(*store, id, oldRoot, newRoot, exists, library);
        QHash<QString, QString> reasons =
            outcome ? linkReasons(outcome.value().items, reason) : QHash<QString, QString>();
        return std::function<void()>([self, id, oldRoot, newRoot, outcome = std::move(outcome),
                                      reasons = std::move(reasons)] {
            if (!self) {
                return;
            }
            self->setRemapping(false);
            if (!outcome) {
                qCWarning(lcPlaylists) << "重新對應失敗" << outcome.error().message;
                self->m_notifier.notify(tr("無法重新對應這個播放清單"), Notifier::Kind::Error);
                return;
            }
            if (self->m_result.value(QStringLiteral("id")).toLongLong() == id) {
                // 讀數與清單換成重新檢查後的；兩個欄位留使用者輸入的
                QVariantMap updated = self->missingResult(id, outcome.value().items, reasons);
                updated.insert(QStringLiteral("oldRoot"), oldRoot);
                updated.insert(QStringLiteral("newRoot"), newRoot);
                updated.insert(QStringLiteral("remapped"), outcome.value().remapped);
                for (const QString& key :
                     {QStringLiteral("mode"), QStringLiteral("fileName"), QStringLiteral("name")}) {
                    updated.insert(key, self->m_result.value(key));
                }
                self->showResult(updated);
            }
            // 項目數可能變了（整張 CUE 展開）：側欄的曲目數跟著更新（F-1）
            self->m_playlists.reload();
            emit self->m_playlists.itemsChanged(id);
        });
    });
}

void PlaylistImporter::openRemap(qint64 id) {
    if (m_busy || m_remapping || !m_result.isEmpty() || id <= 0) {
        return;
    }
    setBusy(true);
    QPointer<PlaylistImporter> self(this);
    // 被取消時清掉忙碌狀態、接著處理匯入佇列（M-04 審查）
    const auto undo = [](PlaylistImporter& importer) {
        importer.setBusy(false);
        importer.next();
    };
    runJob(undo, [self, store = &m_store, id, exists = m_exists, reason = m_reason,
                  library = libraryCheck()] {
        // 先照磁碟重新檢查（頁面上的「找不到檔案」也看磁碟），對話框的數字才對得上
        const auto remaps = store->remaps();
        Result<bool> changed = remaps ? recheckPlaylist(*store, id, remaps.value(), exists, library)
                                      : Result<bool>(remaps.error());
        Result<QList<PlaylistItem>> items = store->items(id);
        Result<std::optional<PlaylistInfo>> info = store->playlist(id);
        QHash<QString, QString> reasons =
            items ? linkReasons(items.value(), reason) : QHash<QString, QString>();
        return std::function<void()>([self, id, changed = std::move(changed),
                                      items = std::move(items), info = std::move(info),
                                      reasons = std::move(reasons)] {
            if (!self) {
                return;
            }
            self->setBusy(false);
            if (changed && changed.value()) {
                self->m_playlists.reload(); // 項目數可能變了（F-1）
                emit self->m_playlists.itemsChanged(id);
            }
            if (!changed || !items || !info || !info.value()) {
                self->m_notifier.notify(tr("無法讀取這個播放清單"), Notifier::Kind::Error);
                self->next();
                return;
            }
            QVariantMap result = self->missingResult(id, items.value(), reasons);
            result.insert(QStringLiteral("mode"), QStringLiteral("playlist"));
            result.insert(QStringLiteral("name"), info.value()->name);
            self->showResult(result);
        });
    });
}

void PlaylistImporter::chooseNewRoot() {
    QPointer<PlaylistImporter> self(this);
    m_dialogs.chooseFolder(tr("現在的資料夾"), [self](const QStringList& paths) {
        if (self && !paths.isEmpty()) {
            emit self->newRootChosen(QDir::toNativeSeparators(paths.first()));
        }
    });
}

void PlaylistImporter::setRemapping(bool remapping) {
    if (remapping != m_remapping) {
        m_remapping = remapping;
        emit remappingChanged();
    }
}

void PlaylistImporter::showResult(const QVariantMap& result) {
    m_result = result;
    emit resultChanged();
    emit busyChanged();
}

void PlaylistImporter::setBusy(bool busy) {
    if (busy != m_busy) {
        m_busy = busy;
        emit busyChanged();
    }
}

void PlaylistImporter::setInstance(PlaylistImporter* instance) {
    g_instance = instance;
}

PlaylistImporter* PlaylistImporter::create(QQmlEngine*, QJSEngine*) {
    Q_ASSERT_X(g_instance, "PlaylistImporter", "模組要在載入側欄前呼叫 setInstance");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

} // namespace auralis::mod::pl

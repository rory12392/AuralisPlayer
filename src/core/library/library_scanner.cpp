#include "library/library_scanner.h"

#include "library/cue_scan.h"
#include "library/file_enumerator.h"
#include "library/tag_reader.h"

#include <auralis/library_query.h>

#include <QDateTime>
#include <QSet>

#include <algorithm>
#include <atomic>
#include <optional>
#include <thread>
#include <vector>

namespace auralis::core {

namespace {

/// 一批讀完標籤就寫進一個交易：批次太小交易太多，太大則取消與進度的反應變慢
constexpr size_t kBatchSize = 256;

/// 交易的範圍：中途丟例外時還原，然後讓例外往外傳。
template <typename Work> void inTransaction(LibraryWriter& writer, Work&& work) {
    writer.begin();
    try {
        work();
        writer.commit();
    } catch (...) {
        writer.rollback();
        throw;
    }
}

/// path_key 是否在某個讀不到的資料夾底下（含資料夾本身）。
bool underAny(const QString& key, const QStringList& folderKeys) {
    return std::any_of(folderKeys.cbegin(), folderKeys.cend(), [&key](const QString& folder) {
        return key == folder || key.startsWith(folder + QLatin1Char('\\'));
    });
}

/// 平行讀一批檔案的標籤；回傳和 paths 同順序的結果。
std::vector<std::optional<Result<TagData>>> readBatch(const std::vector<QString>& paths) {
    std::vector<std::optional<Result<TagData>>> results(paths.size());
    std::atomic<size_t> next{0};
    auto worker = [&] {
        for (size_t i = next++; i < paths.size(); i = next++) {
            // 輔助執行緒的例外沒人接時整個程式會結束，而讀不到的檔案每次掃描（含每次啟動的
            // 增量掃描）都會重讀，所以一律在這裡轉成該檔的錯誤
            try {
                results[i] = readTags(paths[i]);
            } catch (...) {
                results[i] = readTagsExceptionError();
            }
        }
    };
    const size_t helpers = std::min(paths.size(), static_cast<size_t>(tagReaderThreads())) - 1;
    {
        std::vector<std::jthread> threads;
        for (size_t i = 0; i < helpers; ++i) {
            threads.emplace_back(worker);
        }
        worker();
    }
    return results;
}

struct RootScan {
    LibraryRoot root;
    Enumeration listing;
};

class Scanner {
public:
    Scanner(SqliteConnection& db, const ScanCallbacks& callbacks)
        : m_writer(db), m_callbacks(callbacks) {}

    ScanReport run(const QList<LibraryRoot>& roots) {
        m_report.startedAt = QDateTime::currentMSecsSinceEpoch();
        m_report.id = m_writer.insertReport(m_report.startedAt);

        // 先列舉全部音樂資料夾，總數才能在讀標籤前確定（進度顯示「已掃描數／總數」）
        std::vector<RootScan> scans;
        for (const auto& root : roots) {
            scans.push_back({root, enumerateAudioFiles(root.path, m_callbacks.cancelled)});
            m_total += static_cast<int>(scans.back().listing.files.size());
            for (const auto& failure : scans.back().listing.unreadableFolders) {
                m_report.failures.append(failure);
                ++m_report.failed;
            }
            for (const auto& link : scans.back().listing.skippedLinks) {
                m_report.failures.append(link);
                ++m_report.failed;
            }
            if (scans.back().listing.cancelled) {
                m_report.cancelled = true;
                break;
            }
        }
        m_callbacks.progress(0, m_total);

        if (!m_report.cancelled) {
            for (const auto& scan : scans) {
                scanRoot(scan);
                if (m_report.cancelled) {
                    break;
                }
            }
        }
        inTransaction(m_writer, [this] { m_writer.refreshAlbums(); });
        m_report.finishedAt = QDateTime::currentMSecsSinceEpoch();
        m_writer.finishReport(m_report);
        m_callbacks.progress(m_report.scanned, m_total);
        return m_report;
    }

private:
    void scanRoot(const RootScan& scan) {
        const auto& listing = scan.listing;
        QStringList unreadableKeys;
        for (const auto& failure : listing.unreadableFolders) {
            unreadableKeys.append(pathKey(failure.path));
        }

        // 資料夾：已有的沿用 id，新的依先序新增（父資料夾一定先有 id）
        QHash<QString, qint64> knownFolders = m_writer.knownFolders(scan.root.id);
        std::vector<qint64> folderIds(listing.folders.size(), 0);
        QSet<QString> seenFolders;
        inTransaction(m_writer, [&] {
            for (size_t i = 0; i < listing.folders.size(); ++i) {
                const auto& folder = listing.folders[i];
                const QString key = pathKey(folder.path);
                seenFolders.insert(key);
                const auto it = knownFolders.constFind(key);
                folderIds[i] =
                    it != knownFolders.cend()
                        ? it.value()
                        : m_writer.insertFolder(
                              scan.root.id,
                              folder.parent < 0 ? 0 : folderIds[static_cast<size_t>(folder.parent)],
                              folder);
            }
        });

        // 增量：大小與修改時間都沒變、上次也讀得到的檔案不重讀
        const QHash<QString, KnownFile> knownFiles = m_writer.knownFiles(scan.root.id);
        const CueScanPlan cues = planCues(listing);
        QSet<QString> seenFiles;
        std::vector<size_t> toRead;
        for (size_t i = 0; i < listing.files.size(); ++i) {
            const auto& file = listing.files[i];
            const QString key = pathKey(file.path);
            // 由 .cue 分軌的整軌檔不列成整檔曲目（以前收錄過的整檔列會在下面移除）；
            // .cue 本身在讀完音樂檔之後另外處理
            if (cues.coveredAudio.contains(key)) {
                ++m_report.scanned;
                continue;
            }
            if (isCuePath(file.path)) {
                continue;
            }
            seenFiles.insert(key);
            const auto it = knownFiles.constFind(key);
            if (it != knownFiles.cend() && it->ok && it->size == file.size &&
                it->mtimeNs == file.mtimeNs) {
                ++m_report.scanned;
                continue;
            }
            toRead.push_back(i);
        }
        m_callbacks.progress(m_report.scanned, m_total);

        for (size_t start = 0; start < toRead.size(); start += kBatchSize) {
            if (m_callbacks.cancelled()) {
                m_report.cancelled = true;
                return; // 沒讀完時不能判斷哪些檔案已刪除，所以也不移除
            }
            const size_t end = std::min(toRead.size(), start + kBatchSize);
            std::vector<QString> paths;
            for (size_t i = start; i < end; ++i) {
                paths.push_back(listing.files[toRead[i]].path);
            }
            const auto results = readBatch(paths);
            inTransaction(m_writer, [&] {
                for (size_t i = start; i < end; ++i) {
                    const auto& file = listing.files[toRead[i]];
                    const qint64 folderId = folderIds[static_cast<size_t>(file.folder)];
                    const auto& tags = *results[i - start];
                    const bool known = knownFiles.contains(pathKey(file.path));
                    if (tags) {
                        m_writer.writeTrack(folderId, file, tags.value());
                        ++(known ? m_report.updated : m_report.added);
                    } else {
                        m_writer.writeUnreadable(folderId, file, tags.error().message);
                        m_report.failures.append({file.path, tags.error().message});
                        ++m_report.failed;
                    }
                    ++m_report.scanned;
                }
            });
            m_callbacks.progress(m_report.scanned, m_total);
        }
        if (!scanCues(listing, cues, folderIds, knownFiles, seenFiles)) {
            return;
        }

        // 已不存在的檔案與資料夾；讀不到的資料夾底下的保留（可能只是暫時離線或沒有權限）
        inTransaction(m_writer, [&] {
            for (auto it = knownFiles.cbegin(); it != knownFiles.cend(); ++it) {
                if (!seenFiles.contains(it.key()) && !underAny(it.key(), unreadableKeys)) {
                    m_writer.deleteFile(it->id);
                    ++m_report.removed;
                }
            }
            for (auto it = knownFolders.cbegin(); it != knownFolders.cend(); ++it) {
                if (!seenFolders.contains(it.key()) && !underAny(it.key(), unreadableKeys)) {
                    m_writer.deleteFolder(it.value());
                }
            }
            // 資料夾封面：讀不到的資料夾沒有列出檔案，不能當成圖檔被刪掉
            for (size_t i = 0; i < listing.folders.size(); ++i) {
                if (!underAny(pathKey(listing.folders[i].path), unreadableKeys)) {
                    m_writer.assignFolderCover(folderIds[i], listing.folders[i]);
                }
            }
        });
    }

    /// .cue：沒變（.cue 與整軌檔的修改時間都沒變）的不重讀；讀不到的記進報告、下次重讀。
    /// 被取消時回傳 false（和讀標籤一樣，不移除任何東西）。
    bool scanCues(const Enumeration& listing, const CueScanPlan& cues,
                  const std::vector<qint64>& folderIds, const QHash<QString, KnownFile>& knownFiles,
                  QSet<QString>& seenFiles) {
        for (const auto& plan : cues.cues) {
            if (m_callbacks.cancelled()) {
                m_report.cancelled = true;
                return false;
            }
            ++m_report.scanned;
            if (plan.duplicate) {
                continue; // 指向的整軌檔已由另一個 .cue 分軌，不收錄（以前收錄過的會被移除）
            }
            EnumeratedFile file = listing.files[plan.fileIndex];
            file.mtimeNs = plan.mtimeNs;
            const QString key = pathKey(file.path);
            seenFiles.insert(key);
            const auto known = knownFiles.constFind(key);
            if (plan.cue && known != knownFiles.cend() && known->ok && known->size == file.size &&
                known->mtimeNs == file.mtimeNs) {
                continue;
            }
            QString error = plan.error;
            std::vector<CueTrackRow> rows;
            if (plan.cue) {
                auto read = readCueTracks(file.path, *plan.cue);
                if (read) {
                    rows = std::move(read).value();
                } else {
                    error = read.error().message;
                }
            }
            const qint64 folderId = folderIds[static_cast<size_t>(file.folder)];
            inTransaction(m_writer, [&] {
                if (error.isEmpty()) {
                    m_writer.writeCueTracks(folderId, file, rows);
                    ++(known != knownFiles.cend() ? m_report.updated : m_report.added);
                } else {
                    m_writer.writeUnreadable(folderId, file, error);
                    m_report.failures.append({file.path, error});
                    ++m_report.failed;
                }
            });
        }
        m_callbacks.progress(m_report.scanned, m_total);
        return true;
    }

    LibraryWriter m_writer;
    const ScanCallbacks& m_callbacks;
    ScanReport m_report;
    int m_total = 0;
};

} // namespace

int tagReaderThreads() {
    return static_cast<int>(std::clamp(std::thread::hardware_concurrency(), 1u, 4u));
}

ScanReport scanLibrary(SqliteConnection& db, const QList<LibraryRoot>& roots,
                       const ScanCallbacks& callbacks) {
    return Scanner(db, callbacks).run(roots);
}

} // namespace auralis::core

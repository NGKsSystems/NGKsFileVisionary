#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTextStream>

#include <atomic>
#include <chrono>
#include <thread>

#include "core/db/MetaStore.h"
#include "core/db/SqlHelpers.h"
#include "core/query/QueryCore.h"
#include "core/query/QueryTypes.h"
#include "core/scan/ScanCoordinator.h"
#include "core/scan/ScanTask.h"
#include "core/services/RefreshPolicy.h"
#include "core/services/RefreshTypes.h"
#include "core/services/VisionIndexService.h"
#include "ui/MainWindow.h"
#include "ui/model/DirectoryModel.h"
#include "ui/model/QueryResultAdapter.h"

#ifdef _MSC_VER
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#endif

namespace {
void writeLogFile(const QString& logPath, const QStringList& lines)
{
    QFileInfo logInfo(logPath);
    QDir().mkpath(logInfo.absolutePath());

    QSaveFile out(logPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QTextStream ts(&out);
    for (const QString& line : lines) {
        ts << line << '\n';
    }
    out.commit();
}
}

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("NGKsFileVisionary"));
    parser.addHelpOption();

    QCommandLineOption testModeOption(QStringList() << QStringLiteral("test-mode"),
                                      QStringLiteral("Enable deterministic UI test surface."));
    QCommandLineOption actionLogOption(QStringList() << QStringLiteral("action-log"),
                                       QStringLiteral("Path to structured UI action trace log."),
                                       QStringLiteral("path"));
    QCommandLineOption testScriptOption(QStringList() << QStringLiteral("test-script"),
                                        QStringLiteral("Path to scripted action dispatch file."),
                                        QStringLiteral("path"));

    QCommandLineOption metastoreSmokeOption(QStringList() << QStringLiteral("metastore-smoke"),
                                            QStringLiteral("Run metastore smoke check and exit."));
    QCommandLineOption metastorePathOption(QStringList() << QStringLiteral("metastore-path"),
                                           QStringLiteral("Path to sqlite file for metastore smoke run."),
                                           QStringLiteral("path"));
    QCommandLineOption metastoreLogOption(QStringList() << QStringLiteral("metastore-log"),
                                          QStringLiteral("Path to metastore smoke log file."),
                                          QStringLiteral("path"));

    QCommandLineOption scanSmokeOption(QStringList() << QStringLiteral("scan-smoke"),
                                       QStringLiteral("Run scan ingest smoke validation and exit."));
    QCommandLineOption scanRootOption(QStringList() << QStringLiteral("scan-root"),
                                      QStringLiteral("Root path to scan for scan-smoke mode."),
                                      QStringLiteral("path"));
    QCommandLineOption scanDbPathOption(QStringList() << QStringLiteral("scan-db-path"),
                                        QStringLiteral("Path to sqlite file for scan-smoke mode."),
                                        QStringLiteral("path"));
    QCommandLineOption scanLogOption(QStringList() << QStringLiteral("scan-log"),
                                     QStringLiteral("Path to scan smoke log file."),
                                     QStringLiteral("path"));
    QCommandLineOption scanBatchSizeOption(QStringList() << QStringLiteral("scan-batch-size"),
                                           QStringLiteral("Batch size for scan ingest."),
                                           QStringLiteral("n"));
    QCommandLineOption scanCancelAfterOption(QStringList() << QStringLiteral("scan-cancel-after"),
                                             QStringLiteral("Cancel scan after N milliseconds."),
                                             QStringLiteral("ms"));

    QCommandLineOption querySmokeOption(QStringList() << QStringLiteral("query-smoke"),
                                        QStringLiteral("Run QueryCore smoke validation and exit."));
    QCommandLineOption queryDbPathOption(QStringList() << QStringLiteral("query-db-path"),
                                         QStringLiteral("Path to sqlite file for query-smoke mode."),
                                         QStringLiteral("path"));
    QCommandLineOption queryRootOption(QStringList() << QStringLiteral("query-root"),
                                       QStringLiteral("Root path for query-smoke mode."),
                                       QStringLiteral("path"));
    QCommandLineOption queryLogOption(QStringList() << QStringLiteral("query-log"),
                                      QStringLiteral("Path to query smoke log file."),
                                      QStringLiteral("path"));
    QCommandLineOption queryModeOption(QStringList() << QStringLiteral("query-mode"),
                                       QStringLiteral("Query mode: children|flat|subtree|search|all."),
                                       QStringLiteral("mode"));
    QCommandLineOption queryFilterOption(QStringList() << QStringLiteral("query-filter"),
                                         QStringLiteral("Substring filter for query mode."),
                                         QStringLiteral("text"));
    QCommandLineOption queryExtOption(QStringList() << QStringLiteral("query-ext"),
                                      QStringLiteral("Semicolon-delimited extension filter (e.g. .cpp;.h)."),
                                      QStringLiteral("exts"));
    QCommandLineOption querySortOption(QStringList() << QStringLiteral("query-sort"),
                                       QStringLiteral("Sort field: name|modified|size|path."),
                                       QStringLiteral("field"));
    QCommandLineOption queryDescOption(QStringList() << QStringLiteral("query-desc"),
                                       QStringLiteral("Sort descending."));
    QCommandLineOption queryMaxDepthOption(QStringList() << QStringLiteral("query-max-depth"),
                                           QStringLiteral("Maximum depth for flat/subtree query."),
                                           QStringLiteral("n"));
    QCommandLineOption queryIncludeHiddenOption(QStringList() << QStringLiteral("query-include-hidden"),
                                                QStringLiteral("Include hidden entries in query output."));
    QCommandLineOption queryIncludeSystemOption(QStringList() << QStringLiteral("query-include-system"),
                                                QStringLiteral("Include system entries in query output."));

    QCommandLineOption uiQuerySmokeOption(QStringList() << QStringLiteral("ui-query-smoke"),
                                          QStringLiteral("Run DB-backed UI mode smoke validation and exit."));
    QCommandLineOption uiRootOption(QStringList() << QStringLiteral("ui-root"),
                                    QStringLiteral("Root path for UI query smoke mode."),
                                    QStringLiteral("path"));
    QCommandLineOption uiDbPathOption(QStringList() << QStringLiteral("ui-db-path"),
                                      QStringLiteral("Path to sqlite file for UI query smoke mode."),
                                      QStringLiteral("path"));
    QCommandLineOption uiModeOption(QStringList() << QStringLiteral("ui-mode"),
                                    QStringLiteral("UI mode: standard|flat|hierarchy|all."),
                                    QStringLiteral("mode"));
    QCommandLineOption uiLogOption(QStringList() << QStringLiteral("ui-log"),
                                   QStringLiteral("Path to UI query smoke log file."),
                                   QStringLiteral("path"));

    QCommandLineOption refreshSmokeOption(QStringList() << QStringLiteral("refresh-smoke"),
                                          QStringLiteral("Run background refresh smoke validation and exit."));
    QCommandLineOption refreshDbPathOption(QStringList() << QStringLiteral("refresh-db-path"),
                                           QStringLiteral("Path to sqlite file for refresh-smoke mode."),
                                           QStringLiteral("path"));
    QCommandLineOption refreshRootOption(QStringList() << QStringLiteral("refresh-root"),
                                         QStringLiteral("Root path for refresh-smoke mode."),
                                         QStringLiteral("path"));
    QCommandLineOption refreshLogOption(QStringList() << QStringLiteral("refresh-log"),
                                        QStringLiteral("Path to refresh smoke log file."),
                                        QStringLiteral("path"));
    QCommandLineOption refreshForceOption(QStringList() << QStringLiteral("refresh-force"),
                                          QStringLiteral("Force targeted refresh request."));
    QCommandLineOption refreshStaleSecondsOption(QStringList() << QStringLiteral("refresh-stale-seconds"),
                                                 QStringLiteral("Staleness threshold for maybeRefresh checks."),
                                                 QStringLiteral("n"));
    QCommandLineOption refreshRepeatOption(QStringList() << QStringLiteral("refresh-repeat"),
                                           QStringLiteral("How many refresh request cycles to execute."),
                                           QStringLiteral("n"));
    QCommandLineOption refreshDelayMsOption(QStringList() << QStringLiteral("refresh-delay-ms"),
                                            QStringLiteral("Delay between refresh request cycles."),
                                            QStringLiteral("ms"));
    QCommandLineOption refreshQueryModeOption(QStringList() << QStringLiteral("refresh-query-mode"),
                                              QStringLiteral("Query mode: standard|flat|hierarchy."),
                                              QStringLiteral("mode"));

    parser.addOption(testModeOption);
    parser.addOption(actionLogOption);
    parser.addOption(testScriptOption);

    parser.addOption(metastoreSmokeOption);
    parser.addOption(metastorePathOption);
    parser.addOption(metastoreLogOption);

    parser.addOption(scanSmokeOption);
    parser.addOption(scanRootOption);
    parser.addOption(scanDbPathOption);
    parser.addOption(scanLogOption);
    parser.addOption(scanBatchSizeOption);
    parser.addOption(scanCancelAfterOption);

    parser.addOption(querySmokeOption);
    parser.addOption(queryDbPathOption);
    parser.addOption(queryRootOption);
    parser.addOption(queryLogOption);
    parser.addOption(queryModeOption);
    parser.addOption(queryFilterOption);
    parser.addOption(queryExtOption);
    parser.addOption(querySortOption);
    parser.addOption(queryDescOption);
    parser.addOption(queryMaxDepthOption);
    parser.addOption(queryIncludeHiddenOption);
    parser.addOption(queryIncludeSystemOption);
    parser.addOption(uiQuerySmokeOption);
    parser.addOption(uiRootOption);
    parser.addOption(uiDbPathOption);
    parser.addOption(uiModeOption);
    parser.addOption(uiLogOption);
    parser.addOption(refreshSmokeOption);
    parser.addOption(refreshDbPathOption);
    parser.addOption(refreshRootOption);
    parser.addOption(refreshLogOption);
    parser.addOption(refreshForceOption);
    parser.addOption(refreshStaleSecondsOption);
    parser.addOption(refreshRepeatOption);
    parser.addOption(refreshDelayMsOption);
    parser.addOption(refreshQueryModeOption);

    parser.addPositionalArgument(QStringLiteral("root"), QStringLiteral("Optional startup root path."));
    parser.process(app);

    const bool testMode = parser.isSet(testModeOption);
    const QString actionLogPath = parser.value(actionLogOption);
    const QString testScriptPath = parser.value(testScriptOption);
    const QString startupRoot = parser.positionalArguments().isEmpty() ? QString() : parser.positionalArguments().first();

    if (parser.isSet(metastoreSmokeOption)) {
        const QString dbPath = parser.isSet(metastorePathOption)
            ? parser.value(metastorePathOption)
            : QDir::cleanPath(QDir::current().filePath(QStringLiteral("debug/metastore.sqlite3")));
        const QString logPath = parser.isSet(metastoreLogOption)
            ? parser.value(metastoreLogOption)
            : QDir::cleanPath(QDir::current().filePath(QStringLiteral("debug/metastore_smoke.log")));

        QStringList lines;
        lines << QStringLiteral("mode=metastore_smoke");
        lines << QStringLiteral("db_path=%1").arg(QDir::toNativeSeparators(dbPath));

        MetaStore store;
        QString errorText;
        QString migrationLog;
        bool ok = store.initialize(dbPath, &errorText, &migrationLog);
        lines << QStringLiteral("initialize_ok=%1").arg(ok ? QStringLiteral("true") : QStringLiteral("false"));
        if (!migrationLog.isEmpty()) {
            lines << QStringLiteral("migration_log_begin");
            lines << migrationLog.trimmed();
            lines << QStringLiteral("migration_log_end");
        }

        if (ok) {
            const int schemaVersion = store.schemaVersion(&errorText);
            lines << QStringLiteral("schema_version=%1").arg(schemaVersion);

            VolumeRecord volume;
            volume.volumeKey = QStringLiteral("smoke:volume:c");
            volume.rootPath = QStringLiteral("C:/smoke-root");
            volume.displayName = QStringLiteral("Smoke Volume");
            volume.fsType = QStringLiteral("NTFS");
            volume.serialNumber = QStringLiteral("SMK001");
            qint64 volumeId = 0;
            ok = store.upsertVolume(volume, &volumeId, &errorText);
            lines << QStringLiteral("upsert_volume_ok=%1").arg(ok ? QStringLiteral("true") : QStringLiteral("false"));
            lines << QStringLiteral("upsert_volume_id=%1").arg(volumeId);

            if (ok) {
                EntryRecord entry;
                entry.volumeId = volumeId;
                entry.path = QStringLiteral("C:/smoke-root/demo.txt");
                entry.name = QStringLiteral("demo.txt");
                entry.normalizedName = SqlHelpers::normalizedName(entry.name);
                entry.extension = QStringLiteral(".txt");
                entry.isDir = false;
                entry.hasSizeBytes = true;
                entry.sizeBytes = 1234;
                entry.modifiedUtc = SqlHelpers::utcNowIso();
                entry.existsFlag = true;
                entry.metadataVersion = 1;
                qint64 entryId = 0;
                ok = store.upsertEntry(entry, &entryId, &errorText);
                lines << QStringLiteral("upsert_entry_ok=%1").arg(ok ? QStringLiteral("true") : QStringLiteral("false"));
                lines << QStringLiteral("upsert_entry_id=%1").arg(entryId);
            }

            if (ok) {
                FavoriteRecord favorite;
                favorite.path = QStringLiteral("C:/smoke-root/demo.txt");
                favorite.label = QStringLiteral("Smoke Favorite");
                favorite.sortOrder = 10;
                qint64 favoriteId = 0;
                ok = store.upsertFavorite(favorite, &favoriteId, &errorText);
                lines << QStringLiteral("upsert_favorite_ok=%1").arg(ok ? QStringLiteral("true") : QStringLiteral("false"));
                lines << QStringLiteral("upsert_favorite_id=%1").arg(favoriteId);
            }

            if (ok) {
                EntryRecord fetched;
                ok = store.getEntryByPath(QStringLiteral("C:/smoke-root/demo.txt"), &fetched, &errorText);
                lines << QStringLiteral("read_entry_ok=%1").arg(ok ? QStringLiteral("true") : QStringLiteral("false"));
                if (ok) {
                    lines << QStringLiteral("read_entry_name=%1").arg(fetched.name);
                }
            }

            if (ok) {
                QVector<FavoriteRecord> favorites;
                ok = store.listFavorites(&favorites, &errorText);
                lines << QStringLiteral("list_favorites_ok=%1").arg(ok ? QStringLiteral("true") : QStringLiteral("false"));
                lines << QStringLiteral("favorites_count=%1").arg(favorites.size());
            }
        }

        if (!ok && !errorText.isEmpty()) {
            lines << QStringLiteral("error=%1").arg(errorText);
        }

        writeLogFile(logPath, lines);
        store.shutdown();
        return ok ? 0 : 2;
    }

    if (parser.isSet(scanSmokeOption)) {
        const QString rootPath = parser.isSet(scanRootOption)
            ? QDir::cleanPath(parser.value(scanRootOption))
            : QDir::cleanPath(QDir::currentPath());
        const QString dbPath = parser.isSet(scanDbPathOption)
            ? QDir::cleanPath(parser.value(scanDbPathOption))
            : QDir::cleanPath(QDir::current().filePath(QStringLiteral("debug/metastore_vie_p2.sqlite3")));
        const QString logPath = parser.isSet(scanLogOption)
            ? QDir::cleanPath(parser.value(scanLogOption))
            : QDir::cleanPath(QDir::current().filePath(QStringLiteral("debug/scan_smoke.log")));
        const int batchSize = parser.isSet(scanBatchSizeOption)
            ? parser.value(scanBatchSizeOption).toInt()
            : 500;
        const int cancelAfterMs = parser.isSet(scanCancelAfterOption)
            ? parser.value(scanCancelAfterOption).toInt()
            : 0;

        QStringList lines;
        lines << QStringLiteral("mode=scan_smoke");
        lines << QStringLiteral("scan_root=%1").arg(QDir::toNativeSeparators(rootPath));
        lines << QStringLiteral("db_path=%1").arg(QDir::toNativeSeparators(dbPath));
        lines << QStringLiteral("batch_size=%1").arg(batchSize);
        lines << QStringLiteral("cancel_after_ms=%1").arg(cancelAfterMs);

        MetaStore store;
        QString errorText;
        QString migrationLog;
        bool ok = store.initialize(dbPath, &errorText, &migrationLog);
        lines << QStringLiteral("initialize_ok=%1").arg(ok ? QStringLiteral("true") : QStringLiteral("false"));
        if (!migrationLog.isEmpty()) {
            lines << QStringLiteral("migration_log_begin");
            lines << migrationLog.trimmed();
            lines << QStringLiteral("migration_log_end");
        }

        ScanCoordinatorResult result;
        std::atomic_bool cancelRequested(false);
        std::thread cancelThread;

        if (ok) {
            ScanTask task;
            task.rootPath = rootPath;
            task.mode = QStringLiteral("smoke");
            task.batchSize = batchSize;
            task.cancelRequested = &cancelRequested;

            if (cancelAfterMs > 0) {
                cancelThread = std::thread([&cancelRequested, cancelAfterMs]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(cancelAfterMs));
                    cancelRequested.store(true);
                });
            }

            ScanCoordinator coordinator(store);
            ScanCoordinator::Callbacks callbacks;
            callbacks.onLog = [&](const QString& line) {
                lines << QStringLiteral("worker_log=%1").arg(line);
            };
            callbacks.onProgress = [&](const ScanCoordinatorResult& state) {
                lines << QStringLiteral("progress total_seen=%1 inserted=%2 updated=%3 errors=%4")
                             .arg(state.totalSeen)
                             .arg(state.totalInserted)
                             .arg(state.totalUpdated)
                             .arg(state.errorCount);
            };

            ok = coordinator.runScan(task, callbacks, &result);
            lines << QStringLiteral("scan_run_ok=%1").arg(ok ? QStringLiteral("true") : QStringLiteral("false"));
            lines << QStringLiteral("scan_canceled=%1").arg(result.canceled ? QStringLiteral("true") : QStringLiteral("false"));
            lines << QStringLiteral("scan_success=%1").arg(result.success ? QStringLiteral("true") : QStringLiteral("false"));
            lines << QStringLiteral("scan_session_id=%1").arg(result.sessionId);
            lines << QStringLiteral("scan_volume_id=%1").arg(result.volumeId);
            lines << QStringLiteral("scan_total_seen=%1").arg(result.totalSeen);
            lines << QStringLiteral("scan_total_inserted=%1").arg(result.totalInserted);
            lines << QStringLiteral("scan_total_updated=%1").arg(result.totalUpdated);
            lines << QStringLiteral("scan_total_removed=%1").arg(result.totalRemoved);
            lines << QStringLiteral("scan_error_count=%1").arg(result.errorCount);
            if (!result.errorText.isEmpty()) {
                lines << QStringLiteral("scan_error=%1").arg(result.errorText);
            }
        }

        if (cancelThread.joinable()) {
            cancelThread.join();
        }

        if (!ok && !errorText.isEmpty()) {
            lines << QStringLiteral("error=%1").arg(errorText);
        }

        writeLogFile(logPath, lines);
        store.shutdown();
        if (result.canceled) {
            return 0;
        }
        return ok ? 0 : 3;
    }

    if (parser.isSet(querySmokeOption)) {
        const QString dbPath = parser.isSet(queryDbPathOption)
            ? QDir::cleanPath(parser.value(queryDbPathOption))
            : QDir::cleanPath(QDir::current().filePath(QStringLiteral("debug/metastore_vie_p2_main.sqlite3")));
        const QString logPath = parser.isSet(queryLogOption)
            ? QDir::cleanPath(parser.value(queryLogOption))
            : QDir::cleanPath(QDir::current().filePath(QStringLiteral("debug/query_smoke.log")));
        const QString requestedRoot = parser.isSet(queryRootOption)
            ? QDir::cleanPath(parser.value(queryRootOption))
            : QString();
        const QString queryMode = parser.isSet(queryModeOption)
            ? parser.value(queryModeOption).trimmed().toLower()
            : QStringLiteral("all");

        QueryOptions options;
        options.includeHidden = parser.isSet(queryIncludeHiddenOption);
        options.includeSystem = parser.isSet(queryIncludeSystemOption);
        options.extensionFilter = parser.value(queryExtOption);
        options.substringFilter = parser.value(queryFilterOption);
        options.ascending = !parser.isSet(queryDescOption);
        options.maxDepth = parser.isSet(queryMaxDepthOption)
            ? parser.value(queryMaxDepthOption).toInt()
            : -1;

        QString sortError;
        if (!QueryTypesUtil::parseSortField(parser.value(querySortOption), &options.sortField)) {
            sortError = QStringLiteral("invalid_sort_field");
            options.sortField = QuerySortField::Name;
        }

        QStringList lines;
        lines << QStringLiteral("mode=query_smoke");
        lines << QStringLiteral("db_path=%1").arg(QDir::toNativeSeparators(dbPath));
        lines << QStringLiteral("requested_root=%1").arg(QDir::toNativeSeparators(requestedRoot));
        lines << QStringLiteral("query_mode=%1").arg(queryMode);
        lines << QStringLiteral("sort_field=%1").arg(QueryTypesUtil::sortFieldToString(options.sortField));
        lines << QStringLiteral("ascending=%1").arg(options.ascending ? QStringLiteral("true") : QStringLiteral("false"));
        lines << QStringLiteral("max_depth=%1").arg(options.maxDepth);
        lines << QStringLiteral("extension_filter=%1").arg(options.extensionFilter);
        lines << QStringLiteral("substring_filter=%1").arg(options.substringFilter);
        lines << QStringLiteral("include_hidden=%1").arg(options.includeHidden ? QStringLiteral("true") : QStringLiteral("false"));
        lines << QStringLiteral("include_system=%1").arg(options.includeSystem ? QStringLiteral("true") : QStringLiteral("false"));

        if (!sortError.isEmpty()) {
            lines << QStringLiteral("sort_error=%1").arg(sortError);
            lines << QStringLiteral("sort_fallback=name");
        }

        MetaStore store;
        QString errorText;
        QString migrationLog;
        bool ok = store.initialize(dbPath, &errorText, &migrationLog);
        lines << QStringLiteral("initialize_ok=%1").arg(ok ? QStringLiteral("true") : QStringLiteral("false"));
        if (!migrationLog.isEmpty()) {
            lines << QStringLiteral("migration_log_begin");
            lines << migrationLog.trimmed();
            lines << QStringLiteral("migration_log_end");
        }

        QString rootPath = requestedRoot;
        if (ok && rootPath.isEmpty()) {
            QVector<VolumeRecord> volumes;
            if (store.listVolumes(&volumes, &errorText) && !volumes.isEmpty()) {
                rootPath = volumes.first().rootPath;
            }
        }

        lines << QStringLiteral("resolved_root=%1").arg(rootPath);

        auto writeRows = [&](const QString& sectionName, const QueryResult& result) {
            lines << QStringLiteral("%1_begin").arg(sectionName);
            lines << QStringLiteral("ok=%1").arg(result.ok ? QStringLiteral("true") : QStringLiteral("false"));
            if (!result.errorText.isEmpty()) {
                lines << QStringLiteral("error=%1").arg(result.errorText);
            }
            lines << QStringLiteral("count=%1").arg(result.totalCount);
            const int maxRows = qMin(20, result.rows.size());
            for (int i = 0; i < maxRows; ++i) {
                const QueryRow& r = result.rows.at(i);
                lines << QStringLiteral("row id=%1 parent_id=%2 is_dir=%3 ext=%4 hidden=%5 system=%6 size=%7 modified=%8 path=%9")
                             .arg(r.id)
                             .arg(r.hasParentId ? QString::number(r.parentId) : QStringLiteral("NULL"))
                             .arg(r.isDir ? 1 : 0)
                             .arg(r.extension)
                             .arg(r.hiddenFlag ? 1 : 0)
                             .arg(r.systemFlag ? 1 : 0)
                             .arg(r.hasSizeBytes ? QString::number(r.sizeBytes) : QStringLiteral("NULL"))
                             .arg(r.modifiedUtc)
                             .arg(r.path);
            }
            lines << QStringLiteral("%1_end").arg(sectionName);
        };

        bool queryChecksOk = true;
        if (ok) {
            if (rootPath.isEmpty()) {
                ok = false;
                errorText = QStringLiteral("query_root_unresolved");
            } else {
                QueryCore queryCore(store);
                const bool runAll = (queryMode == QStringLiteral("all"));

                if (runAll || queryMode == QStringLiteral("children")) {
                    const QueryResult r = queryCore.queryChildren(rootPath, options);
                    writeRows(QStringLiteral("children_query"), r);
                    queryChecksOk = queryChecksOk && r.ok;
                }

                if (runAll || queryMode == QStringLiteral("flat")) {
                    const QueryResult r = queryCore.queryFlat(rootPath, options);
                    writeRows(QStringLiteral("flat_query"), r);
                    queryChecksOk = queryChecksOk && r.ok;
                }

                if (runAll || queryMode == QStringLiteral("subtree")) {
                    QueryOptions subtreeOptions = options;
                    if (subtreeOptions.maxDepth < 0) {
                        subtreeOptions.maxDepth = 2;
                    }
                    const QueryResult r = queryCore.querySubtree(rootPath, subtreeOptions);
                    writeRows(QStringLiteral("subtree_query"), r);
                    queryChecksOk = queryChecksOk && r.ok;
                }

                if (runAll || queryMode == QStringLiteral("search")) {
                    QueryOptions filtered = options;
                    if (filtered.substringFilter.isEmpty()) {
                        filtered.substringFilter = QStringLiteral("src");
                    }
                    if (filtered.extensionFilter.isEmpty()) {
                        filtered.extensionFilter = QStringLiteral(".txt;.cpp;.h");
                    }
                    const QueryResult filteredResult = queryCore.querySearch(rootPath, filtered);
                    writeRows(QStringLiteral("filtered_query"), filteredResult);
                    queryChecksOk = queryChecksOk && filteredResult.ok;

                    QueryOptions sorted = options;
                    sorted.sortField = QuerySortField::Size;
                    sorted.ascending = false;
                    const QueryResult sortedResult = queryCore.queryFlat(rootPath, sorted);
                    writeRows(QStringLiteral("sorted_query"), sortedResult);
                    queryChecksOk = queryChecksOk && sortedResult.ok;

                    const QString badRoot = rootPath + QStringLiteral("/__query_missing__");
                    const QueryResult badRootResult = queryCore.queryChildren(badRoot, options);
                    writeRows(QStringLiteral("bad_root_query"), badRootResult);
                    queryChecksOk = queryChecksOk && !badRootResult.ok;
                }

                lines << QStringLiteral("root_count=%1").arg(store.countEntriesUnderRoot(rootPath, -1, &errorText));

                QVector<EntryRecord> sampleRows;
                if (store.listSampleRows(10, &sampleRows, &errorText)) {
                    lines << QStringLiteral("sample_rows_count=%1").arg(sampleRows.size());
                    for (const EntryRecord& r : sampleRows) {
                        lines << QStringLiteral("sample_row id=%1 parent_id=%2 is_dir=%3 path=%4")
                                     .arg(r.id)
                                     .arg(r.hasParentId ? QString::number(r.parentId) : QStringLiteral("NULL"))
                                     .arg(r.isDir ? 1 : 0)
                                     .arg(r.path);
                    }
                }

                lines << QStringLiteral("query_checks_ok=%1").arg(queryChecksOk ? QStringLiteral("true") : QStringLiteral("false"));
                ok = ok && queryChecksOk;
            }
        }

        if (!ok && !errorText.isEmpty()) {
            lines << QStringLiteral("error=%1").arg(errorText);
        }

        writeLogFile(logPath, lines);
        store.shutdown();
        return ok ? 0 : 4;
    }

    if (parser.isSet(uiQuerySmokeOption)) {
        const QString dbPath = parser.isSet(uiDbPathOption)
            ? QDir::cleanPath(parser.value(uiDbPathOption))
            : QDir::cleanPath(QDir::current().filePath(QStringLiteral("debug/metastore_vie_p3.sqlite3")));
        const QString rootPath = parser.isSet(uiRootOption)
            ? QDir::cleanPath(parser.value(uiRootOption))
            : QString();
        const QString modeValue = parser.isSet(uiModeOption)
            ? parser.value(uiModeOption).trimmed().toLower()
            : QStringLiteral("all");
        const QString logPath = parser.isSet(uiLogOption)
            ? QDir::cleanPath(parser.value(uiLogOption))
            : QDir::cleanPath(QDir::current().filePath(QStringLiteral("debug/ui_query_smoke.log")));

        QStringList lines;
        lines << QStringLiteral("mode=ui_query_smoke");
        lines << QStringLiteral("db_path=%1").arg(QDir::toNativeSeparators(dbPath));
        lines << QStringLiteral("root_path=%1").arg(QDir::toNativeSeparators(rootPath));
        lines << QStringLiteral("ui_mode=%1").arg(modeValue);

        DirectoryModel uiDirectoryModel;
        QString errorText;
        bool ok = uiDirectoryModel.initialize(dbPath, &errorText);
        lines << QStringLiteral("initialize_ok=%1").arg(ok ? QStringLiteral("true") : QStringLiteral("false"));

        auto writeUiSection = [&](const QString& name,
                                  ViewModeController::UiViewMode mode,
                                  bool filesOnly,
                                  const QString& extFilter,
                                  QuerySortField sortField,
                                  bool ascending,
                                  int maxDepth,
                                  const QString& substring) {
            DirectoryModel::Request request;
            request.rootPath = rootPath;
            request.mode = mode;
            request.includeHidden = false;
            request.includeSystem = false;
            request.foldersFirst = true;
            request.extensionFilter = extFilter;
            request.substringFilter = substring;
            request.sortField = sortField;
            request.ascending = ascending;
            request.maxDepth = maxDepth;
            request.filesOnly = filesOnly;

            const QueryResult result = uiDirectoryModel.query(request);
            const QVector<FileEntry> adapted = QueryResultAdapter::toFileEntries(result);

            lines << QStringLiteral("%1_begin").arg(name);
            lines << QStringLiteral("ok=%1").arg(result.ok ? QStringLiteral("true") : QStringLiteral("false"));
            lines << QStringLiteral("rows=%1").arg(result.rows.size());
            lines << QStringLiteral("adapted_rows=%1").arg(adapted.size());
            if (!result.errorText.isEmpty()) {
                lines << QStringLiteral("error=%1").arg(result.errorText);
            }
            const int maxRows = qMin(20, result.rows.size());
            for (int i = 0; i < maxRows; ++i) {
                const QueryRow& row = result.rows.at(i);
                lines << QStringLiteral("row id=%1 parent_id=%2 is_dir=%3 ext=%4 size=%5 path=%6")
                             .arg(row.id)
                             .arg(row.hasParentId ? QString::number(row.parentId) : QStringLiteral("NULL"))
                             .arg(row.isDir ? 1 : 0)
                             .arg(row.extension)
                             .arg(row.hasSizeBytes ? QString::number(row.sizeBytes) : QStringLiteral("NULL"))
                             .arg(row.path);
            }
            lines << QStringLiteral("%1_end").arg(name);
            return result;
        };

        bool checks = ok;
        if (ok) {
            const bool runAll = (modeValue == QStringLiteral("all"));

            if (runAll || modeValue == QStringLiteral("standard")) {
                const QueryResult r = writeUiSection(QStringLiteral("ui_standard"),
                                                     ViewModeController::UiViewMode::Standard,
                                                     false,
                                                     QString(),
                                                     QuerySortField::Name,
                                                     true,
                                                     -1,
                                                     QString());
                checks = checks && r.ok;
            }

            if (runAll || modeValue == QStringLiteral("flat")) {
                const QueryResult r = writeUiSection(QStringLiteral("ui_flat"),
                                                     ViewModeController::UiViewMode::Flat,
                                                     true,
                                                     QString(),
                                                     QuerySortField::Path,
                                                     true,
                                                     -1,
                                                     QString());
                checks = checks && r.ok;
            }

            if (runAll || modeValue == QStringLiteral("hierarchy")) {
                const QueryResult r = writeUiSection(QStringLiteral("ui_hierarchy"),
                                                     ViewModeController::UiViewMode::Hierarchy,
                                                     false,
                                                     QString(),
                                                     QuerySortField::Name,
                                                     true,
                                                     2,
                                                     QString());
                checks = checks && r.ok;
            }

            const QueryResult filtered = writeUiSection(QStringLiteral("ui_filtered"),
                                                        ViewModeController::UiViewMode::Flat,
                                                        true,
                                                        QStringLiteral(".cpp;.h;.txt"),
                                                        QuerySortField::Name,
                                                        true,
                                                        -1,
                                                        QStringLiteral("src"));
            checks = checks && filtered.ok;

            const QueryResult sorted = writeUiSection(QStringLiteral("ui_sorted"),
                                                      ViewModeController::UiViewMode::Flat,
                                                      true,
                                                      QString(),
                                                      QuerySortField::Size,
                                                      false,
                                                      -1,
                                                      QString());
            checks = checks && sorted.ok;

            DirectoryModel::Request badRequest;
            badRequest.rootPath = QDir::cleanPath(rootPath + QStringLiteral("/__missing_root__"));
            badRequest.mode = ViewModeController::UiViewMode::Standard;
            badRequest.sortField = QuerySortField::Name;
            badRequest.ascending = true;
            const QueryResult badRoot = uiDirectoryModel.query(badRequest);
            lines << QStringLiteral("ui_bad_root_begin");
            lines << QStringLiteral("ok=%1").arg(badRoot.ok ? QStringLiteral("true") : QStringLiteral("false"));
            lines << QStringLiteral("rows=%1").arg(badRoot.rows.size());
            if (!badRoot.errorText.isEmpty()) {
                lines << QStringLiteral("error=%1").arg(badRoot.errorText);
            }
            lines << QStringLiteral("ui_bad_root_end");
            checks = checks && !badRoot.ok;
            lines << QStringLiteral("checks_ok=%1").arg(checks ? QStringLiteral("true") : QStringLiteral("false"));
        }

        if (!ok && !errorText.isEmpty()) {
            lines << QStringLiteral("error=%1").arg(errorText);
        }

        writeLogFile(logPath, lines);
        return checks ? 0 : 5;
    }

    if (parser.isSet(refreshSmokeOption)) {
        const QString dbPath = parser.isSet(refreshDbPathOption)
            ? QDir::cleanPath(parser.value(refreshDbPathOption))
            : QDir::cleanPath(QDir::current().filePath(QStringLiteral("debug/metastore_vie_p3.sqlite3")));
        const QString requestedRoot = parser.isSet(refreshRootOption)
            ? QDir::cleanPath(parser.value(refreshRootOption))
            : QString();
        const QString logPath = parser.isSet(refreshLogOption)
            ? QDir::cleanPath(parser.value(refreshLogOption))
            : QDir::cleanPath(QDir::current().filePath(QStringLiteral("debug/refresh_smoke.log")));
        const bool forceRefresh = parser.isSet(refreshForceOption);
        const int staleSeconds = parser.isSet(refreshStaleSecondsOption)
            ? qMax(0, parser.value(refreshStaleSecondsOption).toInt())
            : 120;
        const int repeatCount = parser.isSet(refreshRepeatOption)
            ? qMax(1, parser.value(refreshRepeatOption).toInt())
            : 1;
        const int delayMs = parser.isSet(refreshDelayMsOption)
            ? qMax(0, parser.value(refreshDelayMsOption).toInt())
            : 0;
        const QString queryMode = parser.isSet(refreshQueryModeOption)
            ? parser.value(refreshQueryModeOption).trimmed().toLower()
            : QStringLiteral("standard");

        QStringList lines;
        lines << QStringLiteral("mode=refresh_smoke");
        lines << QStringLiteral("db_path=%1").arg(QDir::toNativeSeparators(dbPath));
        lines << QStringLiteral("requested_root=%1").arg(QDir::toNativeSeparators(requestedRoot));
        lines << QStringLiteral("query_mode=%1").arg(queryMode);
        lines << QStringLiteral("force_refresh=%1").arg(forceRefresh ? QStringLiteral("true") : QStringLiteral("false"));
        lines << QStringLiteral("stale_threshold_seconds=%1").arg(staleSeconds);
        lines << QStringLiteral("repeat_count=%1").arg(repeatCount);
        lines << QStringLiteral("delay_ms=%1").arg(delayMs);

        QString rootPath = requestedRoot;
        MetaStore rootStore;
        QString rootError;
        QString migrationLog;
        bool ok = rootStore.initialize(dbPath, &rootError, &migrationLog);
        lines << QStringLiteral("root_store_initialize_ok=%1").arg(ok ? QStringLiteral("true") : QStringLiteral("false"));
        if (!migrationLog.isEmpty()) {
            lines << QStringLiteral("migration_log_begin");
            lines << migrationLog.trimmed();
            lines << QStringLiteral("migration_log_end");
        }

        if (ok && rootPath.isEmpty()) {
            QVector<VolumeRecord> volumes;
            if (rootStore.listVolumes(&volumes, &rootError) && !volumes.isEmpty()) {
                rootPath = volumes.first().rootPath;
            }
        }

        lines << QStringLiteral("resolved_root=%1").arg(QDir::toNativeSeparators(rootPath));

        RefreshPolicy policy;
        policy.staleThresholdSeconds = staleSeconds;
        policy.dedupeWindowSeconds = 5;
        policy.defaultMode = QStringLiteral("visible_refresh");
        policy.alwaysRefreshVisiblePath = false;

        VisionIndexService vision;
        QString initError;
        const bool visionInit = vision.initialize(dbPath, policy, &initError);
        lines << QStringLiteral("vision_initialize_ok=%1").arg(visionInit ? QStringLiteral("true") : QStringLiteral("false"));
        if (!visionInit && !initError.isEmpty()) {
            lines << QStringLiteral("vision_initialize_error=%1").arg(initError);
        }

        auto queryOnce = [&](const QString& section) -> QueryResult {
            QueryOptions options;
            options.includeHidden = false;
            options.includeSystem = false;
            options.sortField = QuerySortField::Name;
            options.ascending = true;

            auto start = std::chrono::steady_clock::now();
            QueryResult result;
            if (queryMode == QStringLiteral("flat")) {
                result = vision.queryFlat(rootPath, options);
            } else if (queryMode == QStringLiteral("hierarchy")) {
                options.maxDepth = 3;
                result = vision.queryHierarchy(rootPath, options);
            } else {
                result = vision.queryChildren(rootPath, options);
            }
            const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - start)
                                       .count();

            lines << QStringLiteral("%1_begin").arg(section);
            lines << QStringLiteral("ok=%1").arg(result.ok ? QStringLiteral("true") : QStringLiteral("false"));
            lines << QStringLiteral("rows=%1").arg(result.rows.size());
            lines << QStringLiteral("elapsed_ms=%1").arg(elapsedMs);
            if (!result.errorText.isEmpty()) {
                lines << QStringLiteral("error=%1").arg(result.errorText);
            }
            const int maxRows = qMin(15, result.rows.size());
            for (int i = 0; i < maxRows; ++i) {
                const QueryRow& row = result.rows.at(i);
                lines << QStringLiteral("row id=%1 is_dir=%2 path=%3")
                             .arg(row.id)
                             .arg(row.isDir ? 1 : 0)
                             .arg(row.path);
            }
            lines << QStringLiteral("%1_end").arg(section);
            return result;
        };

        auto writeEvents = [&](const QString& section, const QVector<RefreshEvent>& events) {
            lines << QStringLiteral("%1_begin").arg(section);
            lines << QStringLiteral("count=%1").arg(events.size());
            for (const RefreshEvent& event : events) {
                lines << QStringLiteral("event request_id=%1 state=%2 path=%3 mode=%4 reason=%5 session=%6 seen=%7 inserted=%8 updated=%9 removed=%10 error=%11")
                             .arg(event.requestId)
                             .arg(RefreshTypes::stateToString(event.state))
                             .arg(event.path)
                             .arg(event.mode)
                             .arg(event.reason)
                             .arg(event.sessionId)
                             .arg(event.totalSeen)
                             .arg(event.totalInserted)
                             .arg(event.totalUpdated)
                             .arg(event.totalRemoved)
                             .arg(event.errorText);
            }
            lines << QStringLiteral("%1_end").arg(section);
        };

        qint64 beforeCount = -1;
        if (ok && !rootPath.isEmpty()) {
            beforeCount = rootStore.countEntriesUnderRoot(rootPath, -1, &rootError);
        }
        lines << QStringLiteral("count_before=%1").arg(beforeCount);

        bool checks = ok && visionInit && !rootPath.isEmpty();
        QueryResult cachedResult;
        if (checks) {
            cachedResult = queryOnce(QStringLiteral("cached_before_refresh"));
            checks = checks && cachedResult.ok;
        }

        QVector<RefreshEvent> requestEvents;
        QVector<RefreshEvent> duplicateEvents;
        QVector<RefreshEvent> badPathEvents;
        QVector<RefreshEvent> terminalEvents;

        if (checks) {
            for (int i = 0; i < repeatCount; ++i) {
                const RefreshRequestResult request = vision.requestRefresh(rootPath,
                                                                           QStringLiteral("targeted"),
                                                                           forceRefresh,
                                                                           QStringLiteral("refresh_smoke_primary"));
                lines << QStringLiteral("refresh_request cycle=%1 accepted=%2 state=%3 request_id=%4 reason=%5 error=%6")
                             .arg(i)
                             .arg(request.accepted ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(RefreshTypes::stateToString(request.state))
                             .arg(request.requestId)
                             .arg(request.reason)
                             .arg(request.errorText);

                const RefreshRequestResult duplicate = vision.requestRefresh(rootPath,
                                                                             QStringLiteral("targeted"),
                                                                             forceRefresh,
                                                                             QStringLiteral("refresh_smoke_duplicate"));
                lines << QStringLiteral("refresh_duplicate cycle=%1 accepted=%2 state=%3 request_id=%4 reason=%5 error=%6")
                             .arg(i)
                             .arg(duplicate.accepted ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(RefreshTypes::stateToString(duplicate.state))
                             .arg(duplicate.requestId)
                             .arg(duplicate.reason)
                             .arg(duplicate.errorText);

                const QVector<RefreshEvent> cycleEvents = vision.takeRefreshEvents();
                requestEvents += cycleEvents;

                if (delayMs > 0) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                }
            }

            const QString badPath = QDir::cleanPath(rootPath + QStringLiteral("/__bad_refresh_path__"));
            const RefreshRequestResult badRequest = vision.requestRefresh(badPath,
                                                                          QStringLiteral("targeted"),
                                                                          true,
                                                                          QStringLiteral("refresh_smoke_bad_path"));
            lines << QStringLiteral("refresh_bad_path accepted=%1 state=%2 request_id=%3 reason=%4 error=%5")
                         .arg(badRequest.accepted ? QStringLiteral("true") : QStringLiteral("false"))
                         .arg(RefreshTypes::stateToString(badRequest.state))
                         .arg(badRequest.requestId)
                         .arg(badRequest.reason)
                         .arg(badRequest.errorText);

            badPathEvents = vision.takeRefreshEvents();
            const bool idleOk = vision.waitForRefreshIdle(120000);
            lines << QStringLiteral("wait_for_idle_ok=%1").arg(idleOk ? QStringLiteral("true") : QStringLiteral("false"));
            terminalEvents = vision.takeRefreshEvents();

            QueryResult requery = queryOnce(QStringLiteral("requery_after_refresh"));
            checks = checks && requery.ok && idleOk;
            duplicateEvents = requestEvents;
        }

        qint64 afterCount = -1;
        if (ok && !rootPath.isEmpty()) {
            afterCount = rootStore.countEntriesUnderRoot(rootPath, -1, &rootError);
        }
        lines << QStringLiteral("count_after=%1").arg(afterCount);

        QVector<ScanSessionRecord> sessions;
        if (ok && rootStore.listScanSessions(&sessions, &rootError)) {
            lines << QStringLiteral("scan_sessions_begin");
            int shown = 0;
            for (const ScanSessionRecord& s : sessions) {
                if (shown >= 20) {
                    break;
                }
                if (!QDir::cleanPath(rootPath).isEmpty()
                    && !QDir::cleanPath(s.rootPath).startsWith(QDir::cleanPath(rootPath), Qt::CaseInsensitive)
                    && !QDir::cleanPath(rootPath).startsWith(QDir::cleanPath(s.rootPath), Qt::CaseInsensitive)) {
                    continue;
                }
                lines << QStringLiteral("session id=%1 root=%2 mode=%3 status=%4 started=%5 completed=%6 seen=%7 inserted=%8 updated=%9 removed=%10 error=%11")
                             .arg(s.id)
                             .arg(s.rootPath)
                             .arg(s.mode)
                             .arg(s.status)
                             .arg(s.startedUtc)
                             .arg(s.completedUtc)
                             .arg(s.totalSeen)
                             .arg(s.totalInserted)
                             .arg(s.totalUpdated)
                             .arg(s.totalRemoved)
                             .arg(s.errorText);
                ++shown;
            }
            lines << QStringLiteral("scan_sessions_end");
        }

        writeEvents(QStringLiteral("refresh_request_events"), requestEvents);
        writeEvents(QStringLiteral("refresh_bad_path_events"), badPathEvents);
        writeEvents(QStringLiteral("refresh_terminal_events"), terminalEvents);

        bool sawRunning = false;
        bool sawCompleted = false;
        bool sawDuplicate = false;
        bool sawBadPathFailure = false;

        const auto accumulateChecks = [&](const QVector<RefreshEvent>& events) {
            for (const RefreshEvent& event : events) {
                if (event.state == RefreshState::Running) {
                    sawRunning = true;
                }
                if (event.state == RefreshState::Completed) {
                    sawCompleted = true;
                }
                if (event.state == RefreshState::SkippedDuplicate) {
                    sawDuplicate = true;
                }
                if (event.state == RefreshState::Failed
                    && event.reason == QStringLiteral("invalid_path")) {
                    sawBadPathFailure = true;
                }
            }
        };

        accumulateChecks(requestEvents);
        accumulateChecks(badPathEvents);
        accumulateChecks(terminalEvents);

        lines << QStringLiteral("check_cached_query_ok=%1").arg((cachedResult.ok ? QStringLiteral("true") : QStringLiteral("false")));
        lines << QStringLiteral("check_running_seen=%1").arg(sawRunning ? QStringLiteral("true") : QStringLiteral("false"));
        lines << QStringLiteral("check_completed_seen=%1").arg(sawCompleted ? QStringLiteral("true") : QStringLiteral("false"));
        lines << QStringLiteral("check_duplicate_seen=%1").arg(sawDuplicate ? QStringLiteral("true") : QStringLiteral("false"));
        lines << QStringLiteral("check_bad_path_seen=%1").arg(sawBadPathFailure ? QStringLiteral("true") : QStringLiteral("false"));

        checks = checks && sawRunning && sawCompleted && sawDuplicate && sawBadPathFailure;
        lines << QStringLiteral("checks_ok=%1").arg(checks ? QStringLiteral("true") : QStringLiteral("false"));

        if (!checks && !rootError.isEmpty()) {
            lines << QStringLiteral("error=%1").arg(rootError);
        }

        writeLogFile(logPath, lines);
        rootStore.shutdown();
        vision.shutdown();
        return checks ? 0 : 6;
    }

    MainWindow window(testMode, startupRoot, actionLogPath, testScriptPath);
    window.show();
    return app.exec();
}

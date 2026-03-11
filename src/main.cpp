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
#include "ui/MainWindow.h"

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

    MainWindow window(testMode, startupRoot, actionLogPath, testScriptPath);
    window.show();
    return app.exec();
}

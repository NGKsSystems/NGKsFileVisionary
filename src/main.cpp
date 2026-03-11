#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTextStream>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

#include "core/db/MetaStore.h"
#include "core/db/SqlHelpers.h"
#include "core/query/QueryCore.h"
#include "core/query/QueryTypes.h"
#include "core/index/VisionIndexService.h"
#include "core/scan/ScanCoordinator.h"
#include "core/scan/ScanTask.h"
#include "core/services/RefreshPolicy.h"
#include "core/services/RefreshTypes.h"
#include "core/services/VisionIndexService.h"
#include "core/watch/ChangeEvent.h"
#include "core/watch/WatchBridge.h"
#include "ui/MainWindow.h"
#include "ui/model/DirectoryModel.h"
#include "ui/model/QueryResultAdapter.h"

#ifdef _MSC_VER
#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#endif

namespace {
struct IndexSmokeCliOptions {
    bool enabled = false;
    QString indexRoot;
    QString indexDbPath;
    QString indexLogPath;
    QStringList argsReceived;
    QString parseError;
};

struct WatchSmokeCliOptions {
    bool enabled = false;
    QString watchRoot;
    QString watchDbPath;
    QString watchLogPath;
    QStringList argsReceived;
    QString parseError;
};

class IndexSmokeLogWriter {
public:
    bool open(const QString& logPath, QString* error)
    {
        QFileInfo logInfo(logPath);
        if (!QDir().mkpath(logInfo.absolutePath())) {
            if (error) {
                *error = QStringLiteral("unable_to_create_log_parent_dir");
            }
            return false;
        }

        file_.setFileName(logPath);
        if (!file_.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            if (error) {
                *error = file_.errorString();
            }
            return false;
        }

        stream_.setDevice(&file_);
        return true;
    }

    void writeLine(const QString& line)
    {
        stream_ << line << '\n';
        stream_.flush();
        file_.flush();
    }

private:
    QFile file_;
    QTextStream stream_;
};

void writeStderrLine(const QString& line)
{
    QTextStream err(stderr, QIODevice::WriteOnly);
    err << line << '\n';
    err.flush();
}

bool hasIndexSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--index-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasWatchSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--watch-smoke")) {
            return true;
        }
    }
    return false;
}

IndexSmokeCliOptions parseIndexSmokeOptions(int argc, char* argv[])
{
    IndexSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--index-smoke")) {
            options.enabled = true;
            continue;
        }

        auto consumeValue = [&](QString* out) {
            if ((i + 1) >= argc) {
                options.parseError = QStringLiteral("missing_value_for_%1").arg(token);
                return false;
            }
            ++i;
            *out = QDir::cleanPath(QString::fromLocal8Bit(argv[i]));
            return true;
        };

        if (token == QStringLiteral("--index-root")) {
            if (!consumeValue(&options.indexRoot)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--index-db-path")) {
            if (!consumeValue(&options.indexDbPath)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--index-log")) {
            if (!consumeValue(&options.indexLogPath)) {
                break;
            }
            continue;
        }
    }

    return options;
}

WatchSmokeCliOptions parseWatchSmokeOptions(int argc, char* argv[])
{
    WatchSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--watch-smoke")) {
            options.enabled = true;
            continue;
        }

        auto consumeValue = [&](QString* out) {
            if ((i + 1) >= argc) {
                options.parseError = QStringLiteral("missing_value_for_%1").arg(token);
                return false;
            }
            ++i;
            *out = QDir::cleanPath(QString::fromLocal8Bit(argv[i]));
            return true;
        };

        if (token == QStringLiteral("--watch-root")) {
            if (!consumeValue(&options.watchRoot)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--watch-db-path")) {
            if (!consumeValue(&options.watchDbPath)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--watch-log")) {
            if (!consumeValue(&options.watchLogPath)) {
                break;
            }
            continue;
        }
    }

    return options;
}

QString toIsoUtc(const QDateTime& value);
QString buildEntryHash(const QString& path, const QFileInfo& fileInfo);

bool upsertPathFromFilesystem(MetaStore& store,
                              qint64 volumeId,
                              const QString& rootPath,
                              const QString& path,
                              int scanVersion,
                              QString* errorText)
{
    QFileInfo info(path);
    if (!info.exists()) {
        if (errorText) {
            *errorText = QStringLiteral("target_missing_on_filesystem");
        }
        return false;
    }

    const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(rootPath));
    EntryRecord record;
    record.volumeId = volumeId;
    record.path = QDir::fromNativeSeparators(QDir::cleanPath(info.absoluteFilePath()));
    record.parentPath = (record.path.compare(normalizedRoot, Qt::CaseInsensitive) == 0)
        ? QDir::cleanPath(QFileInfo(record.path).dir().absolutePath())
        : QDir::fromNativeSeparators(QDir::cleanPath(info.absolutePath()));
    record.name = info.fileName().isEmpty() ? info.absoluteFilePath() : info.fileName();
    record.normalizedName = SqlHelpers::normalizedName(record.name);
    record.extension = info.isDir() || info.suffix().isEmpty()
        ? QString()
        : QStringLiteral(".") + info.suffix().toLower();
    record.isDir = info.isDir();
    record.hasSizeBytes = !info.isDir();
    record.sizeBytes = info.isDir() ? 0 : info.size();
    record.createdUtc = toIsoUtc(info.birthTime());
    record.modifiedUtc = toIsoUtc(info.lastModified());
    record.accessedUtc = toIsoUtc(info.lastRead());
    record.hiddenFlag = info.isHidden();
    record.readonlyFlag = !info.isWritable();
    record.existsFlag = true;
    record.indexedAtUtc = SqlHelpers::utcNowIso();
    record.scanVersion = scanVersion;
    record.entryHash = buildEntryHash(record.path, info);
    record.metadataVersion = 1;

    record.hasParentId = false;
    if (record.path.compare(normalizedRoot, Qt::CaseInsensitive) != 0) {
        bool foundParent = false;
        qint64 parentId = 0;
        QString parentError;
        if (!store.resolveParentIdByPath(record.parentPath, &parentId, &foundParent, &parentError)) {
            if (errorText) {
                *errorText = QStringLiteral("resolve_parent_failed path=%1 error=%2").arg(record.parentPath, parentError);
            }
            return false;
        }
        if (foundParent) {
            record.hasParentId = true;
            record.parentId = parentId;
        }
    }

    if (!store.upsertEntry(record, nullptr, nullptr, nullptr, errorText)) {
        return false;
    }
    return true;
}

bool markPathDeleted(MetaStore& store, const QString& path, int scanVersion, QString* errorText)
{
    EntryRecord existing;
    if (!store.getEntryByPath(path, &existing, errorText)) {
        return false;
    }

    existing.existsFlag = false;
    existing.hasSizeBytes = false;
    existing.sizeBytes = 0;
    existing.scanVersion = scanVersion;
    existing.indexedAtUtc = SqlHelpers::utcNowIso();
    return store.upsertEntry(existing, nullptr, nullptr, nullptr, errorText);
}

struct IndexSmokePassResult {
    bool ok = true;
    qint64 seen = 0;
    qint64 inserted = 0;
    qint64 updated = 0;
    qint64 files = 0;
    qint64 directories = 0;
    QString error;
};

QString toIsoUtc(const QDateTime& value)
{
    if (!value.isValid()) {
        return QString();
    }
    return value.toUTC().toString(Qt::ISODate);
}

QString buildEntryHash(const QString& path, const QFileInfo& fileInfo)
{
    const QString payload = QStringLiteral("%1|%2|%3")
                                .arg(path)
                                .arg(fileInfo.isDir() ? -1 : fileInfo.size())
                                .arg(toIsoUtc(fileInfo.lastModified()));
    return QString::fromLatin1(QCryptographicHash::hash(payload.toUtf8(), QCryptographicHash::Sha256).toHex());
}

bool runSynchronousIndexPass(MetaStore& store,
                             qint64 volumeId,
                             const QString& rootPath,
                             int scanVersion,
                             IndexSmokePassResult* out,
                             QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_pass_result");
        }
        return false;
    }

    *out = IndexSmokePassResult();

    const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(rootPath));
    QFileInfo rootInfo(normalizedRoot);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        if (errorText) {
            *errorText = QStringLiteral("invalid_root_path");
        }
        out->ok = false;
        out->error = errorText ? *errorText : QStringLiteral("invalid_root_path");
        return false;
    }

    QVector<QFileInfo> entries;
    entries.append(rootInfo);
    QDirIterator it(normalizedRoot,
                    QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString nextPath = it.next();
        entries.append(QFileInfo(nextPath));
    }

    std::sort(entries.begin(), entries.end(), [](const QFileInfo& a, const QFileInfo& b) {
        return a.absoluteFilePath().compare(b.absoluteFilePath(), Qt::CaseInsensitive) < 0;
    });

    QString txError;
    if (!store.beginTransaction(&txError)) {
        if (errorText) {
            *errorText = txError;
        }
        out->ok = false;
        out->error = txError;
        return false;
    }

    for (const QFileInfo& info : entries) {
        EntryRecord record;
        record.volumeId = volumeId;
        record.path = QDir::fromNativeSeparators(QDir::cleanPath(info.absoluteFilePath()));
        record.parentPath = (record.path.compare(normalizedRoot, Qt::CaseInsensitive) == 0)
            ? QDir::cleanPath(QFileInfo(record.path).dir().absolutePath())
            : QDir::fromNativeSeparators(QDir::cleanPath(info.absolutePath()));
        record.name = info.fileName().isEmpty() ? info.absoluteFilePath() : info.fileName();
        record.normalizedName = SqlHelpers::normalizedName(record.name);
        record.extension = info.isDir() || info.suffix().isEmpty()
            ? QString()
            : QStringLiteral(".") + info.suffix().toLower();
        record.isDir = info.isDir();
        record.hasSizeBytes = !info.isDir();
        record.sizeBytes = info.isDir() ? 0 : info.size();
        record.createdUtc = toIsoUtc(info.birthTime());
        record.modifiedUtc = toIsoUtc(info.lastModified());
        record.accessedUtc = toIsoUtc(info.lastRead());
        record.hiddenFlag = info.isHidden();
        record.readonlyFlag = !info.isWritable();
        record.existsFlag = info.exists();
        record.indexedAtUtc = SqlHelpers::utcNowIso();
        record.scanVersion = scanVersion;
        record.entryHash = buildEntryHash(record.path, info);
        record.metadataVersion = 1;
        record.hasParentId = false;
        if (record.path.compare(normalizedRoot, Qt::CaseInsensitive) != 0) {
            bool foundParent = false;
            qint64 parentId = 0;
            QString parentError;
            if (!store.resolveParentIdByPath(record.parentPath, &parentId, &foundParent, &parentError)) {
                store.rollbackTransaction(nullptr);
                if (errorText) {
                    *errorText = QStringLiteral("resolve_parent_failed path=%1 error=%2").arg(record.parentPath, parentError);
                }
                out->ok = false;
                out->error = errorText ? *errorText : QStringLiteral("resolve_parent_failed");
                return false;
            }
            if (foundParent) {
                record.hasParentId = true;
                record.parentId = parentId;
            }
        }

        bool inserted = false;
        bool updated = false;
        QString upsertError;
        if (!store.upsertEntry(record, nullptr, &inserted, &updated, &upsertError)) {
            store.rollbackTransaction(nullptr);
            if (errorText) {
                *errorText = QStringLiteral("upsert_entry_failed path=%1 error=%2").arg(record.path, upsertError);
            }
            out->ok = false;
            out->error = errorText ? *errorText : upsertError;
            return false;
        }

        ++out->seen;
        if (inserted) {
            ++out->inserted;
        }
        if (updated) {
            ++out->updated;
        }
        if (record.isDir) {
            ++out->directories;
        } else {
            ++out->files;
        }
    }

    if (!store.commitTransaction(&txError)) {
        store.rollbackTransaction(nullptr);
        if (errorText) {
            *errorText = txError;
        }
        out->ok = false;
        out->error = txError;
        return false;
    }

    return true;
}

int runIndexSmokeCli(int argc, char* argv[])
{
    QCoreApplication cliApp(argc, argv);

    const IndexSmokeCliOptions options = parseIndexSmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();
    const QString timestampUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("index_smoke_parse_error=%1").arg(options.parseError));
        return 64;
    }

    if (options.indexLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("index_smoke_error=missing_required_arg_--index-log"));
        return 65;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.indexLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("index_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("index_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.indexLogPath)));
        writeStderrLine(QStringLiteral("index_smoke_log_error=%1").arg(logOpenError));
        return 66;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    auto logStartup = [&]() {
        log.writeLine(QStringLiteral("mode=index_smoke"));
        log.writeLine(QStringLiteral("startup_banner=INDEX_SMOKE_CLI_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("index_root=%1").arg(QDir::toNativeSeparators(options.indexRoot)));
        log.writeLine(QStringLiteral("index_db_path=%1").arg(QDir::toNativeSeparators(options.indexDbPath)));
        log.writeLine(QStringLiteral("index_log=%1").arg(QDir::toNativeSeparators(options.indexLogPath)));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(timestampUtc));
    };

    try {
        logStartup();

        if (options.indexRoot.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("index_smoke_error=missing_required_arg_--index-root"));
            return finishFail(67, QStringLiteral("missing_required_arg_--index-root"));
        }
        if (options.indexDbPath.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("index_smoke_error=missing_required_arg_--index-db-path"));
            return finishFail(68, QStringLiteral("missing_required_arg_--index-db-path"));
        }

        QFileInfo rootInfo(options.indexRoot);
        if (!rootInfo.exists() || !rootInfo.isDir()) {
            writeStderrLine(QStringLiteral("index_smoke_error=invalid_root_path"));
            return finishFail(69, QStringLiteral("invalid_root_path"));
        }

        log.writeLine(QStringLiteral("arg_validation=ok"));

        MetaStore store;
        QString errorText;
        QString migrationLog;
        log.writeLine(QStringLiteral("db_init=begin"));
        if (!store.initialize(options.indexDbPath, &errorText, &migrationLog)) {
            writeStderrLine(QStringLiteral("index_smoke_error=db_init_failure"));
            return finishFail(70, QStringLiteral("db_init_failure:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("db_init=end"));

        log.writeLine(QStringLiteral("open_index_ok=true"));
        if (!migrationLog.isEmpty()) {
            log.writeLine(QStringLiteral("migration_log_begin"));
            log.writeLine(migrationLog.trimmed());
            log.writeLine(QStringLiteral("migration_log_end"));
        }

        IndexRootRecord indexRoot;
        indexRoot.rootPath = QDir::fromNativeSeparators(QDir::cleanPath(options.indexRoot));
        indexRoot.status = QStringLiteral("active");
        indexRoot.createdUtc = SqlHelpers::utcNowIso();
        indexRoot.updatedUtc = indexRoot.createdUtc;
        if (!store.upsertIndexRoot(indexRoot, nullptr, &errorText)) {
            store.shutdown();
            writeStderrLine(QStringLiteral("index_smoke_error=invalid_or_unusable_root"));
            return finishFail(71, QStringLiteral("invalid_or_unusable_root:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("root_validation=ok"));

        VolumeRecord volume;
        volume.volumeKey = QStringLiteral("index_smoke:%1").arg(QDir::fromNativeSeparators(QDir::cleanPath(options.indexRoot)).toLower());
        volume.rootPath = QDir::fromNativeSeparators(QDir::cleanPath(options.indexRoot));
        volume.displayName = QFileInfo(options.indexRoot).fileName();
        volume.fsType = QStringLiteral("native");
        volume.serialNumber = QStringLiteral("index_smoke");
        qint64 volumeId = 0;
        if (!store.upsertVolume(volume, &volumeId, &errorText)) {
            store.shutdown();
            writeStderrLine(QStringLiteral("index_smoke_error=volume_registration_failure"));
            return finishFail(72, QStringLiteral("volume_registration_failure:%1").arg(errorText));
        }

        log.writeLine(QStringLiteral("initial_index_begin"));
        IndexSmokePassResult initialPass;
        if (!runSynchronousIndexPass(store, volumeId, options.indexRoot, 1, &initialPass, &errorText)) {
            store.shutdown();
            writeStderrLine(QStringLiteral("index_smoke_error=index_execution_failure"));
            return finishFail(73, QStringLiteral("index_execution_failure:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("initial_index_end"));
        log.writeLine(QStringLiteral("initial_index_ok=true"));
        log.writeLine(QStringLiteral("initial_seen=%1").arg(initialPass.seen));
        log.writeLine(QStringLiteral("initial_inserted=%1").arg(initialPass.inserted));
        log.writeLine(QStringLiteral("initial_updated=%1").arg(initialPass.updated));

        const QString touchPath = QDir(options.indexRoot).filePath(QStringLiteral(".index_smoke_incremental_marker.txt"));
        QFile marker(touchPath);
        if (!marker.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            store.shutdown();
            writeStderrLine(QStringLiteral("index_smoke_error=incremental_marker_write_failed"));
            return finishFail(74, QStringLiteral("incremental_marker_write_failed:%1").arg(marker.errorString()));
        }
        QTextStream markerStream(&marker);
        markerStream << QStringLiteral("updated_utc=%1\n").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        markerStream.flush();
        marker.close();

        log.writeLine(QStringLiteral("incremental_index_begin"));
        IndexSmokePassResult incrementalPass;
        if (!runSynchronousIndexPass(store, volumeId, options.indexRoot, 2, &incrementalPass, &errorText)) {
            store.shutdown();
            writeStderrLine(QStringLiteral("index_smoke_error=incremental_index_failure"));
            return finishFail(75, QStringLiteral("incremental_index_failure:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("incremental_index_end"));
        log.writeLine(QStringLiteral("incremental_index_ok=true"));
        log.writeLine(QStringLiteral("incremental_seen=%1").arg(incrementalPass.seen));
        log.writeLine(QStringLiteral("incremental_inserted=%1").arg(incrementalPass.inserted));
        log.writeLine(QStringLiteral("incremental_updated=%1").arg(incrementalPass.updated));

        IndexJournalRecord journal;
        journal.rootPath = QDir::fromNativeSeparators(QDir::cleanPath(options.indexRoot));
        journal.path = touchPath;
        journal.eventType = QStringLiteral("incremental_pass_complete");
        journal.scanVersion = 2;
        journal.payload = QStringLiteral("seen=%1;inserted=%2;updated=%3")
                              .arg(incrementalPass.seen)
                              .arg(incrementalPass.inserted)
                              .arg(incrementalPass.updated);
        journal.createdUtc = SqlHelpers::utcNowIso();
        store.appendIndexJournal(journal, nullptr, nullptr);

        IndexStatRecord statTotal;
        statTotal.key = QStringLiteral("total_entries");
        statTotal.value = QString::number(store.countEntries(&errorText));
        statTotal.updatedUtc = SqlHelpers::utcNowIso();
        store.upsertIndexStat(statTotal, nullptr);

        IndexStatRecord statFiles;
        statFiles.key = QStringLiteral("file_count");
        statFiles.value = QString::number(store.countFiles(&errorText));
        statFiles.updatedUtc = SqlHelpers::utcNowIso();
        store.upsertIndexStat(statFiles, nullptr);

        IndexStatRecord statDirs;
        statDirs.key = QStringLiteral("directory_count");
        statDirs.value = QString::number(store.countDirectories(&errorText));
        statDirs.updatedUtc = SqlHelpers::utcNowIso();
        store.upsertIndexStat(statDirs, nullptr);

        log.writeLine(QStringLiteral("query_validation_begin"));
        QueryCore queryCore(store);
        QueryOptions optionsQuery;
        optionsQuery.sortField = QuerySortField::Name;
        optionsQuery.ascending = true;
        const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(options.indexRoot));
        const QueryResult queryResult = queryCore.queryChildren(normalizedRoot, optionsQuery);
        log.writeLine(QStringLiteral("query_children_ok=%1").arg(queryResult.ok ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("query_children_rows=%1").arg(queryResult.rows.size()));
        if (!queryResult.ok) {
            store.shutdown();
            writeStderrLine(QStringLiteral("index_smoke_error=query_validation_failed"));
            return finishFail(76, QStringLiteral("query_validation_failed:%1").arg(queryResult.errorText));
        }
        log.writeLine(QStringLiteral("query_validation_end"));

        store.shutdown();
        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return 0;
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("index_smoke_error=unexpected_exception"));
        return finishFail(74, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("index_smoke_error=unexpected_error"));
        return finishFail(75, QStringLiteral("unexpected_error"));
    }
}

int runWatchSmokeCli(int argc, char* argv[])
{
    QCoreApplication cliApp(argc, argv);

    const WatchSmokeCliOptions options = parseWatchSmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();
    const QString timestampUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("watch_smoke_parse_error=%1").arg(options.parseError));
        return 90;
    }

    if (options.watchLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("watch_smoke_error=missing_required_arg_--watch-log"));
        return 91;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.watchLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("watch_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("watch_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.watchLogPath)));
        writeStderrLine(QStringLiteral("watch_smoke_log_error=%1").arg(logOpenError));
        return 92;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    try {
        log.writeLine(QStringLiteral("mode=watch_smoke"));
        log.writeLine(QStringLiteral("startup_banner=WATCH_SMOKE_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("watch_root=%1").arg(QDir::toNativeSeparators(options.watchRoot)));
        log.writeLine(QStringLiteral("watch_db_path=%1").arg(QDir::toNativeSeparators(options.watchDbPath)));
        log.writeLine(QStringLiteral("watch_log=%1").arg(QDir::toNativeSeparators(options.watchLogPath)));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(timestampUtc));

        if (options.watchRoot.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("watch_smoke_error=missing_required_arg_--watch-root"));
            return finishFail(93, QStringLiteral("missing_required_arg_--watch-root"));
        }
        if (options.watchDbPath.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("watch_smoke_error=missing_required_arg_--watch-db-path"));
            return finishFail(94, QStringLiteral("missing_required_arg_--watch-db-path"));
        }

        QFileInfo rootInfo(options.watchRoot);
        if (!rootInfo.exists() || !rootInfo.isDir()) {
            writeStderrLine(QStringLiteral("watch_smoke_error=invalid_watch_root"));
            return finishFail(95, QStringLiteral("invalid_watch_root"));
        }

        log.writeLine(QStringLiteral("arg_validation=ok"));

        MetaStore store;
        QString errorText;
        QString migrationLog;
        log.writeLine(QStringLiteral("db_init=begin"));
        if (!store.initialize(options.watchDbPath, &errorText, &migrationLog)) {
            writeStderrLine(QStringLiteral("watch_smoke_error=db_init_failure"));
            return finishFail(96, QStringLiteral("db_init_failure:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("db_init=end"));

        if (!migrationLog.isEmpty()) {
            log.writeLine(QStringLiteral("migration_log_begin"));
            log.writeLine(migrationLog.trimmed());
            log.writeLine(QStringLiteral("migration_log_end"));
        }

        const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(options.watchRoot));
        IndexRootRecord indexRoot;
        indexRoot.rootPath = normalizedRoot;
        indexRoot.status = QStringLiteral("active");
        indexRoot.createdUtc = SqlHelpers::utcNowIso();
        indexRoot.updatedUtc = indexRoot.createdUtc;
        if (!store.upsertIndexRoot(indexRoot, nullptr, &errorText)) {
            store.shutdown();
            return finishFail(97, QStringLiteral("index_root_upsert_failed:%1").arg(errorText));
        }

        VolumeRecord volume;
        volume.volumeKey = QStringLiteral("watch_smoke:%1").arg(normalizedRoot.toLower());
        volume.rootPath = normalizedRoot;
        volume.displayName = QFileInfo(normalizedRoot).fileName();
        volume.fsType = QStringLiteral("native");
        volume.serialNumber = QStringLiteral("watch_smoke");
        qint64 volumeId = 0;
        if (!store.upsertVolume(volume, &volumeId, &errorText)) {
            store.shutdown();
            return finishFail(98, QStringLiteral("volume_upsert_failed:%1").arg(errorText));
        }

        IndexSmokePassResult initialPass;
        if (!runSynchronousIndexPass(store, volumeId, normalizedRoot, 1, &initialPass, &errorText)) {
            store.shutdown();
            return finishFail(99, QStringLiteral("initial_index_failed:%1").arg(errorText));
        }

        WatchBridge bridge;
        if (!bridge.start(normalizedRoot, &errorText)) {
            store.shutdown();
            return finishFail(100, QStringLiteral("watcher_start_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("watcher_start_ok=true"));

        bool createSeen = false;
        bool modifySeen = false;
        bool renameSeen = false;
        bool deleteSeen = false;
        int targetedUpdates = 0;
        int scanVersion = 10;
        bool fullRescanTriggered = false;

        auto processEvents = [&](int waitMs) -> bool {
            const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + waitMs;
            while (QDateTime::currentMSecsSinceEpoch() < deadline) {
                const QVector<ChangeEvent> events = bridge.takePendingEvents();
                if (!events.isEmpty()) {
                    for (const ChangeEvent& event : events) {
                        log.writeLine(QStringLiteral("event_json=%1").arg(changeEventToJsonLine(event)));

                        if (event.type == ChangeEventType::Created) {
                            createSeen = true;
                        } else if (event.type == ChangeEventType::Modified) {
                            modifySeen = true;
                        } else if (event.type == ChangeEventType::Deleted) {
                            deleteSeen = true;
                        } else if (event.type == ChangeEventType::RenamedOld
                                   || event.type == ChangeEventType::RenamedNew
                                   || event.type == ChangeEventType::RenamedPair) {
                            renameSeen = true;
                        }

                        QString updateError;
                        bool eventApplied = true;
                        if (event.type == ChangeEventType::Created || event.type == ChangeEventType::Modified || event.type == ChangeEventType::RenamedNew) {
                            eventApplied = upsertPathFromFilesystem(store, volumeId, normalizedRoot, event.targetPath, ++scanVersion, &updateError);
                        } else if (event.type == ChangeEventType::Deleted || event.type == ChangeEventType::RenamedOld) {
                            eventApplied = markPathDeleted(store, event.targetPath, ++scanVersion, &updateError);
                        } else if (event.type == ChangeEventType::RenamedPair) {
                            const bool markOldOk = markPathDeleted(store, event.oldPath, ++scanVersion, &updateError);
                            const bool upsertNewOk = upsertPathFromFilesystem(store, volumeId, normalizedRoot, event.targetPath, ++scanVersion, &updateError);
                            eventApplied = markOldOk || upsertNewOk;
                        }

                        log.writeLine(QStringLiteral("targeted_update event=%1 applied=%2")
                                          .arg(changeEventTypeToString(event.type), eventApplied ? QStringLiteral("true") : QStringLiteral("false")));

                        if (!eventApplied && !updateError.contains(QStringLiteral("target_missing_on_filesystem"), Qt::CaseInsensitive)
                            && !updateError.contains(QStringLiteral("not_found"), Qt::CaseInsensitive)) {
                            bridge.stop();
                            store.shutdown();
                            fullRescanTriggered = false;
                            return false;
                        }

                        if (eventApplied) {
                            ++targetedUpdates;
                        }
                    }
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            return true;
        };

        const QString createPath = QDir(normalizedRoot).filePath(QStringLiteral("watch_live_create.txt"));
        QFile createFile(createPath);
        if (!createFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            bridge.stop();
            store.shutdown();
            return finishFail(101, QStringLiteral("create_file_failed:%1").arg(createFile.errorString()));
        }
        QTextStream createStream(&createFile);
        createStream << QStringLiteral("created\n");
        createFile.close();
        if (!processEvents(3000)) {
            return finishFail(102, QStringLiteral("process_events_failed_after_create"));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(600));

        QFile modifyFile(createPath);
        if (!modifyFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            bridge.stop();
            store.shutdown();
            return finishFail(103, QStringLiteral("modify_file_failed:%1").arg(modifyFile.errorString()));
        }
        QTextStream modifyStream(&modifyFile);
        modifyStream << QStringLiteral("modified_payload_%1\n").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        modifyStream.flush();
        modifyFile.close();
        if (!processEvents(3000)) {
            return finishFail(104, QStringLiteral("process_events_failed_after_modify"));
        }

        const QString renamedPath = QDir(normalizedRoot).filePath(QStringLiteral("watch_live_renamed.txt"));
        if (!QFile::rename(createPath, renamedPath)) {
            bridge.stop();
            store.shutdown();
            return finishFail(105, QStringLiteral("rename_failed"));
        }
        if (!processEvents(3000)) {
            return finishFail(106, QStringLiteral("process_events_failed_after_rename"));
        }

        if (!QFile::remove(renamedPath)) {
            bridge.stop();
            store.shutdown();
            return finishFail(107, QStringLiteral("delete_failed"));
        }
        if (!processEvents(3000)) {
            return finishFail(108, QStringLiteral("process_events_failed_after_delete"));
        }

        processEvents(1000);

        QueryCore queryCore(store);
        QueryOptions optionsQuery;
        optionsQuery.sortField = QuerySortField::Name;
        optionsQuery.ascending = true;
        const QueryResult queryResult = queryCore.queryChildren(normalizedRoot, optionsQuery);
        if (!queryResult.ok) {
            bridge.stop();
            store.shutdown();
            return finishFail(109, QStringLiteral("query_validation_failed:%1").arg(queryResult.errorText));
        }

        EntryRecord deletedCheck;
        QString deletedError;
        const bool deletedTracked = store.getEntryByPath(QDir::fromNativeSeparators(QDir::cleanPath(renamedPath)), &deletedCheck, &deletedError);
        const bool finalDbMatchesFs = deletedTracked && !deletedCheck.existsFlag;

        bridge.stop();
        store.shutdown();

        log.writeLine(QStringLiteral("create_event_seen=%1").arg(createSeen ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("modify_event_seen=%1").arg(modifySeen ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("rename_event_seen=%1").arg(renameSeen ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("delete_event_seen=%1").arg(deleteSeen ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("targeted_update_count=%1").arg(targetedUpdates));
        log.writeLine(QStringLiteral("full_rescan_triggered=%1").arg(fullRescanTriggered ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("query_children_ok=%1").arg(queryResult.ok ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("query_children_rows=%1").arg(queryResult.rows.size()));
        log.writeLine(QStringLiteral("db_state_matches_fs=%1").arg(finalDbMatchesFs ? QStringLiteral("true") : QStringLiteral("false")));

        const bool checksOk = createSeen
            && modifySeen
            && renameSeen
            && deleteSeen
            && targetedUpdates > 0
            && !fullRescanTriggered
            && queryResult.ok
            && finalDbMatchesFs;

        if (!checksOk) {
            return finishFail(110, QStringLiteral("watch_smoke_checks_failed"));
        }

        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return 0;
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("watch_smoke_error=unexpected_exception"));
        return finishFail(111, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("watch_smoke_error=unexpected_error"));
        return finishFail(112, QStringLiteral("unexpected_error"));
    }
}

bool writeLogFile(const QString& logPath, const QStringList& lines)
{
    QFileInfo logInfo(logPath);
    QDir().mkpath(logInfo.absolutePath());

    QSaveFile out(logPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream ts(&out);
    for (const QString& line : lines) {
        ts << line << '\n';
    }
    return out.commit();
}
}

int main(int argc, char* argv[])
{
    if (hasWatchSmokeFlag(argc, argv)) {
        return runWatchSmokeCli(argc, argv);
    }

    if (hasIndexSmokeFlag(argc, argv)) {
        return runIndexSmokeCli(argc, argv);
    }

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

    QCommandLineOption indexSmokeOption(QStringList() << QStringLiteral("index-smoke"),
                                        QStringLiteral("Run index engine smoke validation and exit."));
    QCommandLineOption indexRootOption(QStringList() << QStringLiteral("index-root"),
                                       QStringLiteral("Root path for index-smoke mode."),
                                       QStringLiteral("path"));
    QCommandLineOption indexDbPathOption(QStringList() << QStringLiteral("index-db-path"),
                                         QStringLiteral("Path to sqlite file for index-smoke mode."),
                                         QStringLiteral("path"));
    QCommandLineOption indexLogOption(QStringList() << QStringLiteral("index-log"),
                                      QStringLiteral("Path to index smoke log file."),
                                      QStringLiteral("path"));

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
    parser.addOption(indexSmokeOption);
    parser.addOption(indexRootOption);
    parser.addOption(indexDbPathOption);
    parser.addOption(indexLogOption);

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

    if (parser.isSet(indexSmokeOption)) {
        const QString dbPath = parser.isSet(indexDbPathOption)
            ? QDir::cleanPath(parser.value(indexDbPathOption))
            : QDir::cleanPath(QDir::current().filePath(QStringLiteral("debug/metastore_vie_p6.sqlite3")));
        const QString rootPath = parser.isSet(indexRootOption)
            ? QDir::cleanPath(parser.value(indexRootOption))
            : QDir::cleanPath(QDir::currentPath());
        const QString logPath = parser.isSet(indexLogOption)
            ? QDir::cleanPath(parser.value(indexLogOption))
            : QDir::cleanPath(QDir::current().filePath(QStringLiteral("debug/index_smoke.log")));

        QStringList lines;
        lines << QStringLiteral("mode=index_smoke");
        lines << QStringLiteral("startup_banner=VIE_P6_INDEX_SMOKE_BEGIN");
        lines << QStringLiteral("startup_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        lines << QStringLiteral("db_path=%1").arg(QDir::toNativeSeparators(dbPath));
        lines << QStringLiteral("root_path=%1").arg(QDir::toNativeSeparators(rootPath));
        lines << QStringLiteral("requested_log_path=%1").arg(QDir::toNativeSeparators(logPath));
        lines << QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));
        QFileInfo exeInfo(QCoreApplication::applicationFilePath());
        lines << QStringLiteral("exe_exists=%1").arg(exeInfo.exists() ? QStringLiteral("true") : QStringLiteral("false"));
        lines << QStringLiteral("exe_size_bytes=%1").arg(exeInfo.exists() ? QString::number(exeInfo.size()) : QStringLiteral("-1"));
        lines << QStringLiteral("exe_last_modified_utc=%1").arg(exeInfo.exists() ? exeInfo.lastModified().toUTC().toString(Qt::ISODate) : QString());

        const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(rootPath));
        lines << QStringLiteral("root_normalized=%1").arg(normalizedRoot);
        lines << QStringLiteral("root_exists_native=%1").arg(QDir(rootPath).exists() ? QStringLiteral("true") : QStringLiteral("false"));
        lines << QStringLiteral("root_exists_normalized=%1").arg(QDir(normalizedRoot).exists() ? QStringLiteral("true") : QStringLiteral("false"));

        if (!writeLogFile(logPath, lines)) {
            const QString fallbackLog = QDir::cleanPath(QDir::current().filePath(QStringLiteral("debug/index_smoke_fallback.log")));
            lines << QStringLiteral("fatal=unable_to_write_requested_log_path");
            lines << QStringLiteral("fallback_log=%1").arg(QDir::toNativeSeparators(fallbackLog));
            writeLogFile(fallbackLog, lines);
            return 70;
        }

        VisionIndexEngine::VisionIndexService indexService;
        QString errorText;
        QString migrationLog;
        bool ok = indexService.openIndex(dbPath, &errorText, &migrationLog);
        lines << QStringLiteral("open_index_ok=%1").arg(ok ? QStringLiteral("true") : QStringLiteral("false"));
        if (!migrationLog.isEmpty()) {
            lines << QStringLiteral("migration_log_begin");
            lines << migrationLog.trimmed();
            lines << QStringLiteral("migration_log_end");
        }

        if (ok) {
            ok = indexService.ensureIndexRoot(rootPath, &errorText);
            lines << QStringLiteral("ensure_root_ok=%1").arg(ok ? QStringLiteral("true") : QStringLiteral("false"));
            if (!ok && !errorText.isEmpty()) {
                lines << QStringLiteral("ensure_root_error=%1").arg(errorText);
            }
        }

        if (!ok) {
            MetaStore fallbackStore;
            QString fallbackError;
            QString migrationIgnore;
            if (fallbackStore.initialize(dbPath, &fallbackError, &migrationIgnore)) {
                IndexRootRecord fallbackRoot;
                fallbackRoot.rootPath = normalizedRoot;
                fallbackRoot.status = QStringLiteral("active");
                fallbackRoot.createdUtc = SqlHelpers::utcNowIso();
                fallbackRoot.updatedUtc = fallbackRoot.createdUtc;
                fallbackRoot.lastIndexedUtc = QString();
                fallbackRoot.lastScanVersion = 0;
                ok = fallbackStore.upsertIndexRoot(fallbackRoot, nullptr, &fallbackError);
                fallbackStore.shutdown();
            }
            lines << QStringLiteral("ensure_root_fallback_ok=%1").arg(ok ? QStringLiteral("true") : QStringLiteral("false"));
            if (!ok && !fallbackError.isEmpty()) {
                errorText = fallbackError;
                lines << QStringLiteral("ensure_root_fallback_error=%1").arg(errorText);
            }
        }

        VisionIndexEngine::VisionIndexRunSummary initialSummary;
        VisionIndexEngine::VisionIndexRunSummary incrementalSummary;
        if (ok) {
            initialSummary = indexService.runInitialIndex(rootPath, &errorText);
            lines << QStringLiteral("initial_index_ok=%1").arg(initialSummary.ok ? QStringLiteral("true") : QStringLiteral("false"));
            lines << QStringLiteral("initial_scan_version=%1").arg(initialSummary.scanVersion);
            lines << QStringLiteral("initial_session_id=%1").arg(initialSummary.sessionId);
            lines << QStringLiteral("initial_seen=%1 inserted=%2 updated=%3")
                         .arg(initialSummary.seen)
                         .arg(initialSummary.inserted)
                         .arg(initialSummary.updated);
            if (initialSummary.deduped) {
                lines << QStringLiteral("initial_deduped_reason=%1").arg(initialSummary.dedupeReason);
            }
            if (!initialSummary.errorText.isEmpty()) {
                lines << QStringLiteral("initial_error=%1").arg(initialSummary.errorText);
            }
            ok = ok && initialSummary.ok;
        }

        qint64 countAfterInitial = -1;
        if (ok) {
            MetaStore countStore;
            QString countErr;
            QString migrationIgnore;
            if (countStore.initialize(dbPath, &countErr, &migrationIgnore)) {
                countAfterInitial = countStore.countEntriesUnderRoot(QDir::fromNativeSeparators(rootPath), -1, &countErr);
                countStore.shutdown();
            }
            if (countAfterInitial < 0 && !countErr.isEmpty()) {
                errorText = countErr;
                ok = false;
            }
            lines << QStringLiteral("count_after_initial=%1").arg(countAfterInitial);
        }

        if (ok) {
            incrementalSummary = indexService.runIncrementalIndex(rootPath, &errorText);
            lines << QStringLiteral("incremental_index_ok=%1").arg(incrementalSummary.ok ? QStringLiteral("true") : QStringLiteral("false"));
            lines << QStringLiteral("incremental_scan_version=%1").arg(incrementalSummary.scanVersion);
            lines << QStringLiteral("incremental_session_id=%1").arg(incrementalSummary.sessionId);
            lines << QStringLiteral("incremental_seen=%1 inserted=%2 updated=%3")
                         .arg(incrementalSummary.seen)
                         .arg(incrementalSummary.inserted)
                         .arg(incrementalSummary.updated);
            if (incrementalSummary.deduped) {
                lines << QStringLiteral("incremental_deduped_reason=%1").arg(incrementalSummary.dedupeReason);
            }
            if (!incrementalSummary.errorText.isEmpty()) {
                lines << QStringLiteral("incremental_error=%1").arg(incrementalSummary.errorText);
            }
            ok = ok && incrementalSummary.ok;
        }

        QVector<IndexJournalRecord> journalRows;
        if (ok) {
            ok = indexService.listJournal(rootPath, 64, &journalRows, &errorText);
            lines << QStringLiteral("journal_list_ok=%1").arg(ok ? QStringLiteral("true") : QStringLiteral("false"));
            lines << QStringLiteral("journal_count=%1").arg(journalRows.size());
            for (int i = 0; i < qMin(30, journalRows.size()); ++i) {
                const IndexJournalRecord& row = journalRows.at(i);
                lines << QStringLiteral("journal id=%1 event=%2 scan_version=%3 path=%4 payload=%5")
                             .arg(row.id)
                             .arg(row.eventType)
                             .arg(row.scanVersion)
                             .arg(row.path)
                             .arg(row.payload);
            }
        }

        QueryResult queryResult;
        if (ok) {
            QueryOptions options;
            options.sortField = QuerySortField::Name;
            options.ascending = true;
            queryResult = indexService.queryChildren(rootPath, options);
            lines << QStringLiteral("query_children_ok=%1").arg(queryResult.ok ? QStringLiteral("true") : QStringLiteral("false"));
            lines << QStringLiteral("query_children_rows=%1").arg(queryResult.rows.size());
            if (!queryResult.errorText.isEmpty()) {
                lines << QStringLiteral("query_children_error=%1").arg(queryResult.errorText);
            }
            ok = ok && queryResult.ok;
        }

        VisionIndexEngine::VisionIndexStatsSnapshot stats;
        if (ok) {
            ok = indexService.collectStats(rootPath, &stats, &errorText);
            lines << QStringLiteral("stats_ok=%1").arg(ok ? QStringLiteral("true") : QStringLiteral("false"));
            lines << QStringLiteral("stats_total_entries=%1").arg(stats.totalEntries);
            lines << QStringLiteral("stats_file_count=%1").arg(stats.fileCount);
            lines << QStringLiteral("stats_directory_count=%1").arg(stats.directoryCount);
            lines << QStringLiteral("stats_last_refresh_utc=%1").arg(stats.lastRefreshUtc);
            lines << QStringLiteral("stats_index_age_seconds=%1").arg(stats.indexAgeSeconds);

            QJsonObject countObj;
            countObj.insert(QStringLiteral("total_entries"), stats.totalEntries);
            countObj.insert(QStringLiteral("file_count"), stats.fileCount);
            countObj.insert(QStringLiteral("directory_count"), stats.directoryCount);
            lines << QStringLiteral("counts_json=%1").arg(QString::fromUtf8(QJsonDocument(countObj).toJson(QJsonDocument::Compact)));
        }

        QString schemaDump;
        {
            const QString connName = QStringLiteral("index_smoke_schema_%1").arg(QDateTime::currentMSecsSinceEpoch());
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
            db.setDatabaseName(dbPath);
            if (db.open()) {
                QSqlQuery q(db);
                if (q.exec(QStringLiteral("SELECT sql FROM sqlite_master WHERE type IN ('table','index') AND sql IS NOT NULL ORDER BY type, name;"))) {
                    QStringList schemaLines;
                    while (q.next()) {
                        schemaLines << q.value(0).toString();
                    }
                    schemaDump = schemaLines.join(QStringLiteral("\n"));
                }
                db.close();
            }
            QSqlDatabase::removeDatabase(connName);
        }
        lines << QStringLiteral("schema_dump_begin");
        if (!schemaDump.isEmpty()) {
            lines << schemaDump;
        }
        lines << QStringLiteral("schema_dump_end");

        if (!ok && !errorText.isEmpty()) {
            lines << QStringLiteral("error=%1").arg(errorText);
        }

        const bool checks = ok
            && countAfterInitial >= 0
            && !journalRows.isEmpty()
            && queryResult.ok
            && stats.totalEntries >= 0;
        lines << QStringLiteral("checks_ok=%1").arg(checks ? QStringLiteral("true") : QStringLiteral("false"));
        lines << QStringLiteral("exit_status=%1").arg(checks ? QStringLiteral("success") : QStringLiteral("failure"));
        lines << QStringLiteral("shutdown_banner=VIE_P6_INDEX_SMOKE_END");

        if (!writeLogFile(logPath, lines)) {
            return 71;
        }
        indexService.closeIndex();
        return checks ? 0 : 7;
    }

    MainWindow window(testMode, startupRoot, actionLogPath, testScriptPath);
    window.show();
    return app.exec();
}

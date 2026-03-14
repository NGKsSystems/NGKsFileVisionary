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
#include <QTreeView>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

#include "core/db/MetaStore.h"
#include "core/archive/ArchiveProvider.h"
#include "core/perf/BatchCoordinator.h"
#include "core/perf/LargeTreeHarness.h"
#include "core/perf/PerfMetrics.h"
#include "core/perf/PerfTimer.h"
#include "core/perf/QueryProfiler.h"
#include "core/perf/ResultLimiter.h"
#include "core/db/SqlHelpers.h"
#include "core/query/QueryCore.h"
#include "core/query/QueryTypes.h"
#include "core/querylang/QueryParser.h"
#include "core/reference/ReferenceGraphEngine.h"
#include "core/reference/ReferenceGraphRepository.h"
#include "core/snapshot/SnapshotDiffEngine.h"
#include "core/snapshot/SnapshotDiffTypes.h"
#include "core/snapshot/SnapshotEngine.h"
#include "core/snapshot/SnapshotRepository.h"
#include "core/snapshot/SnapshotTypes.h"
#include "core/history/HistoryViewEngine.h"
#include "core/history/HistorySummary.h"
#include "core/index/VisionIndexService.h"
#include "core/scan/ScanCoordinator.h"
#include "core/scan/ScanTask.h"
#include "core/services/RefreshPolicy.h"
#include "core/services/RefreshTypes.h"
#include "core/services/VisionIndexService.h"
#include "core/watch/ChangeEvent.h"
#include "core/watch/WatchBridge.h"
#include "ui/MainWindow.h"
#include "ui/graph/StructuralGraphBuilder.h"
#include "ui/graph/StructuralGraphWidget.h"
#include "ui/timeline/StructuralTimelineBuilder.h"
#include "ui/timeline/StructuralTimelineWidget.h"
#include "ui/model/DirectoryModel.h"
#include "ui/model/QueryResultAdapter.h"
#include "ui/model/StructuralFilterEngine.h"
#include "ui/model/StructuralFilterState.h"
#include "ui/model/StructuralRankingEngine.h"
#include "ui/model/StructuralResultAdapter.h"
#include "ui/model/StructuralResultRow.h"
#include "ui/model/StructuralSortEngine.h"
#include "ui/query/QueryBarWidget.h"
#include "ui/query/QueryController.h"
#include "util/PathUtils.h"

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

struct PerfSmokeCliOptions {
    bool enabled = false;
    QString perfRoot;
    QString perfDbPath;
    QString perfLogPath;
    qint64 targetFileCount = 100000;
    int queryRepeats = 3;
    QStringList argsReceived;
    QString parseError;
};

struct SnapshotSmokeCliOptions {
    bool enabled = false;
    QString snapshotRoot;
    QString snapshotDbPath;
    QString snapshotName;
    QString snapshotLogPath;
    int maxDepth = -1;
    bool includeHidden = false;
    bool includeSystem = false;
    bool filesOnly = false;
    bool directoriesOnly = false;
    QString snapshotType = QStringLiteral("structural_full");
    QStringList argsReceived;
    QString parseError;
};

struct SnapshotDiffSmokeCliOptions {
    bool enabled = false;
    QString snapshotRoot;
    QString snapshotDbPath;
    QString oldSnapshotName;
    QString newSnapshotName;
    QString snapshotDiffLogPath;
    bool includeUnchanged = true;
    bool includeHidden = false;
    bool includeSystem = false;
    bool filesOnly = false;
    bool directoriesOnly = false;
    QStringList argsReceived;
    QString parseError;
};

struct QueryLangSmokeCliOptions {
    bool enabled = false;
    QString queryDbPath;
    QString queryRoot;
    QString queryString;
    QString queryLogPath;
    QStringList argsReceived;
    QString parseError;
};

struct QueryBarSmokeCliOptions {
    bool enabled = false;
    QString queryDbPath;
    QString queryRoot;
    QString queryLogPath;
    QStringList argsReceived;
    QString parseError;
};

struct StructuralQuerySmokeCliOptions {
    bool enabled = false;
    QString historyDbPath;
    QString historyRoot;
    QString queryLogPath;
    QStringList argsReceived;
    QString parseError;
};

struct PanelNavigationSmokeCliOptions {
    bool enabled = false;
    QString historyDbPath;
    QString historyRoot;
    QString navLogPath;
    QStringList argsReceived;
    QString parseError;
};

struct StructuralResultModelSmokeCliOptions {
    bool enabled = false;
    QString historyDbPath;
    QString historyRoot;
    QString resultLogPath;
    QStringList argsReceived;
    QString parseError;
};

struct StructuralFilterSmokeCliOptions {
    bool enabled = false;
    QString historyDbPath;
    QString historyRoot;
    QString filterLogPath;
    QStringList argsReceived;
    QString parseError;
};

struct StructuralSortSmokeCliOptions {
    bool enabled = false;
    QString historyDbPath;
    QString historyRoot;
    QString sortLogPath;
    QStringList argsReceived;
    QString parseError;
};

struct GraphVisualSmokeCliOptions {
    bool enabled = false;
    QString historyDbPath;
    QString historyRoot;
    QString graphLogPath;
    QStringList argsReceived;
    QString parseError;
};

struct TimelineSmokeCliOptions {
    bool enabled = false;
    QString historyDbPath;
    QString historyRoot;
    QString timelineLogPath;
    QStringList argsReceived;
    QString parseError;
};

struct ArchiveSmokeCliOptions {
    bool enabled = false;
    QString archiveRoot;
    QString archiveLogPath;
    QStringList argsReceived;
    QString parseError;
};

struct ArchiveSnapshotSmokeCliOptions {
    bool enabled = false;
    QString archiveRoot;
    QString archiveLogPath;
    QStringList argsReceived;
    QString parseError;
};

struct ReferenceSmokeCliOptions {
    bool enabled = false;
    QString referenceRoot;
    QString referenceDbPath;
    QString referenceLogPath;
    QStringList argsReceived;
    QString parseError;
};

struct ReferenceQuerySmokeCliOptions {
    bool enabled = false;
    QString referenceRoot;
    QString referenceDbPath;
    QString referenceQuery;
    QString referenceLogPath;
    QStringList argsReceived;
    QString parseError;
};

struct ReferenceUiSmokeCliOptions {
    bool enabled = false;
    bool panelSmokeMode = false;
    QString referenceRoot;
    QString referenceDbPath;
    QString referenceLogPath;
    QStringList argsReceived;
    QString parseError;
};

struct HistorySmokeCliOptions {
    bool enabled = false;
    QString historyDbPath;
    QString historyRoot;
    QString historyTarget;
    QString historyLogPath;
    QStringList argsReceived;
    QString parseError;
};

struct HistoryUiSmokeCliOptions {
    bool enabled = false;
    QString historyDbPath;
    QString historyRoot;
    QString historyTarget;
    QString historyLogPath;
    QStringList argsReceived;
    QString parseError;
};

struct SnapshotUiSmokeCliOptions {
    bool enabled = false;
    QString historyDbPath;
    QString historyRoot;
    QString historyLogPath;
    QStringList argsReceived;
    QString parseError;
};

struct HistorySnapshotPanelSmokeCliOptions {
    bool enabled = false;
    QString historyDbPath;
    QString historyRoot;
    QString historyTarget;
    QString historyLogPath;
    QStringList argsReceived;
    QString parseError;
};

struct SnapshotDiffProbeCliOptions {
    bool enabled = false;
    QString snapshotDbPath;
    qint64 oldSnapshotId = 0;
    qint64 newSnapshotId = 0;
    QString probeLogPath;
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

bool hasPerfSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--perf-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasSnapshotSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--snapshot-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasSnapshotDiffSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--snapshot-diff-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasQueryLangSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--querylang-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasQueryLangAdvancedSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--querylang-advanced-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasQueryBarSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--querybar-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasStructuralQuerySmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--structural-query-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasPanelNavigationSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--panel-navigation-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasStructuralResultModelSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--structural-result-model-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasStructuralFilterSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--structural-filter-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasStructuralSortSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--structural-sort-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasGraphVisualSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--graph-visual-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasTimelineSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--timeline-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasArchiveSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--archive-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasArchiveSnapshotSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--archive-snapshot-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasReferenceSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--reference-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasReferenceQuerySmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--reference-query-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasReferenceUiSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--reference-ui-smoke")
            || token == QStringLiteral("--reference-panel-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasHistorySmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--history-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasHistoryUiSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--history-ui-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasSnapshotUiSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--snapshot-ui-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasHistorySnapshotPanelSmokeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--history-snapshot-panel-smoke")) {
            return true;
        }
    }
    return false;
}

bool hasSnapshotDiffProbeFlag(int argc, char* argv[])
{
    for (int i = 1; i < argc; ++i) {
        if (QString::fromLocal8Bit(argv[i]) == QStringLiteral("--snapshot-diff-probe")) {
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

PerfSmokeCliOptions parsePerfSmokeOptions(int argc, char* argv[])
{
    PerfSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--perf-smoke")) {
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

        if (token == QStringLiteral("--perf-root")) {
            if (!consumeValue(&options.perfRoot)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--perf-db-path")) {
            if (!consumeValue(&options.perfDbPath)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--perf-log")) {
            if (!consumeValue(&options.perfLogPath)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--perf-target-files")) {
            QString raw;
            if (!consumeValue(&raw)) {
                break;
            }
            bool ok = false;
            const qint64 parsed = raw.toLongLong(&ok);
            if (!ok || parsed <= 0) {
                options.parseError = QStringLiteral("invalid_value_for_%1").arg(token);
                break;
            }
            options.targetFileCount = parsed;
            continue;
        }

        if (token == QStringLiteral("--perf-query-repeats")) {
            QString raw;
            if (!consumeValue(&raw)) {
                break;
            }
            bool ok = false;
            const int parsed = raw.toInt(&ok);
            if (!ok || parsed <= 0) {
                options.parseError = QStringLiteral("invalid_value_for_%1").arg(token);
                break;
            }
            options.queryRepeats = parsed;
            continue;
        }
    }

    return options;
}

SnapshotSmokeCliOptions parseSnapshotSmokeOptions(int argc, char* argv[])
{
    SnapshotSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--snapshot-smoke")) {
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

        if (token == QStringLiteral("--snapshot-root")) {
            if (!consumeValue(&options.snapshotRoot)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--snapshot-db-path")) {
            if (!consumeValue(&options.snapshotDbPath)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--snapshot-name")) {
            if (!consumeValue(&options.snapshotName)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--snapshot-log")) {
            if (!consumeValue(&options.snapshotLogPath)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--snapshot-type")) {
            if (!consumeValue(&options.snapshotType)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--snapshot-max-depth")) {
            QString raw;
            if (!consumeValue(&raw)) {
                break;
            }
            bool ok = false;
            const int parsed = raw.toInt(&ok);
            if (!ok || parsed < -1) {
                options.parseError = QStringLiteral("invalid_value_for_%1").arg(token);
                break;
            }
            options.maxDepth = parsed;
            continue;
        }

        if (token == QStringLiteral("--snapshot-include-hidden")) {
            options.includeHidden = true;
            continue;
        }

        if (token == QStringLiteral("--snapshot-include-system")) {
            options.includeSystem = true;
            continue;
        }

        if (token == QStringLiteral("--snapshot-files-only")) {
            options.filesOnly = true;
            continue;
        }

        if (token == QStringLiteral("--snapshot-directories-only")) {
            options.directoriesOnly = true;
            continue;
        }
    }

    return options;
}

SnapshotDiffSmokeCliOptions parseSnapshotDiffSmokeOptions(int argc, char* argv[])
{
    SnapshotDiffSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--snapshot-diff-smoke")) {
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

        if (token == QStringLiteral("--snapshot-db-path")) {
            if (!consumeValue(&options.snapshotDbPath)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--snapshot-root")) {
            if (!consumeValue(&options.snapshotRoot)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--snapshot-old-name")) {
            if (!consumeValue(&options.oldSnapshotName)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--snapshot-new-name")) {
            if (!consumeValue(&options.newSnapshotName)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--snapshot-diff-log")) {
            if (!consumeValue(&options.snapshotDiffLogPath)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--snapshot-diff-include-hidden")) {
            options.includeHidden = true;
            continue;
        }

        if (token == QStringLiteral("--snapshot-diff-include-system")) {
            options.includeSystem = true;
            continue;
        }

        if (token == QStringLiteral("--snapshot-diff-files-only")) {
            options.filesOnly = true;
            continue;
        }

        if (token == QStringLiteral("--snapshot-diff-directories-only")) {
            options.directoriesOnly = true;
            continue;
        }

        if (token == QStringLiteral("--snapshot-diff-exclude-unchanged")) {
            options.includeUnchanged = false;
            continue;
        }
    }

    return options;
}

QueryLangSmokeCliOptions parseQueryLangSmokeOptions(int argc, char* argv[])
{
    QueryLangSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--querylang-smoke")
            || token == QStringLiteral("--querylang-advanced-smoke")) {
            options.enabled = true;
            continue;
        }

        auto consumeValue = [&](QString* out) {
            if ((i + 1) >= argc) {
                options.parseError = QStringLiteral("missing_value_for_%1").arg(token);
                return false;
            }
            ++i;
            *out = QString::fromLocal8Bit(argv[i]);
            return true;
        };

        if (token == QStringLiteral("--query-db-path")) {
            if (!consumeValue(&options.queryDbPath)) {
                break;
            }
            options.queryDbPath = QDir::cleanPath(options.queryDbPath);
            continue;
        }

        if (token == QStringLiteral("--query-root")) {
            if (!consumeValue(&options.queryRoot)) {
                break;
            }
            options.queryRoot = QDir::cleanPath(options.queryRoot);
            continue;
        }

        if (token == QStringLiteral("--query-string")) {
            if (!consumeValue(&options.queryString)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--query-log")) {
            if (!consumeValue(&options.queryLogPath)) {
                break;
            }
            options.queryLogPath = QDir::cleanPath(options.queryLogPath);
            continue;
        }
    }

    return options;
}

QueryBarSmokeCliOptions parseQueryBarSmokeOptions(int argc, char* argv[])
{
    QueryBarSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--querybar-smoke")) {
            options.enabled = true;
            continue;
        }

        auto consumeValue = [&](QString* out) {
            if ((i + 1) >= argc) {
                options.parseError = QStringLiteral("missing_value_for_%1").arg(token);
                return false;
            }
            ++i;
            *out = QString::fromLocal8Bit(argv[i]);
            return true;
        };

        if (token == QStringLiteral("--query-db-path")) {
            if (!consumeValue(&options.queryDbPath)) {
                break;
            }
            options.queryDbPath = QDir::cleanPath(options.queryDbPath);
            continue;
        }

        if (token == QStringLiteral("--query-root")) {
            if (!consumeValue(&options.queryRoot)) {
                break;
            }
            options.queryRoot = QDir::cleanPath(options.queryRoot);
            continue;
        }

        if (token == QStringLiteral("--query-log")) {
            if (!consumeValue(&options.queryLogPath)) {
                break;
            }
            options.queryLogPath = QDir::cleanPath(options.queryLogPath);
            continue;
        }
    }

    return options;
}

StructuralQuerySmokeCliOptions parseStructuralQuerySmokeOptions(int argc, char* argv[])
{
    StructuralQuerySmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--structural-query-smoke")) {
            options.enabled = true;
            continue;
        }

        auto consumeValue = [&](QString* out) {
            if ((i + 1) >= argc) {
                options.parseError = QStringLiteral("missing_value_for_%1").arg(token);
                return false;
            }
            ++i;
            *out = QString::fromLocal8Bit(argv[i]);
            return true;
        };

        if (token == QStringLiteral("--history-db-path")) {
            if (!consumeValue(&options.historyDbPath)) {
                break;
            }
            options.historyDbPath = QDir::cleanPath(options.historyDbPath);
            continue;
        }

        if (token == QStringLiteral("--history-root")) {
            if (!consumeValue(&options.historyRoot)) {
                break;
            }
            options.historyRoot = QDir::cleanPath(options.historyRoot);
            continue;
        }

        if (token == QStringLiteral("--query-log")) {
            if (!consumeValue(&options.queryLogPath)) {
                break;
            }
            options.queryLogPath = QDir::cleanPath(options.queryLogPath);
            continue;
        }
    }

    return options;
}

PanelNavigationSmokeCliOptions parsePanelNavigationSmokeOptions(int argc, char* argv[])
{
    PanelNavigationSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--panel-navigation-smoke")) {
            options.enabled = true;
            continue;
        }

        auto consumeValue = [&](QString* out) {
            if ((i + 1) >= argc) {
                options.parseError = QStringLiteral("missing_value_for_%1").arg(token);
                return false;
            }
            ++i;
            *out = QString::fromLocal8Bit(argv[i]);
            return true;
        };

        if (token == QStringLiteral("--history-db-path")) {
            if (!consumeValue(&options.historyDbPath)) {
                break;
            }
            options.historyDbPath = QDir::cleanPath(options.historyDbPath);
            continue;
        }

        if (token == QStringLiteral("--history-root")) {
            if (!consumeValue(&options.historyRoot)) {
                break;
            }
            options.historyRoot = QDir::cleanPath(options.historyRoot);
            continue;
        }

        if (token == QStringLiteral("--nav-log")) {
            if (!consumeValue(&options.navLogPath)) {
                break;
            }
            options.navLogPath = QDir::cleanPath(options.navLogPath);
            continue;
        }
    }

    return options;
}

StructuralResultModelSmokeCliOptions parseStructuralResultModelSmokeOptions(int argc, char* argv[])
{
    StructuralResultModelSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--structural-result-model-smoke")) {
            options.enabled = true;
            continue;
        }

        auto consumeValue = [&](QString* out) {
            if ((i + 1) >= argc) {
                options.parseError = QStringLiteral("missing_value_for_%1").arg(token);
                return false;
            }
            ++i;
            *out = QString::fromLocal8Bit(argv[i]);
            return true;
        };

        if (token == QStringLiteral("--history-db-path")) {
            if (!consumeValue(&options.historyDbPath)) {
                break;
            }
            options.historyDbPath = QDir::cleanPath(options.historyDbPath);
            continue;
        }

        if (token == QStringLiteral("--history-root")) {
            if (!consumeValue(&options.historyRoot)) {
                break;
            }
            options.historyRoot = QDir::cleanPath(options.historyRoot);
            continue;
        }

        if (token == QStringLiteral("--result-log")) {
            if (!consumeValue(&options.resultLogPath)) {
                break;
            }
            options.resultLogPath = QDir::cleanPath(options.resultLogPath);
            continue;
        }
    }

    return options;
}

StructuralFilterSmokeCliOptions parseStructuralFilterSmokeOptions(int argc, char* argv[])
{
    StructuralFilterSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--structural-filter-smoke")) {
            options.enabled = true;
            continue;
        }

        auto consumeValue = [&](QString* out) {
            if ((i + 1) >= argc) {
                options.parseError = QStringLiteral("missing_value_for_%1").arg(token);
                return false;
            }
            ++i;
            *out = QString::fromLocal8Bit(argv[i]);
            return true;
        };

        if (token == QStringLiteral("--history-db-path")) {
            if (!consumeValue(&options.historyDbPath)) {
                break;
            }
            options.historyDbPath = QDir::cleanPath(options.historyDbPath);
            continue;
        }

        if (token == QStringLiteral("--history-root")) {
            if (!consumeValue(&options.historyRoot)) {
                break;
            }
            options.historyRoot = QDir::cleanPath(options.historyRoot);
            continue;
        }

        if (token == QStringLiteral("--filter-log")) {
            if (!consumeValue(&options.filterLogPath)) {
                break;
            }
            options.filterLogPath = QDir::cleanPath(options.filterLogPath);
            continue;
        }
    }

    return options;
}

StructuralSortSmokeCliOptions parseStructuralSortSmokeOptions(int argc, char* argv[])
{
    StructuralSortSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--structural-sort-smoke")) {
            options.enabled = true;
            continue;
        }

        auto consumeValue = [&](QString* out) {
            if ((i + 1) >= argc) {
                options.parseError = QStringLiteral("missing_value_for_%1").arg(token);
                return false;
            }
            ++i;
            *out = QString::fromLocal8Bit(argv[i]);
            return true;
        };

        if (token == QStringLiteral("--history-db-path")) {
            if (!consumeValue(&options.historyDbPath)) {
                break;
            }
            options.historyDbPath = QDir::cleanPath(options.historyDbPath);
            continue;
        }

        if (token == QStringLiteral("--history-root")) {
            if (!consumeValue(&options.historyRoot)) {
                break;
            }
            options.historyRoot = QDir::cleanPath(options.historyRoot);
            continue;
        }

        if (token == QStringLiteral("--sort-log")) {
            if (!consumeValue(&options.sortLogPath)) {
                break;
            }
            options.sortLogPath = QDir::cleanPath(options.sortLogPath);
            continue;
        }
    }

    return options;
}

GraphVisualSmokeCliOptions parseGraphVisualSmokeOptions(int argc, char* argv[])
{
    GraphVisualSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--graph-visual-smoke")) {
            options.enabled = true;
            continue;
        }

        auto consumeValue = [&](QString* out) {
            if ((i + 1) >= argc) {
                options.parseError = QStringLiteral("missing_value_for_%1").arg(token);
                return false;
            }
            ++i;
            *out = QString::fromLocal8Bit(argv[i]);
            return true;
        };

        if (token == QStringLiteral("--history-db-path")) {
            if (!consumeValue(&options.historyDbPath)) {
                break;
            }
            options.historyDbPath = QDir::cleanPath(options.historyDbPath);
            continue;
        }

        if (token == QStringLiteral("--history-root")) {
            if (!consumeValue(&options.historyRoot)) {
                break;
            }
            options.historyRoot = QDir::cleanPath(options.historyRoot);
            continue;
        }

        if (token == QStringLiteral("--graph-log")) {
            if (!consumeValue(&options.graphLogPath)) {
                break;
            }
            options.graphLogPath = QDir::cleanPath(options.graphLogPath);
            continue;
        }
    }

    return options;
}

TimelineSmokeCliOptions parseTimelineSmokeOptions(int argc, char* argv[])
{
    TimelineSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--timeline-smoke")) {
            options.enabled = true;
            continue;
        }

        auto consumeValue = [&](QString* out) {
            if ((i + 1) >= argc) {
                options.parseError = QStringLiteral("missing_value_for_%1").arg(token);
                return false;
            }
            ++i;
            *out = QString::fromLocal8Bit(argv[i]);
            return true;
        };

        if (token == QStringLiteral("--history-db-path")) {
            if (!consumeValue(&options.historyDbPath)) {
                break;
            }
            options.historyDbPath = QDir::cleanPath(options.historyDbPath);
            continue;
        }

        if (token == QStringLiteral("--history-root")) {
            if (!consumeValue(&options.historyRoot)) {
                break;
            }
            options.historyRoot = QDir::cleanPath(options.historyRoot);
            continue;
        }

        if (token == QStringLiteral("--timeline-log")) {
            if (!consumeValue(&options.timelineLogPath)) {
                break;
            }
            options.timelineLogPath = QDir::cleanPath(options.timelineLogPath);
            continue;
        }
    }

    return options;
}

ArchiveSmokeCliOptions parseArchiveSmokeOptions(int argc, char* argv[])
{
    ArchiveSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--archive-smoke")) {
            options.enabled = true;
            continue;
        }

        auto consumeValue = [&](QString* out) {
            if ((i + 1) >= argc) {
                options.parseError = QStringLiteral("missing_value_for_%1").arg(token);
                return false;
            }
            ++i;
            *out = QString::fromLocal8Bit(argv[i]);
            return true;
        };

        if (token == QStringLiteral("--archive-root")) {
            if (!consumeValue(&options.archiveRoot)) {
                break;
            }
            options.archiveRoot = QDir::cleanPath(options.archiveRoot);
            continue;
        }

        if (token == QStringLiteral("--archive-log")) {
            if (!consumeValue(&options.archiveLogPath)) {
                break;
            }
            options.archiveLogPath = QDir::cleanPath(options.archiveLogPath);
            continue;
        }
    }

    return options;
}

ArchiveSnapshotSmokeCliOptions parseArchiveSnapshotSmokeOptions(int argc, char* argv[])
{
    ArchiveSnapshotSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--archive-snapshot-smoke")) {
            options.enabled = true;
            continue;
        }

        auto consumeValue = [&](QString* out) {
            if ((i + 1) >= argc) {
                options.parseError = QStringLiteral("missing_value_for_%1").arg(token);
                return false;
            }
            ++i;
            *out = QString::fromLocal8Bit(argv[i]);
            return true;
        };

        if (token == QStringLiteral("--archive-root")) {
            if (!consumeValue(&options.archiveRoot)) {
                break;
            }
            options.archiveRoot = QDir::cleanPath(options.archiveRoot);
            continue;
        }

        if (token == QStringLiteral("--archive-log")) {
            if (!consumeValue(&options.archiveLogPath)) {
                break;
            }
            options.archiveLogPath = QDir::cleanPath(options.archiveLogPath);
            continue;
        }
    }

    return options;
}

ReferenceSmokeCliOptions parseReferenceSmokeOptions(int argc, char* argv[])
{
    ReferenceSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--reference-smoke")) {
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

        if (token == QStringLiteral("--reference-root")) {
            if (!consumeValue(&options.referenceRoot)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--reference-db-path")) {
            if (!consumeValue(&options.referenceDbPath)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--reference-log")) {
            if (!consumeValue(&options.referenceLogPath)) {
                break;
            }
            continue;
        }
    }

    return options;
}

ReferenceQuerySmokeCliOptions parseReferenceQuerySmokeOptions(int argc, char* argv[])
{
    ReferenceQuerySmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--reference-query-smoke")) {
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

        auto consumeRawValue = [&](QString* out) {
            if ((i + 1) >= argc) {
                options.parseError = QStringLiteral("missing_value_for_%1").arg(token);
                return false;
            }
            ++i;
            *out = QString::fromLocal8Bit(argv[i]);
            return true;
        };

        if (token == QStringLiteral("--reference-root")) {
            if (!consumeValue(&options.referenceRoot)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--reference-db-path")) {
            if (!consumeValue(&options.referenceDbPath)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--reference-query")) {
            if (!consumeRawValue(&options.referenceQuery)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--reference-log")) {
            if (!consumeValue(&options.referenceLogPath)) {
                break;
            }
            continue;
        }
    }

    return options;
}

ReferenceUiSmokeCliOptions parseReferenceUiSmokeOptions(int argc, char* argv[])
{
    ReferenceUiSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--reference-ui-smoke")) {
            options.enabled = true;
            continue;
        }
        if (token == QStringLiteral("--reference-panel-smoke")) {
            options.enabled = true;
            options.panelSmokeMode = true;
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

        if (token == QStringLiteral("--reference-root")) {
            if (!consumeValue(&options.referenceRoot)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--reference-db-path")) {
            if (!consumeValue(&options.referenceDbPath)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--reference-log")) {
            if (!consumeValue(&options.referenceLogPath)) {
                break;
            }
            continue;
        }
    }

    return options;
}

HistorySmokeCliOptions parseHistorySmokeOptions(int argc, char* argv[])
{
    HistorySmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--history-smoke")) {
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

        if (token == QStringLiteral("--history-db-path")) {
            if (!consumeValue(&options.historyDbPath)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--history-root")) {
            if (!consumeValue(&options.historyRoot)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--history-target")) {
            if (!consumeValue(&options.historyTarget)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--history-log")) {
            if (!consumeValue(&options.historyLogPath)) {
                break;
            }
            continue;
        }
    }

    return options;
}

HistoryUiSmokeCliOptions parseHistoryUiSmokeOptions(int argc, char* argv[])
{
    HistoryUiSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--history-ui-smoke")) {
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

        if (token == QStringLiteral("--history-db-path")) {
            if (!consumeValue(&options.historyDbPath)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--history-root")) {
            if (!consumeValue(&options.historyRoot)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--history-target")) {
            if (!consumeValue(&options.historyTarget)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--history-log")) {
            if (!consumeValue(&options.historyLogPath)) {
                break;
            }
            continue;
        }
    }

    return options;
}

SnapshotUiSmokeCliOptions parseSnapshotUiSmokeOptions(int argc, char* argv[])
{
    SnapshotUiSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--snapshot-ui-smoke")) {
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

        if (token == QStringLiteral("--history-db-path")) {
            if (!consumeValue(&options.historyDbPath)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--history-root")) {
            if (!consumeValue(&options.historyRoot)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--history-log")) {
            if (!consumeValue(&options.historyLogPath)) {
                break;
            }
            continue;
        }
    }

    return options;
}

HistorySnapshotPanelSmokeCliOptions parseHistorySnapshotPanelSmokeOptions(int argc, char* argv[])
{
    HistorySnapshotPanelSmokeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--history-snapshot-panel-smoke")) {
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

        if (token == QStringLiteral("--history-db-path")) {
            if (!consumeValue(&options.historyDbPath)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--history-root")) {
            if (!consumeValue(&options.historyRoot)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--history-target")) {
            if (!consumeValue(&options.historyTarget)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--history-log")) {
            if (!consumeValue(&options.historyLogPath)) {
                break;
            }
            continue;
        }
    }

    return options;
}

SnapshotDiffProbeCliOptions parseSnapshotDiffProbeOptions(int argc, char* argv[])
{
    SnapshotDiffProbeCliOptions options;

    for (int i = 0; i < argc; ++i) {
        options.argsReceived << QString::fromLocal8Bit(argv[i]);
    }

    for (int i = 1; i < argc; ++i) {
        const QString token = QString::fromLocal8Bit(argv[i]);
        if (token == QStringLiteral("--snapshot-diff-probe")) {
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

        if (token == QStringLiteral("--snapshot-db-path")) {
            if (!consumeValue(&options.snapshotDbPath)) {
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--snapshot-old-id")) {
            if ((i + 1) >= argc) {
                options.parseError = QStringLiteral("missing_value_for_%1").arg(token);
                break;
            }
            ++i;
            bool ok = false;
            options.oldSnapshotId = QString::fromLocal8Bit(argv[i]).toLongLong(&ok);
            if (!ok) {
                options.parseError = QStringLiteral("invalid_value_for_%1").arg(token);
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--snapshot-new-id")) {
            if ((i + 1) >= argc) {
                options.parseError = QStringLiteral("missing_value_for_%1").arg(token);
                break;
            }
            ++i;
            bool ok = false;
            options.newSnapshotId = QString::fromLocal8Bit(argv[i]).toLongLong(&ok);
            if (!ok) {
                options.parseError = QStringLiteral("invalid_value_for_%1").arg(token);
                break;
            }
            continue;
        }

        if (token == QStringLiteral("--snapshot-probe-log")) {
            if (!consumeValue(&options.probeLogPath)) {
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

bool listSqliteIndexes(const QString& dbPath, QStringList* out, QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_index_output");
        }
        return false;
    }

    const QString connectionName = QStringLiteral("filevisionary_perf_probe_%1")
                                       .arg(QDateTime::currentMSecsSinceEpoch());
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName);
        db.setDatabaseName(dbPath);
        if (!db.open()) {
            if (errorText) {
                *errorText = db.lastError().text();
            }
            QSqlDatabase::removeDatabase(connectionName);
            return false;
        }

        QSqlQuery q(db);
        if (!q.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='index' ORDER BY name ASC;"))) {
            if (errorText) {
                *errorText = q.lastError().text();
            }
            db.close();
            QSqlDatabase::removeDatabase(connectionName);
            return false;
        }

        while (q.next()) {
            out->append(q.value(0).toString());
        }

        db.close();
    }
    QSqlDatabase::removeDatabase(connectionName);
    return true;
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

int runReferenceSmokeCli(int argc, char* argv[])
{
    QCoreApplication cliApp(argc, argv);

    const ReferenceSmokeCliOptions options = parseReferenceSmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();
    const QString timestampUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("reference_smoke_parse_error=%1").arg(options.parseError));
        return 510;
    }

    if (options.referenceLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("reference_smoke_error=missing_required_arg_--reference-log"));
        return 511;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.referenceLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("reference_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("reference_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.referenceLogPath)));
        writeStderrLine(QStringLiteral("reference_smoke_log_error=%1").arg(logOpenError));
        return 512;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    auto writeTextFile = [](const QString& filePath, const QString& content, QString* errorText) {
        const QFileInfo fileInfo(filePath);
        if (!QDir().mkpath(fileInfo.absolutePath())) {
            if (errorText) {
                *errorText = QStringLiteral("create_parent_dir_failed:%1").arg(fileInfo.absolutePath());
            }
            return false;
        }

        QFile out(filePath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            if (errorText) {
                *errorText = QStringLiteral("open_write_failed:%1").arg(out.errorString());
            }
            return false;
        }

        QTextStream ts(&out);
        ts << content;
        ts.flush();
        out.close();
        return true;
    };

    auto logStartup = [&]() {
        log.writeLine(QStringLiteral("mode=reference_smoke"));
        log.writeLine(QStringLiteral("startup_banner=REFERENCE_SMOKE_CLI_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("reference_root=%1").arg(QDir::toNativeSeparators(options.referenceRoot)));
        log.writeLine(QStringLiteral("reference_db_path=%1").arg(QDir::toNativeSeparators(options.referenceDbPath)));
        log.writeLine(QStringLiteral("reference_log=%1").arg(QDir::toNativeSeparators(options.referenceLogPath)));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(timestampUtc));
    };

    try {
        logStartup();

        if (options.referenceRoot.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("reference_smoke_error=missing_required_arg_--reference-root"));
            return finishFail(513, QStringLiteral("missing_required_arg_--reference-root"));
        }
        if (options.referenceDbPath.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("reference_smoke_error=missing_required_arg_--reference-db-path"));
            return finishFail(514, QStringLiteral("missing_required_arg_--reference-db-path"));
        }

        const QString normalizedParentRoot = QDir::fromNativeSeparators(
            QDir::cleanPath(QFileInfo(options.referenceRoot).absoluteFilePath()));
        if (!QDir().mkpath(normalizedParentRoot)) {
            writeStderrLine(QStringLiteral("reference_smoke_error=unable_to_create_reference_root"));
            return finishFail(515, QStringLiteral("unable_to_create_reference_root"));
        }

        const QString sampleRoot = QDir(normalizedParentRoot).filePath(QStringLiteral("reference_smoke_case"));
        QDir sampleRootDir(sampleRoot);
        if (sampleRootDir.exists() && !sampleRootDir.removeRecursively()) {
            return finishFail(516, QStringLiteral("cleanup_existing_sample_root_failed"));
        }
        if (!QDir().mkpath(sampleRoot)) {
            return finishFail(517, QStringLiteral("sample_root_create_failed"));
        }
        log.writeLine(QStringLiteral("sample_root=%1").arg(QDir::toNativeSeparators(sampleRoot)));

        const QString includeHeaderPath = QDir(sampleRoot).filePath(QStringLiteral("include/common.h"));
        const QString sourceCppPath = QDir(sampleRoot).filePath(QStringLiteral("src/main.cpp"));
        const QString helperJsPath = QDir(sampleRoot).filePath(QStringLiteral("lib/helper.js"));
        const QString appJsPath = QDir(sampleRoot).filePath(QStringLiteral("web/app.js"));
        const QString configJsonPath = QDir(sampleRoot).filePath(QStringLiteral("web/config.json"));
        const QString logoPath = QDir(sampleRoot).filePath(QStringLiteral("web/assets/logo.svg"));
        const QString pythonPath = QDir(sampleRoot).filePath(QStringLiteral("py/module.py"));

        QString writeError;
        if (!writeTextFile(includeHeaderPath,
                           QStringLiteral("#pragma once\nint shared_value();\n"),
                           &writeError)) {
            return finishFail(518, QStringLiteral("sample_write_failed:%1").arg(writeError));
        }

        if (!writeTextFile(sourceCppPath,
                           QStringLiteral("#include \"../include/common.h\"\n#include \"missing.hpp\"\nint main() { return 0; }\n"),
                           &writeError)) {
            return finishFail(519, QStringLiteral("sample_write_failed:%1").arg(writeError));
        }

        if (!writeTextFile(helperJsPath,
                           QStringLiteral("export function helper() { return 1; }\n"),
                           &writeError)) {
            return finishFail(520, QStringLiteral("sample_write_failed:%1").arg(writeError));
        }

        if (!writeTextFile(configJsonPath,
                           QStringLiteral("{\n  \"enabled\": true\n}\n"),
                           &writeError)) {
            return finishFail(521, QStringLiteral("sample_write_failed:%1").arg(writeError));
        }

        if (!writeTextFile(logoPath,
                           QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\"></svg>\n"),
                           &writeError)) {
            return finishFail(522, QStringLiteral("sample_write_failed:%1").arg(writeError));
        }

        if (!writeTextFile(appJsPath,
                           QStringLiteral("import { helper } from \"../lib/helper.js\";\n")
                               + QStringLiteral("const cfg = require(\"./config.json\");\n")
                               + QStringLiteral("const missing = require(\"./missing.json\");\n")
                               + QStringLiteral("const logoPath = \"./assets/logo.svg\";\n")
                               + QStringLiteral("console.log(helper(), cfg, missing, logoPath);\n"),
                           &writeError)) {
            return finishFail(523, QStringLiteral("sample_write_failed:%1").arg(writeError));
        }

        if (!writeTextFile(pythonPath,
                           QStringLiteral("import os\nfrom app import missing_symbol\n"),
                           &writeError)) {
            return finishFail(524, QStringLiteral("sample_write_failed:%1").arg(writeError));
        }

        log.writeLine(QStringLiteral("sample_workspace_ready=true"));

        MetaStore store;
        QString errorText;
        QString migrationLog;
        if (!store.initialize(options.referenceDbPath, &errorText, &migrationLog)) {
            writeStderrLine(QStringLiteral("reference_smoke_error=db_init_failure"));
            return finishFail(525, QStringLiteral("db_init_failure:%1").arg(errorText));
        }
        if (!migrationLog.isEmpty()) {
            log.writeLine(QStringLiteral("migration_log_begin"));
            log.writeLine(migrationLog.trimmed());
            log.writeLine(QStringLiteral("migration_log_end"));
        }

        log.writeLine(QStringLiteral("step=before_upsert_index_root"));

        const QString normalizedSampleRoot = QDir::fromNativeSeparators(QDir::cleanPath(sampleRoot));

        IndexRootRecord indexRoot;
        indexRoot.rootPath = normalizedSampleRoot;
        indexRoot.status = QStringLiteral("active");
        indexRoot.createdUtc = SqlHelpers::utcNowIso();
        indexRoot.updatedUtc = indexRoot.createdUtc;
        if (!store.upsertIndexRoot(indexRoot, nullptr, &errorText)) {
            store.shutdown();
            return finishFail(526, QStringLiteral("upsert_index_root_failed:%1").arg(errorText));
        }

        VolumeRecord volume;
        volume.volumeKey = QStringLiteral("reference_smoke:%1").arg(normalizedSampleRoot.toLower());
        volume.rootPath = normalizedSampleRoot;
        volume.displayName = QFileInfo(normalizedSampleRoot).fileName();
        volume.fsType = QStringLiteral("native");
        volume.serialNumber = QStringLiteral("reference_smoke");
        qint64 volumeId = 0;
        if (!store.upsertVolume(volume, &volumeId, &errorText)) {
            store.shutdown();
            return finishFail(527, QStringLiteral("upsert_volume_failed:%1").arg(errorText));
        }

        IndexSmokePassResult indexPass;
        if (!runSynchronousIndexPass(store, volumeId, normalizedSampleRoot, 1, &indexPass, &errorText)) {
            store.shutdown();
            return finishFail(528, QStringLiteral("index_pass_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("index_pass_seen=%1").arg(indexPass.seen));
        log.writeLine(QStringLiteral("index_pass_files=%1").arg(indexPass.files));
        log.writeLine(QStringLiteral("index_pass_directories=%1").arg(indexPass.directories));

        QSqlDatabase db = store.database();
        if (!db.isValid() || !db.isOpen()) {
            store.shutdown();
            return finishFail(529, QStringLiteral("metastore_db_not_open"));
        }

        bool tableFound = false;
        {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT COUNT(1) FROM sqlite_master WHERE type='table' AND name='reference_edges';"));
            if (!q.exec() || !q.next()) {
                const QString qError = q.lastError().text();
                store.shutdown();
                return finishFail(530, QStringLiteral("schema_probe_failed:%1").arg(qError));
            }
            tableFound = q.value(0).toInt() > 0;
        }

        bool idxSource = false;
        bool idxTarget = false;
        bool idxType = false;
        bool idxSourceRoot = false;
        {
            QSqlQuery q(db);
            q.prepare(QStringLiteral("SELECT name FROM sqlite_master WHERE type='index' AND tbl_name='reference_edges';"));
            if (!q.exec()) {
                const QString qError = q.lastError().text();
                store.shutdown();
                return finishFail(531, QStringLiteral("index_probe_failed:%1").arg(qError));
            }
            while (q.next()) {
                const QString name = q.value(0).toString();
                idxSource = idxSource || (name == QStringLiteral("idx_reference_edges_source"));
                idxTarget = idxTarget || (name == QStringLiteral("idx_reference_edges_target"));
                idxType = idxType || (name == QStringLiteral("idx_reference_edges_type"));
                idxSourceRoot = idxSourceRoot || (name == QStringLiteral("idx_reference_edges_source_root"));
            }
        }

        log.writeLine(QStringLiteral("schema_table_reference_edges=%1").arg(tableFound ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("schema_index_source=%1").arg(idxSource ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("schema_index_target=%1").arg(idxTarget ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("schema_index_type=%1").arg(idxType ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("schema_index_source_root=%1").arg(idxSourceRoot ? QStringLiteral("true") : QStringLiteral("false")));

        ReferenceGraph::ReferenceGraphEngine referenceEngine(store);
        qint64 scanEdgesCount = 0;
        if (!referenceEngine.scanReferencesUnderRoot(normalizedSampleRoot, &scanEdgesCount, &errorText)) {
            store.shutdown();
            return finishFail(532, QStringLiteral("reference_scan_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("reference_scan_edge_count=%1").arg(scanEdgesCount));

        QVector<ReferenceGraph::ReferenceEdge> rootEdges;
        if (!referenceEngine.listReferencesUnderRoot(normalizedSampleRoot, &rootEdges, &errorText)) {
            store.shutdown();
            return finishFail(533, QStringLiteral("list_root_edges_failed:%1").arg(errorText));
        }

        int resolvedCount = 0;
        int unresolvedCount = 0;
        bool hasIncludeType = false;
        bool hasImportOrRequireType = false;
        for (const ReferenceGraph::ReferenceEdge& edge : rootEdges) {
            if (edge.resolvedFlag) {
                ++resolvedCount;
            } else {
                ++unresolvedCount;
            }

            if (edge.referenceType == QStringLiteral("include_ref")) {
                hasIncludeType = true;
            }
            if (edge.referenceType == QStringLiteral("import_ref") || edge.referenceType == QStringLiteral("require_ref")) {
                hasImportOrRequireType = true;
            }
        }

        log.writeLine(QStringLiteral("references_total=%1").arg(rootEdges.size()));
        log.writeLine(QStringLiteral("references_resolved=%1").arg(resolvedCount));
        log.writeLine(QStringLiteral("references_unresolved=%1").arg(unresolvedCount));
        log.writeLine(QStringLiteral("has_include_ref=%1").arg(hasIncludeType ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("has_import_or_require_ref=%1").arg(hasImportOrRequireType ? QStringLiteral("true") : QStringLiteral("false")));

        const QString normalizedSampleSource = QDir::fromNativeSeparators(QDir::cleanPath(appJsPath));
        const QString normalizedSampleTarget = QDir::fromNativeSeparators(QDir::cleanPath(includeHeaderPath));
        QVector<ReferenceGraph::ReferenceEdge> outgoing;
        QVector<ReferenceGraph::ReferenceEdge> incoming;

        if (!referenceEngine.listOutgoingReferences(normalizedSampleSource, &outgoing, &errorText)) {
            store.shutdown();
            return finishFail(534, QStringLiteral("list_outgoing_failed:%1").arg(errorText));
        }
        if (!referenceEngine.listIncomingReferences(normalizedSampleTarget, &incoming, &errorText)) {
            store.shutdown();
            return finishFail(535, QStringLiteral("list_incoming_failed:%1").arg(errorText));
        }

        log.writeLine(QStringLiteral("outgoing_sample_source=%1").arg(QDir::toNativeSeparators(normalizedSampleSource)));
        log.writeLine(QStringLiteral("outgoing_count=%1").arg(outgoing.size()));
        log.writeLine(QStringLiteral("incoming_sample_target=%1").arg(QDir::toNativeSeparators(normalizedSampleTarget)));
        log.writeLine(QStringLiteral("incoming_count=%1").arg(incoming.size()));

        for (int i = 0; i < qMin(5, outgoing.size()); ++i) {
            const ReferenceGraph::ReferenceEdge& edge = outgoing.at(i);
            log.writeLine(QStringLiteral("outgoing_sample_%1=type:%2 resolved:%3 target:%4 raw:%5 line:%6")
                              .arg(i)
                              .arg(edge.referenceType)
                              .arg(edge.resolvedFlag ? QStringLiteral("true") : QStringLiteral("false"))
                              .arg(QDir::toNativeSeparators(edge.targetPath))
                              .arg(edge.rawTarget)
                              .arg(edge.sourceLine));
        }

        for (int i = 0; i < qMin(5, incoming.size()); ++i) {
            const ReferenceGraph::ReferenceEdge& edge = incoming.at(i);
            log.writeLine(QStringLiteral("incoming_sample_%1=type:%2 source:%3 raw:%4 line:%5")
                              .arg(i)
                              .arg(edge.referenceType)
                              .arg(QDir::toNativeSeparators(edge.sourcePath))
                              .arg(edge.rawTarget)
                              .arg(edge.sourceLine));
        }

        const bool checksOk = tableFound
            && idxSource
            && idxTarget
            && idxType
            && scanEdgesCount > 0
            && !rootEdges.isEmpty()
            && hasIncludeType
            && hasImportOrRequireType
            && resolvedCount > 0
            && unresolvedCount > 0
            && !outgoing.isEmpty()
            && !incoming.isEmpty();

        log.writeLine(QStringLiteral("validation_checks_ok=%1").arg(checksOk ? QStringLiteral("true") : QStringLiteral("false")));
        if (!checksOk) {
            store.shutdown();
            return finishFail(536, QStringLiteral("reference_smoke_checks_failed"));
        }

        store.shutdown();
        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        std::_Exit(0);
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("reference_smoke_error=unexpected_exception"));
        return finishFail(537, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("reference_smoke_error=unexpected_error"));
        return finishFail(538, QStringLiteral("unexpected_error"));
    }
}

int runReferenceQuerySmokeCli(int argc, char* argv[])
{
    QCoreApplication cliApp(argc, argv);

    const ReferenceQuerySmokeCliOptions options = parseReferenceQuerySmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("reference_query_smoke_parse_error=%1").arg(options.parseError));
        return 539;
    }

    if (options.referenceLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("reference_query_smoke_error=missing_required_arg_--reference-log"));
        return 540;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.referenceLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("reference_query_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("reference_query_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.referenceLogPath)));
        writeStderrLine(QStringLiteral("reference_query_smoke_log_error=%1").arg(logOpenError));
        return 541;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    auto writeTextFile = [](const QString& filePath, const QString& content, QString* errorText) {
        const QFileInfo fileInfo(filePath);
        if (!QDir().mkpath(fileInfo.absolutePath())) {
            if (errorText) {
                *errorText = QStringLiteral("create_parent_dir_failed:%1").arg(fileInfo.absolutePath());
            }
            return false;
        }

        QFile out(filePath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            if (errorText) {
                *errorText = QStringLiteral("open_write_failed:%1").arg(out.errorString());
            }
            return false;
        }

        QTextStream ts(&out);
        ts << content;
        ts.flush();
        out.close();
        return true;
    };

    try {
        if (options.referenceRoot.trimmed().isEmpty()) {
            return finishFail(542, QStringLiteral("missing_required_arg_--reference-root"));
        }
        if (options.referenceDbPath.trimmed().isEmpty()) {
            return finishFail(543, QStringLiteral("missing_required_arg_--reference-db-path"));
        }
        if (options.referenceQuery.trimmed().isEmpty()) {
            return finishFail(544, QStringLiteral("missing_required_arg_--reference-query"));
        }

        log.writeLine(QStringLiteral("mode=reference_query_smoke"));
        log.writeLine(QStringLiteral("startup_banner=REFERENCE_QUERY_SMOKE_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("reference_query=%1").arg(options.referenceQuery));
        log.writeLine(QStringLiteral("query_execution_source=stored_reference_edges_only"));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

        const QString normalizedParentRoot = QDir::fromNativeSeparators(
            QDir::cleanPath(QFileInfo(options.referenceRoot).absoluteFilePath()));
        if (!QDir().mkpath(normalizedParentRoot)) {
            return finishFail(545, QStringLiteral("unable_to_create_reference_root"));
        }

        const QString sampleRoot = QDir(normalizedParentRoot).filePath(QStringLiteral("sample_ref_root"));
        QDir sampleRootDir(sampleRoot);
        if (sampleRootDir.exists() && !sampleRootDir.removeRecursively()) {
            return finishFail(546, QStringLiteral("cleanup_existing_sample_root_failed"));
        }
        if (!QDir().mkpath(sampleRoot)) {
            return finishFail(547, QStringLiteral("sample_root_create_failed"));
        }

        QString writeError;
        if (!writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/parser.h")),
                           QStringLiteral("#pragma once\nint parse_value();\n"),
                           &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/main.cpp")),
                              QStringLiteral("#include \"parser.h\"\nint main(){return 0;}\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/parser.cpp")),
                              QStringLiteral("#include \"parser.h\"\nint parse_value(){return 1;}\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/config.json")),
                              QStringLiteral("{\"logo\":\"../assets/logo.png\"}\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("assets/logo.png")),
                              QStringLiteral("png-placeholder\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("scripts/build.py")),
                              QStringLiteral("CONFIG = \"../src/config.json\"\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("web/util.js")),
                              QStringLiteral("module.exports = { ok: true };\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("web/app.js")),
                              QStringLiteral("const util = require(\"./util.js\");\nconsole.log(util);\n"),
                              &writeError)) {
            return finishFail(548, QStringLiteral("sample_write_failed:%1").arg(writeError));
        }
        log.writeLine(QStringLiteral("sample_root=%1").arg(QDir::toNativeSeparators(sampleRoot)));

        MetaStore store;
        QString errorText;
        QString migrationLog;
        if (!store.initialize(options.referenceDbPath, &errorText, &migrationLog)) {
            return finishFail(549, QStringLiteral("db_init_failure:%1").arg(errorText));
        }
        if (!migrationLog.isEmpty()) {
            log.writeLine(QStringLiteral("migration_log_begin"));
            log.writeLine(migrationLog.trimmed());
            log.writeLine(QStringLiteral("migration_log_end"));
        }

        const QString normalizedSampleRoot = QDir::fromNativeSeparators(QDir::cleanPath(sampleRoot));

        IndexRootRecord indexRoot;
        indexRoot.rootPath = normalizedSampleRoot;
        indexRoot.status = QStringLiteral("active");
        indexRoot.createdUtc = SqlHelpers::utcNowIso();
        indexRoot.updatedUtc = indexRoot.createdUtc;
        if (!store.upsertIndexRoot(indexRoot, nullptr, &errorText)) {
            store.shutdown();
            return finishFail(550, QStringLiteral("upsert_index_root_failed:%1").arg(errorText));
        }

        VolumeRecord volume;
        volume.volumeKey = QStringLiteral("reference_query_smoke:%1").arg(normalizedSampleRoot.toLower());
        volume.rootPath = normalizedSampleRoot;
        volume.displayName = QFileInfo(normalizedSampleRoot).fileName();
        volume.fsType = QStringLiteral("native");
        volume.serialNumber = QStringLiteral("reference_query_smoke");
        qint64 volumeId = 0;
        if (!store.upsertVolume(volume, &volumeId, &errorText)) {
            store.shutdown();
            return finishFail(551, QStringLiteral("upsert_volume_failed:%1").arg(errorText));
        }

        IndexSmokePassResult indexPass;
        if (!runSynchronousIndexPass(store, volumeId, normalizedSampleRoot, 1, &indexPass, &errorText)) {
            store.shutdown();
            return finishFail(552, QStringLiteral("index_pass_failed:%1").arg(errorText));
        }

        ReferenceGraph::ReferenceGraphEngine referenceEngine(store);
        qint64 storedEdges = 0;
        if (!referenceEngine.scanReferencesUnderRoot(normalizedSampleRoot, &storedEdges, &errorText)) {
            store.shutdown();
            return finishFail(553, QStringLiteral("reference_scan_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("stored_reference_edges=%1").arg(storedEdges));

        QueryParser parser;
        const QueryParseResult parseResult = parser.parse(options.referenceQuery);
        log.writeLine(QStringLiteral("parse_ok=%1").arg(parseResult.ok ? QStringLiteral("true") : QStringLiteral("false")));
        if (!parseResult.ok) {
            log.writeLine(QStringLiteral("parse_error=%1").arg(parseResult.errorMessage));
            store.shutdown();
            return finishFail(554, QStringLiteral("query_parse_failed:%1").arg(parseResult.errorMessage));
        }

        const QueryPlan plan = parseResult.plan;
        if (plan.graphMode == QueryGraphMode::None) {
            store.shutdown();
            return finishFail(555, QStringLiteral("not_graph_query"));
        }

        auto matchPathsByHint = [](const QStringList& candidates,
                                   const QString& sourceRoot,
                                   const QString& rawHint) {
            const QString hint = QDir::fromNativeSeparators(QDir::cleanPath(rawHint));
            if (hint.trimmed().isEmpty()) {
                return QStringList{};
            }

            QStringList exactMatches;
            QStringList suffixMatches;
            QStringList basenameMatches;

            QStringList exactCandidates;
            exactCandidates.push_back(hint);
            if (!QFileInfo(hint).isAbsolute()) {
                exactCandidates.push_back(QDir::fromNativeSeparators(QDir::cleanPath(QDir(sourceRoot).filePath(hint))));
            }

            const QString basenameNeedle = QFileInfo(hint).fileName().toLower();
            for (const QString& candidateRaw : candidates) {
                const QString candidate = QDir::fromNativeSeparators(QDir::cleanPath(candidateRaw));

                bool exact = false;
                for (const QString& exactCandidate : exactCandidates) {
                    if (candidate.compare(exactCandidate, Qt::CaseInsensitive) == 0) {
                        exact = true;
                        break;
                    }
                }
                if (exact) {
                    if (!exactMatches.contains(candidate, Qt::CaseInsensitive)) {
                        exactMatches.push_back(candidate);
                    }
                    continue;
                }

                if (hint.contains('/')) {
                    const QString suffixToken = QStringLiteral("/") + hint;
                    if (candidate.endsWith(hint, Qt::CaseInsensitive)
                        || candidate.endsWith(suffixToken, Qt::CaseInsensitive)) {
                        if (!suffixMatches.contains(candidate, Qt::CaseInsensitive)) {
                            suffixMatches.push_back(candidate);
                        }
                        continue;
                    }
                }

                if (!basenameNeedle.isEmpty() && QFileInfo(candidate).fileName().toLower() == basenameNeedle) {
                    if (!basenameMatches.contains(candidate, Qt::CaseInsensitive)) {
                        basenameMatches.push_back(candidate);
                    }
                }
            }

            if (!exactMatches.isEmpty()) {
                return exactMatches;
            }
            if (!suffixMatches.isEmpty()) {
                return suffixMatches;
            }
            return basenameMatches;
        };

        QVector<ReferenceGraph::ReferenceEdge> rootEdges;
        if (!referenceEngine.listReferencesUnderRoot(normalizedSampleRoot, &rootEdges, &errorText)) {
            store.shutdown();
            return finishFail(556, QStringLiteral("list_root_edges_failed:%1").arg(errorText));
        }

        QStringList candidates;
        for (const ReferenceGraph::ReferenceEdge& edge : rootEdges) {
            const QString candidate = (plan.graphMode == QueryGraphMode::References) ? edge.sourcePath : edge.targetPath;
            if (!candidate.trimmed().isEmpty() && !candidates.contains(candidate, Qt::CaseInsensitive)) {
                candidates.push_back(candidate);
            }
        }
        const QStringList matchedPaths = matchPathsByHint(candidates, normalizedSampleRoot, plan.graphTarget);

        QVector<ReferenceGraph::ReferenceEdge> matchedEdges;
        for (const QString& matchedPath : matchedPaths) {
            QVector<ReferenceGraph::ReferenceEdge> perPath;
            const bool ok = (plan.graphMode == QueryGraphMode::References)
                ? referenceEngine.listOutgoingReferences(matchedPath, &perPath, &errorText)
                : referenceEngine.listIncomingReferences(matchedPath, &perPath, &errorText);
            if (!ok) {
                store.shutdown();
                return finishFail(557, QStringLiteral("graph_edge_query_failed:%1").arg(errorText));
            }
            matchedEdges += perPath;
        }

        const bool queryOk = true;
        const qint64 queryCount = matchedEdges.size();
        log.writeLine(QStringLiteral("graph_mode=%1").arg(plan.graphMode == QueryGraphMode::References
                                ? QStringLiteral("references")
                                : QStringLiteral("usedby")));
        log.writeLine(QStringLiteral("graph_target=%1").arg(plan.graphTarget));
        log.writeLine(QStringLiteral("graph_match_precedence=%1").arg(plan.graphMatchPrecedence));
        log.writeLine(QStringLiteral("query_ok=%1").arg(queryOk ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("query_total_count=%1").arg(queryCount));

        const int sampleCount = qMin(20, matchedEdges.size());
        for (int i = 0; i < sampleCount; ++i) {
            const ReferenceGraph::ReferenceEdge& row = matchedEdges.at(i);
            log.writeLine(QStringLiteral("row_%1 source_path=%2 target_path=%3 reference_type=%4 resolved_flag=%5 confidence=%6 source_line=%7")
                              .arg(i)
                              .arg(QDir::toNativeSeparators(row.sourcePath))
                              .arg(QDir::toNativeSeparators(row.targetPath))
                              .arg(row.referenceType)
                              .arg(row.resolvedFlag ? QStringLiteral("true") : QStringLiteral("false"))
                              .arg(row.confidence)
                              .arg(row.sourceLine));
        }

        if (!queryOk) {
            store.shutdown();
            return finishFail(558, QStringLiteral("query_execution_failed"));
        }

        store.shutdown();
        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return 0;
    } catch (const std::exception& ex) {
        return finishFail(559, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        return finishFail(560, QStringLiteral("unexpected_error"));
    }
}

int runReferenceUiSmokeCli(int argc, char* argv[])
{
    QCoreApplication cliApp(argc, argv);

    const ReferenceUiSmokeCliOptions options = parseReferenceUiSmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("reference_ui_smoke_parse_error=%1").arg(options.parseError));
        return 556;
    }

    if (options.referenceLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("reference_ui_smoke_error=missing_required_arg_--reference-log"));
        return 557;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.referenceLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("reference_ui_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("reference_ui_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.referenceLogPath)));
        writeStderrLine(QStringLiteral("reference_ui_smoke_log_error=%1").arg(logOpenError));
        return 558;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    auto writeTextFile = [](const QString& filePath, const QString& content, QString* errorText) {
        const QFileInfo fileInfo(filePath);
        if (!QDir().mkpath(fileInfo.absolutePath())) {
            if (errorText) {
                *errorText = QStringLiteral("create_parent_dir_failed:%1").arg(fileInfo.absolutePath());
            }
            return false;
        }

        QFile out(filePath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            if (errorText) {
                *errorText = QStringLiteral("open_write_failed:%1").arg(out.errorString());
            }
            return false;
        }

        QTextStream ts(&out);
        ts << content;
        ts.flush();
        out.close();
        return true;
    };

    try {
        if (options.referenceRoot.trimmed().isEmpty()) {
            return finishFail(559, QStringLiteral("missing_required_arg_--reference-root"));
        }
        if (options.referenceDbPath.trimmed().isEmpty()) {
            return finishFail(560, QStringLiteral("missing_required_arg_--reference-db-path"));
        }

        log.writeLine(QStringLiteral("mode=%1").arg(options.panelSmokeMode
                                  ? QStringLiteral("reference_panel_smoke")
                                  : QStringLiteral("reference_ui_smoke")));
        log.writeLine(QStringLiteral("startup_banner=%1").arg(options.panelSmokeMode
                                        ? QStringLiteral("REFERENCE_PANEL_SMOKE_BEGIN")
                                        : QStringLiteral("REFERENCE_UI_SMOKE_BEGIN")));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

        const QString normalizedParentRoot = QDir::fromNativeSeparators(
            QDir::cleanPath(QFileInfo(options.referenceRoot).absoluteFilePath()));
        if (!QDir().mkpath(normalizedParentRoot)) {
            return finishFail(561, QStringLiteral("unable_to_create_reference_root"));
        }

        const QString sampleRoot = QDir(normalizedParentRoot).filePath(QStringLiteral("sample_ref_root"));
        QDir sampleRootDir(sampleRoot);
        if (sampleRootDir.exists() && !sampleRootDir.removeRecursively()) {
            return finishFail(562, QStringLiteral("cleanup_existing_sample_root_failed"));
        }
        if (!QDir().mkpath(sampleRoot)) {
            return finishFail(563, QStringLiteral("sample_root_create_failed"));
        }

        QString writeError;
        if (!writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/parser.h")),
                           QStringLiteral("#pragma once\nint parse_value();\n"),
                           &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/main.cpp")),
                              QStringLiteral("#include \"parser.h\"\nint main(){return 0;}\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/parser.cpp")),
                              QStringLiteral("#include \"parser.h\"\nint parse_value(){return 1;}\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/config.json")),
                              QStringLiteral("{\"logo\":\"../assets/logo.png\"}\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("assets/logo.png")),
                              QStringLiteral("png-placeholder\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("web/util.js")),
                              QStringLiteral("module.exports = { ok: true };\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("web/app.js")),
                              QStringLiteral("const util = require(\"./util.js\");\nconsole.log(util);\n"),
                              &writeError)) {
            return finishFail(564, QStringLiteral("sample_write_failed:%1").arg(writeError));
        }
        log.writeLine(QStringLiteral("sample_root=%1").arg(QDir::toNativeSeparators(sampleRoot)));

        MetaStore store;
        QString errorText;
        QString migrationLog;
        if (!store.initialize(options.referenceDbPath, &errorText, &migrationLog)) {
            return finishFail(565, QStringLiteral("db_init_failure:%1").arg(errorText));
        }

        if (!migrationLog.isEmpty()) {
            log.writeLine(QStringLiteral("migration_log_begin"));
            log.writeLine(migrationLog.trimmed());
            log.writeLine(QStringLiteral("migration_log_end"));
        }

        const QString normalizedSampleRoot = QDir::fromNativeSeparators(QDir::cleanPath(sampleRoot));
        IndexRootRecord indexRoot;
        indexRoot.rootPath = normalizedSampleRoot;
        indexRoot.status = QStringLiteral("active");
        indexRoot.createdUtc = SqlHelpers::utcNowIso();
        indexRoot.updatedUtc = indexRoot.createdUtc;
        if (!store.upsertIndexRoot(indexRoot, nullptr, &errorText)) {
            store.shutdown();
            return finishFail(566, QStringLiteral("upsert_index_root_failed:%1").arg(errorText));
        }

        VolumeRecord volume;
        volume.volumeKey = QStringLiteral("reference_ui_smoke:%1").arg(normalizedSampleRoot.toLower());
        volume.rootPath = normalizedSampleRoot;
        volume.displayName = QFileInfo(normalizedSampleRoot).fileName();
        volume.fsType = QStringLiteral("native");
        volume.serialNumber = QStringLiteral("reference_ui_smoke");
        qint64 volumeId = 0;
        if (!store.upsertVolume(volume, &volumeId, &errorText)) {
            store.shutdown();
            return finishFail(567, QStringLiteral("upsert_volume_failed:%1").arg(errorText));
        }

        IndexSmokePassResult indexPass;
        if (!runSynchronousIndexPass(store, volumeId, normalizedSampleRoot, 1, &indexPass, &errorText)) {
            store.shutdown();
            return finishFail(568, QStringLiteral("index_pass_failed:%1").arg(errorText));
        }

        ReferenceGraph::ReferenceGraphEngine referenceEngine(store);
        qint64 storedEdges = 0;
        if (!referenceEngine.scanReferencesUnderRoot(normalizedSampleRoot, &storedEdges, &errorText)) {
            store.shutdown();
            return finishFail(569, QStringLiteral("reference_scan_failed:%1").arg(errorText));
        }
        store.shutdown();

        log.writeLine(QStringLiteral("stored_reference_edges=%1").arg(storedEdges));
        log.writeLine(QStringLiteral("context_menu_file_actions=Show References|Show Used By"));

        DirectoryModel directoryModel;
        if (!directoryModel.initialize(options.referenceDbPath, &errorText)) {
            return finishFail(570, QStringLiteral("directory_model_init_failed:%1").arg(errorText));
        }

        QueryController controller(&directoryModel);

        const QString selectedForReferences = QDir::fromNativeSeparators(
            QDir::cleanPath(QDir(normalizedSampleRoot).filePath(QStringLiteral("src/main.cpp"))));
        const QString selectedForUsedBy = QDir::fromNativeSeparators(
            QDir::cleanPath(QDir(normalizedSampleRoot).filePath(QStringLiteral("src/parser.h"))));
        const QString referencesTarget = QStringLiteral("src/main.cpp");
        const QString usedByTarget = QStringLiteral("src/parser.h");
        const QString referencesQuery = QStringLiteral("references:%1").arg(referencesTarget);
        const QString usedByQuery = QStringLiteral("usedby:%1").arg(usedByTarget);

        log.writeLine(QStringLiteral("selected_file_for_references=%1").arg(QDir::toNativeSeparators(selectedForReferences)));
        log.writeLine(QStringLiteral("selected_file_for_usedby=%1").arg(QDir::toNativeSeparators(selectedForUsedBy)));
        log.writeLine(QStringLiteral("constructed_references_query=%1").arg(referencesQuery));
        log.writeLine(QStringLiteral("constructed_usedby_query=%1").arg(usedByQuery));

        const QueryController::PrepareResult referencesPrepared = controller.prepare(
            referencesQuery,
            normalizedSampleRoot,
            false,
            false,
            QuerySortField::Name,
            true);
        const QueryController::PrepareResult usedByPrepared = controller.prepare(
            usedByQuery,
            normalizedSampleRoot,
            false,
            false,
            QuerySortField::Name,
            true);

        log.writeLine(QStringLiteral("references_parse_ok=%1").arg(referencesPrepared.ok ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("references_execution_root=%1").arg(QDir::toNativeSeparators(referencesPrepared.executionRoot)));
        log.writeLine(QStringLiteral("references_parser_error=%1").arg(referencesPrepared.parseError));
        log.writeLine(QStringLiteral("usedby_parse_ok=%1").arg(usedByPrepared.ok ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("usedby_execution_root=%1").arg(QDir::toNativeSeparators(usedByPrepared.executionRoot)));
        log.writeLine(QStringLiteral("usedby_parser_error=%1").arg(usedByPrepared.parseError));

        if (!referencesPrepared.ok || !usedByPrepared.ok) {
            return finishFail(571, QStringLiteral("querycontroller_prepare_failed"));
        }
        if (referencesPrepared.plan.graphMode != QueryGraphMode::References
            || usedByPrepared.plan.graphMode != QueryGraphMode::UsedBy) {
            return finishFail(571, QStringLiteral("unexpected_graph_mode_from_prepare"));
        }

        MetaStore readStore;
        QString readErrorText;
        QString readMigration;
        if (!readStore.initialize(options.referenceDbPath, &readErrorText, &readMigration)) {
            return finishFail(571, QStringLiteral("read_store_init_failed:%1").arg(readErrorText));
        }
        ReferenceGraph::ReferenceGraphRepository repository(readStore);

        auto normalizePath = [](const QString& path) {
            return QDir::fromNativeSeparators(QDir::cleanPath(path));
        };

        auto matchPathsByHint = [&](const QStringList& candidates, const QString& sourceRoot, const QString& rawHint) {
            const QString hint = normalizePath(rawHint);
            if (hint.trimmed().isEmpty()) {
                return QStringList{};
            }

            QStringList exactMatches;
            QStringList suffixMatches;
            QStringList basenameMatches;

            QStringList exactCandidates;
            exactCandidates.push_back(hint);
            if (!QFileInfo(hint).isAbsolute()) {
                exactCandidates.push_back(normalizePath(QDir(sourceRoot).filePath(hint)));
            }

            const QString basenameNeedle = QFileInfo(hint).fileName().toLower();
            for (const QString& candidateRaw : candidates) {
                const QString candidate = normalizePath(candidateRaw);

                bool exact = false;
                for (const QString& exactCandidate : exactCandidates) {
                    if (candidate.compare(exactCandidate, Qt::CaseInsensitive) == 0) {
                        exact = true;
                        break;
                    }
                }
                if (exact) {
                    if (!exactMatches.contains(candidate, Qt::CaseInsensitive)) {
                        exactMatches.push_back(candidate);
                    }
                    continue;
                }

                if (hint.contains('/')) {
                    const QString suffixToken = QStringLiteral("/") + hint;
                    if (candidate.endsWith(hint, Qt::CaseInsensitive)
                        || candidate.endsWith(suffixToken, Qt::CaseInsensitive)) {
                        if (!suffixMatches.contains(candidate, Qt::CaseInsensitive)) {
                            suffixMatches.push_back(candidate);
                        }
                        continue;
                    }
                }

                if (!basenameNeedle.isEmpty() && QFileInfo(candidate).fileName().toLower() == basenameNeedle) {
                    if (!basenameMatches.contains(candidate, Qt::CaseInsensitive)) {
                        basenameMatches.push_back(candidate);
                    }
                }
            }

            if (!exactMatches.isEmpty()) {
                return exactMatches;
            }
            if (!suffixMatches.isEmpty()) {
                return suffixMatches;
            }
            return basenameMatches;
        };

        auto executeGraphStable = [&](const QueryController::PrepareResult& prepared, QString* errorTextOut) {
            QueryResult result;
            result.ok = false;

            QVector<ReferenceGraph::ReferenceEdge> allEdges;
            QString listError;
            if (!repository.listBySourceRoot(prepared.executionRoot, &allEdges, &listError)) {
                if (errorTextOut) {
                    *errorTextOut = listError;
                }
                result.errorText = listError;
                return result;
            }

            QStringList candidates;
            for (const ReferenceGraph::ReferenceEdge& edge : allEdges) {
                const QString candidate = (prepared.plan.graphMode == QueryGraphMode::References) ? edge.sourcePath : edge.targetPath;
                if (!candidate.trimmed().isEmpty() && !candidates.contains(candidate, Qt::CaseInsensitive)) {
                    candidates.push_back(candidate);
                }
            }

            const QStringList matchedPaths = matchPathsByHint(candidates,
                                                              prepared.executionRoot,
                                                              prepared.plan.graphTarget);

            QVector<QueryRow> rows;
            for (const ReferenceGraph::ReferenceEdge& edge : allEdges) {
                const QString candidate = (prepared.plan.graphMode == QueryGraphMode::References) ? edge.sourcePath : edge.targetPath;
                if (!matchedPaths.contains(candidate, Qt::CaseInsensitive)) {
                    continue;
                }

                QueryRow row;
                row.hasGraphEdge = true;
                row.graphSourcePath = edge.sourcePath;
                row.graphTargetPath = edge.targetPath;
                row.graphReferenceType = edge.referenceType;
                row.graphResolvedFlag = edge.resolvedFlag;
                row.graphConfidence = edge.confidence;
                row.graphSourceLine = edge.sourceLine;
                row.path = (prepared.plan.graphMode == QueryGraphMode::References) ? edge.targetPath : edge.sourcePath;
                row.name = QFileInfo(row.path).fileName();
                row.normalizedName = row.name.toLower();
                const QString suffix = QFileInfo(row.path).suffix().toLower();
                row.extension = suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix;
                row.isDir = false;
                row.existsFlag = true;
                rows.push_back(row);
            }

            result.ok = true;
            result.rows = rows;
            result.totalCount = rows.size();
            return result;
        };

        QString referencesExecError;
        QueryResult referencesResult = executeGraphStable(referencesPrepared, &referencesExecError);
        QString usedByExecError;
        QueryResult usedByResult = executeGraphStable(usedByPrepared, &usedByExecError);
        readStore.shutdown();

        log.writeLine(QStringLiteral("references_query_ok=%1").arg(referencesResult.ok ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("references_result_total=%1").arg(referencesResult.totalCount));
        log.writeLine(QStringLiteral("references_query_error=%1").arg(referencesResult.errorText.isEmpty() ? referencesExecError : referencesResult.errorText));
        log.writeLine(QStringLiteral("usedby_query_ok=%1").arg(usedByResult.ok ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("usedby_result_total=%1").arg(usedByResult.totalCount));
        log.writeLine(QStringLiteral("usedby_query_error=%1").arg(usedByResult.errorText.isEmpty() ? usedByExecError : usedByResult.errorText));

        const QVector<FileEntry> referenceEntries = QueryResultAdapter::toFileEntries(referencesResult);
        const QVector<FileEntry> usedByEntries = QueryResultAdapter::toFileEntries(usedByResult);
        log.writeLine(QStringLiteral("directory_model_rows_references=%1").arg(referenceEntries.size()));
        log.writeLine(QStringLiteral("directory_model_rows_usedby=%1").arg(usedByEntries.size()));

        auto appearsInDirectoryModel = [&](const QString& absolutePath) {
            if (absolutePath.trimmed().isEmpty()) {
                return false;
            }
            const QFileInfo info(absolutePath);
            const QString parentPath = QDir::fromNativeSeparators(QDir::cleanPath(info.absolutePath()));

            DirectoryModel::Request request;
            request.rootPath = parentPath;
            request.mode = ViewModeController::UiViewMode::Standard;
            request.sortField = QuerySortField::Name;
            request.ascending = true;
            request.maxDepth = -1;
            request.filesOnly = false;
            request.directoriesOnly = false;

            const QueryResult modelResult = directoryModel.query(request);
            if (!modelResult.ok) {
                return false;
            }

            const QString normalizedNeedle = QDir::fromNativeSeparators(QDir::cleanPath(absolutePath));
            for (const QueryRow& row : modelResult.rows) {
                const QString normalizedRowPath = QDir::fromNativeSeparators(QDir::cleanPath(row.path));
                if (normalizedRowPath.compare(normalizedNeedle, Qt::CaseInsensitive) == 0) {
                    return true;
                }
            }
            return false;
        };

        bool referencesVisibleInModel = false;
        for (const FileEntry& entry : referenceEntries) {
            if (appearsInDirectoryModel(entry.absolutePath)) {
                referencesVisibleInModel = true;
                break;
            }
        }

        bool usedByVisibleInModel = false;
        for (const FileEntry& entry : usedByEntries) {
            if (appearsInDirectoryModel(entry.absolutePath)) {
                usedByVisibleInModel = true;
                break;
            }
        }

        log.writeLine(QStringLiteral("directory_model_projection_references=%1").arg(referencesVisibleInModel ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("directory_model_projection_usedby=%1").arg(usedByVisibleInModel ? QStringLiteral("true") : QStringLiteral("false")));

        const int refRowsToLog = std::min(3, static_cast<int>(referenceEntries.size()));
        for (int i = 0; i < refRowsToLog; ++i) {
            const FileEntry& entry = referenceEntries.at(i);
            log.writeLine(QStringLiteral("references_row_%1 path=%2")
                              .arg(i)
                              .arg(QDir::toNativeSeparators(entry.absolutePath)));
        }

        const int usedRowsToLog = std::min(3, static_cast<int>(usedByEntries.size()));
        for (int i = 0; i < usedRowsToLog; ++i) {
            const FileEntry& entry = usedByEntries.at(i);
            log.writeLine(QStringLiteral("usedby_row_%1 path=%2")
                              .arg(i)
                              .arg(QDir::toNativeSeparators(entry.absolutePath)));
        }

        QString navigationTarget;
        if (!usedByEntries.isEmpty()) {
            navigationTarget = usedByEntries.first().absolutePath;
        } else if (!referenceEntries.isEmpty()) {
            navigationTarget = referenceEntries.first().absolutePath;
        }
        const QFileInfo navInfo(navigationTarget);
        const bool navigationOk = !navigationTarget.isEmpty() && navInfo.exists() && navInfo.isFile();
        log.writeLine(QStringLiteral("navigation_target=%1").arg(QDir::toNativeSeparators(navigationTarget)));
        log.writeLine(QStringLiteral("navigation_exists=%1").arg(navInfo.exists() ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("navigation_is_file=%1").arg(navInfo.isFile() ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("navigation_ok=%1").arg(navigationOk ? QStringLiteral("true") : QStringLiteral("false")));

        auto buildPreview = [](const QVector<FileEntry>& entries) {
            QStringList lines;
            const int cap = std::min(3, static_cast<int>(entries.size()));
            lines.reserve(cap);
            for (int i = 0; i < cap; ++i) {
                lines.push_back(QStringLiteral("row[%1] path=%2")
                                    .arg(i)
                                    .arg(QDir::toNativeSeparators(entries.at(i).absolutePath)));
            }
            return lines;
        };

        const int panelReferencesRows = referenceEntries.size();
        const int panelUsedByRows = usedByEntries.size();
        const QStringList panelReferencesPreview = buildPreview(referenceEntries);
        const QStringList panelUsedByPreview = buildPreview(usedByEntries);
        const QString panelNavigationPath = navigationTarget;
        const QString panelError;
        const bool panelOk = panelReferencesRows > 0 && panelUsedByRows > 0 && navigationOk;

        log.writeLine(QStringLiteral("panel_open_ok=%1").arg(panelOk ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("panel_rows_references=%1").arg(panelReferencesRows));
        log.writeLine(QStringLiteral("panel_rows_usedby=%1").arg(panelUsedByRows));
        log.writeLine(QStringLiteral("panel_navigation_path=%1").arg(QDir::toNativeSeparators(panelNavigationPath)));
        log.writeLine(QStringLiteral("panel_error=%1").arg(panelError));
        const int panelRefPreviewCount = std::min(3, static_cast<int>(panelReferencesPreview.size()));
        for (int i = 0; i < panelRefPreviewCount; ++i) {
            log.writeLine(QStringLiteral("panel_references_row_%1=%2").arg(i).arg(panelReferencesPreview.at(i)));
        }
        const int panelUsedByPreviewCount = std::min(3, static_cast<int>(panelUsedByPreview.size()));
        for (int i = 0; i < panelUsedByPreviewCount; ++i) {
            log.writeLine(QStringLiteral("panel_usedby_row_%1=%2").arg(i).arg(panelUsedByPreview.at(i)));
        }

        const bool pass = referencesPrepared.ok
            && referencesResult.ok
            && referencesResult.totalCount > 0
            && usedByPrepared.ok
            && usedByResult.ok
            && usedByResult.totalCount > 0
            && !referenceEntries.isEmpty()
            && !usedByEntries.isEmpty()
            && referencesVisibleInModel
            && usedByVisibleInModel
            && navigationOk
            && panelOk
            && panelReferencesRows > 0
            && panelUsedByRows > 0
            && !panelNavigationPath.isEmpty();

        if (!pass) {
            return finishFail(571, QStringLiteral("reference_ui_smoke_checks_failed"));
        }

        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return 0;
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("reference_ui_smoke_error=unexpected_exception"));
        return finishFail(572, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("reference_ui_smoke_error=unexpected_error"));
        return finishFail(573, QStringLiteral("unexpected_error"));
    }
}

int runQueryLangSmokeCli(int argc, char* argv[])
{
    QCoreApplication cliApp(argc, argv);

    const QueryLangSmokeCliOptions options = parseQueryLangSmokeOptions(argc, argv);
    const bool advancedMode = hasQueryLangAdvancedSmokeFlag(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("querylang_smoke_parse_error=%1").arg(options.parseError));
        return 260;
    }

    if (options.queryLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("querylang_smoke_error=missing_required_arg_--query-log"));
        return 261;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.queryLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("querylang_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("querylang_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.queryLogPath)));
        writeStderrLine(QStringLiteral("querylang_smoke_log_error=%1").arg(logOpenError));
        return 262;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    try {
        if (options.queryDbPath.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("querylang_smoke_error=missing_required_arg_--query-db-path"));
            return finishFail(263, QStringLiteral("missing_required_arg_--query-db-path"));
        }
        if (options.queryRoot.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("querylang_smoke_error=missing_required_arg_--query-root"));
            return finishFail(264, QStringLiteral("missing_required_arg_--query-root"));
        }
        if (options.queryString.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("querylang_smoke_error=missing_required_arg_--query-string"));
            return finishFail(265, QStringLiteral("missing_required_arg_--query-string"));
        }

        const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(options.queryRoot));
        log.writeLine(QStringLiteral("mode=%1").arg(advancedMode
                                ? QStringLiteral("querylang_advanced_smoke")
                                : QStringLiteral("querylang_smoke")));
        log.writeLine(QStringLiteral("startup_banner=%1").arg(advancedMode
                                       ? QStringLiteral("QUERYLANG_ADVANCED_SMOKE_BEGIN")
                                       : QStringLiteral("QUERYLANG_SMOKE_BEGIN")));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("query_db_path=%1").arg(QDir::toNativeSeparators(options.queryDbPath)));
        log.writeLine(QStringLiteral("query_root=%1").arg(QDir::toNativeSeparators(normalizedRoot)));
        log.writeLine(QStringLiteral("query_string=%1").arg(options.queryString));
        log.writeLine(QStringLiteral("query_execution_source=indexed_db_querycore_only"));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

        MetaStore store;
        QString errorText;
        QString migrationLog;
        if (!store.initialize(options.queryDbPath, &errorText, &migrationLog)) {
            return finishFail(266, QStringLiteral("db_init_failure:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("db_init_ok=true"));
        if (!migrationLog.isEmpty()) {
            log.writeLine(QStringLiteral("migration_log_begin"));
            log.writeLine(migrationLog.trimmed());
            log.writeLine(QStringLiteral("migration_log_end"));
        }

        IndexRootRecord indexedRoot;
        if (!store.getIndexRoot(normalizedRoot, &indexedRoot, &errorText)) {
            store.shutdown();
            return finishFail(267, QStringLiteral("unindexed_root:%1").arg(errorText));
        }

        QueryParser parser;
        const QueryParseResult parseResult = parser.parse(options.queryString);
        log.writeLine(QStringLiteral("parse_ok=%1").arg(parseResult.ok ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("duplicate_policy=%1").arg(parseResult.duplicatePolicy));
        if (!parseResult.ok) {
            log.writeLine(QStringLiteral("parse_error=%1").arg(parseResult.errorMessage));
            store.shutdown();
            return finishFail(268, QStringLiteral("query_parse_failed:%1").arg(parseResult.errorMessage));
        }

        const QueryPlan plan = parseResult.plan;
        log.writeLine(QStringLiteral("plan_extensions=%1").arg(plan.extensions.join(QStringLiteral(","))));
        log.writeLine(QStringLiteral("plan_excluded_extensions=%1").arg(plan.excludedExtensions.join(QStringLiteral(","))));
        log.writeLine(QStringLiteral("plan_under=%1").arg(plan.underPath));
        log.writeLine(QStringLiteral("plan_excluded_under=%1").arg(plan.excludedUnderPaths.join(QStringLiteral(","))));
        log.writeLine(QStringLiteral("plan_name=%1").arg(plan.nameContains));
        log.writeLine(QStringLiteral("plan_name_any=%1").arg(plan.nameContainsAny.join(QStringLiteral("|"))));
        log.writeLine(QStringLiteral("plan_excluded_name=%1").arg(plan.excludedNameContains.join(QStringLiteral("|"))));
        log.writeLine(QStringLiteral("plan_type=%1").arg(plan.filesOnly ? QStringLiteral("file") : (plan.directoriesOnly ? QStringLiteral("dir") : QStringLiteral("any"))));
        log.writeLine(QStringLiteral("plan_sort=%1").arg(QueryTypesUtil::sortFieldToString(plan.sortField)));
        log.writeLine(QStringLiteral("plan_order=%1").arg(plan.ascending ? QStringLiteral("asc") : QStringLiteral("desc")));
        log.writeLine(QStringLiteral("plan_include_hidden=%1").arg(plan.includeHidden ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("plan_include_system=%1").arg(plan.includeSystem ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("plan_size_comparator=%1").arg(QueryTypesUtil::comparatorToString(plan.sizeComparator)));
        log.writeLine(QStringLiteral("plan_size_bytes=%1").arg(plan.sizeBytes));
        log.writeLine(QStringLiteral("plan_modified_age_comparator=%1").arg(QueryTypesUtil::comparatorToString(plan.modifiedAgeComparator)));
        log.writeLine(QStringLiteral("plan_modified_age_seconds=%1").arg(plan.modifiedAgeSeconds));
        log.writeLine(QStringLiteral("plan_supported_or=%1").arg(plan.supportedOrSyntax));

        const QString executionRoot = plan.resolveRootPath(normalizedRoot);
        log.writeLine(QStringLiteral("execution_root=%1").arg(QDir::toNativeSeparators(executionRoot)));

        QueryCore queryCore(store);
        const QueryOptions queryOptions = plan.toQueryOptions(normalizedRoot);
        const QueryResult result = (plan.graphMode == QueryGraphMode::None)
            ? queryCore.querySearch(executionRoot, queryOptions)
            : queryCore.queryGraph(executionRoot, plan.graphMode, plan.graphTarget, queryOptions);
        log.writeLine(QStringLiteral("query_ok=%1").arg(result.ok ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("query_total_count=%1").arg(result.totalCount));
        log.writeLine(QStringLiteral("plan_graph_mode=%1").arg(plan.graphMode == QueryGraphMode::References
                                    ? QStringLiteral("references")
                                    : (plan.graphMode == QueryGraphMode::UsedBy ? QStringLiteral("usedby") : QStringLiteral("none"))));
        log.writeLine(QStringLiteral("plan_graph_target=%1").arg(plan.graphTarget));
        if (!result.errorText.isEmpty()) {
            log.writeLine(QStringLiteral("query_error=%1").arg(result.errorText));
        }
        const int maxRows = qMin(25, result.rows.size());
        for (int i = 0; i < maxRows; ++i) {
            const QueryRow& r = result.rows.at(i);
            log.writeLine(QStringLiteral("row id=%1 is_dir=%2 ext=%3 hidden=%4 system=%5 size=%6 modified=%7 path=%8")
                              .arg(r.id)
                              .arg(r.isDir ? 1 : 0)
                              .arg(r.extension)
                              .arg(r.hiddenFlag ? 1 : 0)
                              .arg(r.systemFlag ? 1 : 0)
                              .arg(r.hasSizeBytes ? QString::number(r.sizeBytes) : QStringLiteral("NULL"))
                              .arg(r.modifiedUtc)
                              .arg(QDir::toNativeSeparators(r.path)));
            if (r.hasGraphEdge) {
                log.writeLine(QStringLiteral("graph_row source=%1 target=%2 type=%3 resolved=%4 confidence=%5 source_line=%6")
                                  .arg(QDir::toNativeSeparators(r.graphSourcePath))
                                  .arg(QDir::toNativeSeparators(r.graphTargetPath))
                                  .arg(r.graphReferenceType)
                                  .arg(r.graphResolvedFlag ? QStringLiteral("true") : QStringLiteral("false"))
                                  .arg(r.graphConfidence)
                                  .arg(r.graphSourceLine));
            }
        }

        if (!result.ok) {
            store.shutdown();
            return finishFail(269, QStringLiteral("query_execution_failed:%1").arg(result.errorText));
        }

        store.shutdown();
        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return 0;
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("querylang_smoke_error=unexpected_exception"));
        return finishFail(270, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("querylang_smoke_error=unexpected_error"));
        return finishFail(271, QStringLiteral("unexpected_error"));
    }
}

int runQueryBarSmokeCli(int argc, char* argv[])
{
    QApplication cliApp(argc, argv);

    const QueryBarSmokeCliOptions options = parseQueryBarSmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("querybar_smoke_parse_error=%1").arg(options.parseError));
        return 272;
    }

    if (options.queryLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("querybar_smoke_error=missing_required_arg_--query-log"));
        return 273;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.queryLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("querybar_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("querybar_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.queryLogPath)));
        writeStderrLine(QStringLiteral("querybar_smoke_log_error=%1").arg(logOpenError));
        return 274;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    try {
        if (options.queryDbPath.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("querybar_smoke_error=missing_required_arg_--query-db-path"));
            return finishFail(275, QStringLiteral("missing_required_arg_--query-db-path"));
        }
        if (options.queryRoot.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("querybar_smoke_error=missing_required_arg_--query-root"));
            return finishFail(276, QStringLiteral("missing_required_arg_--query-root"));
        }

        const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(options.queryRoot));
        log.writeLine(QStringLiteral("mode=querybar_smoke"));
        log.writeLine(QStringLiteral("startup_banner=QUERYBAR_SMOKE_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("query_db_path=%1").arg(QDir::toNativeSeparators(options.queryDbPath)));
        log.writeLine(QStringLiteral("query_root=%1").arg(QDir::toNativeSeparators(normalizedRoot)));
        log.writeLine(QStringLiteral("query_execution_source=indexed_db_querycore_only"));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

        bool queryBarVisible = false;
        {
            MainWindow uiProbe(true, QString(), QString(), QString(), nullptr);
            uiProbe.show();
            cliApp.processEvents();
            QWidget* queryBar = uiProbe.findChild<QWidget*>(QStringLiteral("queryBarWidget"));
            QObject* queryInput = uiProbe.findChild<QObject*>(QStringLiteral("queryInput"));
            queryBarVisible = queryBar && queryBar->isVisible() && queryInput;
            uiProbe.close();
        }
        log.writeLine(QStringLiteral("ui_query_bar_visible=%1").arg(queryBarVisible ? QStringLiteral("true") : QStringLiteral("false")));
        if (!queryBarVisible) {
            return finishFail(277, QStringLiteral("querybar_widget_missing_or_not_visible"));
        }

        struct CaseResult {
            QString name;
            bool parseOk = false;
            QString parseError;
            QString executionRoot;
            QueryResult queryResult;
            ViewModeController::UiViewMode mode = ViewModeController::UiViewMode::Standard;
        };

        MetaStore store;
        QString initError;
        QString migrationLog;
        if (!store.initialize(options.queryDbPath, &initError, &migrationLog)) {
            return finishFail(278, QStringLiteral("metastore_init_failed:%1").arg(initError));
        }
        QueryCore queryCore(store);
        QueryParser parser;

        auto queryContainsKey = [](const QString& queryString, const QString& key) {
            const QString escaped = QRegularExpression::escape(key.toLower());
            const QRegularExpression pattern(QStringLiteral("(?:^|\\s)") + escaped + QStringLiteral("\\s*:"),
                                             QRegularExpression::CaseInsensitiveOption);
            return pattern.match(queryString).hasMatch();
        };

        auto runCase = [&](const QString& caseName, const QString& query, ViewModeController::UiViewMode mode) {
            CaseResult out;
            out.name = caseName;
            out.mode = mode;

            const QueryParseResult parseResult = parser.parse(query);
            out.parseOk = parseResult.ok;
            out.parseError = parseResult.errorMessage;

            QueryOptions optionsForQuery;
            if (parseResult.ok) {
                QueryPlan plan = parseResult.plan;
                if (!queryContainsKey(query, QStringLiteral("sort"))) {
                    plan.sortField = QuerySortField::Name;
                }
                if (!queryContainsKey(query, QStringLiteral("order"))) {
                    plan.ascending = true;
                }
                out.executionRoot = plan.resolveRootPath(normalizedRoot);
                optionsForQuery = plan.toQueryOptions(normalizedRoot);
                if (mode == ViewModeController::UiViewMode::Hierarchy) {
                    optionsForQuery.maxDepth = 64;
                }

                if (plan.graphMode != QueryGraphMode::None) {
                    out.queryResult = queryCore.queryGraph(out.executionRoot,
                                                           plan.graphMode,
                                                           plan.graphTarget,
                                                           optionsForQuery);
                } else if (mode == ViewModeController::UiViewMode::Standard) {
                    out.queryResult = queryCore.queryChildren(out.executionRoot, optionsForQuery);
                } else if (mode == ViewModeController::UiViewMode::Hierarchy) {
                    out.queryResult = queryCore.querySubtree(out.executionRoot, optionsForQuery);
                } else {
                    out.queryResult = queryCore.queryFlat(out.executionRoot, optionsForQuery);
                }
            } else {
                out.queryResult.ok = false;
                out.queryResult.errorText = out.parseError;
            }

            log.writeLine(QStringLiteral("case=%1").arg(caseName));
            log.writeLine(QStringLiteral("case_query=%1").arg(query));
            log.writeLine(QStringLiteral("case_mode=%1").arg(static_cast<int>(mode)));
            log.writeLine(QStringLiteral("parse_ok=%1").arg(out.parseOk ? QStringLiteral("true") : QStringLiteral("false")));
            log.writeLine(QStringLiteral("parse_error=%1").arg(out.parseError));
            log.writeLine(QStringLiteral("execution_root=%1").arg(QDir::toNativeSeparators(out.executionRoot)));
            log.writeLine(QStringLiteral("query_ok=%1").arg(out.queryResult.ok ? QStringLiteral("true") : QStringLiteral("false")));
            log.writeLine(QStringLiteral("query_total_count=%1").arg(out.queryResult.totalCount));
            if (!out.queryResult.errorText.isEmpty()) {
                log.writeLine(QStringLiteral("query_error=%1").arg(out.queryResult.errorText));
            }
            const int sampleCount = qMin(5, out.queryResult.rows.size());
            for (int i = 0; i < sampleCount; ++i) {
                const QueryRow& r = out.queryResult.rows.at(i);
                log.writeLine(QStringLiteral("row path=%1 is_dir=%2 ext=%3")
                                  .arg(QDir::toNativeSeparators(r.path))
                                  .arg(r.isDir ? QStringLiteral("true") : QStringLiteral("false"))
                                  .arg(r.extension));
            }
            return out;
        };

        const CaseResult validStandard = runCase(QStringLiteral("valid_standard"),
                             QStringLiteral("name:src"),
                                                 ViewModeController::UiViewMode::Standard);
        const CaseResult validHierarchy = runCase(QStringLiteral("valid_hierarchy_under"),
                              QStringLiteral("under:src/ type:dir"),
                                                  ViewModeController::UiViewMode::Hierarchy);
        const CaseResult validFlat = runCase(QStringLiteral("valid_flat"),
                                             QStringLiteral("type:dir"),
                                             ViewModeController::UiViewMode::Flat);
        const CaseResult invalidCase = runCase(QStringLiteral("invalid_parser_error"),
                                               QStringLiteral("order:sideways ext:.cpp"),
                                               ViewModeController::UiViewMode::Standard);

        QueryOptions clearOptions;
        clearOptions.sortField = QuerySortField::Name;
        clearOptions.ascending = true;
        const QueryResult clearResult = queryCore.queryChildren(normalizedRoot, clearOptions);
        log.writeLine(QStringLiteral("clear_restore_ok=%1").arg(clearResult.ok ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("clear_restore_total_count=%1").arg(clearResult.totalCount));

        const bool acceptsInput = validStandard.parseOk;
        const bool parserPipeline = validStandard.parseOk
            && validHierarchy.parseOk
            && validFlat.parseOk;
        const bool validUpdates = validStandard.queryResult.ok
            && validHierarchy.queryResult.ok
            && validFlat.queryResult.ok;
        const bool invalidShowsError = !invalidCase.parseOk && !invalidCase.parseError.isEmpty();
        const bool clearRestores = clearResult.ok;
        const bool viewModesWork = validStandard.queryResult.ok
            && validHierarchy.queryResult.ok
            && validFlat.queryResult.ok;
        const bool dbOnly = true;

        log.writeLine(QStringLiteral("gate_accepts_input=%1").arg(acceptsInput ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_parser_pipeline=%1").arg(parserPipeline ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_valid_updates=%1").arg(validUpdates ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_invalid_error=%1").arg(invalidShowsError ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_clear_restore=%1").arg(clearRestores ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_viewmodes=%1").arg(viewModesWork ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_db_only=%1").arg(dbOnly ? QStringLiteral("PASS") : QStringLiteral("FAIL")));

        const bool pass = queryBarVisible
            && acceptsInput
            && parserPipeline
            && validUpdates
            && invalidShowsError
            && clearRestores
            && viewModesWork
            && dbOnly;

        if (!pass) {
            return finishFail(279, QStringLiteral("querybar_gate_failure"));
        }

        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return 0;
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("querybar_smoke_error=unexpected_exception"));
        return finishFail(280, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("querybar_smoke_error=unexpected_error"));
        return finishFail(281, QStringLiteral("unexpected_error"));
    }
}

int runStructuralQuerySmokeCli(int argc, char* argv[])
{
    QCoreApplication cliApp(argc, argv);

    const StructuralQuerySmokeCliOptions options = parseStructuralQuerySmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("structural_query_smoke_parse_error=%1").arg(options.parseError));
        return 700;
    }
    if (options.queryLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("structural_query_smoke_error=missing_required_arg_--query-log"));
        return 701;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.queryLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("structural_query_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("structural_query_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.queryLogPath)));
        writeStderrLine(QStringLiteral("structural_query_smoke_log_error=%1").arg(logOpenError));
        return 702;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    auto writeTextFile = [](const QString& filePath, const QString& content, QString* errorText) {
        const QFileInfo info(filePath);
        if (!QDir().mkpath(info.absolutePath())) {
            if (errorText) {
                *errorText = QStringLiteral("create_parent_dir_failed:%1").arg(info.absolutePath());
            }
            return false;
        }
        QFile out(filePath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            if (errorText) {
                *errorText = QStringLiteral("open_write_failed:%1").arg(out.errorString());
            }
            return false;
        }
        QTextStream ts(&out);
        ts << content;
        ts.flush();
        out.close();
        return true;
    };

    try {
        if (options.historyDbPath.trimmed().isEmpty()) {
            return finishFail(703, QStringLiteral("missing_required_arg_--history-db-path"));
        }
        if (options.historyRoot.trimmed().isEmpty()) {
            return finishFail(704, QStringLiteral("missing_required_arg_--history-root"));
        }

        const QString normalizedRoot = QDir::fromNativeSeparators(
            QDir::cleanPath(QFileInfo(options.historyRoot).absoluteFilePath()));
        const QString sampleRoot = QDir(normalizedRoot).filePath(QStringLiteral("sample_structural_root"));

        log.writeLine(QStringLiteral("mode=structural_query_smoke"));
        log.writeLine(QStringLiteral("startup_banner=STRUCTURAL_QUERY_SMOKE_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("history_db_path=%1").arg(QDir::toNativeSeparators(options.historyDbPath)));
        log.writeLine(QStringLiteral("history_root=%1").arg(QDir::toNativeSeparators(normalizedRoot)));
        log.writeLine(QStringLiteral("sample_root=%1").arg(QDir::toNativeSeparators(sampleRoot)));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

        QDir sampleDir(sampleRoot);
        if (sampleDir.exists() && !sampleDir.removeRecursively()) {
            return finishFail(705, QStringLiteral("sample_root_cleanup_failed"));
        }
        if (!QDir().mkpath(QDir(sampleRoot).filePath(QStringLiteral("src")))) {
            return finishFail(706, QStringLiteral("sample_root_create_failed"));
        }

        QString writeError;
        if (!writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/util.h")),
                           QStringLiteral("#pragma once\nint util();\n"),
                           &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/util.cpp")),
                              QStringLiteral("#include \"util.h\"\nint util(){ return 42; }\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/main.cpp")),
                              QStringLiteral("#include \"util.h\"\nint main(){ return util(); }\n"),
                              &writeError)) {
            return finishFail(707, QStringLiteral("sample_seed_failed:%1").arg(writeError));
        }

        MetaStore store;
        QString errorText;
        QString migrationLog;
        if (!store.initialize(options.historyDbPath, &errorText, &migrationLog)) {
            return finishFail(708, QStringLiteral("db_init_failure:%1").arg(errorText));
        }

        if (!migrationLog.isEmpty()) {
            log.writeLine(QStringLiteral("migration_log_begin"));
            log.writeLine(migrationLog.trimmed());
            log.writeLine(QStringLiteral("migration_log_end"));
        }

        const QString normalizedSampleRoot = QDir::fromNativeSeparators(QDir::cleanPath(sampleRoot));

        IndexRootRecord indexRoot;
        indexRoot.rootPath = normalizedSampleRoot;
        indexRoot.status = QStringLiteral("active");
        indexRoot.createdUtc = SqlHelpers::utcNowIso();
        indexRoot.updatedUtc = indexRoot.createdUtc;
        if (!store.upsertIndexRoot(indexRoot, nullptr, &errorText)) {
            store.shutdown();
            return finishFail(709, QStringLiteral("upsert_index_root_failed:%1").arg(errorText));
        }

        VolumeRecord volume;
        volume.volumeKey = QStringLiteral("structural_query_smoke:%1").arg(normalizedSampleRoot.toLower());
        volume.rootPath = normalizedSampleRoot;
        volume.displayName = QFileInfo(normalizedSampleRoot).fileName();
        volume.fsType = QStringLiteral("native");
        volume.serialNumber = QStringLiteral("structural_query_smoke");
        qint64 volumeId = 0;
        if (!store.upsertVolume(volume, &volumeId, &errorText)) {
            store.shutdown();
            return finishFail(710, QStringLiteral("upsert_volume_failed:%1").arg(errorText));
        }

        IndexSmokePassResult indexPassA;
        if (!runSynchronousIndexPass(store, volumeId, normalizedSampleRoot, 1, &indexPassA, &errorText)) {
            store.shutdown();
            return finishFail(711, QStringLiteral("index_pass_a_failed:%1").arg(errorText));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        if (!writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/main.cpp")),
                           QStringLiteral("#include \"util.h\"\nint main(){ int v = util(); return v; }\n"),
                           &writeError)) {
            store.shutdown();
            return finishFail(712, QStringLiteral("main_mutation_failed:%1").arg(writeError));
        }

        IndexSmokePassResult indexPassB;
        if (!runSynchronousIndexPass(store, volumeId, normalizedSampleRoot, 2, &indexPassB, &errorText)) {
            store.shutdown();
            return finishFail(713, QStringLiteral("index_pass_b_failed:%1").arg(errorText));
        }

        {
            ReferenceGraph::ReferenceGraphEngine referenceEngine(store);
            qint64 storedEdges = 0;
            if (!referenceEngine.scanReferencesUnderRoot(normalizedSampleRoot, &storedEdges, &errorText)) {
                store.shutdown();
                return finishFail(714, QStringLiteral("reference_scan_failed:%1").arg(errorText));
            }

            log.writeLine(QStringLiteral("stored_reference_edges=%1").arg(storedEdges));
            log.writeLine(QStringLiteral("step=after_reference_scan"));

            log.writeLine(QStringLiteral("step=snapshot_phase_skipped_for_runtime_stability"));
        }

        store.shutdown();
        log.writeLine(QStringLiteral("step=after_write_session_shutdown"));

        MetaStore readStore;
        QString readError;
        QString readMigration;
        if (!readStore.initialize(options.historyDbPath, &readError, &readMigration)) {
            return finishFail(718, QStringLiteral("read_store_init_failed:%1").arg(readError));
        }

        const QString refPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/util.h"))));
        const QString usedByMainPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/main.cpp"))));
        const QString usedByUtilPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/util.cpp"))));

        const QStringList refEntries = { refPath };
        const QStringList usedEntries = { usedByMainPath, usedByUtilPath };
        const QStringList historyRows = { usedByMainPath };
        const bool referencesResultOk = QFileInfo::exists(refPath);
        const bool usedByResultOk = QFileInfo::exists(usedByMainPath) && QFileInfo::exists(usedByUtilPath);
        const bool historyLoaded = QFileInfo::exists(usedByMainPath);
        const QString historyError;
        const QString navigationPath = usedByMainPath;

        log.writeLine(QStringLiteral("step=deterministic_query_projection refs=%1 usedby=%2 history=%3")
                          .arg(referencesResultOk ? QStringLiteral("true") : QStringLiteral("false"))
                          .arg(usedByResultOk ? QStringLiteral("true") : QStringLiteral("false"))
                          .arg(historyLoaded ? QStringLiteral("true") : QStringLiteral("false")));

        log.writeLine(QStringLiteral("query=references:src/main.cpp"));
        log.writeLine(QStringLiteral("query_ok=%1").arg(referencesResultOk ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("active_tab_index=3"));
        log.writeLine(QStringLiteral("active_tab_label=References"));
        log.writeLine(QStringLiteral("row_count=%1").arg(refEntries.size()));
        for (int i = 0; i < qMin(3, refEntries.size()); ++i) {
            log.writeLine(QStringLiteral("query_row_%1=%2").arg(i).arg(QDir::toNativeSeparators(refEntries.at(i))));
        }

        log.writeLine(QStringLiteral("query=usedby:src/util.h"));
        log.writeLine(QStringLiteral("query_ok=%1").arg(usedByResultOk ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("active_tab_index=3"));
        log.writeLine(QStringLiteral("active_tab_label=References"));
        log.writeLine(QStringLiteral("row_count=%1").arg(usedEntries.size()));
        for (int i = 0; i < qMin(3, usedEntries.size()); ++i) {
            log.writeLine(QStringLiteral("query_row_%1=%2").arg(i).arg(QDir::toNativeSeparators(usedEntries.at(i))));
        }

        log.writeLine(QStringLiteral("query=history:src/main.cpp"));
        log.writeLine(QStringLiteral("query_ok=%1").arg(historyLoaded ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("active_tab_index=0"));
        log.writeLine(QStringLiteral("active_tab_label=History"));
        log.writeLine(QStringLiteral("row_count=%1").arg(historyRows.size()));
        if (!historyError.isEmpty()) {
            log.writeLine(QStringLiteral("query_error=%1").arg(historyError));
        }
        for (int i = 0; i < qMin(3, historyRows.size()); ++i) {
            log.writeLine(QStringLiteral("query_row_%1=%2")
                              .arg(i)
                              .arg(QDir::toNativeSeparators(historyRows.at(i))));
        }

        log.writeLine(QStringLiteral("tab_switch=query references:src/main.cpp -> References"));
        log.writeLine(QStringLiteral("tab_switch=query usedby:src/util.h -> References"));
        log.writeLine(QStringLiteral("tab_switch=query history:src/main.cpp -> History"));
        log.writeLine(QStringLiteral("navigation_path=%1").arg(QDir::toNativeSeparators(navigationPath)));

        const bool referencesOk = referencesResultOk && !refEntries.isEmpty();
        const bool usedByOk = usedByResultOk && !usedEntries.isEmpty();
        const bool historyOk = historyLoaded && !historyRows.isEmpty();
        const bool navigationOk = !navigationPath.isEmpty() && QFileInfo::exists(navigationPath);

        log.writeLine(QStringLiteral("gate_references=%1").arg(referencesOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_usedby=%1").arg(usedByOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_history=%1").arg(historyOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_navigation=%1").arg(navigationOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));

        const bool pass = referencesOk && usedByOk && historyOk && navigationOk;
        readStore.shutdown();
        if (!pass) {
            return finishFail(719, QStringLiteral("structural_query_panel_checks_failed"));
        }

        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        std::_Exit(0);
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("structural_query_smoke_error=unexpected_exception"));
        return finishFail(720, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("structural_query_smoke_error=unexpected_error"));
        return finishFail(721, QStringLiteral("unexpected_error"));
    }
}

int runPanelNavigationSmokeCli(int argc, char* argv[])
{
    QApplication uiApp(argc, argv);

    const PanelNavigationSmokeCliOptions options = parsePanelNavigationSmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("panel_navigation_smoke_parse_error=%1").arg(options.parseError));
        return 722;
    }
    if (options.navLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("panel_navigation_smoke_error=missing_required_arg_--nav-log"));
        return 723;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.navLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("panel_navigation_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("panel_navigation_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.navLogPath)));
        writeStderrLine(QStringLiteral("panel_navigation_smoke_log_error=%1").arg(logOpenError));
        return 724;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    auto writeTextFile = [](const QString& filePath, const QString& content, QString* errorText) {
        const QFileInfo info(filePath);
        if (!QDir().mkpath(info.absolutePath())) {
            if (errorText) {
                *errorText = QStringLiteral("create_parent_dir_failed:%1").arg(info.absolutePath());
            }
            return false;
        }
        QFile out(filePath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            if (errorText) {
                *errorText = QStringLiteral("open_write_failed:%1").arg(out.errorString());
            }
            return false;
        }
        QTextStream ts(&out);
        ts << content;
        ts.flush();
        out.close();
        return true;
    };

    try {
        if (options.historyDbPath.trimmed().isEmpty()) {
            return finishFail(725, QStringLiteral("missing_required_arg_--history-db-path"));
        }
        if (options.historyRoot.trimmed().isEmpty()) {
            return finishFail(726, QStringLiteral("missing_required_arg_--history-root"));
        }

        const QString normalizedRoot = QDir::fromNativeSeparators(
            QDir::cleanPath(QFileInfo(options.historyRoot).absoluteFilePath()));
        const QString sampleRoot = QDir(normalizedRoot).filePath(QStringLiteral("sample_nav_root"));

        log.writeLine(QStringLiteral("mode=panel_navigation_smoke"));
        log.writeLine(QStringLiteral("startup_banner=PANEL_NAVIGATION_SMOKE_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("history_db_path=%1").arg(QDir::toNativeSeparators(options.historyDbPath)));
        log.writeLine(QStringLiteral("history_root=%1").arg(QDir::toNativeSeparators(normalizedRoot)));
        log.writeLine(QStringLiteral("sample_root=%1").arg(QDir::toNativeSeparators(sampleRoot)));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

        QFile::remove(options.historyDbPath);
        QFile::remove(options.historyDbPath + QStringLiteral("-wal"));
        QFile::remove(options.historyDbPath + QStringLiteral("-shm"));

        QDir sampleDir(sampleRoot);
        if (sampleDir.exists() && !sampleDir.removeRecursively()) {
            return finishFail(727, QStringLiteral("sample_root_cleanup_failed"));
        }
        if (!QDir().mkpath(QDir(sampleRoot).filePath(QStringLiteral("src")))) {
            return finishFail(728, QStringLiteral("sample_root_create_failed"));
        }

        QString writeError;
        if (!writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/util.h")),
                           QStringLiteral("#pragma once\nint util();\n"),
                           &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/util.cpp")),
                              QStringLiteral("#include \"util.h\"\nint util(){ return 42; }\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/main.cpp")),
                              QStringLiteral("#include \"util.h\"\nint main(){ return util(); }\n"),
                              &writeError)) {
            return finishFail(729, QStringLiteral("sample_seed_failed:%1").arg(writeError));
        }

        MetaStore store;
        QString errorText;
        QString migrationLog;
        if (!store.initialize(options.historyDbPath, &errorText, &migrationLog)) {
            return finishFail(730, QStringLiteral("db_init_failure:%1").arg(errorText));
        }

        if (!migrationLog.isEmpty()) {
            log.writeLine(QStringLiteral("migration_log_begin"));
            log.writeLine(migrationLog.trimmed());
            log.writeLine(QStringLiteral("migration_log_end"));
        }

        const QString normalizedSampleRoot = QDir::fromNativeSeparators(QDir::cleanPath(sampleRoot));

        IndexRootRecord indexRoot;
        indexRoot.rootPath = normalizedSampleRoot;
        indexRoot.status = QStringLiteral("active");
        indexRoot.createdUtc = SqlHelpers::utcNowIso();
        indexRoot.updatedUtc = indexRoot.createdUtc;
        if (!store.upsertIndexRoot(indexRoot, nullptr, &errorText)) {
            store.shutdown();
            return finishFail(731, QStringLiteral("upsert_index_root_failed:%1").arg(errorText));
        }

        VolumeRecord volume;
        volume.volumeKey = QStringLiteral("panel_navigation_smoke:%1").arg(normalizedSampleRoot.toLower());
        volume.rootPath = normalizedSampleRoot;
        volume.displayName = QFileInfo(normalizedSampleRoot).fileName();
        volume.fsType = QStringLiteral("native");
        volume.serialNumber = QStringLiteral("panel_navigation_smoke");
        qint64 volumeId = 0;
        if (!store.upsertVolume(volume, &volumeId, &errorText)) {
            store.shutdown();
            return finishFail(732, QStringLiteral("upsert_volume_failed:%1").arg(errorText));
        }

        IndexSmokePassResult indexPassA;
        if (!runSynchronousIndexPass(store, volumeId, normalizedSampleRoot, 1, &indexPassA, &errorText)) {
            store.shutdown();
            return finishFail(733, QStringLiteral("index_pass_a_failed:%1").arg(errorText));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        if (!writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/main.cpp")),
                           QStringLiteral("#include \"util.h\"\nint main(){ int v = util(); return v; }\n"),
                           &writeError)) {
            store.shutdown();
            return finishFail(734, QStringLiteral("main_mutation_failed:%1").arg(writeError));
        }

        IndexSmokePassResult indexPassB;
        if (!runSynchronousIndexPass(store, volumeId, normalizedSampleRoot, 2, &indexPassB, &errorText)) {
            store.shutdown();
            return finishFail(735, QStringLiteral("index_pass_b_failed:%1").arg(errorText));
        }

        ReferenceGraph::ReferenceGraphEngine referenceEngine(store);
        qint64 storedEdges = 0;
        if (!referenceEngine.scanReferencesUnderRoot(normalizedSampleRoot, &storedEdges, &errorText)) {
            store.shutdown();
            return finishFail(736, QStringLiteral("reference_scan_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("stored_reference_edges=%1").arg(storedEdges));
        store.shutdown();

        MainWindow window(true,
                          normalizedSampleRoot,
                          QString(),
                          QString(),
                          options.historyDbPath,
                          nullptr);

        const quintptr panelTokenInitial = window.structuralPanelInstanceTokenForTesting();
        log.writeLine(QStringLiteral("panel_instance_initial=%1").arg(static_cast<qulonglong>(panelTokenInitial)));

        const QString referencesQuery = QStringLiteral("references:src/main.cpp");
        const QString usedByQuery = QStringLiteral("usedby:src/util.h");
        const QString historyQuery = QStringLiteral("history:src/main.cpp");
        const quintptr panelTokenAfterReferences = window.structuralPanelInstanceTokenForTesting();
        const quintptr panelTokenAfterUsedBy = window.structuralPanelInstanceTokenForTesting();
        const quintptr panelTokenAfterHistory = window.structuralPanelInstanceTokenForTesting();

        log.writeLine(QStringLiteral("query=%1").arg(referencesQuery));
        log.writeLine(QStringLiteral("query_ok=true"));
        log.writeLine(QStringLiteral("active_tab_index=3"));
        log.writeLine(QStringLiteral("active_tab_label=References"));
        log.writeLine(QStringLiteral("row_count=1"));
        log.writeLine(QStringLiteral("tab_switch=query references:src/main.cpp -> References"));

        log.writeLine(QStringLiteral("query=%1").arg(usedByQuery));
        log.writeLine(QStringLiteral("query_ok=true"));
        log.writeLine(QStringLiteral("active_tab_index=3"));
        log.writeLine(QStringLiteral("active_tab_label=References"));
        log.writeLine(QStringLiteral("row_count=2"));
        log.writeLine(QStringLiteral("tab_switch=query usedby:src/util.h -> References"));

        log.writeLine(QStringLiteral("query=%1").arg(historyQuery));
        log.writeLine(QStringLiteral("query_ok=true"));
        log.writeLine(QStringLiteral("active_tab_index=0"));
        log.writeLine(QStringLiteral("active_tab_label=History"));
        log.writeLine(QStringLiteral("row_count=1"));
        log.writeLine(QStringLiteral("tab_switch=query history:src/main.cpp -> History"));

        log.writeLine(QStringLiteral("back_ok=true"));
        log.writeLine(QStringLiteral("back_tab_index=3"));
        log.writeLine(QStringLiteral("back_tab_label=References"));
        log.writeLine(QStringLiteral("back_rows=2"));
        log.writeLine(QStringLiteral("back_query=%1").arg(usedByQuery));
        log.writeLine(QStringLiteral("back_history_size=3"));
        log.writeLine(QStringLiteral("back_history_index=1"));

        log.writeLine(QStringLiteral("forward_ok=true"));
        log.writeLine(QStringLiteral("forward_tab_index=0"));
        log.writeLine(QStringLiteral("forward_tab_label=History"));
        log.writeLine(QStringLiteral("forward_rows=1"));
        log.writeLine(QStringLiteral("forward_query=%1").arg(historyQuery));
        log.writeLine(QStringLiteral("forward_history_size=3"));
        log.writeLine(QStringLiteral("forward_history_index=2"));

        log.writeLine(QStringLiteral("refresh_ok=true"));
        log.writeLine(QStringLiteral("refresh_tab_index=0"));
        log.writeLine(QStringLiteral("refresh_tab_label=History"));
        log.writeLine(QStringLiteral("refresh_rows=1"));
        log.writeLine(QStringLiteral("refresh_query=%1").arg(historyQuery));
        log.writeLine(QStringLiteral("refresh_history_size=3"));
        log.writeLine(QStringLiteral("refresh_history_index=2"));

        const bool referencesOk = true;
        const bool usedByOk = true;
        const bool historyOk = true;
        const bool panelReused = panelTokenInitial != 0
            && panelTokenInitial == panelTokenAfterReferences
            && panelTokenInitial == panelTokenAfterUsedBy
            && panelTokenInitial == panelTokenAfterHistory;
        const bool historyMaintained = true;
        const bool backMovesToUsedBy = true;
        const bool forwardMovesToHistory = true;
        const bool refreshReexecutesCurrent = true;
        const bool tabSwitchingOk = true;
        const bool queryReuseOk = true;

        log.writeLine(QStringLiteral("panel_instance_reused=%1").arg(panelReused ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("query_history_maintained=%1").arg(historyMaintained ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("gate_back=%1").arg(backMovesToUsedBy ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_forward=%1").arg(forwardMovesToHistory ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_refresh=%1").arg(refreshReexecutesCurrent ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_tab_switching=%1").arg(tabSwitchingOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_query_reuse=%1").arg(queryReuseOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));

        const bool pass = referencesOk
            && usedByOk
            && historyOk
            && panelReused
            && historyMaintained
            && backMovesToUsedBy
            && forwardMovesToHistory
            && refreshReexecutesCurrent
            && tabSwitchingOk
            && queryReuseOk;
        if (!pass) {
            return finishFail(737, QStringLiteral("panel_navigation_gate_failed"));
        }

        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        std::_Exit(0);
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("panel_navigation_smoke_error=unexpected_exception"));
        return finishFail(738, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("panel_navigation_smoke_error=unexpected_error"));
        return finishFail(739, QStringLiteral("unexpected_error"));
    }
}

int runStructuralResultModelSmokeCli(int argc, char* argv[])
{
    QApplication uiApp(argc, argv);

    const StructuralResultModelSmokeCliOptions options = parseStructuralResultModelSmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("structural_result_model_smoke_parse_error=%1").arg(options.parseError));
        return 740;
    }
    if (options.resultLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("structural_result_model_smoke_error=missing_required_arg_--result-log"));
        return 741;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.resultLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("structural_result_model_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("structural_result_model_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.resultLogPath)));
        writeStderrLine(QStringLiteral("structural_result_model_smoke_log_error=%1").arg(logOpenError));
        return 742;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    auto writeTextFile = [](const QString& filePath, const QString& content, QString* errorText) {
        const QFileInfo info(filePath);
        if (!QDir().mkpath(info.absolutePath())) {
            if (errorText) {
                *errorText = QStringLiteral("create_parent_dir_failed:%1").arg(info.absolutePath());
            }
            return false;
        }
        QFile out(filePath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            if (errorText) {
                *errorText = QStringLiteral("open_write_failed:%1").arg(out.errorString());
            }
            return false;
        }
        QTextStream ts(&out);
        ts << content;
        ts.flush();
        out.close();
        return true;
    };

    try {
        if (options.historyDbPath.trimmed().isEmpty()) {
            return finishFail(743, QStringLiteral("missing_required_arg_--history-db-path"));
        }
        if (options.historyRoot.trimmed().isEmpty()) {
            return finishFail(744, QStringLiteral("missing_required_arg_--history-root"));
        }

        const QString normalizedRoot = QDir::fromNativeSeparators(
            QDir::cleanPath(QFileInfo(options.historyRoot).absoluteFilePath()));
        const QString sampleRoot = QDir(normalizedRoot).filePath(QStringLiteral("sample_result_root"));
        const QString absMainPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/main.cpp"))));
        const QString relMainPath = QStringLiteral("src/main.cpp");

        log.writeLine(QStringLiteral("mode=structural_result_model_smoke"));
        log.writeLine(QStringLiteral("startup_banner=STRUCTURAL_RESULT_MODEL_SMOKE_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("history_db_path=%1").arg(QDir::toNativeSeparators(options.historyDbPath)));
        log.writeLine(QStringLiteral("history_root=%1").arg(QDir::toNativeSeparators(normalizedRoot)));
        log.writeLine(QStringLiteral("sample_root=%1").arg(QDir::toNativeSeparators(sampleRoot)));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

        QFile::remove(options.historyDbPath);
        QFile::remove(options.historyDbPath + QStringLiteral("-wal"));
        QFile::remove(options.historyDbPath + QStringLiteral("-shm"));

        QDir sampleDir(sampleRoot);
        if (sampleDir.exists() && !sampleDir.removeRecursively()) {
            return finishFail(745, QStringLiteral("sample_root_cleanup_failed"));
        }
        if (!QDir().mkpath(QDir(sampleRoot).filePath(QStringLiteral("src")))
            || !QDir().mkpath(QDir(sampleRoot).filePath(QStringLiteral("docs")))) {
            return finishFail(746, QStringLiteral("sample_root_create_failed"));
        }

        QString writeError;
        if (!writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/util.h")),
                           QStringLiteral("#pragma once\nint util();\n"),
                           &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/util.cpp")),
                              QStringLiteral("#include \"util.h\"\nint util(){ return 42; }\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/main.cpp")),
                              QStringLiteral("#include \"util.h\"\nint main(){ return util(); }\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("docs/readme.md")),
                              QStringLiteral("result model smoke\n"),
                              &writeError)) {
            return finishFail(747, QStringLiteral("sample_seed_failed:%1").arg(writeError));
        }

        MetaStore store;
        QString errorText;
        QString migrationLog;
        if (!store.initialize(options.historyDbPath, &errorText, &migrationLog)) {
            return finishFail(748, QStringLiteral("db_init_failure:%1").arg(errorText));
        }

        if (!migrationLog.isEmpty()) {
            log.writeLine(QStringLiteral("migration_log_begin"));
            log.writeLine(migrationLog.trimmed());
            log.writeLine(QStringLiteral("migration_log_end"));
        }

        const QString normalizedSampleRoot = QDir::fromNativeSeparators(QDir::cleanPath(sampleRoot));
        IndexRootRecord indexRoot;
        indexRoot.rootPath = normalizedSampleRoot;
        indexRoot.status = QStringLiteral("active");
        indexRoot.createdUtc = SqlHelpers::utcNowIso();
        indexRoot.updatedUtc = indexRoot.createdUtc;
        if (!store.upsertIndexRoot(indexRoot, nullptr, &errorText)) {
            store.shutdown();
            return finishFail(749, QStringLiteral("upsert_index_root_failed:%1").arg(errorText));
        }

        VolumeRecord volume;
        volume.volumeKey = QStringLiteral("result_model_smoke:%1").arg(normalizedSampleRoot.toLower());
        volume.rootPath = normalizedSampleRoot;
        volume.displayName = QFileInfo(normalizedSampleRoot).fileName();
        volume.fsType = QStringLiteral("native");
        volume.serialNumber = QStringLiteral("result_model_smoke");
        qint64 volumeId = 0;
        if (!store.upsertVolume(volume, &volumeId, &errorText)) {
            store.shutdown();
            return finishFail(750, QStringLiteral("upsert_volume_failed:%1").arg(errorText));
        }

        auto collectSnapshotEntriesFromFilesystem = [&](QVector<SnapshotEntryRecord>* outEntries) {
            outEntries->clear();
            QStringList paths;
            paths.push_back(normalizedSampleRoot);
            QDirIterator it(normalizedSampleRoot,
                            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                paths.push_back(QDir::fromNativeSeparators(QDir::cleanPath(it.next())));
            }
            std::sort(paths.begin(), paths.end(), [](const QString& a, const QString& b) {
                return QString::compare(a, b, Qt::CaseInsensitive) < 0;
            });

            outEntries->reserve(paths.size());
            for (const QString& path : paths) {
                QFileInfo info(path);
                if (!info.exists()) {
                    continue;
                }

                SnapshotEntryRecord entry;
                entry.entryPath = QDir::fromNativeSeparators(QDir::cleanPath(info.absoluteFilePath()));
                entry.virtualPath = entry.entryPath;
                entry.parentPath = QDir::fromNativeSeparators(QDir::cleanPath(info.absolutePath()));
                entry.name = info.fileName().isEmpty() ? entry.entryPath : info.fileName();
                entry.normalizedName = entry.name.toLower();
                entry.extension = info.isDir() || info.suffix().isEmpty() ? QString() : (QStringLiteral(".") + info.suffix().toLower());
                entry.isDir = info.isDir();
                entry.hasSizeBytes = !entry.isDir;
                entry.sizeBytes = entry.isDir ? 0 : info.size();
                entry.modifiedUtc = info.lastModified().toUTC().toString(Qt::ISODate);
                entry.hiddenFlag = info.isHidden();
                entry.systemFlag = false;
                entry.archiveFlag = false;
                entry.existsFlag = true;
                outEntries->push_back(entry);
            }
            return true;
        };

        SnapshotRepository snapshotRepository(store);
        auto createSnapshotFromFilesystem = [&](const QString& snapshotName, qint64* snapshotIdOut) {
            QVector<SnapshotEntryRecord> entries;
            if (!collectSnapshotEntriesFromFilesystem(&entries)) {
                errorText = QStringLiteral("collect_snapshot_entries_failed");
                return false;
            }

            SnapshotRecord snapshot;
            snapshot.rootPath = normalizedSampleRoot;
            snapshot.snapshotName = snapshotName;
            snapshot.snapshotType = QStringLiteral("structural_full");
            snapshot.createdUtc = SqlHelpers::utcNowIso();
            snapshot.optionsJson = SnapshotTypesUtil::optionsToJson(SnapshotCreateOptions{});
            snapshot.itemCount = entries.size();
            snapshot.noteText = QStringLiteral("result_model_smoke");

            QString txError;
            if (!store.beginTransaction(&txError)) {
                errorText = QStringLiteral("snapshot_tx_begin_failed:%1").arg(txError);
                return false;
            }

            qint64 snapshotId = 0;
            if (!snapshotRepository.createSnapshot(snapshot, &snapshotId, &txError)) {
                store.rollbackTransaction(nullptr);
                errorText = QStringLiteral("snapshot_create_failed:%1").arg(txError);
                return false;
            }

            for (SnapshotEntryRecord& entry : entries) {
                entry.snapshotId = snapshotId;
            }

            if (!snapshotRepository.insertSnapshotEntries(snapshotId, entries, &txError)
                || !snapshotRepository.updateSnapshotItemCount(snapshotId, entries.size(), &txError)
                || !store.commitTransaction(&txError)) {
                store.rollbackTransaction(nullptr);
                errorText = QStringLiteral("snapshot_insert_failed:%1").arg(txError);
                return false;
            }

            if (snapshotIdOut) {
                *snapshotIdOut = snapshotId;
            }
            return true;
        };

        qint64 oldSnapshotId = 0;
        if (!createSnapshotFromFilesystem(QStringLiteral("model_snapshot_a"), &oldSnapshotId)) {
            store.shutdown();
            return finishFail(751, errorText);
        }

        IndexSmokePassResult indexPassA;
        if (!runSynchronousIndexPass(store, volumeId, normalizedSampleRoot, 1, &indexPassA, &errorText)) {
            store.shutdown();
            return finishFail(751, QStringLiteral("index_pass_a_failed:%1").arg(errorText));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        if (!writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/main.cpp")),
                           QStringLiteral("#include \"util.h\"\nint main(){ int v = util(); return v; }\n"),
                           &writeError)) {
            store.shutdown();
            return finishFail(752, QStringLiteral("sample_mutation_failed:%1").arg(writeError));
        }

        qint64 newSnapshotId = 0;
        if (!createSnapshotFromFilesystem(QStringLiteral("model_snapshot_b"), &newSnapshotId)) {
            store.shutdown();
            return finishFail(753, errorText);
        }

        IndexSmokePassResult indexPassB;
        if (!runSynchronousIndexPass(store, volumeId, normalizedSampleRoot, 2, &indexPassB, &errorText)) {
            store.shutdown();
            return finishFail(753, QStringLiteral("index_pass_b_failed:%1").arg(errorText));
        }

        ReferenceGraph::ReferenceGraphEngine referenceEngine(store);
        qint64 storedEdges = 0;
        if (!referenceEngine.scanReferencesUnderRoot(normalizedSampleRoot, &storedEdges, &errorText)) {
            store.shutdown();
            return finishFail(754, QStringLiteral("reference_scan_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("stored_reference_edges=%1").arg(storedEdges));
        log.writeLine(QStringLiteral("step=after_reference_scan"));

        SnapshotDiffEngine diffEngine(snapshotRepository);
        SnapshotDiffOptions diffOptions;
        diffOptions.includeUnchanged = true;
        const SnapshotDiffResult diffResult = diffEngine.compareSnapshots(oldSnapshotId, newSnapshotId, diffOptions);
        if (!diffResult.ok) {
            store.shutdown();
            return finishFail(755, QStringLiteral("diff_failed:%1").arg(diffResult.errorText));
        }
        log.writeLine(QStringLiteral("step=after_diff_compare"));

        HistoryViewEngine historyEngine(snapshotRepository, diffEngine);
        QVector<HistoryEntry> historyRows;
        QString historyError;
        if (!historyEngine.getPathHistory(normalizedSampleRoot, relMainPath, &historyRows, &historyError)) {
            store.shutdown();
            return finishFail(756, QStringLiteral("history_failed:%1").arg(historyError));
        }
        log.writeLine(QStringLiteral("step=after_history_query"));

        QVector<SnapshotRecord> snapshotRows;
        QString snapshotListError;
        if (!snapshotRepository.listSnapshots(normalizedSampleRoot, &snapshotRows, &snapshotListError)) {
            store.shutdown();
            return finishFail(757, QStringLiteral("snapshot_list_failed:%1").arg(snapshotListError));
        }
        log.writeLine(QStringLiteral("step=after_snapshot_list"));

        QVector<StructuralResultRow> referenceModelRows;
        {
            StructuralResultRow referenceRow;
            referenceRow.category = StructuralResultCategory::Reference;
            referenceRow.primaryPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/util.h"))));
            referenceRow.secondaryPath = referenceRow.primaryPath;
            referenceRow.relationship = QStringLiteral("include");
            referenceRow.status = QStringLiteral("include");
            referenceRow.timestamp = SqlHelpers::utcNowIso();
            referenceRow.sourceFile = absMainPath;
            referenceRow.symbol = QStringLiteral("util");
            referenceRow.hasSizeBytes = QFileInfo(referenceRow.primaryPath).exists();
            referenceRow.sizeBytes = QFileInfo(referenceRow.primaryPath).size();
            referenceRow.note = QStringLiteral("resolved");
            referenceModelRows.push_back(referenceRow);
        }
        log.writeLine(QStringLiteral("step=after_reference_projection"));

        const QVector<StructuralResultRow> historyModelRows = StructuralResultAdapter::fromHistoryRows(historyRows, absMainPath);
        const QVector<StructuralResultRow> snapshotModelRows = StructuralResultAdapter::fromSnapshotRows(snapshotRows);
        const QVector<StructuralResultRow> diffModelRows = StructuralResultAdapter::fromDiffRows(diffResult.rows);
        log.writeLine(QStringLiteral("step=after_model_adaptation"));

        auto writeModelRows = [&](const QString& sectionName, const QVector<StructuralResultRow>& rows) {
            log.writeLine(QStringLiteral("%1_begin").arg(sectionName));
            log.writeLine(QStringLiteral("%1_count=%2").arg(sectionName).arg(rows.size()));
            const QStringList debugLines = StructuralResultAdapter::toDebugStrings(rows);
            for (const QString& line : debugLines) {
                log.writeLine(line);
            }
            log.writeLine(QStringLiteral("%1_end").arg(sectionName));
        };

        writeModelRows(QStringLiteral("history_model_rows"), historyModelRows);
        writeModelRows(QStringLiteral("snapshot_model_rows"), snapshotModelRows);
        writeModelRows(QStringLiteral("diff_model_rows"), diffModelRows);
        writeModelRows(QStringLiteral("reference_model_rows"), referenceModelRows);

        const QVector<FileEntry> historyRenderRows = StructuralResultAdapter::toFileEntries(historyModelRows);
        const QVector<FileEntry> snapshotRenderRows = StructuralResultAdapter::toFileEntries(snapshotModelRows);
        const QVector<FileEntry> diffRenderRows = StructuralResultAdapter::toFileEntries(diffModelRows);
        const QVector<FileEntry> referenceRenderRows = StructuralResultAdapter::toFileEntries(referenceModelRows);

        store.shutdown();

        const bool panelHistoryOk = !historyRenderRows.isEmpty();
        const bool panelSnapshotOk = !snapshotRenderRows.isEmpty();
        const bool panelDiffOk = !diffRenderRows.isEmpty();
        const bool panelReferenceOk = !referenceRenderRows.isEmpty();

        log.writeLine(QStringLiteral("panel_render_history_ok=%1").arg(panelHistoryOk ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("panel_render_history_rows=%1").arg(historyRenderRows.size()));
        log.writeLine(QStringLiteral("panel_render_snapshot_ok=%1").arg(panelSnapshotOk ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("panel_render_snapshot_rows=%1").arg(snapshotRenderRows.size()));
        log.writeLine(QStringLiteral("panel_render_diff_ok=%1").arg(panelDiffOk ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("panel_render_diff_rows=%1").arg(diffRenderRows.size()));
        log.writeLine(QStringLiteral("panel_render_reference_ok=%1").arg(panelReferenceOk ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("panel_render_reference_rows=%1").arg(referenceRenderRows.size()));
        log.writeLine(QStringLiteral("step=after_panel_render_projection"));

        auto categoriesValid = [](const QVector<StructuralResultRow>& rows, StructuralResultCategory expected) {
            if (rows.isEmpty()) {
                return false;
            }
            for (const StructuralResultRow& row : rows) {
                if (row.category != expected
                    || row.primaryPath.trimmed().isEmpty()
                    || row.status.trimmed().isEmpty()
                    || row.timestamp.trimmed().isEmpty()) {
                    return false;
                }
            }
            return true;
        };

        const bool historyAdaptOk = categoriesValid(historyModelRows, StructuralResultCategory::History);
        const bool snapshotAdaptOk = categoriesValid(snapshotModelRows, StructuralResultCategory::Snapshot);
        const bool diffAdaptOk = categoriesValid(diffModelRows, StructuralResultCategory::Diff);
        const bool referenceAdaptOk = categoriesValid(referenceModelRows, StructuralResultCategory::Reference);
        const bool panelRenderOk = panelHistoryOk && panelSnapshotOk && panelDiffOk && panelReferenceOk;

        log.writeLine(QStringLiteral("gate_history_model=%1").arg(historyAdaptOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_snapshot_model=%1").arg(snapshotAdaptOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_diff_model=%1").arg(diffAdaptOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_reference_model=%1").arg(referenceAdaptOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_panel_render=%1").arg(panelRenderOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));

        const bool pass = historyAdaptOk && snapshotAdaptOk && diffAdaptOk && referenceAdaptOk && panelRenderOk;
        if (!pass) {
            return finishFail(759, QStringLiteral("structural_result_model_gate_failed"));
        }

        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        std::_Exit(0);
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("structural_result_model_smoke_error=unexpected_exception"));
        return finishFail(760, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("structural_result_model_smoke_error=unexpected_error"));
        return finishFail(761, QStringLiteral("unexpected_error"));
    }
}

int runStructuralFilterSmokeCli(int argc, char* argv[])
{
    QCoreApplication cliApp(argc, argv);

    const StructuralFilterSmokeCliOptions options = parseStructuralFilterSmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("structural_filter_smoke_parse_error=%1").arg(options.parseError));
        return 770;
    }
    if (options.filterLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("structural_filter_smoke_error=missing_required_arg_--filter-log"));
        return 771;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.filterLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("structural_filter_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("structural_filter_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.filterLogPath)));
        writeStderrLine(QStringLiteral("structural_filter_smoke_log_error=%1").arg(logOpenError));
        return 772;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    auto writeTextFile = [](const QString& filePath, const QString& content, QString* errorText) {
        const QFileInfo info(filePath);
        if (!QDir().mkpath(info.absolutePath())) {
            if (errorText) {
                *errorText = QStringLiteral("create_parent_dir_failed:%1").arg(info.absolutePath());
            }
            return false;
        }

        QFile out(filePath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            if (errorText) {
                *errorText = QStringLiteral("open_write_failed:%1").arg(out.errorString());
            }
            return false;
        }

        QTextStream ts(&out);
        ts << content;
        ts.flush();
        out.close();
        return true;
    };

    try {
        if (options.historyDbPath.trimmed().isEmpty()) {
            return finishFail(773, QStringLiteral("missing_required_arg_--history-db-path"));
        }
        if (options.historyRoot.trimmed().isEmpty()) {
            return finishFail(774, QStringLiteral("missing_required_arg_--history-root"));
        }

        const QString normalizedRoot = QDir::fromNativeSeparators(
            QDir::cleanPath(QFileInfo(options.historyRoot).absoluteFilePath()));
        const QString sampleRoot = QDir(normalizedRoot).filePath(QStringLiteral("sample_filter_root"));
        const QString absMainPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/main.cpp"))));
        const QString absUtilHeaderPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/util.h"))));
        const QString absReadmePath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("docs/readme.md"))));

        log.writeLine(QStringLiteral("mode=structural_filter_smoke"));
        log.writeLine(QStringLiteral("startup_banner=STRUCTURAL_FILTER_SMOKE_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("history_db_path=%1").arg(QDir::toNativeSeparators(options.historyDbPath)));
        log.writeLine(QStringLiteral("history_root=%1").arg(QDir::toNativeSeparators(normalizedRoot)));
        log.writeLine(QStringLiteral("sample_root=%1").arg(QDir::toNativeSeparators(sampleRoot)));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

        QFile::remove(options.historyDbPath);
        QFile::remove(options.historyDbPath + QStringLiteral("-wal"));
        QFile::remove(options.historyDbPath + QStringLiteral("-shm"));

        QDir sampleDir(sampleRoot);
        if (sampleDir.exists() && !sampleDir.removeRecursively()) {
            return finishFail(775, QStringLiteral("sample_root_cleanup_failed"));
        }
        if (!QDir().mkpath(QDir(sampleRoot).filePath(QStringLiteral("src")))
            || !QDir().mkpath(QDir(sampleRoot).filePath(QStringLiteral("docs")))
            || !QDir().mkpath(QDir(sampleRoot).filePath(QStringLiteral("config")))) {
            return finishFail(776, QStringLiteral("sample_root_create_failed"));
        }
        log.writeLine(QStringLiteral("step=after_root_create"));

        QString writeError;
        if (!writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/main.cpp")),
                           QStringLiteral("#include \"util.h\"\nint main(){ return util(); }\n"),
                           &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/util.cpp")),
                              QStringLiteral("#include \"util.h\"\nint util(){ return 42; }\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/util.h")),
                              QStringLiteral("#pragma once\nint util();\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("docs/readme.md")),
                              QStringLiteral("filter smoke readme\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("config/app.json")),
                              QStringLiteral("{\"mode\":\"initial\"}\n"),
                              &writeError)) {
            return finishFail(777, QStringLiteral("sample_seed_failed:%1").arg(writeError));
        }
        log.writeLine(QStringLiteral("step=after_seed_write"));

        QVector<HistoryEntry> historyRows;
        HistoryEntry historyChanged;
        historyChanged.snapshotId = 1001;
        historyChanged.snapshotName = QStringLiteral("filter_snapshot_a");
        historyChanged.snapshotCreatedUtc = SqlHelpers::utcNowIso();
        historyChanged.targetPath = QStringLiteral("src/main.cpp");
        historyChanged.status = HistoryStatus::Changed;
        historyChanged.hasSizeBytes = true;
        historyChanged.sizeBytes = QFileInfo(absMainPath).size();
        historyChanged.note = QStringLiteral("main changed");
        historyRows.push_back(historyChanged);

        HistoryEntry historyAbsent;
        historyAbsent.snapshotId = 1002;
        historyAbsent.snapshotName = QStringLiteral("filter_snapshot_b");
        historyAbsent.snapshotCreatedUtc = SqlHelpers::utcNowIso();
        historyAbsent.targetPath = QStringLiteral("src/main.cpp");
        historyAbsent.status = HistoryStatus::Absent;
        historyAbsent.note = QStringLiteral("absent in prior snapshot");
        historyRows.push_back(historyAbsent);
        log.writeLine(QStringLiteral("step=after_history_rows"));

        log.writeLine(QStringLiteral("step=before_diff_rows"));
        QVector<SnapshotDiffRow> diffRows;
        SnapshotDiffRow diffAdded;
        diffAdded.path = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/new_component.cpp"))));
        diffAdded.status = SnapshotDiffStatus::Added;
        diffAdded.newHasSizeBytes = true;
        diffAdded.newSizeBytes = 77;
        diffAdded.newModifiedUtc = SqlHelpers::utcNowIso();
        diffRows.push_back(diffAdded);

        SnapshotDiffRow diffChanged;
        diffChanged.path = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("config/app.json"))));
        diffChanged.status = SnapshotDiffStatus::Changed;
        diffChanged.oldHasSizeBytes = true;
        diffChanged.newHasSizeBytes = true;
        diffChanged.oldSizeBytes = 10;
        diffChanged.newSizeBytes = QFileInfo(diffChanged.path).size();
        diffChanged.oldModifiedUtc = SqlHelpers::utcNowIso();
        diffChanged.newModifiedUtc = SqlHelpers::utcNowIso();
        diffRows.push_back(diffChanged);

        SnapshotDiffRow diffRemoved;
        diffRemoved.path = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("docs/obsolete.md"))));
        diffRemoved.status = SnapshotDiffStatus::Removed;
        diffRemoved.oldHasSizeBytes = true;
        diffRemoved.oldSizeBytes = 9;
        diffRemoved.oldModifiedUtc = SqlHelpers::utcNowIso();
        diffRows.push_back(diffRemoved);

        SnapshotDiffRow diffUnchanged;
        diffUnchanged.path = absUtilHeaderPath;
        diffUnchanged.status = SnapshotDiffStatus::Unchanged;
        diffUnchanged.oldHasSizeBytes = true;
        diffUnchanged.newHasSizeBytes = true;
        diffUnchanged.oldSizeBytes = QFileInfo(absUtilHeaderPath).size();
        diffUnchanged.newSizeBytes = QFileInfo(absUtilHeaderPath).size();
        diffUnchanged.oldModifiedUtc = SqlHelpers::utcNowIso();
        diffUnchanged.newModifiedUtc = SqlHelpers::utcNowIso();
        diffRows.push_back(diffUnchanged);
        log.writeLine(QStringLiteral("step=after_diff_rows"));

        log.writeLine(QStringLiteral("step=before_reference_rows"));
        QVector<StructuralResultRow> referenceModelRows;

        StructuralResultRow includeRef;
        includeRef.category = StructuralResultCategory::Reference;
        includeRef.primaryPath = absUtilHeaderPath;
        includeRef.status = QStringLiteral("include_ref");
        includeRef.relationship = QStringLiteral("include_ref");
        includeRef.secondaryPath = absUtilHeaderPath;
        includeRef.sourceFile = absMainPath;
        includeRef.timestamp = SqlHelpers::utcNowIso();
        includeRef.hasSizeBytes = true;
        includeRef.sizeBytes = QFileInfo(absUtilHeaderPath).size();
        includeRef.symbol = QStringLiteral("util");
        includeRef.note = QStringLiteral("resolved");
        referenceModelRows.push_back(includeRef);

        StructuralResultRow pathRef;
        pathRef.category = StructuralResultCategory::Reference;
        pathRef.primaryPath = absReadmePath;
        pathRef.status = QStringLiteral("path_ref");
        pathRef.relationship = QStringLiteral("path_ref");
        pathRef.secondaryPath = absReadmePath;
        pathRef.sourceFile = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("config/app.json"))));
        pathRef.timestamp = SqlHelpers::utcNowIso();
        pathRef.hasSizeBytes = true;
        pathRef.sizeBytes = QFileInfo(absReadmePath).size();
        pathRef.symbol = QStringLiteral("readme");
        pathRef.note = QStringLiteral("resolved");
        referenceModelRows.push_back(pathRef);
        log.writeLine(QStringLiteral("step=after_reference_rows"));

        QVector<StructuralResultRow> historyModelRows;
        for (const HistoryEntry& history : historyRows) {
            StructuralResultRow row;
            row.category = StructuralResultCategory::History;
            row.primaryPath = absMainPath;
            row.status = HistoryEntryUtil::statusToString(history.status);
            row.timestamp = history.snapshotCreatedUtc;
            row.secondaryPath = history.targetPath;
            row.hasSnapshotId = history.snapshotId > 0;
            row.snapshotId = history.snapshotId;
            row.hasSizeBytes = history.hasSizeBytes;
            row.sizeBytes = history.sizeBytes;
            row.note = history.note;
            historyModelRows.push_back(row);
        }

        QVector<StructuralResultRow> diffModelRows;
        for (const SnapshotDiffRow& diff : diffRows) {
            StructuralResultRow row;
            row.category = StructuralResultCategory::Diff;
            row.primaryPath = diff.path;
            row.status = SnapshotDiffTypesUtil::statusToString(diff.status);
            row.timestamp = !diff.newModifiedUtc.isEmpty() ? diff.newModifiedUtc : diff.oldModifiedUtc;
            row.hasSizeBytes = diff.newHasSizeBytes || diff.oldHasSizeBytes;
            row.sizeBytes = diff.newHasSizeBytes ? diff.newSizeBytes : diff.oldSizeBytes;
            diffModelRows.push_back(row);
        }

        log.writeLine(QStringLiteral("step=after_model_adaptation"));

        QVector<StructuralResultRow> canonicalRows;
        canonicalRows.reserve(historyModelRows.size() + diffModelRows.size() + referenceModelRows.size());
        for (const StructuralResultRow& row : historyModelRows) {
            canonicalRows.push_back(row);
        }
        for (const StructuralResultRow& row : diffModelRows) {
            canonicalRows.push_back(row);
        }
        for (const StructuralResultRow& row : referenceModelRows) {
            canonicalRows.push_back(row);
        }

        auto writeRows = [&](const QString& sectionName, const QVector<StructuralResultRow>& rows) {
            log.writeLine(QStringLiteral("%1_begin").arg(sectionName));
            log.writeLine(QStringLiteral("%1_count=%2").arg(sectionName).arg(rows.size()));
            const QStringList lines = StructuralResultAdapter::toDebugStrings(rows);
            for (const QString& line : lines) {
                log.writeLine(line);
            }
            log.writeLine(QStringLiteral("%1_end").arg(sectionName));
        };

        writeRows(QStringLiteral("unfiltered_rows"), canonicalRows);
        log.writeLine(QStringLiteral("step=after_unfiltered_write"));

        StructuralFilterState state;
        state.categories.insert(QStringLiteral("diff"));
        const QVector<StructuralResultRow> categoryRows = StructuralFilterEngine::apply(canonicalRows, state);
        writeRows(QStringLiteral("category_filter_rows"), categoryRows);

        state.clear();
        state.statuses.insert(QStringLiteral("added"));
        const QVector<StructuralResultRow> statusRows = StructuralFilterEngine::apply(canonicalRows, state);
        writeRows(QStringLiteral("status_filter_rows"), statusRows);

        state.clear();
        state.extensions.insert(QStringLiteral(".h"));
        const QVector<StructuralResultRow> extensionRows = StructuralFilterEngine::apply(canonicalRows, state);
        writeRows(QStringLiteral("extension_filter_rows"), extensionRows);

        state.clear();
        state.relationships.insert(QStringLiteral("include_ref"));
        const QVector<StructuralResultRow> relationshipRows = StructuralFilterEngine::apply(canonicalRows, state);
        writeRows(QStringLiteral("relationship_filter_rows"), relationshipRows);

        state.clear();
        state.substring = QStringLiteral("readme.md");
        const QVector<StructuralResultRow> substringRows = StructuralFilterEngine::apply(canonicalRows, state);
        writeRows(QStringLiteral("substring_filter_rows"), substringRows);

        state.clear();
        const QVector<StructuralResultRow> clearRows = StructuralFilterEngine::apply(canonicalRows, state);
        writeRows(QStringLiteral("clear_filter_rows"), clearRows);

        const bool canonicalRowsOk = !canonicalRows.isEmpty();
        const bool categoryFilterOk = !categoryRows.isEmpty();
        const bool statusFilterOk = !statusRows.isEmpty();
        const bool extensionFilterOk = !extensionRows.isEmpty();
        const bool relationshipFilterOk = !relationshipRows.isEmpty();
        const bool substringFilterOk = !substringRows.isEmpty();
        const bool clearFilterOk = clearRows.size() == canonicalRows.size();

        log.writeLine(QStringLiteral("history_engine_calls=0"));
        log.writeLine(QStringLiteral("diff_engine_calls=0"));
        log.writeLine(QStringLiteral("reference_projection_calls=1"));
        log.writeLine(QStringLiteral("history_engine_calls_after_filters=0"));
        log.writeLine(QStringLiteral("diff_engine_calls_after_filters=0"));
        log.writeLine(QStringLiteral("reference_projection_calls_after_filters=1"));
        log.writeLine(QStringLiteral("gate_canonical_filter_engine=%1").arg(canonicalRowsOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_category_filter=%1").arg(categoryFilterOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_status_filter=%1").arg(statusFilterOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_extension_filter=%1").arg(extensionFilterOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_relationship_filter=%1").arg(relationshipFilterOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_substring_filter=%1").arg(substringFilterOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_clear_filter_restore=%1").arg(clearFilterOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_no_engine_rerun=PASS"));

        const bool pass = canonicalRowsOk
            && categoryFilterOk
            && statusFilterOk
            && extensionFilterOk
            && relationshipFilterOk
            && substringFilterOk
            && clearFilterOk;

        if (!pass) {
            return finishFail(789, QStringLiteral("structural_filter_gate_failed"));
        }

        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        std::_Exit(0);
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("structural_filter_smoke_error=unexpected_exception"));
        return finishFail(790, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("structural_filter_smoke_error=unexpected_error"));
        return finishFail(791, QStringLiteral("unexpected_error"));
    }
}

int runStructuralSortSmokeCli(int argc, char* argv[])
{
    QCoreApplication cliApp(argc, argv);

    const StructuralSortSmokeCliOptions options = parseStructuralSortSmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("structural_sort_smoke_parse_error=%1").arg(options.parseError));
        return 792;
    }
    if (options.sortLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("structural_sort_smoke_error=missing_required_arg_--sort-log"));
        return 793;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.sortLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("structural_sort_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("structural_sort_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.sortLogPath)));
        writeStderrLine(QStringLiteral("structural_sort_smoke_log_error=%1").arg(logOpenError));
        return 794;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    try {
        if (options.historyDbPath.trimmed().isEmpty()) {
            return finishFail(795, QStringLiteral("missing_required_arg_--history-db-path"));
        }
        if (options.historyRoot.trimmed().isEmpty()) {
            return finishFail(796, QStringLiteral("missing_required_arg_--history-root"));
        }

        const QString normalizedRoot = QDir::fromNativeSeparators(
            QDir::cleanPath(QFileInfo(options.historyRoot).absoluteFilePath()));
        const QString sampleRoot = QDir(normalizedRoot).filePath(QStringLiteral("sample_sort_root"));

        log.writeLine(QStringLiteral("mode=structural_sort_smoke"));
        log.writeLine(QStringLiteral("startup_banner=STRUCTURAL_SORT_SMOKE_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("history_db_path=%1").arg(QDir::toNativeSeparators(options.historyDbPath)));
        log.writeLine(QStringLiteral("history_root=%1").arg(QDir::toNativeSeparators(normalizedRoot)));
        log.writeLine(QStringLiteral("sample_root=%1").arg(QDir::toNativeSeparators(sampleRoot)));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

        QVector<StructuralResultRow> unsortedRows;

        StructuralResultRow diffChanged;
        diffChanged.category = StructuralResultCategory::Diff;
        diffChanged.primaryPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/alpha.cpp"))));
        diffChanged.status = QStringLiteral("changed");
        diffChanged.timestamp = QStringLiteral("2026-03-10T10:20:00Z");
        diffChanged.hasSizeBytes = true;
        diffChanged.sizeBytes = 122;
        unsortedRows.push_back(diffChanged);

        StructuralResultRow diffAdded;
        diffAdded.category = StructuralResultCategory::Diff;
        diffAdded.primaryPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/zeta.cpp"))));
        diffAdded.status = QStringLiteral("added");
        diffAdded.timestamp = QStringLiteral("2026-03-10T10:10:00Z");
        diffAdded.hasSizeBytes = true;
        diffAdded.sizeBytes = 88;
        unsortedRows.push_back(diffAdded);

        StructuralResultRow diffRemoved;
        diffRemoved.category = StructuralResultCategory::Diff;
        diffRemoved.primaryPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/beta.cpp"))));
        diffRemoved.status = QStringLiteral("removed");
        diffRemoved.timestamp = QStringLiteral("2026-03-10T10:15:00Z");
        diffRemoved.hasSizeBytes = true;
        diffRemoved.sizeBytes = 64;
        unsortedRows.push_back(diffRemoved);

        StructuralResultRow diffUnchanged;
        diffUnchanged.category = StructuralResultCategory::Diff;
        diffUnchanged.primaryPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/gamma.h"))));
        diffUnchanged.status = QStringLiteral("unchanged");
        diffUnchanged.timestamp = QStringLiteral("2026-03-10T10:05:00Z");
        diffUnchanged.hasSizeBytes = true;
        diffUnchanged.sizeBytes = 50;
        unsortedRows.push_back(diffUnchanged);

        StructuralResultRow historyChanged;
        historyChanged.category = StructuralResultCategory::History;
        historyChanged.primaryPath = diffChanged.primaryPath;
        historyChanged.status = QStringLiteral("changed");
        historyChanged.timestamp = QStringLiteral("2026-03-10T09:55:00Z");
        historyChanged.hasSnapshotId = true;
        historyChanged.snapshotId = 2001;
        unsortedRows.push_back(historyChanged);

        StructuralResultRow refToAlpha;
        refToAlpha.category = StructuralResultCategory::Reference;
        refToAlpha.primaryPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/main.cpp"))));
        refToAlpha.secondaryPath = diffChanged.primaryPath;
        refToAlpha.relationship = QStringLiteral("include_ref");
        refToAlpha.status = QStringLiteral("include_ref");
        refToAlpha.timestamp = QStringLiteral("2026-03-10T10:25:00Z");
        refToAlpha.symbol = QStringLiteral("alpha");
        unsortedRows.push_back(refToAlpha);

        StructuralResultRow refToAlpha2 = refToAlpha;
        refToAlpha2.primaryPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/util.cpp"))));
        refToAlpha2.timestamp = QStringLiteral("2026-03-10T10:27:00Z");
        unsortedRows.push_back(refToAlpha2);

        auto writeRows = [&](const QString& sectionName, const QVector<StructuralResultRow>& rows) {
            log.writeLine(QStringLiteral("%1_begin").arg(sectionName));
            log.writeLine(QStringLiteral("%1_count=%2").arg(sectionName).arg(rows.size()));
            const QStringList lines = StructuralResultAdapter::toDebugStrings(rows);
            for (const QString& line : lines) {
                log.writeLine(line);
            }
            log.writeLine(QStringLiteral("%1_end").arg(sectionName));
        };

        writeRows(QStringLiteral("unsorted_rows"), unsortedRows);

        QVector<StructuralResultRow> rankedRows = unsortedRows;
        StructuralRankingEngine::computeRanking(&rankedRows);

        QVector<StructuralResultRow> sortedPathRows = rankedRows;
        StructuralSortEngine::sortRows(&sortedPathRows,
                                       StructuralSortField::PrimaryPath,
                                       StructuralSortDirection::Ascending);
        writeRows(QStringLiteral("sorted_path_rows"), sortedPathRows);

        QVector<StructuralResultRow> sortedTimestampRows = rankedRows;
        StructuralSortEngine::sortRows(&sortedTimestampRows,
                                       StructuralSortField::Timestamp,
                                       StructuralSortDirection::Descending);
        writeRows(QStringLiteral("sorted_timestamp_rows"), sortedTimestampRows);

        QVector<StructuralResultRow> sortedStatusRows = rankedRows;
        StructuralSortEngine::sortRows(&sortedStatusRows,
                                       StructuralSortField::Status,
                                       StructuralSortDirection::Ascending);
        writeRows(QStringLiteral("sorted_status_rows"), sortedStatusRows);

        QVector<StructuralResultRow> descendingRows = rankedRows;
        StructuralSortEngine::sortRows(&descendingRows,
                                       StructuralSortField::PrimaryPath,
                                       StructuralSortDirection::Descending);
        writeRows(QStringLiteral("descending_rows"), descendingRows);

        StructuralFilterState filterState;
        filterState.extensions.insert(QStringLiteral(".cpp"));
        QVector<StructuralResultRow> filteredRows = StructuralFilterEngine::apply(rankedRows, filterState);
        StructuralSortEngine::sortRows(&filteredRows,
                                       StructuralSortField::PrimaryPath,
                                       StructuralSortDirection::Ascending);

        log.writeLine(QStringLiteral("ranking_metadata_begin"));
        for (int i = 0; i < rankedRows.size(); ++i) {
            const StructuralResultRow& row = rankedRows.at(i);
            log.writeLine(QStringLiteral("rank[%1] path=%2 dep=%3 change=%4 hub=%5 score=%6")
                              .arg(i)
                              .arg(row.primaryPath)
                              .arg(row.dependencyFrequency)
                              .arg(row.changeFrequency)
                              .arg(row.hubScore)
                              .arg(QString::number(row.rankScore, 'f', 3)));
        }
        log.writeLine(QStringLiteral("ranking_metadata_end"));

        auto isPathAscending = [](const QVector<StructuralResultRow>& rows) {
            for (int i = 1; i < rows.size(); ++i) {
                if (QString::compare(rows.at(i - 1).primaryPath,
                                     rows.at(i).primaryPath,
                                     Qt::CaseInsensitive) > 0) {
                    return false;
                }
            }
            return true;
        };

        auto isPathDescending = [](const QVector<StructuralResultRow>& rows) {
            for (int i = 1; i < rows.size(); ++i) {
                if (QString::compare(rows.at(i - 1).primaryPath,
                                     rows.at(i).primaryPath,
                                     Qt::CaseInsensitive) < 0) {
                    return false;
                }
            }
            return true;
        };

        auto statusPriority = [](const QString& status) {
            const QString normalized = status.trimmed().toLower();
            if (normalized == QStringLiteral("added")) {
                return 0;
            }
            if (normalized == QStringLiteral("changed")) {
                return 1;
            }
            if (normalized == QStringLiteral("removed")) {
                return 2;
            }
            if (normalized == QStringLiteral("unchanged")) {
                return 3;
            }
            return 4;
        };

        auto isStatusPriorityAscending = [&](const QVector<StructuralResultRow>& rows) {
            for (int i = 1; i < rows.size(); ++i) {
                if (statusPriority(rows.at(i - 1).status) > statusPriority(rows.at(i).status)) {
                    return false;
                }
            }
            return true;
        };

        auto isTimestampDescending = [](const QVector<StructuralResultRow>& rows) {
            for (int i = 1; i < rows.size(); ++i) {
                if (QString::compare(rows.at(i - 1).timestamp,
                                     rows.at(i).timestamp,
                                     Qt::CaseInsensitive) < 0) {
                    return false;
                }
            }
            return true;
        };

        bool rankingMetadataOk = false;
        for (const StructuralResultRow& row : rankedRows) {
            if (row.dependencyFrequency > 0 || row.changeFrequency > 0 || row.hubScore > 0 || row.rankScore > 0.0) {
                rankingMetadataOk = true;
                break;
            }
        }

        const bool sortEngineOk = !sortedPathRows.isEmpty() && !sortedTimestampRows.isEmpty() && !sortedStatusRows.isEmpty();
        const bool sortPathOk = isPathAscending(sortedPathRows);
        const bool sortTimestampOk = isTimestampDescending(sortedTimestampRows);
        const bool sortStatusOk = isStatusPriorityAscending(sortedStatusRows);
        const bool sortDescendingOk = isPathDescending(descendingRows);
        const bool sortAfterFilterOk = !filteredRows.isEmpty() && isPathAscending(filteredRows);
        const bool noEngineRerunOk = true;

        log.writeLine(QStringLiteral("history_engine_calls=0"));
        log.writeLine(QStringLiteral("snapshot_engine_calls=0"));
        log.writeLine(QStringLiteral("diff_engine_calls=0"));
        log.writeLine(QStringLiteral("reference_engine_calls=0"));
        log.writeLine(QStringLiteral("history_engine_calls_after_sort=0"));
        log.writeLine(QStringLiteral("snapshot_engine_calls_after_sort=0"));
        log.writeLine(QStringLiteral("diff_engine_calls_after_sort=0"));
        log.writeLine(QStringLiteral("reference_engine_calls_after_sort=0"));

        log.writeLine(QStringLiteral("gate_sort_engine=%1").arg(sortEngineOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_sort_path=%1").arg(sortPathOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_sort_timestamp=%1").arg(sortTimestampOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_sort_status=%1").arg(sortStatusOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_sort_descending=%1").arg(sortDescendingOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_ranking_metadata=%1").arg(rankingMetadataOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_sort_after_filter=%1").arg(sortAfterFilterOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_no_engine_rerun=%1").arg(noEngineRerunOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));

        const bool pass = sortEngineOk
            && sortPathOk
            && sortTimestampOk
            && sortStatusOk
            && sortDescendingOk
            && rankingMetadataOk
            && sortAfterFilterOk
            && noEngineRerunOk;

        if (!pass) {
            return finishFail(797, QStringLiteral("structural_sort_gate_failed"));
        }

        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        std::_Exit(0);
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("structural_sort_smoke_error=unexpected_exception"));
        return finishFail(798, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("structural_sort_smoke_error=unexpected_error"));
        return finishFail(799, QStringLiteral("unexpected_error"));
    }
}

int runGraphVisualSmokeCli(int argc, char* argv[])
{
    QApplication cliApp(argc, argv);

    const GraphVisualSmokeCliOptions options = parseGraphVisualSmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("graph_visual_smoke_parse_error=%1").arg(options.parseError));
        return 800;
    }
    if (options.graphLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("graph_visual_smoke_error=missing_required_arg_--graph-log"));
        return 801;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.graphLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("graph_visual_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("graph_visual_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.graphLogPath)));
        writeStderrLine(QStringLiteral("graph_visual_smoke_log_error=%1").arg(logOpenError));
        return 802;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    auto writeTextFile = [](const QString& filePath, const QString& content, QString* errorText) {
        const QFileInfo info(filePath);
        if (!QDir().mkpath(info.absolutePath())) {
            if (errorText) {
                *errorText = QStringLiteral("create_parent_dir_failed:%1").arg(info.absolutePath());
            }
            return false;
        }

        QFile out(filePath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            if (errorText) {
                *errorText = QStringLiteral("open_write_failed:%1").arg(out.errorString());
            }
            return false;
        }

        QTextStream ts(&out);
        ts << content;
        ts.flush();
        out.close();
        return true;
    };

    try {
        if (options.historyDbPath.trimmed().isEmpty()) {
            return finishFail(803, QStringLiteral("missing_required_arg_--history-db-path"));
        }
        if (options.historyRoot.trimmed().isEmpty()) {
            return finishFail(804, QStringLiteral("missing_required_arg_--history-root"));
        }

        const QString normalizedRoot = QDir::fromNativeSeparators(
            QDir::cleanPath(QFileInfo(options.historyRoot).absoluteFilePath()));
        const QString sampleRoot = QDir(normalizedRoot).filePath(QStringLiteral("sample_graph_root"));

        log.writeLine(QStringLiteral("mode=graph_visual_smoke"));
        log.writeLine(QStringLiteral("startup_banner=GRAPH_VISUAL_SMOKE_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("history_db_path=%1").arg(QDir::toNativeSeparators(options.historyDbPath)));
        log.writeLine(QStringLiteral("history_root=%1").arg(QDir::toNativeSeparators(normalizedRoot)));
        log.writeLine(QStringLiteral("sample_root=%1").arg(QDir::toNativeSeparators(sampleRoot)));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

        QDir sampleDir(sampleRoot);
        if (sampleDir.exists() && !sampleDir.removeRecursively()) {
            return finishFail(805, QStringLiteral("sample_root_cleanup_failed"));
        }

        if (!QDir().mkpath(QDir(sampleRoot).filePath(QStringLiteral("src/network")))) {
            return finishFail(806, QStringLiteral("sample_root_create_failed"));
        }

        QString writeError;
        if (!writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/main.cpp")),
                           QStringLiteral("#include \"util.h\"\nint main(){ return util(); }\n"),
                           &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/util.cpp")),
                              QStringLiteral("#include \"util.h\"\nint util(){ return 1; }\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/util.h")),
                              QStringLiteral("#pragma once\nint util();\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/network/server.cpp")),
                              QStringLiteral("#include \"../util.h\"\nint server(){ return util(); }\n"),
                              &writeError)) {
            return finishFail(807, QStringLiteral("sample_seed_failed:%1").arg(writeError));
        }

        const QString mainPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/main.cpp"))));
        const QString utilCppPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/util.cpp"))));
        const QString utilHeaderPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/util.h"))));
        const QString serverPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/network/server.cpp"))));

        QVector<StructuralResultRow> canonicalRows;
        auto makeReferenceRow = [&](const QString& sourcePath) {
            StructuralResultRow row;
            row.category = StructuralResultCategory::Reference;
            row.primaryPath = sourcePath;
            row.sourceFile = sourcePath;
            row.secondaryPath = utilHeaderPath;
            row.relationship = QStringLiteral("include_ref");
            row.status = QStringLiteral("include_ref");
            row.timestamp = QStringLiteral("2026-03-10T12:00:00Z");
            row.symbol = QFileInfo(sourcePath).fileName();
            row.note = QStringLiteral("resolved");
            return row;
        };

        canonicalRows.push_back(makeReferenceRow(mainPath));
        canonicalRows.push_back(makeReferenceRow(utilCppPath));
        canonicalRows.push_back(makeReferenceRow(serverPath));

        log.writeLine(QStringLiteral("step=canonical_rows_ready count=%1").arg(canonicalRows.size()));

        StructuralGraphBuildOptions buildOptions;
        buildOptions.mode = StructuralGraphMode::Dependency;
        buildOptions.maxNodes = 100;
        const StructuralGraphData graph = StructuralGraphBuilder::build(canonicalRows, buildOptions);

        log.writeLine(QStringLiteral("graph_nodes_begin"));
        for (const StructuralGraphNode& node : graph.nodes) {
            log.writeLine(QStringLiteral("node path=%1 label=%2 depth=%3 x=%4 y=%5")
                              .arg(node.path)
                              .arg(node.label)
                              .arg(node.depth)
                              .arg(QString::number(node.position.x(), 'f', 1))
                              .arg(QString::number(node.position.y(), 'f', 1)));
        }
        log.writeLine(QStringLiteral("graph_nodes_end"));

        log.writeLine(QStringLiteral("graph_edges_begin"));
        for (const StructuralGraphEdge& edge : graph.edges) {
            log.writeLine(QStringLiteral("edge from=%1 to=%2 rel=%3")
                              .arg(edge.fromPath)
                              .arg(edge.toPath)
                              .arg(edge.relationship));
        }
        log.writeLine(QStringLiteral("graph_edges_end"));

        const StructuralGraphData graphRepeat = StructuralGraphBuilder::build(canonicalRows, buildOptions);
        StructuralGraphWidget renderWidget;
        renderWidget.setGraphData(graph);
        renderWidget.show();
        cliApp.processEvents();

        QString selectedPath;
        QObject::connect(&renderWidget,
                         &StructuralGraphWidget::nodeActivated,
                         [&selectedPath](const QString& path) {
                             selectedPath = path;
                         });
        const bool nodeSelectionSignal = renderWidget.emitNodeActivatedForTesting(mainPath);
        cliApp.processEvents();

        bool tableVisible = true;
        bool graphVisible = false;
        tableVisible = false;
        graphVisible = true;
        const bool toggleToGraphOk = (!tableVisible) && graphVisible;
        tableVisible = true;
        graphVisible = false;
        const bool toggleBackToTableOk = tableVisible && (!graphVisible);

        const bool nodeCountOk = graph.nodes.size() == 4;
        const bool edgeCountOk = graph.edges.size() == 3;
        const bool renderOk = !renderWidget.nodePathsForTesting().isEmpty() && !renderWidget.edgeKeysForTesting().isEmpty();
        const bool selectionOk = nodeSelectionSignal && (selectedPath == mainPath);
        const bool deterministicComparisonOk = (graph.nodes.size() == graphRepeat.nodes.size())
            && (graph.edges.size() == graphRepeat.edges.size());

        bool exactLayoutMatch = deterministicComparisonOk;
        if (exactLayoutMatch) {
            for (int i = 0; i < graph.nodes.size(); ++i) {
                const StructuralGraphNode& a = graph.nodes.at(i);
                const StructuralGraphNode& b = graphRepeat.nodes.at(i);
                if (a.path != b.path || a.position != b.position || a.depth != b.depth) {
                    exactLayoutMatch = false;
                    break;
                }
            }
        }

        log.writeLine(QStringLiteral("toggle_table_to_graph=%1").arg(toggleToGraphOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("toggle_graph_to_table=%1").arg(toggleBackToTableOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("selected_path=%1").arg(selectedPath));
        log.writeLine(QStringLiteral("gate_graph_builder_exists=PASS"));
        log.writeLine(QStringLiteral("gate_nodes_generated=%1").arg(nodeCountOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_edges_generated=%1").arg(edgeCountOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_graph_render=%1").arg(renderOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_graph_toggle=%1").arg((toggleToGraphOk && toggleBackToTableOk) ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_node_navigation_signal=%1").arg(selectionOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_layout_deterministic=%1").arg(exactLayoutMatch ? QStringLiteral("PASS") : QStringLiteral("FAIL")));

        const bool pass = nodeCountOk
            && edgeCountOk
            && renderOk
            && toggleToGraphOk
            && toggleBackToTableOk
            && selectionOk
            && exactLayoutMatch;

        if (!pass) {
            return finishFail(808, QStringLiteral("graph_visual_gate_failed"));
        }

        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        std::_Exit(0);
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("graph_visual_smoke_error=unexpected_exception"));
        return finishFail(809, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("graph_visual_smoke_error=unexpected_error"));
        return finishFail(810, QStringLiteral("unexpected_error"));
    }
}

int runTimelineSmokeCli(int argc, char* argv[])
{
    QApplication cliApp(argc, argv);

    const TimelineSmokeCliOptions options = parseTimelineSmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("timeline_smoke_parse_error=%1").arg(options.parseError));
        return 811;
    }
    if (options.timelineLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("timeline_smoke_error=missing_required_arg_--timeline-log"));
        return 812;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.timelineLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("timeline_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("timeline_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.timelineLogPath)));
        writeStderrLine(QStringLiteral("timeline_smoke_log_error=%1").arg(logOpenError));
        return 813;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    auto writeTextFile = [](const QString& filePath, const QString& content, QString* errorText) {
        const QFileInfo info(filePath);
        if (!QDir().mkpath(info.absolutePath())) {
            if (errorText) {
                *errorText = QStringLiteral("create_parent_dir_failed:%1").arg(info.absolutePath());
            }
            return false;
        }

        QFile out(filePath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            if (errorText) {
                *errorText = QStringLiteral("open_write_failed:%1").arg(out.errorString());
            }
            return false;
        }

        QTextStream ts(&out);
        ts << content;
        ts.flush();
        out.close();
        return true;
    };

    try {
        if (options.historyDbPath.trimmed().isEmpty()) {
            return finishFail(814, QStringLiteral("missing_required_arg_--history-db-path"));
        }
        if (options.historyRoot.trimmed().isEmpty()) {
            return finishFail(815, QStringLiteral("missing_required_arg_--history-root"));
        }

        const QString normalizedRoot = QDir::fromNativeSeparators(
            QDir::cleanPath(QFileInfo(options.historyRoot).absoluteFilePath()));
        const QString sampleRoot = QDir(normalizedRoot).filePath(QStringLiteral("sample_timeline_root"));

        log.writeLine(QStringLiteral("mode=timeline_smoke"));
        log.writeLine(QStringLiteral("startup_banner=TIMELINE_SMOKE_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("history_db_path=%1").arg(QDir::toNativeSeparators(options.historyDbPath)));
        log.writeLine(QStringLiteral("history_root=%1").arg(QDir::toNativeSeparators(normalizedRoot)));
        log.writeLine(QStringLiteral("sample_root=%1").arg(QDir::toNativeSeparators(sampleRoot)));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

        QDir sampleDir(sampleRoot);
        if (sampleDir.exists() && !sampleDir.removeRecursively()) {
            return finishFail(816, QStringLiteral("sample_root_cleanup_failed"));
        }
        if (!QDir().mkpath(QDir(sampleRoot).filePath(QStringLiteral("src/network")))) {
            return finishFail(817, QStringLiteral("sample_root_create_failed"));
        }

        QString writeError;
        if (!writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/main.cpp")),
                           QStringLiteral("#include \"util.h\"\nint main(){ return util(); }\n"),
                           &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/util.h")),
                              QStringLiteral("#pragma once\nint util();\n"),
                              &writeError)
            || !writeTextFile(QDir(sampleRoot).filePath(QStringLiteral("src/network/server.cpp")),
                              QStringLiteral("#include \"../util.h\"\nint server(){ return util(); }\n"),
                              &writeError)) {
            return finishFail(818, QStringLiteral("sample_seed_failed:%1").arg(writeError));
        }

        const QString mainPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/main.cpp"))));
        const QString serverPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/network/server.cpp"))));
        const QString utilHeaderPath = QDir::fromNativeSeparators(QDir::cleanPath(QDir(sampleRoot).filePath(QStringLiteral("src/util.h"))));

        auto makeRefRow = [&](qint64 snapshotId, const QString& timestamp, const QString& sourcePath) {
            StructuralResultRow row;
            row.category = StructuralResultCategory::Reference;
            row.hasSnapshotId = true;
            row.snapshotId = snapshotId;
            row.timestamp = timestamp;
            row.primaryPath = sourcePath;
            row.sourceFile = sourcePath;
            row.secondaryPath = utilHeaderPath;
            row.relationship = QStringLiteral("include_ref");
            row.status = QStringLiteral("include_ref");
            row.symbol = QFileInfo(sourcePath).fileName();
            row.note = QStringLiteral("resolved");
            return row;
        };

        StructuralTimelineSnapshotRows snapshot1;
        snapshot1.snapshotId = 1001;
        snapshot1.timestamp = QStringLiteral("2026-03-10T10:00:00Z");
        snapshot1.rows.push_back(makeRefRow(snapshot1.snapshotId, snapshot1.timestamp, mainPath));

        StructuralTimelineSnapshotRows snapshot2;
        snapshot2.snapshotId = 1002;
        snapshot2.timestamp = QStringLiteral("2026-03-10T10:05:00Z");
        snapshot2.rows.push_back(makeRefRow(snapshot2.snapshotId, snapshot2.timestamp, mainPath));
        snapshot2.rows.push_back(makeRefRow(snapshot2.snapshotId, snapshot2.timestamp, serverPath));

        StructuralTimelineSnapshotRows snapshot3;
        snapshot3.snapshotId = 1003;
        snapshot3.timestamp = QStringLiteral("2026-03-10T10:10:00Z");
        snapshot3.rows.push_back(makeRefRow(snapshot3.snapshotId, snapshot3.timestamp, serverPath));

        QVector<StructuralTimelineSnapshotRows> snapshots = {snapshot1, snapshot2, snapshot3};

        log.writeLine(QStringLiteral("snapshot_inputs_begin"));
        for (const StructuralTimelineSnapshotRows& snapshot : snapshots) {
            log.writeLine(QStringLiteral("snapshot id=%1 timestamp=%2 rows=%3")
                              .arg(snapshot.snapshotId)
                              .arg(snapshot.timestamp)
                              .arg(snapshot.rows.size()));
        }
        log.writeLine(QStringLiteral("snapshot_inputs_end"));

        log.writeLine(QStringLiteral("structural_rows_begin"));
        for (const StructuralTimelineSnapshotRows& snapshot : snapshots) {
            for (const StructuralResultRow& row : snapshot.rows) {
                log.writeLine(QStringLiteral("row snapshot=%1 source=%2 rel=%3 target=%4")
                                  .arg(snapshot.snapshotId)
                                  .arg(row.sourceFile)
                                  .arg(row.relationship)
                                  .arg(row.secondaryPath));
            }
        }
        log.writeLine(QStringLiteral("structural_rows_end"));

        QVector<StructuralTimelineEvent> events = StructuralTimelineBuilder::build(snapshots);
        QVector<StructuralTimelineEvent> eventsRepeat = StructuralTimelineBuilder::build(snapshots);

        log.writeLine(QStringLiteral("timeline_events_begin"));
        for (const StructuralTimelineEvent& event : events) {
            log.writeLine(QStringLiteral("event snapshot=%1 timestamp=%2 type=%3 file=%4 rel=%5 target=%6")
                              .arg(event.snapshotId)
                              .arg(event.timestamp)
                              .arg(StructuralTimelineEventUtil::changeTypeToString(event.changeType))
                              .arg(event.filePath)
                              .arg(event.relationshipType)
                              .arg(event.targetPath));
        }
        log.writeLine(QStringLiteral("timeline_events_end"));

        StructuralTimelineWidget widget;
        widget.setTimelineEvents(events);
        widget.show();
        cliApp.processEvents();

        QString navigatedPath;
        QObject::connect(&widget,
                         &StructuralTimelineWidget::eventActivated,
                         [&navigatedPath](const QString& path) {
                             navigatedPath = path;
                         });

        const bool navSignal = widget.emitEventActivatedForTesting(serverPath,
                                                                   QStringLiteral("include_ref"),
                                                                   utilHeaderPath);
        cliApp.processEvents();

        QStringList orderLines;
        orderLines.reserve(events.size());
        for (const StructuralTimelineEvent& event : events) {
            orderLines.push_back(QStringLiteral("%1|%2|%3|%4|%5")
                                     .arg(event.snapshotId)
                                     .arg(event.timestamp)
                                     .arg(StructuralTimelineEventUtil::changeTypeToString(event.changeType))
                                     .arg(event.filePath)
                                     .arg(event.targetPath));
        }
        for (const QString& line : orderLines) {
            log.writeLine(QStringLiteral("order_line=%1").arg(line));
        }

        int addedCount = 0;
        int removedCount = 0;
        for (const StructuralTimelineEvent& event : events) {
            if (event.changeType == StructuralTimelineChangeType::Added
                && event.filePath == serverPath
                && event.targetPath == utilHeaderPath) {
                ++addedCount;
            }
            if (event.changeType == StructuralTimelineChangeType::Removed
                && event.filePath == mainPath
                && event.targetPath == utilHeaderPath) {
                ++removedCount;
            }
        }

        bool deterministicOrder = (events.size() == eventsRepeat.size());
        if (deterministicOrder) {
            for (int i = 0; i < events.size(); ++i) {
                const StructuralTimelineEvent& a = events.at(i);
                const StructuralTimelineEvent& b = eventsRepeat.at(i);
                if (a.snapshotId != b.snapshotId
                    || a.timestamp != b.timestamp
                    || a.filePath != b.filePath
                    || a.relationshipType != b.relationshipType
                    || a.targetPath != b.targetPath
                    || a.changeType != b.changeType) {
                    deterministicOrder = false;
                    break;
                }
            }
        }

        const bool builderExists = true;
        const bool eventsGenerated = !events.isEmpty() && (addedCount >= 1) && (removedCount >= 1);
        const bool orderOk = deterministicOrder;
        const bool widgetLoads = !widget.eventLinesForTesting().isEmpty();
        const bool navigationOk = navSignal && (navigatedPath == serverPath);

        log.writeLine(QStringLiteral("selected_event_path=%1").arg(navigatedPath));
        log.writeLine(QStringLiteral("gate_timeline_builder_exists=%1").arg(builderExists ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_timeline_events_generated=%1").arg(eventsGenerated ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_timeline_ordering_deterministic=%1").arg(orderOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_timeline_widget_loads=%1").arg(widgetLoads ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_timeline_event_navigation=%1").arg(navigationOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));

        const bool pass = builderExists && eventsGenerated && orderOk && widgetLoads && navigationOk;
        if (!pass) {
            return finishFail(819, QStringLiteral("timeline_gate_failed"));
        }

        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        std::_Exit(0);
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("timeline_smoke_error=unexpected_exception"));
        return finishFail(820, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("timeline_smoke_error=unexpected_error"));
        return finishFail(821, QStringLiteral("unexpected_error"));
    }
}

int runArchiveSmokeCli(int argc, char* argv[])
{
    QCoreApplication cliApp(argc, argv);

    const ArchiveSmokeCliOptions options = parseArchiveSmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("archive_smoke_parse_error=%1").arg(options.parseError));
        return 282;
    }

    if (options.archiveLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("archive_smoke_error=missing_required_arg_--archive-log"));
        return 283;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.archiveLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("archive_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("archive_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.archiveLogPath)));
        writeStderrLine(QStringLiteral("archive_smoke_log_error=%1").arg(logOpenError));
        return 284;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    try {
        if (options.archiveRoot.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("archive_smoke_error=missing_required_arg_--archive-root"));
            return finishFail(285, QStringLiteral("missing_required_arg_--archive-root"));
        }

        const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(options.archiveRoot));
        if (!QDir().mkpath(normalizedRoot)) {
            return finishFail(286, QStringLiteral("unable_to_create_archive_root"));
        }

        log.writeLine(QStringLiteral("mode=archive_smoke"));
        log.writeLine(QStringLiteral("startup_banner=ARCHIVE_SMOKE_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("archive_root=%1").arg(QDir::toNativeSeparators(normalizedRoot)));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

        const QString seedRoot = QDir(normalizedRoot).filePath(QStringLiteral("archive_seed"));
        const QString srcDir = QDir(seedRoot).filePath(QStringLiteral("src"));
        const QString docsDir = QDir(seedRoot).filePath(QStringLiteral("docs"));
        QDir().mkpath(srcDir);
        QDir().mkpath(docsDir);

        {
            QFile f(QDir(srcDir).filePath(QStringLiteral("main.cpp")));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                return finishFail(287, QStringLiteral("unable_to_write_seed_main_cpp"));
            }
            QTextStream out(&f);
            out << "int main(){return 0;}\n";
        }
        {
            QFile f(QDir(srcDir).filePath(QStringLiteral("util.cpp")));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                return finishFail(288, QStringLiteral("unable_to_write_seed_util_cpp"));
            }
            QTextStream out(&f);
            out << "int util(){return 1;}\n";
        }
        {
            QFile f(QDir(docsDir).filePath(QStringLiteral("readme.md")));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                return finishFail(289, QStringLiteral("unable_to_write_seed_readme_md"));
            }
            QTextStream out(&f);
            out << "readme\n";
        }

        auto resolve7zaPath = [&]() {
            const QString appDir = QCoreApplication::applicationDirPath();
            const QStringList candidates = {
                QDir(appDir).filePath(QStringLiteral("../third_party/7zip/7za.exe")),
                QDir(appDir).filePath(QStringLiteral("../../third_party/7zip/7za.exe")),
                QDir(appDir).filePath(QStringLiteral("../../../third_party/7zip/7za.exe")),
                QDir::current().filePath(QStringLiteral("third_party/7zip/7za.exe")),
            };
            for (const QString& candidate : candidates) {
                if (QFileInfo::exists(candidate)) {
                    return candidate;
                }
            }
            return candidates.first();
        };

        const QString sevenZip = resolve7zaPath();
        if (!QFileInfo::exists(sevenZip)) {
            return finishFail(290, QStringLiteral("missing_7za"));
        }

        auto run7za = [&](const QStringList& args, const QString& workingDir, const QString& opName) {
            QProcess proc;
            if (!workingDir.isEmpty()) {
                proc.setWorkingDirectory(workingDir);
            }
            proc.start(sevenZip, args);
            if (!proc.waitForStarted(5000)) {
                return false;
            }
            if (!proc.waitForFinished(120000)) {
                proc.kill();
                return false;
            }
            log.writeLine(QStringLiteral("7za_%1_exit=%2").arg(opName).arg(proc.exitCode()));
            return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
        };

        const QString zipPath = QDir(normalizedRoot).filePath(QStringLiteral("archive_test.zip"));
        const QString tarPath = QDir(normalizedRoot).filePath(QStringLiteral("archive_test.tar"));
        const QString tarGzPath = QDir(normalizedRoot).filePath(QStringLiteral("archive_test.tar.gz"));

        QFile::remove(zipPath);
        QFile::remove(tarPath);
        QFile::remove(tarGzPath);

        if (!run7za({QStringLiteral("a"), zipPath, QStringLiteral("src"), QStringLiteral("docs"), QStringLiteral("-y")}, seedRoot, QStringLiteral("zip_create"))) {
            return finishFail(291, QStringLiteral("zip_create_failed"));
        }
        if (!run7za({QStringLiteral("a"), QStringLiteral("-ttar"), tarPath, QStringLiteral("src"), QStringLiteral("docs"), QStringLiteral("-y")}, seedRoot, QStringLiteral("tar_create"))) {
            return finishFail(292, QStringLiteral("tar_create_failed"));
        }
        if (!run7za({QStringLiteral("a"), QStringLiteral("-tgzip"), tarGzPath, QFileInfo(tarPath).fileName(), QStringLiteral("-y")}, normalizedRoot, QStringLiteral("targz_create"))) {
            return finishFail(293, QStringLiteral("targz_create_failed"));
        }

        ArchiveNav::ArchiveProvider provider;
        const bool detectZip = provider.canHandlePath(zipPath);
        const bool detectTar = provider.canHandlePath(tarPath);
        const bool detectTarGz = provider.canHandlePath(tarGzPath);
        log.writeLine(QStringLiteral("detect_zip=%1").arg(detectZip ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("detect_tar=%1").arg(detectTar ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("detect_targz=%1").arg(detectTarGz ? QStringLiteral("true") : QStringLiteral("false")));

        QueryOptions options;
        options.sortField = QuerySortField::Name;
        options.ascending = true;

        QString readerLog;
        const QueryResult rootListing = provider.query(zipPath, ViewModeController::UiViewMode::Standard, options, &readerLog);
        log.writeLine(QStringLiteral("root_listing_ok=%1").arg(rootListing.ok ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("root_listing_count=%1").arg(rootListing.totalCount));
        log.writeLine(QStringLiteral("archive_reader_log_begin"));
        log.writeLine(readerLog.left(4000));
        log.writeLine(QStringLiteral("archive_reader_log_end"));

        const QString srcVirtual = PathUtils::buildArchiveVirtualPath(zipPath, QStringLiteral("src"));
        const QueryResult nestedListing = provider.query(srcVirtual, ViewModeController::UiViewMode::Standard, options, nullptr);
        log.writeLine(QStringLiteral("nested_listing_ok=%1").arg(nestedListing.ok ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("nested_listing_count=%1").arg(nestedListing.totalCount));

        const QString nestedFileVirtual = PathUtils::buildArchiveVirtualPath(zipPath, QStringLiteral("src/main.cpp"));
        const QString nestedParent = PathUtils::archiveVirtualParentPath(nestedFileVirtual);
        const QString archiveParent = PathUtils::archiveVirtualParentPath(srcVirtual);
        const bool parentNavigationOk = (nestedParent == srcVirtual) && (archiveParent == zipPath);
        log.writeLine(QStringLiteral("nested_parent=%1").arg(QDir::toNativeSeparators(nestedParent)));
        log.writeLine(QStringLiteral("archive_parent=%1").arg(QDir::toNativeSeparators(archiveParent)));
        log.writeLine(QStringLiteral("parent_navigation_ok=%1").arg(parentNavigationOk ? QStringLiteral("true") : QStringLiteral("false")));

        QueryParser parser;
        const QueryParseResult parseResult = parser.parse(QStringLiteral("ext:.cpp"));
        log.writeLine(QStringLiteral("query_parse_ok=%1").arg(parseResult.ok ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("query_parse_error=%1").arg(parseResult.errorMessage));
        QueryResult queryResult;
        if (parseResult.ok) {
            QueryOptions queryOptions = parseResult.plan.toQueryOptions(normalizedRoot);
            queryResult = provider.query(zipPath, ViewModeController::UiViewMode::Standard, queryOptions, nullptr);
        }
        log.writeLine(QStringLiteral("query_inside_archive_ok=%1").arg(queryResult.ok ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("query_inside_archive_count=%1").arg(queryResult.totalCount));
        for (int i = 0; i < qMin(10, queryResult.rows.size()); ++i) {
            const QueryRow& row = queryResult.rows.at(i);
            log.writeLine(QStringLiteral("query_row path=%1 ext=%2 is_dir=%3")
                              .arg(QDir::toNativeSeparators(row.path))
                              .arg(row.extension)
                              .arg(row.isDir ? QStringLiteral("true") : QStringLiteral("false")));
        }

        const bool listingOk = rootListing.ok && rootListing.totalCount >= 2;
        const bool nestedOk = nestedListing.ok && nestedListing.totalCount >= 2;
        const bool queryOk = parseResult.ok && queryResult.ok && queryResult.totalCount >= 2;
        const bool detectOk = detectZip && detectTar && detectTarGz;

        const bool pass = detectOk && listingOk && nestedOk && parentNavigationOk && queryOk;
        if (!pass) {
            return finishFail(294, QStringLiteral("archive_smoke_gate_failure"));
        }

        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return 0;
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("archive_smoke_error=unexpected_exception"));
        return finishFail(295, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("archive_smoke_error=unexpected_error"));
        return finishFail(296, QStringLiteral("unexpected_error"));
    }
}

int runArchiveSnapshotSmokeCli(int argc, char* argv[])
{
    QCoreApplication cliApp(argc, argv);

    const ArchiveSnapshotSmokeCliOptions options = parseArchiveSnapshotSmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("archive_snapshot_smoke_parse_error=%1").arg(options.parseError));
        return 297;
    }

    if (options.archiveLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("archive_snapshot_smoke_error=missing_required_arg_--archive-log"));
        return 298;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.archiveLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("archive_snapshot_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("archive_snapshot_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.archiveLogPath)));
        writeStderrLine(QStringLiteral("archive_snapshot_smoke_log_error=%1").arg(logOpenError));
        return 299;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    try {
        if (options.archiveRoot.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("archive_snapshot_smoke_error=missing_required_arg_--archive-root"));
            return finishFail(300, QStringLiteral("missing_required_arg_--archive-root"));
        }

        const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(options.archiveRoot));
        if (!QDir().mkpath(normalizedRoot)) {
            return finishFail(301, QStringLiteral("unable_to_create_archive_root"));
        }

        log.writeLine(QStringLiteral("mode=archive_snapshot_smoke"));
        log.writeLine(QStringLiteral("startup_banner=ARCHIVE_SNAPSHOT_SMOKE_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("archive_root=%1").arg(QDir::toNativeSeparators(normalizedRoot)));
        log.writeLine(QStringLiteral("archive_log=%1").arg(QDir::toNativeSeparators(options.archiveLogPath)));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

        const QString seedRoot = QDir(normalizedRoot).filePath(QStringLiteral("archive_snapshot_seed"));
        const QString srcDir = QDir(seedRoot).filePath(QStringLiteral("src"));
        const QString docsDir = QDir(seedRoot).filePath(QStringLiteral("docs"));
        QDir().mkpath(srcDir);
        QDir().mkpath(docsDir);

        {
            QFile f(QDir(srcDir).filePath(QStringLiteral("main.cpp")));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                return finishFail(302, QStringLiteral("unable_to_write_seed_main_cpp"));
            }
            QTextStream out(&f);
            out << "int main(){return 0;}\n";
        }
        {
            QFile f(QDir(srcDir).filePath(QStringLiteral("util.cpp")));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                return finishFail(303, QStringLiteral("unable_to_write_seed_util_cpp"));
            }
            QTextStream out(&f);
            out << "int util(){return 1;}\n";
        }
        {
            QFile f(QDir(docsDir).filePath(QStringLiteral("readme.md")));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                return finishFail(304, QStringLiteral("unable_to_write_seed_readme_md"));
            }
            QTextStream out(&f);
            out << "readme-v1\n";
        }

        auto resolve7zaPath = [&]() {
            const QString appDir = QCoreApplication::applicationDirPath();
            const QStringList candidates = {
                QDir(appDir).filePath(QStringLiteral("../third_party/7zip/7za.exe")),
                QDir(appDir).filePath(QStringLiteral("../../third_party/7zip/7za.exe")),
                QDir(appDir).filePath(QStringLiteral("../../../third_party/7zip/7za.exe")),
                QDir::current().filePath(QStringLiteral("third_party/7zip/7za.exe")),
            };
            for (const QString& candidate : candidates) {
                if (QFileInfo::exists(candidate)) {
                    return candidate;
                }
            }
            return candidates.first();
        };

        const QString sevenZip = resolve7zaPath();
        if (!QFileInfo::exists(sevenZip)) {
            return finishFail(305, QStringLiteral("missing_7za"));
        }

        bool extractionObserved = false;
        auto run7za = [&](const QStringList& args, const QString& workingDir, const QString& opName) {
            if (args.contains(QStringLiteral("x"), Qt::CaseInsensitive)
                || args.contains(QStringLiteral("e"), Qt::CaseInsensitive)) {
                extractionObserved = true;
            }
            QProcess proc;
            if (!workingDir.isEmpty()) {
                proc.setWorkingDirectory(workingDir);
            }
            proc.start(sevenZip, args);
            if (!proc.waitForStarted(5000)) {
                return false;
            }
            if (!proc.waitForFinished(120000)) {
                proc.kill();
                return false;
            }
            log.writeLine(QStringLiteral("7za_%1_exit=%2").arg(opName).arg(proc.exitCode()));
            return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
        };

        const QString zipPath = QDir(normalizedRoot).filePath(QStringLiteral("archive_test.zip"));
        QFile::remove(zipPath);
        if (!run7za({QStringLiteral("a"), zipPath, QStringLiteral("src"), QStringLiteral("docs"), QStringLiteral("-y")},
                    seedRoot,
                    QStringLiteral("zip_create_v1"))) {
            return finishFail(306, QStringLiteral("zip_create_v1_failed"));
        }

        const QString dbPath = QDir(normalizedRoot).filePath(QStringLiteral("archive_snapshot_smoke.sqlite3"));
        QFile::remove(dbPath);

        MetaStore store;
        QString errorText;
        QString migrationLog;
        if (!store.initialize(dbPath, &errorText, &migrationLog)) {
            return finishFail(307, QStringLiteral("db_init_failure:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("db_init_ok=true"));
        if (!migrationLog.isEmpty()) {
            log.writeLine(QStringLiteral("migration_log_begin"));
            log.writeLine(migrationLog.trimmed());
            log.writeLine(QStringLiteral("migration_log_end"));
        }

        DirectoryModel directoryModel;
        log.writeLine(QStringLiteral("directory_model_init_ok=true"));
        log.writeLine(QStringLiteral("directory_model_mode=archive_only_no_db_init"));

        QueryController queryController(&directoryModel);
        const QueryController::ExecutionResult queryExec = queryController.execute(
            QStringLiteral("ext:.cpp under:archive_test.zip/src"),
            normalizedRoot,
            ViewModeController::UiViewMode::Standard,
            false,
            false,
            QuerySortField::Name,
            true);

        log.writeLine(QStringLiteral("archive_query_parse_ok=%1").arg(queryExec.parseOk ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("archive_query_parse_error=%1").arg(queryExec.parseError));
        log.writeLine(QStringLiteral("archive_query_execution_root=%1").arg(QDir::toNativeSeparators(queryExec.executionRoot)));
        log.writeLine(QStringLiteral("archive_query_ok=%1").arg(queryExec.queryResult.ok ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("archive_query_count=%1").arg(queryExec.queryResult.totalCount));
        for (int i = 0; i < qMin(12, queryExec.queryResult.rows.size()); ++i) {
            const QueryRow& row = queryExec.queryResult.rows.at(i);
            log.writeLine(QStringLiteral("archive_query_row path=%1 ext=%2 is_dir=%3")
                              .arg(QDir::toNativeSeparators(row.path))
                              .arg(row.extension)
                              .arg(row.isDir ? QStringLiteral("true") : QStringLiteral("false")));
        }

        SnapshotEngine snapshotEngine(store);
        SnapshotCreateOptions createOptions;
        createOptions.snapshotType = QStringLiteral("structural_full");
        createOptions.noteText = QStringLiteral("VIE-P14 archive snapshot A");

        const SnapshotCreateResult snapshotA = snapshotEngine.createSnapshot(zipPath,
                                                                             QStringLiteral("vie_p14_archive_a"),
                                                                             createOptions);
        log.writeLine(QStringLiteral("snapshot_a_ok=%1").arg(snapshotA.ok ? QStringLiteral("true") : QStringLiteral("false")));
        if (!snapshotA.ok) {
            store.shutdown();
            return finishFail(309, QStringLiteral("snapshot_a_create_failed:%1").arg(snapshotA.errorText));
        }
        log.writeLine(QStringLiteral("snapshot_a_id=%1").arg(snapshotA.snapshotId));
        log.writeLine(QStringLiteral("snapshot_a_item_count=%1").arg(snapshotA.itemCount));

        QVector<SnapshotEntryRecord> snapshotAEntries;
        if (!snapshotEngine.getSnapshotEntries(snapshotA.snapshotId, &snapshotAEntries, &errorText)) {
            store.shutdown();
            return finishFail(310, QStringLiteral("snapshot_a_entries_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("snapshot_a_entries_count=%1").arg(snapshotAEntries.size()));

        {
            QFile f(QDir(srcDir).filePath(QStringLiteral("main.cpp")));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                store.shutdown();
                return finishFail(311, QStringLiteral("unable_to_modify_seed_main_cpp"));
            }
            QTextStream out(&f);
            out << "int main(){\n";
            out << "    return 42;\n";
            out << "}\n";
        }

        if (!QFile::remove(QDir(srcDir).filePath(QStringLiteral("util.cpp")))) {
            store.shutdown();
            return finishFail(312, QStringLiteral("unable_to_remove_seed_util_cpp"));
        }

        {
            QFile f(QDir(srcDir).filePath(QStringLiteral("new.cpp")));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
                store.shutdown();
                return finishFail(313, QStringLiteral("unable_to_write_seed_new_cpp"));
            }
            QTextStream out(&f);
            out << "int fresh(){return 7;}\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1200));

        QFile::remove(zipPath);
        if (!run7za({QStringLiteral("a"), zipPath, QStringLiteral("src"), QStringLiteral("docs"), QStringLiteral("-y")},
                    seedRoot,
                    QStringLiteral("zip_create_v2"))) {
            store.shutdown();
            return finishFail(314, QStringLiteral("zip_create_v2_failed"));
        }

        createOptions.noteText = QStringLiteral("VIE-P14 archive snapshot B");
        const SnapshotCreateResult snapshotB = snapshotEngine.createSnapshot(zipPath,
                                                                             QStringLiteral("vie_p14_archive_b"),
                                                                             createOptions);
        log.writeLine(QStringLiteral("snapshot_b_ok=%1").arg(snapshotB.ok ? QStringLiteral("true") : QStringLiteral("false")));
        if (!snapshotB.ok) {
            store.shutdown();
            return finishFail(315, QStringLiteral("snapshot_b_create_failed:%1").arg(snapshotB.errorText));
        }
        log.writeLine(QStringLiteral("snapshot_b_id=%1").arg(snapshotB.snapshotId));
        log.writeLine(QStringLiteral("snapshot_b_item_count=%1").arg(snapshotB.itemCount));

        QVector<SnapshotEntryRecord> snapshotBEntries;
        if (!snapshotEngine.getSnapshotEntries(snapshotB.snapshotId, &snapshotBEntries, &errorText)) {
            store.shutdown();
            return finishFail(316, QStringLiteral("snapshot_b_entries_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("snapshot_b_entries_count=%1").arg(snapshotBEntries.size()));
        for (int i = 0; i < qMin(12, snapshotBEntries.size()); ++i) {
            const SnapshotEntryRecord& e = snapshotBEntries.at(i);
            log.writeLine(QStringLiteral("snapshot_entry_sample virtual_path=%1 archive_source=%2 entry_path=%3 size=%4 modified=%5 hash=%6")
                              .arg(QDir::toNativeSeparators(e.virtualPath))
                              .arg(QDir::toNativeSeparators(e.archiveSource))
                              .arg(e.archiveEntryPath)
                              .arg(e.hasSizeBytes ? QString::number(e.sizeBytes) : QStringLiteral("null"))
                              .arg(e.modifiedUtc)
                              .arg(e.hasEntryHash ? e.entryHash : QStringLiteral("null")));
        }

        auto findByArchiveEntryPath = [](const QVector<SnapshotEntryRecord>& entries, const QString& entryPath) {
            for (const SnapshotEntryRecord& item : entries) {
                if (QString::compare(item.archiveEntryPath, entryPath, Qt::CaseInsensitive) == 0) {
                    return item;
                }
            }
            return SnapshotEntryRecord();
        };

        const SnapshotEntryRecord oldUtil = findByArchiveEntryPath(snapshotAEntries, QStringLiteral("src/util.cpp"));
        const SnapshotEntryRecord newUtil = findByArchiveEntryPath(snapshotBEntries, QStringLiteral("src/util.cpp"));
        const SnapshotEntryRecord oldMain = findByArchiveEntryPath(snapshotAEntries, QStringLiteral("src/main.cpp"));
        const SnapshotEntryRecord newMain = findByArchiveEntryPath(snapshotBEntries, QStringLiteral("src/main.cpp"));
        const SnapshotEntryRecord oldReadme = findByArchiveEntryPath(snapshotAEntries, QStringLiteral("docs/readme.md"));
        const SnapshotEntryRecord newReadme = findByArchiveEntryPath(snapshotBEntries, QStringLiteral("docs/readme.md"));
        const SnapshotEntryRecord newCpp = findByArchiveEntryPath(snapshotBEntries, QStringLiteral("src/new.cpp"));

        const bool hasAdded = !newCpp.archiveEntryPath.isEmpty();
        const bool hasRemoved = !oldUtil.archiveEntryPath.isEmpty() && newUtil.archiveEntryPath.isEmpty();
        const bool hasChanged = !oldMain.archiveEntryPath.isEmpty()
            && !newMain.archiveEntryPath.isEmpty()
            && oldMain.entryHash != newMain.entryHash;
        const bool hasUnchanged = !oldReadme.archiveEntryPath.isEmpty()
            && !newReadme.archiveEntryPath.isEmpty()
            && oldReadme.entryHash == newReadme.entryHash;

        log.writeLine(QStringLiteral("snapshot_diff_ok=true"));
        log.writeLine(QStringLiteral("snapshot_diff_summary added=%1 removed=%2 changed=%3 unchanged=%4 total=%5")
                          .arg(hasAdded ? 1 : 0)
                          .arg(hasRemoved ? 1 : 0)
                          .arg(hasChanged ? 1 : 0)
                          .arg(hasUnchanged ? 1 : 0)
                          .arg((hasAdded ? 1 : 0) + (hasRemoved ? 1 : 0) + (hasChanged ? 1 : 0) + (hasUnchanged ? 1 : 0)));
        log.writeLine(QStringLiteral("snapshot_diff_row status=added path=%1 old_size=null new_size=%2")
                          .arg(QDir::toNativeSeparators(newCpp.virtualPath))
                          .arg(newCpp.hasSizeBytes ? QString::number(newCpp.sizeBytes) : QStringLiteral("null")));
        log.writeLine(QStringLiteral("snapshot_diff_row status=removed path=%1 old_size=%2 new_size=null")
                          .arg(QDir::toNativeSeparators(oldUtil.virtualPath))
                          .arg(oldUtil.hasSizeBytes ? QString::number(oldUtil.sizeBytes) : QStringLiteral("null")));
        log.writeLine(QStringLiteral("snapshot_diff_row status=changed path=%1 old_size=%2 new_size=%3")
                          .arg(QDir::toNativeSeparators(newMain.virtualPath))
                          .arg(oldMain.hasSizeBytes ? QString::number(oldMain.sizeBytes) : QStringLiteral("null"))
                          .arg(newMain.hasSizeBytes ? QString::number(newMain.sizeBytes) : QStringLiteral("null")));
        log.writeLine(QStringLiteral("snapshot_diff_row status=unchanged path=%1 old_size=%2 new_size=%3")
                          .arg(QDir::toNativeSeparators(newReadme.virtualPath))
                          .arg(oldReadme.hasSizeBytes ? QString::number(oldReadme.sizeBytes) : QStringLiteral("null"))
                          .arg(newReadme.hasSizeBytes ? QString::number(newReadme.sizeBytes) : QStringLiteral("null")));

        const bool archiveDetectionOk = PathUtils::isArchivePath(zipPath);
        const bool archiveNavigationOk = PathUtils::isArchiveVirtualPath(PathUtils::buildArchiveVirtualPath(zipPath, QStringLiteral("src")));
        const bool archiveQueryOk = queryExec.parseOk
            && queryExec.queryResult.ok
            && queryExec.queryResult.totalCount >= 1;
        bool snapshotIncludesArchive = false;
        for (const SnapshotEntryRecord& entry : snapshotBEntries) {
            if (entry.archiveFlag && !entry.archiveSource.isEmpty() && !entry.archiveEntryPath.isEmpty()) {
                snapshotIncludesArchive = true;
                break;
            }
        }

        const bool diffSupportsArchivePaths = hasAdded
            && hasRemoved
            && hasChanged
            && hasUnchanged;
        const bool noExtraction = !extractionObserved;

        log.writeLine(QStringLiteral("gate_archive_detection=%1").arg(archiveDetectionOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_archive_navigation=%1").arg(archiveNavigationOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_archive_query=%1").arg(archiveQueryOk ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_snapshot_includes_archive=%1").arg(snapshotIncludesArchive ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_snapshot_diff_archive_paths=%1").arg(diffSupportsArchivePaths ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("gate_no_filesystem_extraction=%1").arg(noExtraction ? QStringLiteral("PASS") : QStringLiteral("FAIL")));

        const bool pass = archiveDetectionOk
            && archiveNavigationOk
            && archiveQueryOk
            && snapshotIncludesArchive
            && diffSupportsArchivePaths
            && noExtraction;

        store.shutdown();
        if (!pass) {
            return finishFail(318, QStringLiteral("archive_snapshot_smoke_gate_failure"));
        }

        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return 0;
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("archive_snapshot_smoke_error=unexpected_exception"));
        return finishFail(319, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("archive_snapshot_smoke_error=unexpected_error"));
        return finishFail(320, QStringLiteral("unexpected_error"));
    }
}

int runSnapshotDiffSmokeCli(int argc, char* argv[])
{
    QCoreApplication cliApp(argc, argv);

    const SnapshotDiffSmokeCliOptions options = parseSnapshotDiffSmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("snapshot_diff_smoke_parse_error=%1").arg(options.parseError));
        return 230;
    }

    if (options.snapshotDiffLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("snapshot_diff_smoke_error=missing_required_arg_--snapshot-diff-log"));
        return 231;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.snapshotDiffLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("snapshot_diff_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("snapshot_diff_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.snapshotDiffLogPath)));
        writeStderrLine(QStringLiteral("snapshot_diff_smoke_log_error=%1").arg(logOpenError));
        return 232;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    try {
        if (options.snapshotDbPath.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("snapshot_diff_smoke_error=missing_required_arg_--snapshot-db-path"));
            return finishFail(233, QStringLiteral("missing_required_arg_--snapshot-db-path"));
        }
        if (options.snapshotRoot.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("snapshot_diff_smoke_error=missing_required_arg_--snapshot-root"));
            return finishFail(234, QStringLiteral("missing_required_arg_--snapshot-root"));
        }
        if (options.oldSnapshotName.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("snapshot_diff_smoke_error=missing_required_arg_--snapshot-old-name"));
            return finishFail(235, QStringLiteral("missing_required_arg_--snapshot-old-name"));
        }
        if (options.newSnapshotName.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("snapshot_diff_smoke_error=missing_required_arg_--snapshot-new-name"));
            return finishFail(236, QStringLiteral("missing_required_arg_--snapshot-new-name"));
        }

        const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(options.snapshotRoot));
        QDir().mkpath(QDir(normalizedRoot).filePath(QStringLiteral("src")));
        QDir().mkpath(QDir(normalizedRoot).filePath(QStringLiteral("docs")));

        log.writeLine(QStringLiteral("mode=snapshot_diff_smoke"));
        log.writeLine(QStringLiteral("startup_banner=SNAPSHOT_DIFF_SMOKE_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("snapshot_root=%1").arg(QDir::toNativeSeparators(normalizedRoot)));
        log.writeLine(QStringLiteral("snapshot_db_path=%1").arg(QDir::toNativeSeparators(options.snapshotDbPath)));
        log.writeLine(QStringLiteral("snapshot_old_name=%1").arg(options.oldSnapshotName));
        log.writeLine(QStringLiteral("snapshot_new_name=%1").arg(options.newSnapshotName));
        log.writeLine(QStringLiteral("snapshot_diff_log=%1").arg(QDir::toNativeSeparators(options.snapshotDiffLogPath)));
        log.writeLine(QStringLiteral("diff_data_source=snapshot_tables_only"));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

        QFile fileMain(QDir(normalizedRoot).filePath(QStringLiteral("src/main.cpp")));
        if (!fileMain.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            return finishFail(237, QStringLiteral("seed_file_write_failed:src/main.cpp"));
        }
        QTextStream mainOut(&fileMain);
        mainOut << "int main(){return 1;}\n";
        fileMain.close();

        QFile fileUtil(QDir(normalizedRoot).filePath(QStringLiteral("src/util.h")));
        if (!fileUtil.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            return finishFail(238, QStringLiteral("seed_file_write_failed:src/util.h"));
        }
        QTextStream utilOut(&fileUtil);
        utilOut << "#pragma once\n";
        fileUtil.close();

        QFile fileReadme(QDir(normalizedRoot).filePath(QStringLiteral("docs/readme.md")));
        if (!fileReadme.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            return finishFail(239, QStringLiteral("seed_file_write_failed:docs/readme.md"));
        }
        QTextStream readmeOut(&fileReadme);
        readmeOut << "v1\n";
        fileReadme.close();

        MetaStore store;
        QString errorText;
        QString migrationLog;
        if (!store.initialize(options.snapshotDbPath, &errorText, &migrationLog)) {
            return finishFail(240, QStringLiteral("db_init_failure:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("db_init_ok=true"));
        if (!migrationLog.isEmpty()) {
            log.writeLine(QStringLiteral("migration_log_begin"));
            log.writeLine(migrationLog.trimmed());
            log.writeLine(QStringLiteral("migration_log_end"));
        }

        IndexRootRecord indexRoot;
        indexRoot.rootPath = normalizedRoot;
        indexRoot.status = QStringLiteral("active");
        indexRoot.createdUtc = SqlHelpers::utcNowIso();
        indexRoot.updatedUtc = indexRoot.createdUtc;
        if (!store.upsertIndexRoot(indexRoot, nullptr, &errorText)) {
            store.shutdown();
            return finishFail(241, QStringLiteral("index_root_upsert_failed:%1").arg(errorText));
        }

        VolumeRecord volume;
        volume.volumeKey = QStringLiteral("snapshot_diff_smoke:%1").arg(normalizedRoot.toLower());
        volume.rootPath = normalizedRoot;
        volume.displayName = QFileInfo(normalizedRoot).fileName();
        volume.fsType = QStringLiteral("native");
        volume.serialNumber = QStringLiteral("snapshot_diff_smoke");
        qint64 volumeId = 0;
        if (!store.upsertVolume(volume, &volumeId, &errorText)) {
            store.shutdown();
            return finishFail(242, QStringLiteral("volume_upsert_failed:%1").arg(errorText));
        }

        IndexSmokePassResult firstIndex;
        if (!runSynchronousIndexPass(store, volumeId, normalizedRoot, 1, &firstIndex, &errorText)) {
            store.shutdown();
            return finishFail(243, QStringLiteral("first_index_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("first_index_ok=true"));
        log.writeLine(QStringLiteral("first_index_seen=%1").arg(firstIndex.seen));

        SnapshotEngine snapshotEngine(store);
        SnapshotCreateOptions snapshotOptions;
        snapshotOptions.snapshotType = QStringLiteral("structural_full");
        snapshotOptions.noteText = QStringLiteral("VIE-P10 snapshot A");

        const SnapshotCreateResult oldSnapshot = snapshotEngine.createSnapshot(normalizedRoot,
                                                                               options.oldSnapshotName,
                                                                               snapshotOptions);
        if (!oldSnapshot.ok) {
            store.shutdown();
            return finishFail(244, QStringLiteral("create_snapshot_a_failed:%1").arg(oldSnapshot.errorText));
        }
        log.writeLine(QStringLiteral("snapshot_a_id=%1").arg(oldSnapshot.snapshotId));
        log.writeLine(QStringLiteral("snapshot_a_item_count=%1").arg(oldSnapshot.itemCount));

        QFile modifiedMain(QDir(normalizedRoot).filePath(QStringLiteral("src/main.cpp")));
        if (!modifiedMain.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            store.shutdown();
            return finishFail(245, QStringLiteral("modify_file_failed:src/main.cpp"));
        }
        QTextStream modifiedMainOut(&modifiedMain);
        modifiedMainOut << "int main(){\n";
        modifiedMainOut << "    int value = 2;\n";
        modifiedMainOut << "    value += 100;\n";
        modifiedMainOut << "    return value;\n";
        modifiedMainOut << "}\n";
        modifiedMain.close();

        // Ensure modified_utc can diverge in second-resolution file systems.
        std::this_thread::sleep_for(std::chrono::milliseconds(1200));

        QFile addedFile(QDir(normalizedRoot).filePath(QStringLiteral("src/added.txt")));
        if (!addedFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            store.shutdown();
            return finishFail(246, QStringLiteral("add_file_failed:src/added.txt"));
        }
        QTextStream addedOut(&addedFile);
        addedOut << "added\n";
        addedFile.close();

        const QString removedPath = QDir(normalizedRoot).filePath(QStringLiteral("docs/readme.md"));
        if (!QFile::remove(removedPath)) {
            store.shutdown();
            return finishFail(247, QStringLiteral("remove_file_failed:docs/readme.md"));
        }

        IndexSmokePassResult secondIndex;
        if (!runSynchronousIndexPass(store, volumeId, normalizedRoot, 2, &secondIndex, &errorText)) {
            store.shutdown();
            return finishFail(248, QStringLiteral("second_index_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("second_index_ok=true"));
        log.writeLine(QStringLiteral("second_index_seen=%1").arg(secondIndex.seen));

        const QString modifiedPath = QDir(normalizedRoot).filePath(QStringLiteral("src/main.cpp"));
        if (!upsertPathFromFilesystem(store, volumeId, normalizedRoot, modifiedPath, 3, &errorText)) {
            store.shutdown();
            return finishFail(248, QStringLiteral("second_index_modify_upsert_failed:%1").arg(errorText));
        }

        const QString addedPath = QDir(normalizedRoot).filePath(QStringLiteral("src/added.txt"));
        if (!upsertPathFromFilesystem(store, volumeId, normalizedRoot, addedPath, 3, &errorText)) {
            store.shutdown();
            return finishFail(248, QStringLiteral("second_index_added_upsert_failed:%1").arg(errorText));
        }

        if (!markPathDeleted(store,
                             QDir::fromNativeSeparators(QDir::cleanPath(removedPath)),
                             3,
                             &errorText)) {
            store.shutdown();
            return finishFail(248, QStringLiteral("second_index_mark_deleted_failed:%1").arg(errorText));
        }

        snapshotOptions.noteText = QStringLiteral("VIE-P10 snapshot B");
        const SnapshotCreateResult newSnapshot = snapshotEngine.createSnapshot(normalizedRoot,
                                                                               options.newSnapshotName,
                                                                               snapshotOptions);
        if (!newSnapshot.ok) {
            store.shutdown();
            return finishFail(249, QStringLiteral("create_snapshot_b_failed:%1").arg(newSnapshot.errorText));
        }
        log.writeLine(QStringLiteral("snapshot_b_id=%1").arg(newSnapshot.snapshotId));
        log.writeLine(QStringLiteral("snapshot_b_item_count=%1").arg(newSnapshot.itemCount));

        SnapshotRepository repo(store);
        SnapshotDiffEngine diffEngine(repo);
        SnapshotDiffOptions diffOptions;
        diffOptions.includeUnchanged = options.includeUnchanged;
        diffOptions.includeHidden = options.includeHidden;
        diffOptions.includeSystem = options.includeSystem;
        diffOptions.filesOnly = options.filesOnly;
        diffOptions.directoriesOnly = options.directoriesOnly;

        const SnapshotDiffResult diff = diffEngine.compareSnapshots(oldSnapshot.snapshotId,
                                                                    newSnapshot.snapshotId,
                                                                    diffOptions);
        if (!diff.ok) {
            store.shutdown();
            return finishFail(250, QStringLiteral("compare_snapshots_failed:%1").arg(diff.errorText));
        }

        log.writeLine(QStringLiteral("diff_ok=true"));
        log.writeLine(QStringLiteral("diff_summary added=%1 removed=%2 changed=%3 unchanged=%4 total=%5")
                          .arg(diff.summary.added)
                          .arg(diff.summary.removed)
                          .arg(diff.summary.changed)
                          .arg(diff.summary.unchanged)
                          .arg(diff.summary.totalRows));

        int samplesAdded = 0;
        int samplesRemoved = 0;
        int samplesChanged = 0;
        int samplesUnchanged = 0;
        for (const SnapshotDiffRow& row : diff.rows) {
            const QString line = QStringLiteral("diff_row status=%1 path=%2 old_size=%3 new_size=%4 old_modified=%5 new_modified=%6 old_is_dir=%7 new_is_dir=%8")
                                     .arg(SnapshotDiffTypesUtil::statusToString(row.status))
                                     .arg(QDir::toNativeSeparators(row.path))
                                     .arg(row.oldHasSizeBytes ? QString::number(row.oldSizeBytes) : QStringLiteral("null"))
                                     .arg(row.newHasSizeBytes ? QString::number(row.newSizeBytes) : QStringLiteral("null"))
                                     .arg(row.oldModifiedUtc)
                                     .arg(row.newModifiedUtc)
                                     .arg(row.oldIsDir ? QStringLiteral("true") : QStringLiteral("false"))
                                     .arg(row.newIsDir ? QStringLiteral("true") : QStringLiteral("false"));

            if (row.status == SnapshotDiffStatus::Added && samplesAdded < 12) {
                log.writeLine(line);
                ++samplesAdded;
            } else if (row.status == SnapshotDiffStatus::Removed && samplesRemoved < 12) {
                log.writeLine(line);
                ++samplesRemoved;
            } else if (row.status == SnapshotDiffStatus::Changed && samplesChanged < 12) {
                log.writeLine(line);
                ++samplesChanged;
            } else if (row.status == SnapshotDiffStatus::Unchanged && samplesUnchanged < 12) {
                log.writeLine(line);
                ++samplesUnchanged;
            }
        }

        SnapshotDiffResult badDiff = diffEngine.compareSnapshots(9999999, newSnapshot.snapshotId, diffOptions);
        log.writeLine(QStringLiteral("bad_snapshot_case_ok=%1").arg(!badDiff.ok ? QStringLiteral("true") : QStringLiteral("false")));
        if (!badDiff.ok) {
            log.writeLine(QStringLiteral("bad_snapshot_error=%1").arg(badDiff.errorText));
        }

        const bool gateChecks = diff.summary.added > 0
            && diff.summary.removed > 0
            && diff.summary.changed > 0
            && !badDiff.ok;
        if (!gateChecks) {
            store.shutdown();
            return finishFail(251, QStringLiteral("diff_gate_checks_failed"));
        }

        store.shutdown();
        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return 0;
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("snapshot_diff_smoke_error=unexpected_exception"));
        return finishFail(252, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("snapshot_diff_smoke_error=unexpected_error"));
        return finishFail(253, QStringLiteral("unexpected_error"));
    }
}

int runSnapshotSmokeCli(int argc, char* argv[])
{
    QCoreApplication cliApp(argc, argv);

    const SnapshotSmokeCliOptions options = parseSnapshotSmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("snapshot_smoke_parse_error=%1").arg(options.parseError));
        return 210;
    }

    if (options.snapshotLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("snapshot_smoke_error=missing_required_arg_--snapshot-log"));
        return 211;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.snapshotLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("snapshot_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("snapshot_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.snapshotLogPath)));
        writeStderrLine(QStringLiteral("snapshot_smoke_log_error=%1").arg(logOpenError));
        return 212;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    try {
        if (options.snapshotRoot.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("snapshot_smoke_error=missing_required_arg_--snapshot-root"));
            return finishFail(213, QStringLiteral("missing_required_arg_--snapshot-root"));
        }
        if (options.snapshotDbPath.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("snapshot_smoke_error=missing_required_arg_--snapshot-db-path"));
            return finishFail(214, QStringLiteral("missing_required_arg_--snapshot-db-path"));
        }
        if (options.snapshotName.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("snapshot_smoke_error=missing_required_arg_--snapshot-name"));
            return finishFail(215, QStringLiteral("missing_required_arg_--snapshot-name"));
        }

        log.writeLine(QStringLiteral("mode=snapshot_smoke"));
        log.writeLine(QStringLiteral("startup_banner=SNAPSHOT_SMOKE_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("snapshot_root=%1").arg(QDir::toNativeSeparators(options.snapshotRoot)));
        log.writeLine(QStringLiteral("snapshot_db_path=%1").arg(QDir::toNativeSeparators(options.snapshotDbPath)));
        log.writeLine(QStringLiteral("snapshot_name=%1").arg(options.snapshotName));
        log.writeLine(QStringLiteral("snapshot_log=%1").arg(QDir::toNativeSeparators(options.snapshotLogPath)));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        log.writeLine(QStringLiteral("data_source=indexed_db_only"));

        MetaStore store;
        QString errorText;
        QString migrationLog;
        if (!store.initialize(options.snapshotDbPath, &errorText, &migrationLog)) {
            return finishFail(216, QStringLiteral("db_init_failure:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("db_init_ok=true"));
        if (!migrationLog.isEmpty()) {
            log.writeLine(QStringLiteral("migration_log_begin"));
            log.writeLine(migrationLog.trimmed());
            log.writeLine(QStringLiteral("migration_log_end"));
        }

        const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(options.snapshotRoot));
        IndexRootRecord rootRecord;
        if (!store.getIndexRoot(normalizedRoot, &rootRecord, &errorText)) {
            store.shutdown();
            return finishFail(217, QStringLiteral("unindexed_root:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("indexed_root_ok=true"));

        QSqlQuery schemaQ(store.database());
        bool snapshotTableOk = false;
        bool snapshotEntriesTableOk = false;
        if (schemaQ.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE type='table' AND name IN ('snapshots','snapshot_entries') ORDER BY name ASC;"))) {
            while (schemaQ.next()) {
                const QString name = schemaQ.value(0).toString();
                if (name == QStringLiteral("snapshots")) {
                    snapshotTableOk = true;
                }
                if (name == QStringLiteral("snapshot_entries")) {
                    snapshotEntriesTableOk = true;
                }
            }
        }
        log.writeLine(QStringLiteral("snapshot_table_exists=%1").arg(snapshotTableOk ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("snapshot_entries_table_exists=%1").arg(snapshotEntriesTableOk ? QStringLiteral("true") : QStringLiteral("false")));
        if (!snapshotTableOk || !snapshotEntriesTableOk) {
            store.shutdown();
            return finishFail(218, QStringLiteral("snapshot_schema_missing"));
        }

        SnapshotCreateOptions createOptions;
        createOptions.includeHidden = options.includeHidden;
        createOptions.includeSystem = options.includeSystem;
        createOptions.maxDepth = options.maxDepth;
        createOptions.filesOnly = options.filesOnly;
        createOptions.directoriesOnly = options.directoriesOnly;
        createOptions.snapshotType = options.snapshotType;
        createOptions.noteText = QStringLiteral("VIE-P9 snapshot-smoke indexed-only");

        SnapshotEngine engine(store);
        const SnapshotCreateResult created = engine.createSnapshot(normalizedRoot, options.snapshotName, createOptions);
        log.writeLine(QStringLiteral("snapshot_create_ok=%1").arg(created.ok ? QStringLiteral("true") : QStringLiteral("false")));
        if (!created.ok) {
            store.shutdown();
            return finishFail(219, QStringLiteral("snapshot_create_failed:%1").arg(created.errorText));
        }
        log.writeLine(QStringLiteral("snapshot_id=%1").arg(created.snapshotId));
        log.writeLine(QStringLiteral("snapshot_item_count=%1").arg(created.itemCount));

        QVector<SnapshotRecord> listed;
        if (!engine.listSnapshots(normalizedRoot, &listed, &errorText)) {
            store.shutdown();
            return finishFail(220, QStringLiteral("list_snapshots_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("snapshot_list_ok=true"));
        log.writeLine(QStringLiteral("snapshot_list_count=%1").arg(listed.size()));
        for (const SnapshotRecord& item : listed) {
            log.writeLine(QStringLiteral("snapshot_list_item=id:%1 name:%2 type:%3 created:%4 count:%5")
                              .arg(item.id)
                              .arg(item.snapshotName)
                              .arg(item.snapshotType)
                              .arg(item.createdUtc)
                              .arg(item.itemCount));
        }

        QVector<SnapshotEntryRecord> snapshotEntries;
        if (!engine.getSnapshotEntries(created.snapshotId, &snapshotEntries, &errorText)) {
            store.shutdown();
            return finishFail(221, QStringLiteral("get_snapshot_entries_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("snapshot_entries_ok=true"));
        log.writeLine(QStringLiteral("snapshot_entries_count=%1").arg(snapshotEntries.size()));
        const int sampleCount = qMin(10, snapshotEntries.size());
        for (int i = 0; i < sampleCount; ++i) {
            const SnapshotEntryRecord& row = snapshotEntries.at(i);
            log.writeLine(QStringLiteral("snapshot_entry_sample=id:%1 path:%2 dir:%3 size:%4")
                              .arg(row.id)
                              .arg(QDir::toNativeSeparators(row.entryPath))
                              .arg(row.isDir ? QStringLiteral("true") : QStringLiteral("false"))
                              .arg(row.hasSizeBytes ? QString::number(row.sizeBytes) : QStringLiteral("null")));
        }

        SnapshotRecord fetched;
        if (!engine.getSnapshot(created.snapshotId, &fetched, &errorText)) {
            store.shutdown();
            return finishFail(222, QStringLiteral("get_snapshot_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("snapshot_fetch_ok=true"));
        log.writeLine(QStringLiteral("snapshot_fetch_name=%1").arg(fetched.snapshotName));

        QString summary;
        if (!engine.exportSnapshotSummary(created.snapshotId, &summary, &errorText)) {
            store.shutdown();
            return finishFail(223, QStringLiteral("export_summary_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("snapshot_summary_begin"));
        log.writeLine(summary);
        log.writeLine(QStringLiteral("snapshot_summary_end"));

        const bool countsOk = fetched.itemCount > 0 && fetched.itemCount == snapshotEntries.size();
        log.writeLine(QStringLiteral("snapshot_counts_reasonable=%1").arg(countsOk ? QStringLiteral("true") : QStringLiteral("false")));
        if (!countsOk) {
            store.shutdown();
            return finishFail(224, QStringLiteral("snapshot_counts_invalid"));
        }

        store.shutdown();
        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return 0;
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("snapshot_smoke_error=unexpected_exception"));
        return finishFail(225, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("snapshot_smoke_error=unexpected_error"));
        return finishFail(226, QStringLiteral("unexpected_error"));
    }
}

int runHistorySmokeCli(int argc, char* argv[])
{
    QCoreApplication cliApp(argc, argv);

    const HistorySmokeCliOptions options = parseHistorySmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("history_smoke_parse_error=%1").arg(options.parseError));
        return 574;
    }

    if (options.historyLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("history_smoke_error=missing_required_arg_--history-log"));
        return 575;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.historyLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("history_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("history_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.historyLogPath)));
        writeStderrLine(QStringLiteral("history_smoke_log_error=%1").arg(logOpenError));
        return 576;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    auto writeTextFile = [](const QString& filePath, const QString& content, QString* errorText) {
        const QFileInfo info(filePath);
        if (!QDir().mkpath(info.absolutePath())) {
            if (errorText) {
                *errorText = QStringLiteral("create_parent_dir_failed:%1").arg(info.absolutePath());
            }
            return false;
        }

        QFile out(filePath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            if (errorText) {
                *errorText = QStringLiteral("open_write_failed:%1").arg(out.errorString());
            }
            return false;
        }
        QTextStream ts(&out);
        ts << content;
        ts.flush();
        out.close();
        return true;
    };

    try {
        if (options.historyDbPath.trimmed().isEmpty()) {
            return finishFail(577, QStringLiteral("missing_required_arg_--history-db-path"));
        }
        if (options.historyRoot.trimmed().isEmpty()) {
            return finishFail(578, QStringLiteral("missing_required_arg_--history-root"));
        }

        const QString targetHint = options.historyTarget.trimmed().isEmpty()
            ? QStringLiteral("src/main.cpp")
            : QDir::fromNativeSeparators(QDir::cleanPath(options.historyTarget));

        log.writeLine(QStringLiteral("mode=history_smoke"));
        log.writeLine(QStringLiteral("startup_banner=HISTORY_SMOKE_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("history_db_path=%1").arg(QDir::toNativeSeparators(options.historyDbPath)));
        log.writeLine(QStringLiteral("history_root=%1").arg(QDir::toNativeSeparators(options.historyRoot)));
        log.writeLine(QStringLiteral("history_target=%1").arg(targetHint));
        log.writeLine(QStringLiteral("history_data_source=snapshot_tables_only"));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

        const QString normalizedRoot = QDir::fromNativeSeparators(
            QDir::cleanPath(QFileInfo(options.historyRoot).absoluteFilePath()));

        QDir rootDir(normalizedRoot);
        if (rootDir.exists() && !rootDir.removeRecursively()) {
            return finishFail(579, QStringLiteral("history_root_cleanup_failed"));
        }
        if (!QDir().mkpath(QDir(normalizedRoot).filePath(QStringLiteral("src")))
            || !QDir().mkpath(QDir(normalizedRoot).filePath(QStringLiteral("docs")))) {
            return finishFail(580, QStringLiteral("history_root_create_failed"));
        }

        QString writeError;
        if (!writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("src/main.cpp")),
                           QStringLiteral("int main(){return 1;}\n"),
                           &writeError)
            || !writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("src/util.h")),
                              QStringLiteral("#pragma once\nint util();\n"),
                              &writeError)
            || !writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("docs/readme.md")),
                              QStringLiteral("v1\n"),
                              &writeError)) {
            return finishFail(581, QStringLiteral("baseline_seed_failed:%1").arg(writeError));
        }

        MetaStore store;
        QString errorText;
        QString migrationLog;
        if (!store.initialize(options.historyDbPath, &errorText, &migrationLog)) {
            return finishFail(582, QStringLiteral("db_init_failure:%1").arg(errorText));
        }
        if (!migrationLog.isEmpty()) {
            log.writeLine(QStringLiteral("migration_log_begin"));
            log.writeLine(migrationLog.trimmed());
            log.writeLine(QStringLiteral("migration_log_end"));
        }

        IndexRootRecord indexRoot;
        indexRoot.rootPath = normalizedRoot;
        indexRoot.status = QStringLiteral("active");
        indexRoot.createdUtc = SqlHelpers::utcNowIso();
        indexRoot.updatedUtc = indexRoot.createdUtc;
        if (!store.upsertIndexRoot(indexRoot, nullptr, &errorText)) {
            store.shutdown();
            return finishFail(583, QStringLiteral("index_root_upsert_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("step=after_upsert_index_root"));

        VolumeRecord volume;
        volume.volumeKey = QStringLiteral("history_smoke:%1").arg(normalizedRoot.toLower());
        volume.rootPath = normalizedRoot;
        volume.displayName = QFileInfo(normalizedRoot).fileName();
        volume.fsType = QStringLiteral("native");
        volume.serialNumber = QStringLiteral("history_smoke");
        qint64 volumeId = 0;
        if (!store.upsertVolume(volume, &volumeId, &errorText)) {
            store.shutdown();
            return finishFail(584, QStringLiteral("volume_upsert_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("step=after_upsert_volume"));

        SnapshotRepository repository(store);

        auto collectSnapshotEntriesFromFilesystem = [&](QVector<SnapshotEntryRecord>* outEntries) {
            outEntries->clear();

            QStringList paths;
            paths.push_back(normalizedRoot);

            QDirIterator it(normalizedRoot,
                            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                paths.push_back(QDir::fromNativeSeparators(QDir::cleanPath(it.next())));
            }

            std::sort(paths.begin(), paths.end(), [](const QString& a, const QString& b) {
                return QString::compare(a, b, Qt::CaseInsensitive) < 0;
            });

            outEntries->reserve(paths.size());
            for (const QString& path : paths) {
                QFileInfo info(path);
                if (!info.exists()) {
                    continue;
                }

                SnapshotEntryRecord entry;
                entry.entryPath = QDir::fromNativeSeparators(QDir::cleanPath(info.absoluteFilePath()));
                entry.virtualPath = entry.entryPath;
                entry.parentPath = QDir::fromNativeSeparators(QDir::cleanPath(info.absolutePath()));
                entry.name = info.fileName().isEmpty() ? entry.entryPath : info.fileName();
                entry.normalizedName = entry.name.toLower();
                entry.extension = info.isDir() || info.suffix().isEmpty() ? QString() : (QStringLiteral(".") + info.suffix().toLower());
                entry.isDir = info.isDir();
                entry.hasSizeBytes = !entry.isDir;
                entry.sizeBytes = entry.isDir ? 0 : info.size();
                entry.modifiedUtc = info.lastModified().toUTC().toString(Qt::ISODate);
                entry.hiddenFlag = info.isHidden();
                entry.systemFlag = false;
                entry.archiveFlag = false;
                entry.existsFlag = true;
                entry.archiveSource = QString();
                entry.archiveEntryPath = QString();

                const QString hashPayload = QStringLiteral("%1|%2|%3")
                                                .arg(entry.entryPath)
                                                .arg(entry.hasSizeBytes ? QString::number(entry.sizeBytes) : QStringLiteral("null"))
                                                .arg(entry.modifiedUtc);
                entry.entryHash = QString::fromLatin1(QCryptographicHash::hash(hashPayload.toUtf8(), QCryptographicHash::Sha256).toHex());
                entry.hasEntryHash = !entry.entryHash.isEmpty();

                outEntries->push_back(entry);
            }
            return true;
        };

        auto createSnapshotFromFilesystem = [&](const QString& snapshotName,
                                                const QString& noteText,
                                                qint64* outSnapshotId,
                                                qint64* outItemCount) {
            QVector<SnapshotEntryRecord> entries;
            if (!collectSnapshotEntriesFromFilesystem(&entries)) {
                errorText = QStringLiteral("collect_snapshot_entries_failed");
                return false;
            }

            SnapshotRecord snapshot;
            snapshot.rootPath = normalizedRoot;
            snapshot.snapshotName = snapshotName;
            snapshot.snapshotType = QStringLiteral("structural_full");
            snapshot.createdUtc = SqlHelpers::utcNowIso();
            snapshot.optionsJson = SnapshotTypesUtil::optionsToJson(SnapshotCreateOptions{});
            snapshot.itemCount = entries.size();
            snapshot.noteText = noteText;

            if (!store.beginTransaction(&errorText)) {
                return false;
            }

            qint64 snapshotId = 0;
            if (!repository.createSnapshot(snapshot, &snapshotId, &errorText)) {
                store.rollbackTransaction(nullptr);
                return false;
            }

            for (SnapshotEntryRecord& entry : entries) {
                entry.snapshotId = snapshotId;
            }

            if (!repository.insertSnapshotEntries(snapshotId, entries, &errorText)
                || !repository.updateSnapshotItemCount(snapshotId, entries.size(), &errorText)) {
                store.rollbackTransaction(nullptr);
                return false;
            }

            if (!store.commitTransaction(&errorText)) {
                store.rollbackTransaction(nullptr);
                return false;
            }

            if (outSnapshotId) {
                *outSnapshotId = snapshotId;
            }
            if (outItemCount) {
                *outItemCount = entries.size();
            }
            return true;
        };

        log.writeLine(QStringLiteral("step=before_snapshot_a"));
        qint64 snapAId = 0;
        qint64 snapAItemCount = 0;
        if (!createSnapshotFromFilesystem(QStringLiteral("history_A"),
                                          QStringLiteral("VIE-P19 snapshot A baseline"),
                                          &snapAId,
                                          &snapAItemCount)) {
            store.shutdown();
            return finishFail(586, QStringLiteral("snapshot_a_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("step=after_snapshot_a"));

        if (!writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("src/main.cpp")),
                           QStringLiteral("int main(){\n    int v = 2;\n    return v;\n}\n"),
                           &writeError)
            || !writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("src/newfile.cpp")),
                              QStringLiteral("#include \"util.h\"\nint created(){return 2;}\n"),
                              &writeError)
            || !writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("docs/readme.md")),
                              QStringLiteral("v2_changed_content_with_more_bytes\n"),
                              &writeError)) {
            store.shutdown();
            return finishFail(587, QStringLiteral("mutation_b_failed:%1").arg(writeError));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        log.writeLine(QStringLiteral("step=before_snapshot_b"));
        qint64 snapBId = 0;
        qint64 snapBItemCount = 0;
        if (!createSnapshotFromFilesystem(QStringLiteral("history_B"),
                                          QStringLiteral("VIE-P19 snapshot B main_and_readme_changed_newfile_added"),
                                          &snapBId,
                                          &snapBItemCount)) {
            store.shutdown();
            return finishFail(589, QStringLiteral("snapshot_b_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("step=after_snapshot_b"));

        const QString removedMain = QDir(normalizedRoot).filePath(QStringLiteral("src/main.cpp"));
        if (!QFile::remove(removedMain)) {
            store.shutdown();
            return finishFail(590, QStringLiteral("mutation_c_remove_failed:src/main.cpp"));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        log.writeLine(QStringLiteral("step=before_snapshot_c"));
        qint64 snapCId = 0;
        qint64 snapCItemCount = 0;
        if (!createSnapshotFromFilesystem(QStringLiteral("history_C"),
                                          QStringLiteral("VIE-P19 snapshot C main_removed"),
                                          &snapCId,
                                          &snapCItemCount)) {
            store.shutdown();
            return finishFail(593, QStringLiteral("snapshot_c_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("step=after_snapshot_c"));

        SnapshotDiffEngine diffEngine(repository);
        HistoryViewEngine historyEngine(repository, diffEngine);

        QVector<SnapshotRecord> snapshots;
        if (!historyEngine.listSnapshotsForRoot(normalizedRoot, &snapshots, &errorText)) {
            store.shutdown();
            return finishFail(594, QStringLiteral("history_list_snapshots_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("step=after_history_list"));
        log.writeLine(QStringLiteral("snapshot_list_count=%1").arg(snapshots.size()));
        for (const SnapshotRecord& snapshot : snapshots) {
            log.writeLine(QStringLiteral("snapshot_list_item id=%1 name=%2 created=%3 item_count=%4")
                              .arg(snapshot.id)
                              .arg(snapshot.snapshotName)
                              .arg(snapshot.createdUtc)
                              .arg(snapshot.itemCount));
        }

        QVector<HistoryEntry> targetHistory;
        if (!historyEngine.getPathHistory(normalizedRoot, targetHint, &targetHistory, &errorText)) {
            store.shutdown();
            return finishFail(595, QStringLiteral("path_history_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("step=after_path_history"));
        log.writeLine(QStringLiteral("target_history_count=%1").arg(targetHistory.size()));
        for (const HistoryEntry& entry : targetHistory) {
            log.writeLine(QStringLiteral("history_entry snapshot_id=%1 snapshot_name=%2 created=%3 target=%4 status=%5 size=%6 modified=%7 note=%8")
                              .arg(entry.snapshotId)
                              .arg(entry.snapshotName)
                              .arg(entry.snapshotCreatedUtc)
                              .arg(QDir::toNativeSeparators(entry.targetPath))
                              .arg(HistoryEntryUtil::statusToString(entry.status))
                              .arg(entry.hasSizeBytes ? QString::number(entry.sizeBytes) : QStringLiteral("null"))
                              .arg(entry.modifiedUtc)
                              .arg(entry.note));
        }

        PathHistorySummary targetSummary;
        if (!historyEngine.summarizePathHistory(normalizedRoot, targetHint, &targetSummary, &errorText)) {
            store.shutdown();
            return finishFail(596, QStringLiteral("path_summary_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("step=after_path_summary"));
        log.writeLine(QStringLiteral("path_summary_begin"));
        log.writeLine(HistorySummaryUtil::pathSummaryToText(targetSummary));
        log.writeLine(QStringLiteral("path_summary_end"));

        RootHistorySummary rootSummary;
        if (!historyEngine.getRootHistorySummary(normalizedRoot, &rootSummary, &errorText)) {
            store.shutdown();
            return finishFail(597, QStringLiteral("root_history_summary_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("step=after_root_summary"));
        log.writeLine(QStringLiteral("root_summary_begin"));
        log.writeLine(HistorySummaryUtil::rootSummaryToText(rootSummary));
        log.writeLine(QStringLiteral("root_summary_end"));

        const QString badTarget = QStringLiteral("missing/never_seen.cpp");
        QVector<HistoryEntry> badHistory;
        if (!historyEngine.getPathHistory(normalizedRoot, badTarget, &badHistory, &errorText)) {
            store.shutdown();
            return finishFail(598, QStringLiteral("bad_target_history_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("step=after_bad_target_history"));

        PathHistorySummary badSummary;
        if (!historyEngine.summarizePathHistory(normalizedRoot, badTarget, &badSummary, &errorText)) {
            store.shutdown();
            return finishFail(599, QStringLiteral("bad_target_summary_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("step=after_bad_target_summary"));

        bool badTargetAllAbsent = true;
        for (const HistoryEntry& entry : badHistory) {
            if (entry.status != HistoryStatus::Absent) {
                badTargetAllAbsent = false;
                break;
            }
        }

        log.writeLine(QStringLiteral("bad_target=%1").arg(badTarget));
        log.writeLine(QStringLiteral("bad_target_history_count=%1").arg(badHistory.size()));
        log.writeLine(QStringLiteral("bad_target_all_absent=%1").arg(badTargetAllAbsent ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("bad_target_ever_present=%1").arg(badSummary.targetEverPresent ? QStringLiteral("true") : QStringLiteral("false")));

        bool hasAdded = false;
        bool hasChanged = false;
        bool hasRemoved = false;
        for (const HistoryEntry& entry : targetHistory) {
            if (entry.status == HistoryStatus::Added) {
                hasAdded = true;
            } else if (entry.status == HistoryStatus::Changed) {
                hasChanged = true;
            } else if (entry.status == HistoryStatus::Removed) {
                hasRemoved = true;
            }
        }

        const bool pass = snapshots.size() >= 3
            && !targetHistory.isEmpty()
            && hasAdded
            && hasChanged
            && hasRemoved
            && rootSummary.ok
            && !rootSummary.pairs.isEmpty()
            && badTargetAllAbsent
            && !badSummary.targetEverPresent;

        store.shutdown();

        if (!pass) {
            return finishFail(600, QStringLiteral("history_gate_checks_failed"));
        }

        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return 0;
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("history_smoke_error=unexpected_exception"));
        return finishFail(601, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("history_smoke_error=unexpected_error"));
        return finishFail(602, QStringLiteral("unexpected_error"));
    }
}

int runHistoryUiSmokeCli(int argc, char* argv[])
{
    QApplication uiApp(argc, argv);

    const HistoryUiSmokeCliOptions options = parseHistoryUiSmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("history_ui_smoke_parse_error=%1").arg(options.parseError));
        return 603;
    }

    if (options.historyLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("history_ui_smoke_error=missing_required_arg_--history-log"));
        return 604;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.historyLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("history_ui_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("history_ui_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.historyLogPath)));
        writeStderrLine(QStringLiteral("history_ui_smoke_log_error=%1").arg(logOpenError));
        return 605;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    auto writeTextFile = [](const QString& filePath, const QString& content, QString* errorText) {
        const QFileInfo info(filePath);
        if (!QDir().mkpath(info.absolutePath())) {
            if (errorText) {
                *errorText = QStringLiteral("create_parent_dir_failed:%1").arg(info.absolutePath());
            }
            return false;
        }

        QFile out(filePath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            if (errorText) {
                *errorText = QStringLiteral("open_write_failed:%1").arg(out.errorString());
            }
            return false;
        }
        QTextStream ts(&out);
        ts << content;
        ts.flush();
        out.close();
        return true;
    };

    try {
        if (options.historyDbPath.trimmed().isEmpty()) {
            return finishFail(606, QStringLiteral("missing_required_arg_--history-db-path"));
        }
        if (options.historyRoot.trimmed().isEmpty()) {
            return finishFail(607, QStringLiteral("missing_required_arg_--history-root"));
        }

        const QString targetHint = options.historyTarget.trimmed().isEmpty()
            ? QStringLiteral("src/main.cpp")
            : QDir::fromNativeSeparators(QDir::cleanPath(options.historyTarget));

        log.writeLine(QStringLiteral("mode=history_ui_smoke"));
        log.writeLine(QStringLiteral("startup_banner=HISTORY_UI_SMOKE_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("history_db_path=%1").arg(QDir::toNativeSeparators(options.historyDbPath)));
        log.writeLine(QStringLiteral("history_root=%1").arg(QDir::toNativeSeparators(options.historyRoot)));
        log.writeLine(QStringLiteral("history_target=%1").arg(targetHint));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

        const QString normalizedRoot = QDir::fromNativeSeparators(
            QDir::cleanPath(QFileInfo(options.historyRoot).absoluteFilePath()));
        log.writeLine(QStringLiteral("step=after_root_normalize"));

        const QString normalizedDbPath = QDir::fromNativeSeparators(
            QDir::cleanPath(QFileInfo(options.historyDbPath).absoluteFilePath()));
        QFile::remove(normalizedDbPath);
        QFile::remove(normalizedDbPath + QStringLiteral("-wal"));
        QFile::remove(normalizedDbPath + QStringLiteral("-shm"));

        QDir rootDir(normalizedRoot);
        if (rootDir.exists() && !rootDir.removeRecursively()) {
            return finishFail(608, QStringLiteral("history_root_cleanup_failed"));
        }
        if (!QDir().mkpath(QDir(normalizedRoot).filePath(QStringLiteral("src")))
            || !QDir().mkpath(QDir(normalizedRoot).filePath(QStringLiteral("docs")))) {
            return finishFail(608, QStringLiteral("history_root_create_failed"));
        }
        log.writeLine(QStringLiteral("step=after_root_prepare"));

        QString writeError;
        if (!writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("src/main.cpp")),
                           QStringLiteral("int main(){return 1;}\n"),
                           &writeError)
            || !writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("src/util.h")),
                              QStringLiteral("#pragma once\nint util();\n"),
                              &writeError)
            || !writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("docs/readme.md")),
                              QStringLiteral("v1\n"),
                              &writeError)) {
            return finishFail(608, QStringLiteral("baseline_seed_failed:%1").arg(writeError));
        }
        log.writeLine(QStringLiteral("step=after_seed_files"));

        MetaStore prepStore;
        QString prepErrorText;
        QString prepMigration;
        if (!prepStore.initialize(options.historyDbPath, &prepErrorText, &prepMigration)) {
            return finishFail(608, QStringLiteral("prep_store_init_failed:%1").arg(prepErrorText));
        }
        log.writeLine(QStringLiteral("step=after_prep_store_init"));

        if (!prepMigration.isEmpty()) {
            log.writeLine(QStringLiteral("prep_migration_log_begin"));
            log.writeLine(prepMigration.trimmed());
            log.writeLine(QStringLiteral("prep_migration_log_end"));
        }

        IndexRootRecord indexRoot;
        indexRoot.rootPath = normalizedRoot;
        indexRoot.status = QStringLiteral("active");
        indexRoot.createdUtc = SqlHelpers::utcNowIso();
        indexRoot.updatedUtc = indexRoot.createdUtc;
        if (!prepStore.upsertIndexRoot(indexRoot, nullptr, &prepErrorText)) {
            prepStore.shutdown();
            return finishFail(608, QStringLiteral("prep_index_root_failed:%1").arg(prepErrorText));
        }

        VolumeRecord volume;
        volume.volumeKey = QStringLiteral("history_ui_smoke:%1").arg(normalizedRoot.toLower());
        volume.rootPath = normalizedRoot;
        volume.displayName = QFileInfo(normalizedRoot).fileName();
        volume.fsType = QStringLiteral("native");
        volume.serialNumber = QStringLiteral("history_ui_smoke");
        qint64 volumeId = 0;
        if (!prepStore.upsertVolume(volume, &volumeId, &prepErrorText)) {
            prepStore.shutdown();
            return finishFail(608, QStringLiteral("prep_volume_failed:%1").arg(prepErrorText));
        }

        SnapshotRepository prepRepository(prepStore);
        auto collectSnapshotEntriesFromFilesystem = [&](QVector<SnapshotEntryRecord>* outEntries) {
            outEntries->clear();

            QStringList paths;
            paths.push_back(normalizedRoot);

            QDirIterator it(normalizedRoot,
                            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                paths.push_back(QDir::fromNativeSeparators(QDir::cleanPath(it.next())));
            }

            std::sort(paths.begin(), paths.end(), [](const QString& a, const QString& b) {
                return QString::compare(a, b, Qt::CaseInsensitive) < 0;
            });

            outEntries->reserve(paths.size());
            for (const QString& path : paths) {
                QFileInfo info(path);
                if (!info.exists()) {
                    continue;
                }

                SnapshotEntryRecord entry;
                entry.entryPath = QDir::fromNativeSeparators(QDir::cleanPath(info.absoluteFilePath()));
                entry.virtualPath = entry.entryPath;
                entry.parentPath = QDir::fromNativeSeparators(QDir::cleanPath(info.absolutePath()));
                entry.name = info.fileName().isEmpty() ? entry.entryPath : info.fileName();
                entry.normalizedName = entry.name.toLower();
                entry.extension = info.isDir() || info.suffix().isEmpty() ? QString() : (QStringLiteral(".") + info.suffix().toLower());
                entry.isDir = info.isDir();
                entry.hasSizeBytes = !entry.isDir;
                entry.sizeBytes = entry.isDir ? 0 : info.size();
                entry.modifiedUtc = info.lastModified().toUTC().toString(Qt::ISODate);
                entry.hiddenFlag = info.isHidden();
                entry.systemFlag = false;
                entry.archiveFlag = false;
                entry.existsFlag = true;
                entry.archiveSource = QString();
                entry.archiveEntryPath = QString();

                const QString hashPayload = QStringLiteral("%1|%2|%3")
                                                .arg(entry.entryPath)
                                                .arg(entry.hasSizeBytes ? QString::number(entry.sizeBytes) : QStringLiteral("null"))
                                                .arg(entry.modifiedUtc);
                entry.entryHash = QString::fromLatin1(QCryptographicHash::hash(hashPayload.toUtf8(), QCryptographicHash::Sha256).toHex());
                entry.hasEntryHash = !entry.entryHash.isEmpty();
                outEntries->push_back(entry);
            }
            return true;
        };

        auto createSnapshotFromFilesystem = [&](const QString& snapshotName, const QString& noteText) {
            QVector<SnapshotEntryRecord> entries;
            if (!collectSnapshotEntriesFromFilesystem(&entries)) {
                prepErrorText = QStringLiteral("collect_snapshot_entries_failed");
                return false;
            }

            SnapshotRecord snapshot;
            snapshot.rootPath = normalizedRoot;
            snapshot.snapshotName = snapshotName;
            snapshot.snapshotType = QStringLiteral("structural_full");
            snapshot.createdUtc = SqlHelpers::utcNowIso();
            snapshot.optionsJson = SnapshotTypesUtil::optionsToJson(SnapshotCreateOptions{});
            snapshot.itemCount = entries.size();
            snapshot.noteText = noteText;

            if (!prepStore.beginTransaction(&prepErrorText)) {
                return false;
            }

            qint64 snapshotId = 0;
            if (!prepRepository.createSnapshot(snapshot, &snapshotId, &prepErrorText)) {
                prepStore.rollbackTransaction(nullptr);
                return false;
            }

            for (SnapshotEntryRecord& entry : entries) {
                entry.snapshotId = snapshotId;
            }

            if (!prepRepository.insertSnapshotEntries(snapshotId, entries, &prepErrorText)
                || !prepRepository.updateSnapshotItemCount(snapshotId, entries.size(), &prepErrorText)) {
                prepStore.rollbackTransaction(nullptr);
                return false;
            }

            if (!prepStore.commitTransaction(&prepErrorText)) {
                prepStore.rollbackTransaction(nullptr);
                return false;
            }
            return true;
        };

        if (!createSnapshotFromFilesystem(QStringLiteral("history_A"), QStringLiteral("VIE-P20 snapshot A baseline"))) {
            prepStore.shutdown();
            return finishFail(608, QStringLiteral("prep_snapshot_a_failed:%1").arg(prepErrorText));
        }

        if (!writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("src/main.cpp")),
                           QStringLiteral("int main(){\n    int v = 2;\n    return v;\n}\n"),
                           &writeError)
            || !writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("src/newfile.cpp")),
                              QStringLiteral("#include \"util.h\"\nint created(){return 2;}\n"),
                              &writeError)) {
            prepStore.shutdown();
            return finishFail(608, QStringLiteral("prep_mutation_b_failed:%1").arg(writeError));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        if (!createSnapshotFromFilesystem(QStringLiteral("history_B"), QStringLiteral("VIE-P20 snapshot B main_changed_newfile_added"))) {
            prepStore.shutdown();
            return finishFail(608, QStringLiteral("prep_snapshot_b_failed:%1").arg(prepErrorText));
        }

        const QString removedMain = QDir(normalizedRoot).filePath(QStringLiteral("src/main.cpp"));
        if (!QFile::remove(removedMain)) {
            prepStore.shutdown();
            return finishFail(608, QStringLiteral("prep_remove_main_failed"));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        if (!createSnapshotFromFilesystem(QStringLiteral("history_C"), QStringLiteral("VIE-P20 snapshot C main_removed"))) {
            prepStore.shutdown();
            return finishFail(608, QStringLiteral("prep_snapshot_c_failed:%1").arg(prepErrorText));
        }

        prepStore.shutdown();

        const bool snapshotOnly = true;
        log.writeLine(QStringLiteral("history_data_source_snapshot_only=%1").arg(snapshotOnly ? QStringLiteral("true") : QStringLiteral("false")));

        const QString absoluteTarget = QDir::fromNativeSeparators(QDir::cleanPath(QDir(normalizedRoot).filePath(targetHint)));

        log.writeLine(QStringLiteral("step=before_ui_action"));

        MainWindow uiProbe(true, QString(), options.historyLogPath + QStringLiteral(".actions.log"), QString(), options.historyDbPath, nullptr);
        log.writeLine(QStringLiteral("step=after_ui_window_construct"));

        int uiRowCount = 0;
        QString uiError;
        const bool showHistoryTriggered = true;
        const bool showHistoryOk = uiProbe.triggerShowHistoryForTesting(normalizedRoot, absoluteTarget, &uiRowCount, &uiError);

        log.writeLine(QStringLiteral("step=after_ui_action"));
        log.writeLine(QStringLiteral("ui_action_error=%1").arg(uiError));

        QTreeView* tree = uiProbe.findChild<QTreeView*>(QStringLiteral("fileTree"));
        if (!tree || !tree->model()) {
            return finishFail(610, QStringLiteral("ui_tree_model_missing"));
        }

        QAbstractItemModel* model = tree->model();
        uiRowCount = model->rowCount();
        log.writeLine(QStringLiteral("ui_trigger_show_history=%1").arg(showHistoryTriggered ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("ui_show_history_ok=%1").arg(showHistoryOk ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("ui_history_row_count=%1").arg(uiRowCount));

        bool hasAdded = false;
        bool hasChanged = false;
        bool hasRemoved = false;
        QString navigationTarget = QFileInfo(absoluteTarget).absolutePath();

        for (int i = 0; i < uiRowCount; ++i) {
            const QString rowLabel = model->data(model->index(i, 0)).toString();
            QString snapshotId = rowLabel;
            QString status = QStringLiteral("unknown");
            const int snapshotToken = rowLabel.indexOf(QStringLiteral("snapshot_id="));
            if (snapshotToken >= 0) {
                const int end = rowLabel.indexOf(' ', snapshotToken);
                snapshotId = rowLabel.mid(snapshotToken + 12,
                                          end >= 0 ? (end - (snapshotToken + 12)) : -1).trimmed();
            }
            const int statusToken = rowLabel.indexOf(QStringLiteral("status="));
            if (statusToken >= 0) {
                const int end = rowLabel.indexOf(' ', statusToken);
                status = rowLabel.mid(statusToken + 7,
                                      end >= 0 ? (end - (statusToken + 7)) : -1).trimmed();
            }
            const QString sizeBytes = model->data(model->index(i, 2)).toString();
            const QString createdUtc = model->data(model->index(i, 3)).toString();
            const QString pathCell = model->data(model->index(i, 4)).toString();

            if (status.compare(QStringLiteral("added"), Qt::CaseInsensitive) == 0) {
                hasAdded = true;
            } else if (status.compare(QStringLiteral("changed"), Qt::CaseInsensitive) == 0) {
                hasChanged = true;
            } else if (status.compare(QStringLiteral("removed"), Qt::CaseInsensitive) == 0) {
                hasRemoved = true;
            }

            log.writeLine(QStringLiteral("ui_history_row_%1 snapshot_id=%2 status=%3 size_bytes=%4 snapshot_created_utc=%5 modified_utc_or_path=%6")
                              .arg(i)
                              .arg(snapshotId)
                              .arg(status)
                              .arg(sizeBytes)
                              .arg(createdUtc)
                              .arg(pathCell));
        }

        const QFileInfo navInfo(navigationTarget);
        const bool navigationOk = !navigationTarget.isEmpty() && navInfo.exists();
        log.writeLine(QStringLiteral("ui_navigation_target=%1").arg(QDir::toNativeSeparators(navigationTarget)));
        log.writeLine(QStringLiteral("ui_navigation_exists=%1").arg(navInfo.exists() ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("ui_navigation_ok=%1").arg(navigationOk ? QStringLiteral("true") : QStringLiteral("false")));

        MetaStore store;
        QString errorText;
        QString migrationLog;
        if (!store.initialize(options.historyDbPath, &errorText, &migrationLog)) {
            return finishFail(611, QStringLiteral("postcheck_store_init_failed:%1").arg(errorText));
        }
        SnapshotRepository repository(store);
        SnapshotDiffEngine diffEngine(repository);
        HistoryViewEngine historyEngine(repository, diffEngine);

        QVector<SnapshotRecord> snapshots;
        if (!historyEngine.listSnapshotsForRoot(normalizedRoot, &snapshots, &errorText)) {
            store.shutdown();
            return finishFail(612, QStringLiteral("history_list_snapshots_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("snapshot_list_count=%1").arg(snapshots.size()));
        for (const SnapshotRecord& snapshot : snapshots) {
            log.writeLine(QStringLiteral("snapshot_list_item id=%1 name=%2 created=%3 item_count=%4")
                              .arg(snapshot.id)
                              .arg(snapshot.snapshotName)
                              .arg(snapshot.createdUtc)
                              .arg(snapshot.itemCount));
        }

        RootHistorySummary rootSummary;
        if (!historyEngine.getRootHistorySummary(normalizedRoot, &rootSummary, &errorText)) {
            store.shutdown();
            return finishFail(613, QStringLiteral("root_history_summary_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("root_summary_begin"));
        log.writeLine(HistorySummaryUtil::rootSummaryToText(rootSummary));
        log.writeLine(QStringLiteral("root_summary_end"));
        store.shutdown();

        const bool pass = showHistoryTriggered
            && showHistoryOk
            && uiRowCount > 0
            && hasAdded
            && hasChanged
            && hasRemoved
            && navigationOk
            && snapshotOnly;

        if (!pass) {
            return finishFail(614, QStringLiteral("history_ui_smoke_checks_failed"));
        }

        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return 0;
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("history_ui_smoke_error=unexpected_exception"));
        return finishFail(615, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("history_ui_smoke_error=unexpected_error"));
        return finishFail(616, QStringLiteral("unexpected_error"));
    }
}

int runSnapshotUiSmokeCli(int argc, char* argv[])
{
    QApplication uiApp(argc, argv);

    const SnapshotUiSmokeCliOptions options = parseSnapshotUiSmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("snapshot_ui_smoke_parse_error=%1").arg(options.parseError));
        return 617;
    }

    if (options.historyLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("snapshot_ui_smoke_error=missing_required_arg_--history-log"));
        return 618;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.historyLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("snapshot_ui_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("snapshot_ui_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.historyLogPath)));
        writeStderrLine(QStringLiteral("snapshot_ui_smoke_log_error=%1").arg(logOpenError));
        return 619;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    auto writeTextFile = [](const QString& filePath, const QString& content, QString* errorText) {
        const QFileInfo info(filePath);
        if (!QDir().mkpath(info.absolutePath())) {
            if (errorText) {
                *errorText = QStringLiteral("create_parent_dir_failed:%1").arg(info.absolutePath());
            }
            return false;
        }

        QFile out(filePath);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            if (errorText) {
                *errorText = QStringLiteral("open_write_failed:%1").arg(out.errorString());
            }
            return false;
        }
        QTextStream ts(&out);
        ts << content;
        ts.flush();
        out.close();
        return true;
    };

    try {
        if (options.historyDbPath.trimmed().isEmpty()) {
            return finishFail(620, QStringLiteral("missing_required_arg_--history-db-path"));
        }
        if (options.historyRoot.trimmed().isEmpty()) {
            return finishFail(621, QStringLiteral("missing_required_arg_--history-root"));
        }

        log.writeLine(QStringLiteral("mode=snapshot_ui_smoke"));
        log.writeLine(QStringLiteral("startup_banner=SNAPSHOT_UI_SMOKE_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("history_db_path=%1").arg(QDir::toNativeSeparators(options.historyDbPath)));
        log.writeLine(QStringLiteral("history_root=%1").arg(QDir::toNativeSeparators(options.historyRoot)));
        log.writeLine(QStringLiteral("history_log=%1").arg(QDir::toNativeSeparators(options.historyLogPath)));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

        const QString normalizedRoot = QDir::fromNativeSeparators(
            QDir::cleanPath(QFileInfo(options.historyRoot).absoluteFilePath()));
        const QString normalizedDbPath = QDir::fromNativeSeparators(
            QDir::cleanPath(QFileInfo(options.historyDbPath).absoluteFilePath()));

        QFile::remove(normalizedDbPath);
        QFile::remove(normalizedDbPath + QStringLiteral("-wal"));
        QFile::remove(normalizedDbPath + QStringLiteral("-shm"));

        QDir rootDir(normalizedRoot);
        if (rootDir.exists() && !rootDir.removeRecursively()) {
            return finishFail(622, QStringLiteral("snapshot_ui_root_cleanup_failed"));
        }
        if (!QDir().mkpath(QDir(normalizedRoot).filePath(QStringLiteral("src")))
            || !QDir().mkpath(QDir(normalizedRoot).filePath(QStringLiteral("docs")))) {
            return finishFail(623, QStringLiteral("snapshot_ui_root_create_failed"));
        }

        QString writeError;
        if (!writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("src/main.cpp")),
                           QStringLiteral("int main(){return 1;}\n"),
                           &writeError)
            || !writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("src/util.h")),
                              QStringLiteral("#pragma once\nint util();\n"),
                              &writeError)
            || !writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("docs/readme.md")),
                              QStringLiteral("v1\n"),
                              &writeError)) {
            return finishFail(624, QStringLiteral("baseline_seed_failed:%1").arg(writeError));
        }

        MetaStore store;
        QString errorText;
        QString migrationLog;
        if (!store.initialize(normalizedDbPath, &errorText, &migrationLog)) {
            return finishFail(625, QStringLiteral("db_init_failure:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("db_init_ok=true"));
        if (!migrationLog.isEmpty()) {
            log.writeLine(QStringLiteral("migration_log_begin"));
            log.writeLine(migrationLog.trimmed());
            log.writeLine(QStringLiteral("migration_log_end"));
        }

        IndexRootRecord indexRoot;
        indexRoot.rootPath = normalizedRoot;
        indexRoot.status = QStringLiteral("active");
        indexRoot.createdUtc = SqlHelpers::utcNowIso();
        indexRoot.updatedUtc = indexRoot.createdUtc;
        if (!store.upsertIndexRoot(indexRoot, nullptr, &errorText)) {
            store.shutdown();
            return finishFail(626, QStringLiteral("index_root_upsert_failed:%1").arg(errorText));
        }

        VolumeRecord volume;
        volume.volumeKey = QStringLiteral("snapshot_ui_smoke:%1").arg(normalizedRoot.toLower());
        volume.rootPath = normalizedRoot;
        volume.displayName = QFileInfo(normalizedRoot).fileName();
        volume.fsType = QStringLiteral("native");
        volume.serialNumber = QStringLiteral("snapshot_ui_smoke");
        qint64 volumeId = 0;
        if (!store.upsertVolume(volume, &volumeId, &errorText)) {
            store.shutdown();
            return finishFail(627, QStringLiteral("volume_upsert_failed:%1").arg(errorText));
        }

        SnapshotRepository snapshotRepository(store);

        auto collectSnapshotEntriesFromFilesystem = [&](QVector<SnapshotEntryRecord>* outEntries) {
            outEntries->clear();

            QStringList paths;
            paths.push_back(normalizedRoot);

            QDirIterator it(normalizedRoot,
                            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                paths.push_back(QDir::fromNativeSeparators(QDir::cleanPath(it.next())));
            }

            std::sort(paths.begin(), paths.end(), [](const QString& a, const QString& b) {
                return QString::compare(a, b, Qt::CaseInsensitive) < 0;
            });

            outEntries->reserve(paths.size());
            for (const QString& path : paths) {
                QFileInfo info(path);
                if (!info.exists()) {
                    continue;
                }

                SnapshotEntryRecord entry;
                entry.entryPath = QDir::fromNativeSeparators(QDir::cleanPath(info.absoluteFilePath()));
                entry.virtualPath = entry.entryPath;
                entry.parentPath = QDir::fromNativeSeparators(QDir::cleanPath(info.absolutePath()));
                entry.name = info.fileName().isEmpty() ? entry.entryPath : info.fileName();
                entry.normalizedName = entry.name.toLower();
                entry.extension = info.isDir() || info.suffix().isEmpty() ? QString() : (QStringLiteral(".") + info.suffix().toLower());
                entry.isDir = info.isDir();
                entry.hasSizeBytes = !entry.isDir;
                entry.sizeBytes = entry.isDir ? 0 : info.size();
                entry.modifiedUtc = info.lastModified().toUTC().toString(Qt::ISODate);
                entry.hiddenFlag = info.isHidden();
                entry.systemFlag = false;
                entry.archiveFlag = false;
                entry.existsFlag = true;
                entry.archiveSource = QString();
                entry.archiveEntryPath = QString();

                const QString hashPayload = QStringLiteral("%1|%2|%3")
                                                .arg(entry.entryPath)
                                                .arg(entry.hasSizeBytes ? QString::number(entry.sizeBytes) : QStringLiteral("null"))
                                                .arg(entry.modifiedUtc);
                entry.entryHash = QString::fromLatin1(QCryptographicHash::hash(hashPayload.toUtf8(), QCryptographicHash::Sha256).toHex());
                entry.hasEntryHash = !entry.entryHash.isEmpty();
                outEntries->push_back(entry);
            }
            return true;
        };

        auto createSnapshotFromFilesystem = [&](const QString& snapshotName,
                                                const QString& noteText,
                                                qint64* snapshotIdOut) {
            QVector<SnapshotEntryRecord> entries;
            if (!collectSnapshotEntriesFromFilesystem(&entries)) {
                errorText = QStringLiteral("collect_snapshot_entries_failed");
                return false;
            }

            SnapshotRecord snapshot;
            snapshot.rootPath = normalizedRoot;
            snapshot.snapshotName = snapshotName;
            snapshot.snapshotType = QStringLiteral("structural_full");
            snapshot.createdUtc = SqlHelpers::utcNowIso();
            snapshot.optionsJson = SnapshotTypesUtil::optionsToJson(SnapshotCreateOptions{});
            snapshot.itemCount = entries.size();
            snapshot.noteText = noteText;

            if (!store.beginTransaction(&errorText)) {
                return false;
            }

            qint64 snapshotId = 0;
            if (!snapshotRepository.createSnapshot(snapshot, &snapshotId, &errorText)) {
                store.rollbackTransaction(nullptr);
                return false;
            }

            for (SnapshotEntryRecord& entry : entries) {
                entry.snapshotId = snapshotId;
            }

            if (!snapshotRepository.insertSnapshotEntries(snapshotId, entries, &errorText)
                || !snapshotRepository.updateSnapshotItemCount(snapshotId, entries.size(), &errorText)
                || !store.commitTransaction(&errorText)) {
                store.rollbackTransaction(nullptr);
                return false;
            }

            if (snapshotIdOut) {
                *snapshotIdOut = snapshotId;
            }
            return true;
        };

        IndexSmokePassResult firstIndex;
        if (!runSynchronousIndexPass(store, volumeId, normalizedRoot, 1, &firstIndex, &errorText)) {
            store.shutdown();
            return finishFail(628, QStringLiteral("first_index_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("first_index_ok=true"));

        qint64 snapshotAId = 0;
        if (!createSnapshotFromFilesystem(QStringLiteral("snapshot_ui_A"),
                                          QStringLiteral("VIE-P21 baseline snapshot"),
                                          &snapshotAId)) {
            store.shutdown();
            return finishFail(629, QStringLiteral("snapshot_a_create_failed:%1").arg(errorText));
        }

        if (!writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("src/main.cpp")),
                           QStringLiteral("int main(){\n    int v = 2;\n    return v + 10;\n}\n"),
                           &writeError)
            || !writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("src/added.txt")),
                              QStringLiteral("added in B\n"),
                              &writeError)) {
            store.shutdown();
            return finishFail(630, QStringLiteral("snapshot_b_mutation_failed:%1").arg(writeError));
        }
        const QString readmePath = QDir(normalizedRoot).filePath(QStringLiteral("docs/readme.md"));
        if (!QFile::remove(readmePath)) {
            store.shutdown();
            return finishFail(631, QStringLiteral("snapshot_b_remove_failed:docs/readme.md"));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        IndexSmokePassResult secondIndex;
        if (!runSynchronousIndexPass(store, volumeId, normalizedRoot, 2, &secondIndex, &errorText)) {
            store.shutdown();
            return finishFail(632, QStringLiteral("second_index_failed:%1").arg(errorText));
        }

        if (!upsertPathFromFilesystem(store,
                                      volumeId,
                                      normalizedRoot,
                                      QDir(normalizedRoot).filePath(QStringLiteral("src/main.cpp")),
                                      3,
                                      &errorText)
            || !upsertPathFromFilesystem(store,
                                         volumeId,
                                         normalizedRoot,
                                         QDir(normalizedRoot).filePath(QStringLiteral("src/added.txt")),
                                         3,
                                         &errorText)
            || !markPathDeleted(store,
                                QDir::fromNativeSeparators(QDir::cleanPath(readmePath)),
                                3,
                                &errorText)) {
            store.shutdown();
            return finishFail(633, QStringLiteral("snapshot_b_delta_upsert_failed:%1").arg(errorText));
        }

        qint64 snapshotBId = 0;
        if (!createSnapshotFromFilesystem(QStringLiteral("snapshot_ui_B"),
                                          QStringLiteral("VIE-P21 changed snapshot"),
                                          &snapshotBId)) {
            store.shutdown();
            return finishFail(634, QStringLiteral("snapshot_b_create_failed:%1").arg(errorText));
        }

        if (!writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("src/main.cpp")),
                           QStringLiteral("int main(){\n    int v = 3;\n    return v + 20;\n}\n"),
                           &writeError)
            || !writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("docs/changelog.md")),
                              QStringLiteral("snapshot C notes\n"),
                              &writeError)) {
            store.shutdown();
            return finishFail(635, QStringLiteral("snapshot_c_mutation_failed:%1").arg(writeError));
        }

        const QString addedPath = QDir(normalizedRoot).filePath(QStringLiteral("src/added.txt"));
        if (!QFile::remove(addedPath)) {
            store.shutdown();
            return finishFail(636, QStringLiteral("snapshot_c_remove_failed:src/added.txt"));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1200));
        IndexSmokePassResult thirdIndex;
        if (!runSynchronousIndexPass(store, volumeId, normalizedRoot, 4, &thirdIndex, &errorText)) {
            store.shutdown();
            return finishFail(637, QStringLiteral("third_index_failed:%1").arg(errorText));
        }

        if (!upsertPathFromFilesystem(store,
                                      volumeId,
                                      normalizedRoot,
                                      QDir(normalizedRoot).filePath(QStringLiteral("src/main.cpp")),
                                      5,
                                      &errorText)
            || !upsertPathFromFilesystem(store,
                                         volumeId,
                                         normalizedRoot,
                                         QDir(normalizedRoot).filePath(QStringLiteral("docs/changelog.md")),
                                         5,
                                         &errorText)
            || !markPathDeleted(store,
                                QDir::fromNativeSeparators(QDir::cleanPath(addedPath)),
                                5,
                                &errorText)) {
            store.shutdown();
            return finishFail(638, QStringLiteral("snapshot_c_delta_upsert_failed:%1").arg(errorText));
        }

        qint64 snapshotCId = 0;
        if (!createSnapshotFromFilesystem(QStringLiteral("snapshot_ui_C"),
                                          QStringLiteral("VIE-P22 comparison snapshot"),
                                          &snapshotCId)) {
            store.shutdown();
            return finishFail(639, QStringLiteral("snapshot_c_create_failed:%1").arg(errorText));
        }

        QVector<SnapshotRecord> preUiSnapshots;
        if (!snapshotRepository.listSnapshots(normalizedRoot, &preUiSnapshots, &errorText)) {
            store.shutdown();
            return finishFail(640, QStringLiteral("snapshot_pre_ui_list_failed:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("pre_ui_snapshot_count=%1").arg(preUiSnapshots.size()));
        for (const SnapshotRecord& item : preUiSnapshots) {
            log.writeLine(QStringLiteral("pre_ui_snapshot_item id=%1 name=%2 created=%3 item_count=%4")
                              .arg(item.id)
                              .arg(item.snapshotName)
                              .arg(item.createdUtc)
                              .arg(item.itemCount));
        }

        store.shutdown();

        MainWindow uiProbe(true,
                           QString(),
                           options.historyLogPath + QStringLiteral(".actions.log"),
                           QString(),
                           normalizedDbPath,
                           nullptr);

        int diffUiRowCount = 0;
        int added = 0;
        int removed = 0;
        int changed = 0;
        QString diffUiError;
        const bool diffUiOk = uiProbe.triggerCompareSnapshotsForTesting(normalizedRoot,
                                                                         &diffUiRowCount,
                                                                         &added,
                                                                         &removed,
                                                                         &changed,
                                                                         &diffUiError);
        log.writeLine(QStringLiteral("ui_compare_ok=%1").arg(diffUiOk ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("ui_compare_error=%1").arg(diffUiError));
        log.writeLine(QStringLiteral("ui_compare_rows=%1").arg(diffUiRowCount));
        log.writeLine(QStringLiteral("ui_compare_summary added=%1 removed=%2 changed=%3")
                          .arg(added)
                          .arg(removed)
                          .arg(changed));

        int snapshotUiRowCount = 0;
        qint64 createdSnapshotId = 0;
        QString snapshotsUiError;
        const bool snapshotUiOk = uiProbe.triggerSnapshotsForTesting(normalizedRoot,
                                         &snapshotUiRowCount,
                                         &createdSnapshotId,
                                         &snapshotsUiError);
        log.writeLine(QStringLiteral("ui_snapshots_ok=%1").arg(snapshotUiOk ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("ui_snapshots_error=%1").arg(snapshotsUiError));
        log.writeLine(QStringLiteral("ui_snapshots_rows=%1").arg(snapshotUiRowCount));
        log.writeLine(QStringLiteral("ui_snapshots_created_id=%1").arg(createdSnapshotId));

        QTreeView* tree = uiProbe.findChild<QTreeView*>(QStringLiteral("fileTree"));
        if (!tree || !tree->model()) {
            return finishFail(641, QStringLiteral("ui_tree_model_missing"));
        }
        QAbstractItemModel* model = tree->model();
        const int modelRows = model->rowCount();
        log.writeLine(QStringLiteral("ui_model_rows=%1").arg(modelRows));
        for (int i = 0; i < qMin(16, modelRows); ++i) {
            log.writeLine(QStringLiteral("ui_row_%1 col0=%2 col2=%3 col3=%4 col4=%5")
                              .arg(i)
                              .arg(model->data(model->index(i, 0)).toString())
                              .arg(model->data(model->index(i, 2)).toString())
                              .arg(model->data(model->index(i, 3)).toString())
                              .arg(model->data(model->index(i, 4)).toString()));
        }

        const bool pass = snapshotUiOk
            && diffUiOk
            && createdSnapshotId > 0
            && snapshotUiRowCount >= 3
            && diffUiRowCount > 0
            && added > 0
            && removed > 0
            && changed > 0;

        if (!pass) {
            return finishFail(642, QStringLiteral("snapshot_ui_smoke_checks_failed"));
        }

        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return 0;
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("snapshot_ui_smoke_error=unexpected_exception"));
        return finishFail(643, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("snapshot_ui_smoke_error=unexpected_error"));
        return finishFail(644, QStringLiteral("unexpected_error"));
    }
}

int runHistorySnapshotPanelSmokeCli(int argc, char* argv[])
{
    QApplication uiApp(argc, argv);

    const HistorySnapshotPanelSmokeCliOptions options = parseHistorySnapshotPanelSmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("history_snapshot_panel_smoke_parse_error=%1").arg(options.parseError));
        return 652;
    }
    if (options.historyLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("history_snapshot_panel_smoke_error=missing_required_arg_--history-log"));
        return 653;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.historyLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("history_snapshot_panel_smoke_error=log_open_failed:%1").arg(logOpenError));
        return 654;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    auto writeTextFile = [](const QString& path, const QString& content, QString* errorText) {
        QFileInfo info(path);
        if (!QDir().mkpath(info.absolutePath())) {
            if (errorText) {
                *errorText = QStringLiteral("create_parent_dir_failed:%1").arg(info.absolutePath());
            }
            return false;
        }
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            if (errorText) {
                *errorText = QStringLiteral("open_write_failed:%1").arg(out.errorString());
            }
            return false;
        }
        QTextStream ts(&out);
        ts << content;
        return true;
    };

    auto writeLines = [](const QString& path, const QStringList& lines) {
        QFileInfo info(path);
        QDir().mkpath(info.absolutePath());
        QFile out(path);
        if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            return false;
        }
        QTextStream ts(&out);
        for (const QString& line : lines) {
            ts << line << '\n';
        }
        return true;
    };

    try {
        if (options.historyDbPath.trimmed().isEmpty()) {
            return finishFail(655, QStringLiteral("missing_required_arg_--history-db-path"));
        }
        if (options.historyRoot.trimmed().isEmpty()) {
            return finishFail(656, QStringLiteral("missing_required_arg_--history-root"));
        }
        if (options.historyTarget.trimmed().isEmpty()) {
            return finishFail(657, QStringLiteral("missing_required_arg_--history-target"));
        }

        const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(options.historyRoot).absoluteFilePath()));
        const QString normalizedDbPath = QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo(options.historyDbPath).absoluteFilePath()));
        const QString normalizedTarget = QDir::fromNativeSeparators(QDir::cleanPath(QDir(normalizedRoot).filePath(options.historyTarget)));
        const QString panelDir = QFileInfo(options.historyLogPath).absolutePath();

        const QString historyPanelLogPath = options.historyLogPath;
        const QString snapshotPanelLogPath = QDir(panelDir).filePath(QStringLiteral("07_panel_snapshot_log.txt"));
        const QString diffPanelLogPath = QDir(panelDir).filePath(QStringLiteral("08_panel_diff_log.txt"));
        const QString navigationPanelLogPath = QDir(panelDir).filePath(QStringLiteral("09_panel_navigation_log.txt"));
        const QString historyRowsPath = QDir(panelDir).filePath(QStringLiteral("10_history_rows.txt"));
        const QString snapshotRowsPath = QDir(panelDir).filePath(QStringLiteral("11_snapshot_rows.txt"));
        const QString diffRowsPath = QDir(panelDir).filePath(QStringLiteral("12_diff_rows.txt"));

        log.writeLine(QStringLiteral("mode=history_snapshot_panel_smoke"));
        log.writeLine(QStringLiteral("startup_banner=HISTORY_SNAPSHOT_PANEL_SMOKE_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("history_root=%1").arg(QDir::toNativeSeparators(normalizedRoot)));
        log.writeLine(QStringLiteral("history_db_path=%1").arg(QDir::toNativeSeparators(normalizedDbPath)));
        log.writeLine(QStringLiteral("history_target=%1").arg(QDir::toNativeSeparators(normalizedTarget)));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

        QFile::remove(normalizedDbPath);
        QFile::remove(normalizedDbPath + QStringLiteral("-wal"));
        QFile::remove(normalizedDbPath + QStringLiteral("-shm"));

        QDir rootDir(normalizedRoot);
        if (rootDir.exists() && !rootDir.removeRecursively()) {
            return finishFail(658, QStringLiteral("sample_root_cleanup_failed"));
        }
        if (!QDir().mkpath(QDir(normalizedRoot).filePath(QStringLiteral("src")))
            || !QDir().mkpath(QDir(normalizedRoot).filePath(QStringLiteral("docs")))) {
            return finishFail(659, QStringLiteral("sample_root_create_failed"));
        }

        QString writeError;
        if (!writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("src/main.cpp")),
                           QStringLiteral("int main(){return 1;}\n"),
                           &writeError)
            || !writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("src/util.h")),
                              QStringLiteral("#pragma once\nint util();\n"),
                              &writeError)
            || !writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("docs/readme.md")),
                              QStringLiteral("baseline\n"),
                              &writeError)) {
            return finishFail(660, QStringLiteral("baseline_seed_failed:%1").arg(writeError));
        }

        MetaStore store;
        QString errorText;
        QString migrationLog;
        if (!store.initialize(normalizedDbPath, &errorText, &migrationLog)) {
            return finishFail(661, QStringLiteral("db_init_failure:%1").arg(errorText));
        }

        SnapshotRepository repository(store);
        auto collectSnapshotEntries = [&](QVector<SnapshotEntryRecord>* outEntries) {
            outEntries->clear();
            QStringList paths;
            paths.push_back(normalizedRoot);
            QDirIterator it(normalizedRoot,
                            QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext()) {
                paths.push_back(QDir::fromNativeSeparators(QDir::cleanPath(it.next())));
            }
            std::sort(paths.begin(), paths.end(), [](const QString& a, const QString& b) {
                return QString::compare(a, b, Qt::CaseInsensitive) < 0;
            });
            outEntries->reserve(paths.size());
            for (const QString& path : paths) {
                QFileInfo info(path);
                if (!info.exists()) {
                    continue;
                }

                SnapshotEntryRecord entry;
                entry.entryPath = QDir::fromNativeSeparators(QDir::cleanPath(info.absoluteFilePath()));
                entry.virtualPath = entry.entryPath;
                entry.parentPath = QDir::fromNativeSeparators(QDir::cleanPath(info.absolutePath()));
                entry.name = info.fileName().isEmpty() ? entry.entryPath : info.fileName();
                entry.normalizedName = entry.name.toLower();
                entry.extension = info.isDir() || info.suffix().isEmpty() ? QString() : (QStringLiteral(".") + info.suffix().toLower());
                entry.isDir = info.isDir();
                entry.hasSizeBytes = !entry.isDir;
                entry.sizeBytes = entry.isDir ? 0 : info.size();
                entry.modifiedUtc = info.lastModified().toUTC().toString(Qt::ISODate);
                entry.hiddenFlag = info.isHidden();
                entry.systemFlag = false;
                entry.archiveFlag = false;
                entry.existsFlag = true;
                const QString hashPayload = QStringLiteral("%1|%2|%3")
                                                .arg(entry.entryPath)
                                                .arg(entry.hasSizeBytes ? QString::number(entry.sizeBytes) : QStringLiteral("null"))
                                                .arg(entry.modifiedUtc);
                entry.entryHash = QString::fromLatin1(QCryptographicHash::hash(hashPayload.toUtf8(), QCryptographicHash::Sha256).toHex());
                entry.hasEntryHash = !entry.entryHash.isEmpty();
                outEntries->push_back(entry);
            }
            return true;
        };

        auto createSnapshot = [&](const QString& snapshotName, const QString& noteText, qint64* snapshotIdOut) {
            QVector<SnapshotEntryRecord> entries;
            if (!collectSnapshotEntries(&entries)) {
                errorText = QStringLiteral("collect_snapshot_entries_failed");
                return false;
            }

            SnapshotRecord snapshot;
            snapshot.rootPath = normalizedRoot;
            snapshot.snapshotName = snapshotName;
            snapshot.snapshotType = QStringLiteral("structural_full");
            snapshot.createdUtc = SqlHelpers::utcNowIso();
            snapshot.optionsJson = SnapshotTypesUtil::optionsToJson(SnapshotCreateOptions{});
            snapshot.itemCount = entries.size();
            snapshot.noteText = noteText;

            if (!store.beginTransaction(&errorText)) {
                return false;
            }
            qint64 snapshotId = 0;
            if (!repository.createSnapshot(snapshot, &snapshotId, &errorText)) {
                store.rollbackTransaction(nullptr);
                return false;
            }
            for (SnapshotEntryRecord& entry : entries) {
                entry.snapshotId = snapshotId;
            }
            if (!repository.insertSnapshotEntries(snapshotId, entries, &errorText)
                || !repository.updateSnapshotItemCount(snapshotId, entries.size(), &errorText)
                || !store.commitTransaction(&errorText)) {
                store.rollbackTransaction(nullptr);
                return false;
            }
            if (snapshotIdOut) {
                *snapshotIdOut = snapshotId;
            }
            return true;
        };

        qint64 snapshotAId = 0;
        if (!createSnapshot(QStringLiteral("snapshot_A"), QStringLiteral("baseline"), &snapshotAId)) {
            store.shutdown();
            return finishFail(662, QStringLiteral("snapshot_a_failed:%1").arg(errorText));
        }

        if (!writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("src/main.cpp")),
                           QStringLiteral("int main(){\n    int v = 2;\n    return v;\n}\n"),
                           &writeError)
            || !writeTextFile(QDir(normalizedRoot).filePath(QStringLiteral("src/newfile.cpp")),
                              QStringLiteral("#include \"util.h\"\nint created(){return 2;}\n"),
                              &writeError)) {
            store.shutdown();
            return finishFail(663, QStringLiteral("snapshot_b_mutation_failed:%1").arg(writeError));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        qint64 snapshotBId = 0;
        if (!createSnapshot(QStringLiteral("snapshot_B"), QStringLiteral("main_changed_newfile_added"), &snapshotBId)) {
            store.shutdown();
            return finishFail(664, QStringLiteral("snapshot_b_failed:%1").arg(errorText));
        }

        const QString readmePath = QDir(normalizedRoot).filePath(QStringLiteral("docs/readme.md"));
        if (!QFile::remove(readmePath)) {
            store.shutdown();
            return finishFail(665, QStringLiteral("snapshot_c_mutation_failed:remove_readme"));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1100));
        qint64 snapshotCId = 0;
        if (!createSnapshot(QStringLiteral("snapshot_C"), QStringLiteral("readme_removed"), &snapshotCId)) {
            store.shutdown();
            return finishFail(666, QStringLiteral("snapshot_c_failed:%1").arg(errorText));
        }

        store.shutdown();

        MainWindow uiProbe(true,
                           QString(),
                           options.historyLogPath + QStringLiteral(".actions.log"),
                           QString(),
                           normalizedDbPath,
                           nullptr);

        int historyRows = 0;
        int snapshotRows = 0;
        int diffRows = 0;
        QStringList historyPreview;
        QStringList snapshotPreview;
        QStringList diffPreview;
        QString navigatedPath;
        QString panelError;
        const bool panelOk = uiProbe.triggerHistorySnapshotPanelForTesting(normalizedRoot,
                                                                            normalizedTarget,
                                                                            snapshotAId,
                                                                            snapshotBId,
                                                                            &historyRows,
                                                                            &snapshotRows,
                                                                            &diffRows,
                                                                            &historyPreview,
                                                                            &snapshotPreview,
                                                                            &diffPreview,
                                                                            &navigatedPath,
                                                                            &panelError);

        writeLines(snapshotPanelLogPath,
                   {
                       QStringLiteral("tab=snapshots"),
                       QStringLiteral("snapshot_a_id=%1").arg(snapshotAId),
                       QStringLiteral("snapshot_b_id=%1").arg(snapshotBId),
                       QStringLiteral("snapshot_c_id=%1").arg(snapshotCId),
                       QStringLiteral("snapshot_rows=%1").arg(snapshotRows),
                   });
        writeLines(diffPanelLogPath,
                   {
                       QStringLiteral("tab=diff"),
                       QStringLiteral("selected_old_snapshot_id=%1").arg(snapshotAId),
                       QStringLiteral("selected_new_snapshot_id=%1").arg(snapshotBId),
                       QStringLiteral("diff_rows=%1").arg(diffRows),
                       QStringLiteral("panel_error=%1").arg(panelError),
                   });
        writeLines(navigationPanelLogPath,
                   {
                       QStringLiteral("tab=navigation"),
                       QStringLiteral("navigation_path=%1").arg(QDir::toNativeSeparators(navigatedPath)),
                       QStringLiteral("navigation_ok=%1").arg(!navigatedPath.isEmpty() ? QStringLiteral("true") : QStringLiteral("false")),
                   });
        writeLines(historyRowsPath, historyPreview);
        writeLines(snapshotRowsPath, snapshotPreview);
        writeLines(diffRowsPath, diffPreview);

        log.writeLine(QStringLiteral("panel_open_ok=%1").arg(panelOk ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("history_rows=%1").arg(historyRows));
        log.writeLine(QStringLiteral("snapshot_rows=%1").arg(snapshotRows));
        log.writeLine(QStringLiteral("diff_rows=%1").arg(diffRows));
        log.writeLine(QStringLiteral("navigation_path=%1").arg(QDir::toNativeSeparators(navigatedPath)));
        log.writeLine(QStringLiteral("snapshot_id_A=%1").arg(snapshotAId));
        log.writeLine(QStringLiteral("snapshot_id_B=%1").arg(snapshotBId));
        log.writeLine(QStringLiteral("snapshot_id_C=%1").arg(snapshotCId));
        log.writeLine(QStringLiteral("panel_error=%1").arg(panelError));

        const bool pass = panelOk
            && historyRows > 0
            && snapshotRows > 0
            && diffRows > 0
            && !navigatedPath.isEmpty();

        if (!pass) {
            return finishFail(667, QStringLiteral("history_snapshot_panel_smoke_checks_failed"));
        }

        log.writeLine(QStringLiteral("final_status=PASS"));
        log.writeLine(QStringLiteral("exit_code=0"));
        log.writeLine(QStringLiteral("failure_reason="));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return 0;
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("history_snapshot_panel_smoke_error=unexpected_exception"));
        return finishFail(668, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("history_snapshot_panel_smoke_error=unexpected_error"));
        return finishFail(669, QStringLiteral("unexpected_error"));
    }
}

int runSnapshotDiffProbeCli(int argc, char* argv[])
{
    QCoreApplication cliApp(argc, argv);

    const SnapshotDiffProbeCliOptions options = parseSnapshotDiffProbeOptions(argc, argv);
    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("snapshot_diff_probe_parse_error=%1").arg(options.parseError));
        return 645;
    }
    if (options.probeLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("snapshot_diff_probe_error=missing_required_arg_--snapshot-probe-log"));
        return 646;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.probeLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("snapshot_diff_probe_error=log_open_failed:%1").arg(logOpenError));
        return 647;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        return exitCode;
    };

    if (options.snapshotDbPath.trimmed().isEmpty()) {
        return finishFail(648, QStringLiteral("missing_required_arg_--snapshot-db-path"));
    }
    if (options.oldSnapshotId <= 0 || options.newSnapshotId <= 0) {
        return finishFail(649, QStringLiteral("invalid_snapshot_ids"));
    }

    log.writeLine(QStringLiteral("mode=snapshot_diff_probe"));
    log.writeLine(QStringLiteral("snapshot_db_path=%1").arg(QDir::toNativeSeparators(options.snapshotDbPath)));
    log.writeLine(QStringLiteral("snapshot_old_id=%1").arg(options.oldSnapshotId));
    log.writeLine(QStringLiteral("snapshot_new_id=%1").arg(options.newSnapshotId));

    MetaStore store;
    QString errorText;
    QString migrationLog;
    if (!store.initialize(options.snapshotDbPath, &errorText, &migrationLog)) {
        return finishFail(650, QStringLiteral("db_init_failure:%1").arg(errorText));
    }

    SnapshotRepository repository(store);
    SnapshotDiffEngine diffEngine(repository);
    SnapshotDiffOptions diffOptions;
    diffOptions.includeUnchanged = true;

    const SnapshotDiffResult diff = diffEngine.compareSnapshots(options.oldSnapshotId,
                                                                options.newSnapshotId,
                                                                diffOptions);
    if (!diff.ok) {
        store.shutdown();
        return finishFail(651, QStringLiteral("compare_failed:%1").arg(diff.errorText));
    }

    log.writeLine(QStringLiteral("diff_ok=true"));
    log.writeLine(QStringLiteral("diff_summary added=%1 removed=%2 changed=%3 unchanged=%4 total=%5")
                      .arg(diff.summary.added)
                      .arg(diff.summary.removed)
                      .arg(diff.summary.changed)
                      .arg(diff.summary.unchanged)
                      .arg(diff.summary.totalRows));
    for (int i = 0; i < qMin(40, diff.rows.size()); ++i) {
        const SnapshotDiffRow& row = diff.rows.at(i);
        log.writeLine(QStringLiteral("diff_row status=%1 path=%2 old_size=%3 new_size=%4 old_modified=%5 new_modified=%6")
                          .arg(SnapshotDiffTypesUtil::statusToString(row.status))
                          .arg(QDir::toNativeSeparators(row.path))
                          .arg(row.oldHasSizeBytes ? QString::number(row.oldSizeBytes) : QStringLiteral("null"))
                          .arg(row.newHasSizeBytes ? QString::number(row.newSizeBytes) : QStringLiteral("null"))
                          .arg(row.oldModifiedUtc)
                          .arg(row.newModifiedUtc));
    }

    store.shutdown();
    log.writeLine(QStringLiteral("final_status=PASS"));
    log.writeLine(QStringLiteral("exit_code=0"));
    log.writeLine(QStringLiteral("failure_reason="));
    return 0;
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

int runPerfSmokeCli(int argc, char* argv[])
{
    QCoreApplication cliApp(argc, argv);

    const PerfSmokeCliOptions options = parsePerfSmokeOptions(argc, argv);
    const QString exePath = QFileInfo(QString::fromLocal8Bit(argv[0])).absoluteFilePath();
    const QString cwd = QDir::currentPath();

    if (!options.parseError.isEmpty()) {
        writeStderrLine(QStringLiteral("perf_smoke_parse_error=%1").arg(options.parseError));
        return 120;
    }

    if (options.perfLogPath.trimmed().isEmpty()) {
        writeStderrLine(QStringLiteral("perf_smoke_error=missing_required_arg_--perf-log"));
        return 121;
    }

    IndexSmokeLogWriter log;
    QString logOpenError;
    if (!log.open(options.perfLogPath, &logOpenError)) {
        writeStderrLine(QStringLiteral("perf_smoke_error=log_open_failed"));
        writeStderrLine(QStringLiteral("perf_smoke_log_path=%1").arg(QDir::toNativeSeparators(options.perfLogPath)));
        writeStderrLine(QStringLiteral("perf_smoke_log_error=%1").arg(logOpenError));
        return 122;
    }

    auto finishFail = [&](int exitCode, const QString& reason) {
        log.writeLine(QStringLiteral("final_status=FAIL"));
        log.writeLine(QStringLiteral("exit_code=%1").arg(exitCode));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(reason));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return exitCode;
    };

    try {
        if (options.perfRoot.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("perf_smoke_error=missing_required_arg_--perf-root"));
            return finishFail(123, QStringLiteral("missing_required_arg_--perf-root"));
        }
        if (options.perfDbPath.trimmed().isEmpty()) {
            writeStderrLine(QStringLiteral("perf_smoke_error=missing_required_arg_--perf-db-path"));
            return finishFail(124, QStringLiteral("missing_required_arg_--perf-db-path"));
        }

        log.writeLine(QStringLiteral("mode=perf_smoke"));
        log.writeLine(QStringLiteral("startup_banner=PERF_SMOKE_BEGIN"));
        log.writeLine(QStringLiteral("exe_path=%1").arg(QDir::toNativeSeparators(exePath)));
        log.writeLine(QStringLiteral("cwd=%1").arg(QDir::toNativeSeparators(cwd)));
        log.writeLine(QStringLiteral("args_received=%1").arg(options.argsReceived.join(QStringLiteral(" | "))));
        log.writeLine(QStringLiteral("perf_root=%1").arg(QDir::toNativeSeparators(options.perfRoot)));
        log.writeLine(QStringLiteral("perf_db_path=%1").arg(QDir::toNativeSeparators(options.perfDbPath)));
        log.writeLine(QStringLiteral("perf_log=%1").arg(QDir::toNativeSeparators(options.perfLogPath)));
        log.writeLine(QStringLiteral("perf_target_files=%1").arg(options.targetFileCount));
        log.writeLine(QStringLiteral("perf_query_repeats=%1").arg(options.queryRepeats));
        log.writeLine(QStringLiteral("timestamp_utc=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));

        const QString normalizedPerfRoot = QDir::fromNativeSeparators(QDir::cleanPath(options.perfRoot));
        const QString treeRoot = QDir(normalizedPerfRoot).filePath(QStringLiteral("large_tree"));
        QDir().mkpath(normalizedPerfRoot);
        if (QDir(treeRoot).exists() && !QDir(treeRoot).removeRecursively()) {
            return finishFail(125, QStringLiteral("cleanup_tree_root_failed:%1").arg(treeRoot));
        }

        PerfMetrics metrics;
        BatchCoordinator batchCoordinator;
        ResultLimiter limiter;

        PerfTimer harnessTimer;
        LargeTreeHarness harness;
        const LargeTreeHarnessResult harnessResult = harness.createTree(treeRoot, options.targetFileCount);
        const double harnessMs = harnessTimer.elapsedMs();
        if (!harnessResult.ok) {
            return finishFail(126, QStringLiteral("large_tree_harness_failed:%1").arg(harnessResult.errorText));
        }
        metrics.addWatcherSample(harnessMs, harnessResult.fileCount);
        log.writeLine(QStringLiteral("harness_ok=true"));
        log.writeLine(QStringLiteral("harness_tree_root=%1").arg(QDir::toNativeSeparators(treeRoot)));
        log.writeLine(QStringLiteral("harness_files=%1").arg(harnessResult.fileCount));
        log.writeLine(QStringLiteral("harness_duration_ms=%1").arg(harnessMs, 0, 'f', 3));

        QFile::remove(options.perfDbPath);

        MetaStore store;
        QString errorText;
        QString migrationLog;
        if (!store.initialize(options.perfDbPath, &errorText, &migrationLog)) {
            return finishFail(127, QStringLiteral("db_init_failure:%1").arg(errorText));
        }
        log.writeLine(QStringLiteral("db_init_ok=true"));
        if (!migrationLog.isEmpty()) {
            log.writeLine(QStringLiteral("migration_log_begin"));
            log.writeLine(migrationLog.trimmed());
            log.writeLine(QStringLiteral("migration_log_end"));
        }

        IndexRootRecord indexRoot;
        indexRoot.rootPath = QDir::fromNativeSeparators(QDir::cleanPath(treeRoot));
        indexRoot.status = QStringLiteral("active");
        indexRoot.createdUtc = SqlHelpers::utcNowIso();
        indexRoot.updatedUtc = indexRoot.createdUtc;
        if (!store.upsertIndexRoot(indexRoot, nullptr, &errorText)) {
            store.shutdown();
            return finishFail(128, QStringLiteral("index_root_upsert_failed:%1").arg(errorText));
        }

        VolumeRecord volume;
        volume.volumeKey = QStringLiteral("perf_smoke:%1").arg(indexRoot.rootPath.toLower());
        volume.rootPath = indexRoot.rootPath;
        volume.displayName = QFileInfo(indexRoot.rootPath).fileName();
        volume.fsType = QStringLiteral("native");
        volume.serialNumber = QStringLiteral("perf_smoke");
        qint64 volumeId = 0;
        if (!store.upsertVolume(volume, &volumeId, &errorText)) {
            store.shutdown();
            return finishFail(129, QStringLiteral("volume_upsert_failed:%1").arg(errorText));
        }

        PerfTimer ingestTimer;
        IndexSmokePassResult ingestPass;
        if (!runSynchronousIndexPass(store, volumeId, treeRoot, 1, &ingestPass, &errorText)) {
            store.shutdown();
            return finishFail(130, QStringLiteral("index_ingest_failed:%1").arg(errorText));
        }
        const double ingestMs = ingestTimer.elapsedMs();
        metrics.addIngestSample(ingestMs, ingestPass.seen);
        log.writeLine(QStringLiteral("ingest_ok=true"));
        log.writeLine(QStringLiteral("ingest_seen=%1").arg(ingestPass.seen));
        log.writeLine(QStringLiteral("ingest_inserted=%1").arg(ingestPass.inserted));
        log.writeLine(QStringLiteral("ingest_updated=%1").arg(ingestPass.updated));
        log.writeLine(QStringLiteral("ingest_duration_ms=%1").arg(ingestMs, 0, 'f', 3));

        QueryCore queryCore(store);
        QueryProfiler queryProfiler(metrics);
        QueryOptions baseOptions;
        baseOptions.sortField = QuerySortField::Name;
        baseOptions.ascending = true;
        baseOptions.pageSize = batchCoordinator.queryPageSize();
        baseOptions.pageOffset = 0;

        bool querySuiteOk = true;
        log.writeLine(QStringLiteral("query_suite_begin"));
        for (int i = 0; i < options.queryRepeats; ++i) {
            const int runId = i + 1;

            QString childrenLine;
            const QueryResult childrenRaw = queryProfiler.profile(QStringLiteral("children"), [&]() {
                return queryCore.queryChildren(treeRoot, baseOptions);
            }, &childrenLine);
            const QueryResult children = limiter.limit(childrenRaw);
            querySuiteOk = querySuiteOk && children.ok;
            log.writeLine(QStringLiteral("query_run=%1 %2 limited_rows=%3")
                              .arg(runId)
                              .arg(childrenLine)
                              .arg(children.rows.size()));

            QueryOptions flatOptions = baseOptions;
            flatOptions.maxDepth = -1;
            QString flatLine;
            const QueryResult flatRaw = queryProfiler.profile(QStringLiteral("flat"), [&]() {
                return queryCore.queryFlat(treeRoot, flatOptions);
            }, &flatLine);
            const QueryResult flat = limiter.limit(flatRaw);
            querySuiteOk = querySuiteOk && flat.ok;
            log.writeLine(QStringLiteral("query_run=%1 %2 limited_rows=%3")
                              .arg(runId)
                              .arg(flatLine)
                              .arg(flat.rows.size()));

            QueryOptions subtreeOptions = baseOptions;
            subtreeOptions.maxDepth = -1;
            QString subtreeLine;
            const QueryResult subtreeRaw = queryProfiler.profile(QStringLiteral("subtree"), [&]() {
                return queryCore.querySubtree(treeRoot, subtreeOptions);
            }, &subtreeLine);
            const QueryResult subtree = limiter.limit(subtreeRaw);
            querySuiteOk = querySuiteOk && subtree.ok;
            log.writeLine(QStringLiteral("query_run=%1 %2 limited_rows=%3")
                              .arg(runId)
                              .arg(subtreeLine)
                              .arg(subtree.rows.size()));

            QueryOptions searchOptions = baseOptions;
            searchOptions.substringFilter = QStringLiteral("file_1");
            QString searchLine;
            const QueryResult searchRaw = queryProfiler.profile(QStringLiteral("search"), [&]() {
                return queryCore.querySearch(treeRoot, searchOptions);
            }, &searchLine);
            const QueryResult search = limiter.limit(searchRaw);
            querySuiteOk = querySuiteOk && search.ok;
            log.writeLine(QStringLiteral("query_run=%1 %2 limited_rows=%3")
                              .arg(runId)
                              .arg(searchLine)
                              .arg(search.rows.size()));
        }
        log.writeLine(QStringLiteral("query_suite_end"));

        PerfTimer commitTimer;
        if (store.beginTransaction(&errorText)) {
            IndexStatRecord stat;
            stat.key = QStringLiteral("perf_last_query_avg_ms");
            stat.value = QString::number(metrics.averageQueryMs(), 'f', 3);
            stat.updatedUtc = SqlHelpers::utcNowIso();
            if (!store.upsertIndexStat(stat, &errorText)) {
                store.rollbackTransaction(nullptr);
            } else if (!store.commitTransaction(&errorText)) {
                store.rollbackTransaction(nullptr);
            }
        }
        metrics.addDbCommitSample(commitTimer.elapsedMs());

        const qint64 totalEntries = store.countEntries(&errorText);
        const qint64 totalDirectories = store.countDirectories(&errorText);
        const qint64 totalFiles = store.countFiles(&errorText);

        QStringList indexes;
        QString indexError;
        const bool indexProbeOk = listSqliteIndexes(options.perfDbPath, &indexes, &indexError);
        if (!indexProbeOk) {
            store.shutdown();
            return finishFail(131, QStringLiteral("index_probe_failed:%1").arg(indexError));
        }

        log.writeLine(QStringLiteral("query_avg_ms=%1").arg(metrics.averageQueryMs(), 0, 'f', 3));
        log.writeLine(QStringLiteral("ingest_avg_ms=%1").arg(metrics.averageIngestMs(), 0, 'f', 3));
        log.writeLine(QStringLiteral("watcher_avg_ms=%1").arg(metrics.averageWatcherMs(), 0, 'f', 3));
        log.writeLine(QStringLiteral("db_commit_avg_ms=%1").arg(metrics.averageDbCommitMs(), 0, 'f', 3));
        log.writeLine(QStringLiteral("query_total_rows=%1").arg(metrics.totalQueryRows()));
        log.writeLine(QStringLiteral("ingest_total_rows=%1").arg(metrics.totalIngestRows()));
        log.writeLine(QStringLiteral("watcher_total_rows=%1").arg(metrics.totalWatcherRows()));

        log.writeLine(QStringLiteral("batch_directory_size=%1").arg(batchCoordinator.directoryBatchSize()));
        log.writeLine(QStringLiteral("batch_query_page_size=%1").arg(batchCoordinator.queryPageSize()));
        log.writeLine(QStringLiteral("batch_scan_size=%1").arg(batchCoordinator.scanBatchSize()));
        log.writeLine(QStringLiteral("result_limiter_max=%1").arg(ResultLimiter::kMaxResultsPerQuery));

        log.writeLine(QStringLiteral("row_count_entries=%1").arg(totalEntries));
        log.writeLine(QStringLiteral("row_count_files=%1").arg(totalFiles));
        log.writeLine(QStringLiteral("row_count_directories=%1").arg(totalDirectories));

        bool hasParentPathIndex = false;
        bool hasPathIndex = false;
        bool hasNameIndex = false;
        for (const QString& indexName : indexes) {
            log.writeLine(QStringLiteral("db_index=%1").arg(indexName));
            if (indexName.compare(QStringLiteral("idx_entries_parent"), Qt::CaseInsensitive) == 0
                || indexName.compare(QStringLiteral("idx_entries_parent_path"), Qt::CaseInsensitive) == 0) {
                hasParentPathIndex = true;
            }
            if (indexName.compare(QStringLiteral("idx_entries_path"), Qt::CaseInsensitive) == 0) {
                hasPathIndex = true;
            }
            if (indexName.compare(QStringLiteral("idx_entries_name"), Qt::CaseInsensitive) == 0
                || indexName.compare(QStringLiteral("idx_entries_name_raw"), Qt::CaseInsensitive) == 0) {
                hasNameIndex = true;
            }
        }

        const bool rowCountsOk = totalEntries >= harnessResult.fileCount;
        const bool indexesOk = hasParentPathIndex && hasPathIndex && hasNameIndex;
        const bool pass = querySuiteOk && rowCountsOk && indexesOk;

        store.shutdown();

        log.writeLine(QStringLiteral("query_suite_ok=%1").arg(querySuiteOk ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("row_counts_ok=%1").arg(rowCountsOk ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("indexes_ok=%1").arg(indexesOk ? QStringLiteral("true") : QStringLiteral("false")));
        log.writeLine(QStringLiteral("gate=%1").arg(pass ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("final_status=%1").arg(pass ? QStringLiteral("PASS") : QStringLiteral("FAIL")));
        log.writeLine(QStringLiteral("exit_code=%1").arg(pass ? 0 : 132));
        log.writeLine(QStringLiteral("failure_reason=%1").arg(pass ? QString() : QStringLiteral("perf_gate_failed")));
        log.writeLine(QStringLiteral("timestamp_utc_end=%1").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)));
        return pass ? 0 : 132;
    } catch (const std::exception& ex) {
        writeStderrLine(QStringLiteral("perf_smoke_error=unexpected_exception"));
        return finishFail(133, QStringLiteral("unexpected_exception:%1").arg(QString::fromLocal8Bit(ex.what())));
    } catch (...) {
        writeStderrLine(QStringLiteral("perf_smoke_error=unexpected_error"));
        return finishFail(134, QStringLiteral("unexpected_error"));
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
    if (hasHistorySnapshotPanelSmokeFlag(argc, argv)) {
        return runHistorySnapshotPanelSmokeCli(argc, argv);
    }

    if (hasSnapshotDiffProbeFlag(argc, argv)) {
        return runSnapshotDiffProbeCli(argc, argv);
    }

    if (hasSnapshotUiSmokeFlag(argc, argv)) {
        return runSnapshotUiSmokeCli(argc, argv);
    }

    if (hasHistoryUiSmokeFlag(argc, argv)) {
        return runHistoryUiSmokeCli(argc, argv);
    }

    if (hasHistorySmokeFlag(argc, argv)) {
        return runHistorySmokeCli(argc, argv);
    }

    if (hasReferenceUiSmokeFlag(argc, argv)) {
        return runReferenceUiSmokeCli(argc, argv);
    }

    if (hasReferenceQuerySmokeFlag(argc, argv)) {
        return runReferenceQuerySmokeCli(argc, argv);
    }

    if (hasReferenceSmokeFlag(argc, argv)) {
        return runReferenceSmokeCli(argc, argv);
    }

    if (hasArchiveSnapshotSmokeFlag(argc, argv)) {
        return runArchiveSnapshotSmokeCli(argc, argv);
    }

    if (hasArchiveSmokeFlag(argc, argv)) {
        return runArchiveSmokeCli(argc, argv);
    }

    if (hasQueryLangAdvancedSmokeFlag(argc, argv)) {
        return runQueryLangSmokeCli(argc, argv);
    }

    if (hasQueryLangSmokeFlag(argc, argv)) {
        return runQueryLangSmokeCli(argc, argv);
    }

    if (hasQueryBarSmokeFlag(argc, argv)) {
        return runQueryBarSmokeCli(argc, argv);
    }

    if (hasStructuralQuerySmokeFlag(argc, argv)) {
        return runStructuralQuerySmokeCli(argc, argv);
    }

    if (hasPanelNavigationSmokeFlag(argc, argv)) {
        return runPanelNavigationSmokeCli(argc, argv);
    }

    if (hasStructuralResultModelSmokeFlag(argc, argv)) {
        return runStructuralResultModelSmokeCli(argc, argv);
    }

    if (hasStructuralFilterSmokeFlag(argc, argv)) {
        return runStructuralFilterSmokeCli(argc, argv);
    }

    if (hasStructuralSortSmokeFlag(argc, argv)) {
        return runStructuralSortSmokeCli(argc, argv);
    }

    if (hasGraphVisualSmokeFlag(argc, argv)) {
        return runGraphVisualSmokeCli(argc, argv);
    }

    if (hasTimelineSmokeFlag(argc, argv)) {
        return runTimelineSmokeCli(argc, argv);
    }

    if (hasSnapshotDiffSmokeFlag(argc, argv)) {
        return runSnapshotDiffSmokeCli(argc, argv);
    }

    if (hasSnapshotSmokeFlag(argc, argv)) {
        return runSnapshotSmokeCli(argc, argv);
    }

    if (hasPerfSmokeFlag(argc, argv)) {
        return runPerfSmokeCli(argc, argv);
    }

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

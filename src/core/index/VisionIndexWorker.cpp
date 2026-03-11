#include "VisionIndexWorker.h"

#include <QDir>

#include "VisionIndexJournal.h"
#include "core/db/MetaStore.h"
#include "core/scan/ScanCoordinator.h"
#include "core/scan/ScanTask.h"

namespace VisionIndexEngine
{
VisionIndexWorker::VisionIndexWorker(MetaStore& store, VisionIndexJournal& journal)
    : m_store(store)
    , m_journal(journal)
{
}

bool VisionIndexWorker::run(const VisionIndexWorkItem& item,
                            qint64 nextScanVersion,
                            VisionIndexWorkResult* out,
                            QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_work_result");
        }
        return false;
    }

    out->scanVersion = nextScanVersion;

    QString journalError;
    m_journal.append(item.rootPath,
                     item.rootPath,
                     QStringLiteral("scan_started"),
                     nextScanVersion,
                     QStringLiteral("mode=%1 visible_priority=%2").arg(item.mode).arg(item.visiblePriority ? 1 : 0),
                     &journalError);

    ScanCoordinator coordinator(m_store);
    ScanTask task;
    task.rootPath = item.rootPath;
    task.mode = item.mode;
    task.batchSize = 500;

    ScanCoordinatorResult result;
    ScanCoordinator::Callbacks callbacks;
    callbacks.onLog = [&](const QString& line) {
        QString ignored;
        m_journal.append(item.rootPath,
                         item.rootPath,
                         QStringLiteral("scan_log"),
                         nextScanVersion,
                         line,
                         &ignored);
    };
    callbacks.onProgress = nullptr;

    const bool ok = coordinator.runScan(task, callbacks, &result);
    out->ok = ok && result.success;
    out->canceled = result.canceled;
    out->errorText = result.errorText;
    out->sessionId = result.sessionId;
    out->totalSeen = result.totalSeen;
    out->totalInserted = result.totalInserted;
    out->totalUpdated = result.totalUpdated;

    QString payload = QStringLiteral("session_id=%1 seen=%2 inserted=%3 updated=%4")
                          .arg(result.sessionId)
                          .arg(result.totalSeen)
                          .arg(result.totalInserted)
                          .arg(result.totalUpdated);
    if (!out->errorText.isEmpty()) {
        payload += QStringLiteral(" error=%1").arg(out->errorText);
    }

    m_journal.append(item.rootPath,
                     item.rootPath,
                     out->ok ? QStringLiteral("scan_completed") : (out->canceled ? QStringLiteral("scan_canceled") : QStringLiteral("scan_failed")),
                     nextScanVersion,
                     payload,
                     &journalError);

    if (!out->ok && errorText && !out->errorText.isEmpty()) {
        *errorText = out->errorText;
    }
    return out->ok;
}
}

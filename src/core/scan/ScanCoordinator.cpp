#include "ScanCoordinator.h"

#include <QDir>
#include <QFileInfo>

#include "ScanWorker.h"
#include "core/db/MetaStore.h"
#include "core/db/SqlHelpers.h"

ScanCoordinator::ScanCoordinator(MetaStore& store)
    : m_store(store)
{
}

bool ScanCoordinator::runScan(const ScanTask& task,
                              const Callbacks& callbacks,
                              ScanCoordinatorResult* outResult)
{
    if (!outResult) {
        return false;
    }

    ScanCoordinatorResult state;

    if (!m_store.isReady()) {
        state.errorText = QStringLiteral("metastore_not_ready");
        *outResult = state;
        return false;
    }

    const QString rootPath = task.normalizedRootPath();
    QFileInfo rootInfo(rootPath);
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        state.errorText = QStringLiteral("scan_root_not_usable: %1").arg(rootPath);
        *outResult = state;
        return false;
    }

    VolumeRecord volume;
    volume.volumeKey = buildVolumeKey(rootPath);
    volume.rootPath = rootPath;
    volume.displayName = rootInfo.fileName().isEmpty() ? rootPath : rootInfo.fileName();
    volume.fsType = QStringLiteral("filesystem");
    volume.serialNumber = QString();
    volume.updatedUtc = SqlHelpers::utcNowIso();

    QString dbError;
    if (!m_store.upsertVolume(volume, &state.volumeId, &dbError)) {
        state.errorText = QStringLiteral("upsert_volume_failed: %1").arg(dbError);
        *outResult = state;
        return false;
    }

    ScanSessionRecord session;
    session.rootPath = rootPath;
    session.mode = task.mode.isEmpty() ? QStringLiteral("full") : task.mode;
    session.status = QStringLiteral("running");
    session.startedUtc = SqlHelpers::utcNowIso();

    if (!m_store.createScanSession(session, &state.sessionId, &dbError)) {
        state.errorText = QStringLiteral("create_scan_session_failed: %1").arg(dbError);
        *outResult = state;
        return false;
    }

    ScanTask workerTask = task;
    workerTask.volumeId = state.volumeId;
    workerTask.scanSessionId = state.sessionId;

    ScanWorker worker;
    ScanWorker::Callbacks workerCallbacks;
    workerCallbacks.onBatch = [&](const QVector<ScanIngestItem>& batch) {
        QString ingestError;
        if (!ingestBatch(batch, &state, &ingestError) && state.errorText.isEmpty()) {
            state.errorText = ingestError;
        }
        if (callbacks.onProgress) {
            callbacks.onProgress(state);
        }
    };
    workerCallbacks.onProgress = [&](const ScanWorkerProgress& progress) {
        state.totalSeen = progress.totalSeen;
        state.errorCount = progress.errorCount;
        QString progressError;
        if (!m_store.updateScanSessionProgress(state.sessionId,
                                               state.totalSeen,
                                               state.totalInserted,
                                               state.totalUpdated,
                                               state.totalRemoved,
                                               &progressError) && callbacks.onLog) {
            callbacks.onLog(QStringLiteral("scan_session_progress_update_failed error=%1").arg(progressError));
        }
        if (callbacks.onProgress) {
            callbacks.onProgress(state);
        }
    };
    workerCallbacks.onLog = callbacks.onLog;

    const ScanWorker::Result workerResult = worker.run(workerTask, workerCallbacks);
    state.totalSeen = workerResult.progress.totalSeen;
    state.errorCount = workerResult.progress.errorCount;

    if (!workerResult.errorText.isEmpty() && state.errorText.isEmpty()) {
        state.errorText = workerResult.errorText;
    }

    if (!state.errorText.isEmpty()) {
        QString failError;
        m_store.failScanSession(state.sessionId,
                                state.totalSeen,
                                state.totalInserted,
                                state.totalUpdated,
                                state.totalRemoved,
                                state.errorText,
                                &failError);
        state.success = false;
        *outResult = state;
        return false;
    }

    if (workerResult.canceled) {
        QString cancelError;
        if (!m_store.cancelScanSession(state.sessionId,
                                       state.totalSeen,
                                       state.totalInserted,
                                       state.totalUpdated,
                                       state.totalRemoved,
                                       &cancelError) && callbacks.onLog) {
            callbacks.onLog(QStringLiteral("cancel_scan_session_failed error=%1").arg(cancelError));
        }
        state.canceled = true;
        state.success = false;
        *outResult = state;
        return true;
    }

    QString completeError;
    if (!m_store.completeScanSession(state.sessionId,
                                     state.totalSeen,
                                     state.totalInserted,
                                     state.totalUpdated,
                                     state.totalRemoved,
                                     &completeError)) {
        state.errorText = QStringLiteral("complete_scan_session_failed: %1").arg(completeError);
        state.success = false;
        *outResult = state;
        return false;
    }

    state.success = workerResult.success;
    *outResult = state;
    return state.success;
}

bool ScanCoordinator::ingestBatch(const QVector<ScanIngestItem>& batch,
                                  ScanCoordinatorResult* state,
                                  QString* errorText)
{
    if (batch.isEmpty()) {
        return true;
    }

    QString dbError;
    if (!m_store.beginTransaction(&dbError)) {
        if (errorText) {
            *errorText = QStringLiteral("begin_tx_failed: %1").arg(dbError);
        }
        return false;
    }

    bool ok = true;
    for (const ScanIngestItem& item : batch) {
        EntryRecord record = item.record;

        if (!item.parentPath.isEmpty()) {
            bool found = false;
            qint64 parentId = 0;
            if (!m_store.resolveParentIdByPath(item.parentPath, &parentId, &found, &dbError)) {
                ok = false;
                if (errorText) {
                    *errorText = QStringLiteral("resolve_parent_failed path=%1 error=%2").arg(item.parentPath, dbError);
                }
                break;
            }
            record.hasParentId = found;
            record.parentId = parentId;
        } else {
            record.hasParentId = false;
        }

        bool inserted = false;
        bool updated = false;
        if (!m_store.upsertEntry(record, nullptr, &inserted, &updated, &dbError)) {
            ok = false;
            if (errorText) {
                *errorText = QStringLiteral("upsert_entry_failed path=%1 error=%2").arg(record.path, dbError);
            }
            break;
        }

        if (inserted) {
            state->totalInserted += 1;
        }
        if (updated) {
            state->totalUpdated += 1;
        }
    }

    if (!ok) {
        m_store.rollbackTransaction();
        return false;
    }

    if (!m_store.commitTransaction(&dbError)) {
        m_store.rollbackTransaction();
        if (errorText) {
            *errorText = QStringLiteral("commit_tx_failed: %1").arg(dbError);
        }
        return false;
    }

    return true;
}

QString ScanCoordinator::buildVolumeKey(const QString& rootPath) const
{
    const QString absolute = QDir::cleanPath(QDir(rootPath).absolutePath());
    if (absolute.size() >= 2 && absolute.at(1) == QChar(':')) {
        return QStringLiteral("vol:%1").arg(absolute.left(2).toLower());
    }
    return QStringLiteral("vol:%1").arg(absolute.toLower());
}

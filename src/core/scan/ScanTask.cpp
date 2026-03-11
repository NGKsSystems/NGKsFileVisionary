#include "ScanTask.h"

#include <QDir>

QString ScanTask::normalizedRootPath() const
{
    return QDir::cleanPath(QDir(rootPath).absolutePath());
}

int ScanTask::effectiveBatchSize() const
{
    return batchSize > 0 ? batchSize : 500;
}

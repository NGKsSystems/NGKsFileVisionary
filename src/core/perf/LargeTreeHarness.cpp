#include "LargeTreeHarness.h"

#include <QDir>
#include <QFile>
#include <QTextStream>

LargeTreeHarnessResult LargeTreeHarness::createTree(const QString& rootPath, qint64 targetFileCount) const
{
    LargeTreeHarnessResult result;

    if (targetFileCount <= 0) {
        result.errorText = QStringLiteral("invalid_target_file_count");
        return result;
    }

    QDir root(rootPath);
    if (!root.exists() && !QDir().mkpath(rootPath)) {
        result.errorText = QStringLiteral("create_root_failed");
        return result;
    }

    const QStringList top = {
        QStringLiteral("src"),
        QStringLiteral("docs"),
        QStringLiteral("node_modules"),
        QStringLiteral("build")
    };
    for (const QString& part : top) {
        if (!QDir().mkpath(root.filePath(part))) {
            result.errorText = QStringLiteral("create_subdir_failed:%1").arg(part);
            return result;
        }
    }

    qint64 written = 0;
    for (qint64 i = 0; i < targetFileCount; ++i) {
        const QString bucket = (i % 4 == 0)
            ? QStringLiteral("src")
            : (i % 4 == 1)
                ? QStringLiteral("docs")
                : (i % 4 == 2)
                    ? QStringLiteral("node_modules")
                    : QStringLiteral("build");
        const QString dirPart = QStringLiteral("%1/batch_%2").arg(bucket).arg(i / 250);
        if (!QDir().mkpath(root.filePath(dirPart))) {
            result.errorText = QStringLiteral("create_batch_dir_failed:%1").arg(dirPart);
            return result;
        }

        const QString filePath = root.filePath(QStringLiteral("%1/file_%2.txt").arg(dirPart).arg(i));
        QFile f(filePath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            result.errorText = QStringLiteral("create_file_failed:%1").arg(filePath);
            return result;
        }

        QTextStream ts(&f);
        ts << QStringLiteral("seed=%1\n").arg(i);
        f.close();
        ++written;
    }

    result.ok = true;
    result.fileCount = written;
    return result;
}

#pragma once

#include <QString>

struct LargeTreeHarnessResult
{
    bool ok = false;
    qint64 fileCount = 0;
    QString errorText;
};

class LargeTreeHarness
{
public:
    LargeTreeHarnessResult createTree(const QString& rootPath, qint64 targetFileCount) const;
};

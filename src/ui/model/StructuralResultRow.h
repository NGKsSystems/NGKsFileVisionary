#pragma once

#include <QDateTime>
#include <QString>

#include "core/FileScanner.h"

enum class StructuralResultCategory
{
    History,
    Snapshot,
    Diff,
    Reference,
};

struct StructuralResultRow
{
    StructuralResultCategory category = StructuralResultCategory::History;
    QString primaryPath;
    QString secondaryPath;
    QString relationship;
    QString status;
    qint64 snapshotId = 0;
    bool hasSnapshotId = false;
    QString timestamp;
    qint64 sizeBytes = 0;
    bool hasSizeBytes = false;
    QString sourceFile;
    QString symbol;
    QString note;
    int dependencyFrequency = 0;
    int changeFrequency = 0;
    int hubScore = 0;
    double rankScore = 0.0;
};

namespace StructuralResultRowUtil
{
QString categoryToString(StructuralResultCategory category);
FileEntry toFileEntry(const StructuralResultRow& row);
}

#pragma once

#include <QString>

namespace ReferenceGraph
{
struct ReferenceEdge
{
    qint64 id = 0;
    QString sourceRoot;
    QString sourcePath;
    QString targetPath;
    QString rawTarget;
    QString referenceType;
    bool resolvedFlag = false;
    QString confidence;
    int sourceLine = 0;
    QString createdUtc;
    QString extractorVersion;
};
}

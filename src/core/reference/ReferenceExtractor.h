#pragma once

#include <QString>
#include <QVector>

#include "ReferenceEdge.h"

namespace ReferenceGraph
{
class ReferenceExtractor
{
public:
    bool supportsFile(const QString& path) const;

    bool extractFromFile(const QString& sourcePath,
                         const QString& sourceRoot,
                         QVector<ReferenceEdge>* out,
                         QString* errorText = nullptr) const;

private:
    QString resolveTargetPath(const QString& sourcePath,
                              const QString& sourceRoot,
                              const QString& rawTarget,
                              const QString& referenceType,
                              bool* resolved) const;
};
}

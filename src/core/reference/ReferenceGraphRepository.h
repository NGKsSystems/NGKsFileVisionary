#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "ReferenceEdge.h"

class MetaStore;

namespace ReferenceGraph
{
class ReferenceGraphRepository
{
public:
    explicit ReferenceGraphRepository(MetaStore& store);

    bool deleteBySourceRoot(const QString& sourceRoot, QString* errorText = nullptr);
    bool insertEdges(const QVector<ReferenceEdge>& edges, QString* errorText = nullptr);

    bool listOutgoingReferences(const QString& sourcePath,
                                QVector<ReferenceEdge>* out,
                                QString* errorText = nullptr) const;
    bool listIncomingReferences(const QString& targetPath,
                                QVector<ReferenceEdge>* out,
                                QString* errorText = nullptr) const;
    bool listBySourceRoot(const QString& sourceRoot,
                          QVector<ReferenceEdge>* out,
                          QString* errorText = nullptr) const;

    bool listDistinctSourcePathsByRoot(const QString& sourceRoot,
                                       QStringList* out,
                                       QString* errorText = nullptr) const;
    bool listDistinctTargetPathsByRoot(const QString& sourceRoot,
                                       QStringList* out,
                                       QString* errorText = nullptr) const;

private:
    MetaStore& m_store;
};
}

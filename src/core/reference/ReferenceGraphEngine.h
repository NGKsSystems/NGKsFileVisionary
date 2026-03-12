#pragma once

#include <QString>
#include <QVector>

#include "ReferenceEdge.h"

class MetaStore;

namespace ReferenceGraph
{
class ReferenceExtractor;
class ReferenceGraphRepository;

class ReferenceGraphEngine
{
public:
    explicit ReferenceGraphEngine(MetaStore& store);
    ~ReferenceGraphEngine();

    bool scanReferencesUnderRoot(const QString& rootPath,
                                 qint64* outCount = nullptr,
                                 QString* errorText = nullptr);

    bool extractReferencesForFile(const QString& sourcePath,
                                  const QString& sourceRoot,
                                  QVector<ReferenceEdge>* out,
                                  QString* errorText = nullptr);

    bool storeReferenceEdges(const QVector<ReferenceEdge>& edges, QString* errorText = nullptr);

    bool listOutgoingReferences(const QString& path,
                                QVector<ReferenceEdge>* out,
                                QString* errorText = nullptr) const;

    bool listIncomingReferences(const QString& path,
                                QVector<ReferenceEdge>* out,
                                QString* errorText = nullptr) const;

    bool listReferencesUnderRoot(const QString& rootPath,
                                 QVector<ReferenceEdge>* out,
                                 QString* errorText = nullptr) const;

private:
    MetaStore& m_store;
    ReferenceExtractor* m_extractor;
    ReferenceGraphRepository* m_repository;
};
}

#include "ReferenceGraphEngine.h"

#include <QDir>

#include "ReferenceExtractor.h"
#include "ReferenceGraphRepository.h"
#include "core/db/DbTypes.h"
#include "core/db/MetaStore.h"

namespace ReferenceGraph
{
ReferenceGraphEngine::ReferenceGraphEngine(MetaStore& store)
    : m_store(store)
    , m_extractor(new ReferenceExtractor())
    , m_repository(new ReferenceGraphRepository(store))
{
}

ReferenceGraphEngine::~ReferenceGraphEngine()
{
    delete m_repository;
    m_repository = nullptr;

    delete m_extractor;
    m_extractor = nullptr;
}

bool ReferenceGraphEngine::scanReferencesUnderRoot(const QString& rootPath,
                                                   qint64* outCount,
                                                   QString* errorText)
{
    const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(rootPath));

    QVector<EntryRecord> rows;
    if (!m_store.findSubtreeByRootPath(normalizedRoot, -1, &rows, errorText)) {
        return false;
    }

    QVector<ReferenceEdge> allEdges;
    for (const EntryRecord& row : rows) {
        if (row.isDir || !row.existsFlag) {
            continue;
        }
        if (!m_extractor->supportsFile(row.path)) {
            continue;
        }

        QVector<ReferenceEdge> perFile;
        if (!extractReferencesForFile(row.path, normalizedRoot, &perFile, errorText)) {
            return false;
        }
        allEdges += perFile;
    }

    QString txError;
    if (!m_store.beginTransaction(&txError)) {
        if (errorText) {
            *errorText = QStringLiteral("begin_tx_failed:%1").arg(txError);
        }
        return false;
    }

    if (!m_repository->deleteBySourceRoot(normalizedRoot, &txError)) {
        m_store.rollbackTransaction(nullptr);
        if (errorText) {
            *errorText = QStringLiteral("delete_root_edges_failed:%1").arg(txError);
        }
        return false;
    }

    if (!m_repository->insertEdges(allEdges, &txError)) {
        m_store.rollbackTransaction(nullptr);
        if (errorText) {
            *errorText = QStringLiteral("insert_edges_failed:%1").arg(txError);
        }
        return false;
    }

    if (!m_store.commitTransaction(&txError)) {
        m_store.rollbackTransaction(nullptr);
        if (errorText) {
            *errorText = QStringLiteral("commit_tx_failed:%1").arg(txError);
        }
        return false;
    }

    if (outCount) {
        *outCount = allEdges.size();
    }
    return true;
}

bool ReferenceGraphEngine::extractReferencesForFile(const QString& sourcePath,
                                                    const QString& sourceRoot,
                                                    QVector<ReferenceEdge>* out,
                                                    QString* errorText)
{
    return m_extractor->extractFromFile(sourcePath, sourceRoot, out, errorText);
}

bool ReferenceGraphEngine::storeReferenceEdges(const QVector<ReferenceEdge>& edges, QString* errorText)
{
    return m_repository->insertEdges(edges, errorText);
}

bool ReferenceGraphEngine::listOutgoingReferences(const QString& path,
                                                  QVector<ReferenceEdge>* out,
                                                  QString* errorText) const
{
    const QString normalizedPath = QDir::fromNativeSeparators(QDir::cleanPath(path));
    return m_repository->listOutgoingReferences(normalizedPath, out, errorText);
}

bool ReferenceGraphEngine::listIncomingReferences(const QString& path,
                                                  QVector<ReferenceEdge>* out,
                                                  QString* errorText) const
{
    const QString normalizedPath = QDir::fromNativeSeparators(QDir::cleanPath(path));
    return m_repository->listIncomingReferences(normalizedPath, out, errorText);
}

bool ReferenceGraphEngine::listReferencesUnderRoot(const QString& rootPath,
                                                   QVector<ReferenceEdge>* out,
                                                   QString* errorText) const
{
    const QString normalizedRoot = QDir::fromNativeSeparators(QDir::cleanPath(rootPath));
    return m_repository->listBySourceRoot(normalizedRoot, out, errorText);
}
}

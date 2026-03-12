#include "ReferenceGraphRepository.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>

#include "core/db/MetaStore.h"
#include "core/db/SqlHelpers.h"

namespace ReferenceGraph
{
namespace
{
ReferenceEdge fromQuery(const QSqlQuery& q)
{
    ReferenceEdge edge;
    edge.id = q.value(0).toLongLong();
    edge.sourceRoot = q.value(1).toString();
    edge.sourcePath = q.value(2).toString();
    edge.targetPath = q.value(3).toString();
    edge.rawTarget = q.value(4).toString();
    edge.referenceType = q.value(5).toString();
    edge.resolvedFlag = q.value(6).toInt() != 0;
    edge.confidence = q.value(7).toString();
    edge.sourceLine = q.value(8).toInt();
    edge.createdUtc = q.value(9).toString();
    edge.extractorVersion = q.value(10).toString();
    return edge;
}
}

ReferenceGraphRepository::ReferenceGraphRepository(MetaStore& store)
    : m_store(store)
{
}

bool ReferenceGraphRepository::deleteBySourceRoot(const QString& sourceRoot, QString* errorText)
{
    QSqlDatabase db = m_store.database();
    if (!db.isValid() || !db.isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM reference_edges WHERE source_root=?;"));
    q.addBindValue(sourceRoot);
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }
    return true;
}

bool ReferenceGraphRepository::insertEdges(const QVector<ReferenceEdge>& edges, QString* errorText)
{
    QSqlDatabase db = m_store.database();
    if (!db.isValid() || !db.isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "INSERT INTO reference_edges(source_root, source_path, target_path, raw_target, reference_type, resolved_flag, confidence, source_line, created_utc, extractor_version) "
        "VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?);"));

    for (const ReferenceEdge& edge : edges) {
        q.bindValue(0, edge.sourceRoot);
        q.bindValue(1, edge.sourcePath);
        q.bindValue(2, edge.targetPath);
        q.bindValue(3, edge.rawTarget);
        q.bindValue(4, edge.referenceType);
        q.bindValue(5, SqlHelpers::boolToInt(edge.resolvedFlag));
        q.bindValue(6, edge.confidence);
        q.bindValue(7, edge.sourceLine > 0 ? QVariant(edge.sourceLine) : QVariant(QVariant::Int));
        q.bindValue(8, edge.createdUtc);
        q.bindValue(9, edge.extractorVersion);

        if (!q.exec()) {
            if (errorText) {
                *errorText = q.lastError().text();
            }
            return false;
        }
    }

    return true;
}

bool ReferenceGraphRepository::listOutgoingReferences(const QString& sourcePath,
                                                      QVector<ReferenceEdge>* out,
                                                      QString* errorText) const
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_vector");
        }
        return false;
    }

    QSqlDatabase db = m_store.database();
    if (!db.isValid() || !db.isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    out->clear();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, source_root, source_path, target_path, raw_target, reference_type, resolved_flag, confidence, source_line, created_utc, extractor_version "
        "FROM reference_edges WHERE source_path=? ORDER BY source_line ASC, id ASC;"));
    q.addBindValue(sourcePath);
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    while (q.next()) {
        out->push_back(fromQuery(q));
    }
    return true;
}

bool ReferenceGraphRepository::listIncomingReferences(const QString& targetPath,
                                                      QVector<ReferenceEdge>* out,
                                                      QString* errorText) const
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_vector");
        }
        return false;
    }

    QSqlDatabase db = m_store.database();
    if (!db.isValid() || !db.isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    out->clear();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, source_root, source_path, target_path, raw_target, reference_type, resolved_flag, confidence, source_line, created_utc, extractor_version "
        "FROM reference_edges WHERE target_path=? ORDER BY source_path ASC, source_line ASC, id ASC;"));
    q.addBindValue(targetPath);
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    while (q.next()) {
        out->push_back(fromQuery(q));
    }
    return true;
}

bool ReferenceGraphRepository::listBySourceRoot(const QString& sourceRoot,
                                                QVector<ReferenceEdge>* out,
                                                QString* errorText) const
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_vector");
        }
        return false;
    }

    QSqlDatabase db = m_store.database();
    if (!db.isValid() || !db.isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    out->clear();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT id, source_root, source_path, target_path, raw_target, reference_type, resolved_flag, confidence, source_line, created_utc, extractor_version "
        "FROM reference_edges WHERE source_root=? ORDER BY source_path ASC, source_line ASC, id ASC;"));
    q.addBindValue(sourceRoot);
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    while (q.next()) {
        out->push_back(fromQuery(q));
    }
    return true;
}

bool ReferenceGraphRepository::listDistinctSourcePathsByRoot(const QString& sourceRoot,
                                                             QStringList* out,
                                                             QString* errorText) const
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_list");
        }
        return false;
    }

    QSqlDatabase db = m_store.database();
    if (!db.isValid() || !db.isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    out->clear();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT DISTINCT source_path "
        "FROM reference_edges WHERE source_root=? "
        "ORDER BY source_path ASC;"));
    q.addBindValue(sourceRoot);
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    while (q.next()) {
        out->append(q.value(0).toString());
    }
    return true;
}

bool ReferenceGraphRepository::listDistinctTargetPathsByRoot(const QString& sourceRoot,
                                                             QStringList* out,
                                                             QString* errorText) const
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_list");
        }
        return false;
    }

    QSqlDatabase db = m_store.database();
    if (!db.isValid() || !db.isOpen()) {
        if (errorText) {
            *errorText = QStringLiteral("metastore_not_ready");
        }
        return false;
    }

    out->clear();
    QSqlQuery q(db);
    q.prepare(QStringLiteral(
        "SELECT DISTINCT target_path "
        "FROM reference_edges WHERE source_root=? "
        "ORDER BY target_path ASC;"));
    q.addBindValue(sourceRoot);
    if (!q.exec()) {
        if (errorText) {
            *errorText = q.lastError().text();
        }
        return false;
    }

    while (q.next()) {
        out->append(q.value(0).toString());
    }
    return true;
}
}

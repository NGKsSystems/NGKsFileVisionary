#include "DbConnection.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

DbConnection::DbConnection()
{
}

DbConnection::~DbConnection()
{
    close();
}

bool DbConnection::open(const QString& dbPath)
{
    close();

    const QFileInfo dbInfo(dbPath);
    QDir().mkpath(dbInfo.absolutePath());

    m_connectionName = QStringLiteral("filevisionary_db_%1").arg(QUuid::createUuid().toString(QUuid::Id128));
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        return false;
    }

    m_dbPath = dbPath;
    m_lastError.clear();
    return applyPragmas();
}

void DbConnection::close()
{
    if (!m_connectionName.isEmpty()) {
        if (m_db.isOpen()) {
            m_db.close();
        }
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase(m_connectionName);
        m_connectionName.clear();
    }
}

bool DbConnection::isOpen() const
{
    return m_db.isOpen();
}

QString DbConnection::lastError() const
{
    return m_lastError;
}

QString DbConnection::databasePath() const
{
    return m_dbPath;
}

bool DbConnection::beginTransaction()
{
    if (!m_db.transaction()) {
        m_lastError = m_db.lastError().text();
        return false;
    }
    return true;
}

bool DbConnection::commit()
{
    if (!m_db.commit()) {
        m_lastError = m_db.lastError().text();
        return false;
    }
    return true;
}

bool DbConnection::rollback()
{
    if (!m_db.rollback()) {
        m_lastError = m_db.lastError().text();
        return false;
    }
    return true;
}

QSqlDatabase DbConnection::database() const
{
    return m_db;
}

bool DbConnection::applyPragmas()
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("PRAGMA foreign_keys = ON;"))) {
        m_lastError = q.lastError().text();
        return false;
    }

    if (!q.exec(QStringLiteral("PRAGMA journal_mode = WAL;"))) {
        m_lastError = q.lastError().text();
        return false;
    }

    if (!q.exec(QStringLiteral("PRAGMA synchronous = NORMAL;"))) {
        m_lastError = q.lastError().text();
        return false;
    }

    if (!q.exec(QStringLiteral("PRAGMA temp_store = MEMORY;"))) {
        m_lastError = q.lastError().text();
        return false;
    }

    if (!q.exec(QStringLiteral("PRAGMA mmap_size = 268435456;"))) {
        m_lastError = q.lastError().text();
        return false;
    }

    return true;
}

#pragma once

#include <QSqlDatabase>
#include <QString>

class DbConnection
{
public:
    DbConnection();
    ~DbConnection();

    bool open(const QString& dbPath);
    void close();

    bool isOpen() const;
    QString lastError() const;
    QString databasePath() const;

    bool beginTransaction();
    bool commit();
    bool rollback();

    QSqlDatabase database() const;

private:
    bool applyPragmas();

private:
    QString m_connectionName;
    QString m_dbPath;
    QString m_lastError;
    QSqlDatabase m_db;
};

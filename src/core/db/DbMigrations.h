#pragma once

#include <QString>

class DbConnection;

class DbMigrations
{
public:
    static constexpr int kSchemaVersion = 1;

    static bool migrate(DbConnection& connection, QString* migrationLog = nullptr);
    static int currentVersion(DbConnection& connection, QString* errorText = nullptr);

private:
    static bool ensureSchemaInfoTable(DbConnection& connection, QString* errorText = nullptr);
    static bool applyV1(DbConnection& connection, QString* errorText = nullptr);
};

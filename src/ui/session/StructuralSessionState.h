#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

struct StructuralSessionState
{
    bool panelOpen = false;
    int activeTab = 0;
    int viewMode = 0;
    QString currentQuery;
    QString rootPath;
    QString targetPath;

    QString categoryFilter;
    QString statusFilter;
    QString extensionFilter;
    QString relationshipFilter;
    QString textFilter;

    int sortField = 0;
    int sortDirection = 0;

    int historyIndex = -1;
    QStringList queryHistory;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& json, StructuralSessionState* out, QString* errorText = nullptr);
};

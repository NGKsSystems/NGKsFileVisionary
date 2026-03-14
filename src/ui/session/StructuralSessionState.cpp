#include "StructuralSessionState.h"

#include <QJsonArray>

QJsonObject StructuralSessionState::toJson() const
{
    QJsonObject out;
    out.insert(QStringLiteral("panelOpen"), panelOpen);
    out.insert(QStringLiteral("activeTab"), activeTab);
    out.insert(QStringLiteral("viewMode"), viewMode);
    out.insert(QStringLiteral("currentQuery"), currentQuery);
    out.insert(QStringLiteral("rootPath"), rootPath);
    out.insert(QStringLiteral("targetPath"), targetPath);

    out.insert(QStringLiteral("categoryFilter"), categoryFilter);
    out.insert(QStringLiteral("statusFilter"), statusFilter);
    out.insert(QStringLiteral("extensionFilter"), extensionFilter);
    out.insert(QStringLiteral("relationshipFilter"), relationshipFilter);
    out.insert(QStringLiteral("textFilter"), textFilter);

    out.insert(QStringLiteral("sortField"), sortField);
    out.insert(QStringLiteral("sortDirection"), sortDirection);

    out.insert(QStringLiteral("historyIndex"), historyIndex);
    QJsonArray history;
    for (const QString& query : queryHistory) {
        history.push_back(query);
    }
    out.insert(QStringLiteral("queryHistory"), history);
    return out;
}

bool StructuralSessionState::fromJson(const QJsonObject& json, StructuralSessionState* out, QString* errorText)
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("output_state_missing");
        }
        return false;
    }

    StructuralSessionState state;
    state.panelOpen = json.value(QStringLiteral("panelOpen")).toBool(false);
    state.activeTab = json.value(QStringLiteral("activeTab")).toInt(0);
    state.viewMode = json.value(QStringLiteral("viewMode")).toInt(0);
    state.currentQuery = json.value(QStringLiteral("currentQuery")).toString();
    state.rootPath = json.value(QStringLiteral("rootPath")).toString();
    state.targetPath = json.value(QStringLiteral("targetPath")).toString();

    state.categoryFilter = json.value(QStringLiteral("categoryFilter")).toString();
    state.statusFilter = json.value(QStringLiteral("statusFilter")).toString();
    state.extensionFilter = json.value(QStringLiteral("extensionFilter")).toString();
    state.relationshipFilter = json.value(QStringLiteral("relationshipFilter")).toString();
    state.textFilter = json.value(QStringLiteral("textFilter")).toString();

    state.sortField = json.value(QStringLiteral("sortField")).toInt(0);
    state.sortDirection = json.value(QStringLiteral("sortDirection")).toInt(0);

    state.historyIndex = json.value(QStringLiteral("historyIndex")).toInt(-1);
    const QJsonArray history = json.value(QStringLiteral("queryHistory")).toArray();
    for (const QJsonValue& value : history) {
        state.queryHistory.push_back(value.toString());
    }

    *out = state;
    return true;
}

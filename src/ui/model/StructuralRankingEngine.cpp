#include "StructuralRankingEngine.h"

#include <QHash>

namespace {
bool isChangeLikeStatus(const QString& status)
{
    const QString normalized = status.trimmed().toLower();
    return normalized == QStringLiteral("added")
        || normalized == QStringLiteral("changed")
        || normalized == QStringLiteral("removed");
}
}

namespace StructuralRankingEngine
{
void computeRanking(QVector<StructuralResultRow>* rows)
{
    if (!rows || rows->isEmpty()) {
        return;
    }

    QHash<QString, int> primaryFrequency;
    QHash<QString, int> hubByPath;
    QHash<QString, int> changeByPath;

    for (const StructuralResultRow& row : *rows) {
        const QString key = row.primaryPath.trimmed().toLower();
        primaryFrequency[key] = primaryFrequency.value(key) + 1;

        if (row.category == StructuralResultCategory::Reference) {
            const QString target = (row.secondaryPath.isEmpty() ? row.primaryPath : row.secondaryPath).trimmed().toLower();
            hubByPath[target] = hubByPath.value(target) + 1;
        }

        if (isChangeLikeStatus(row.status)) {
            changeByPath[key] = changeByPath.value(key) + 1;
        }
    }

    for (StructuralResultRow& row : *rows) {
        const QString key = row.primaryPath.trimmed().toLower();
        row.dependencyFrequency = primaryFrequency.value(key);
        row.changeFrequency = changeByPath.value(key);
        row.hubScore = hubByPath.value(key);

        row.rankScore = static_cast<double>(row.dependencyFrequency)
            + (static_cast<double>(row.changeFrequency) * 1.5)
            + (static_cast<double>(row.hubScore) * 2.0);
    }
}
}

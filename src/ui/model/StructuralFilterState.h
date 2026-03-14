#pragma once

#include <QSet>
#include <QString>

struct StructuralFilterState
{
    QSet<QString> categories;
    QSet<QString> statuses;
    QSet<QString> extensions;
    QSet<QString> relationships;
    QString substring;

    void clear();
    bool isEmpty() const;
};

namespace StructuralFilterStateUtil
{
QString normalizeToken(const QString& value);
}

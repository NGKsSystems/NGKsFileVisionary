#pragma once

#include <QStringList>

struct StructuralAutocompleteContext
{
    QStringList knownPaths;
    QStringList snapshotTokens;
    QString currentTargetPath;
};

namespace StructuralQueryAutocomplete
{
QStringList buildSuggestions(const QString& input, const StructuralAutocompleteContext& context);
}

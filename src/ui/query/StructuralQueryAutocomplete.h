#pragma once

#include <QStringList>

struct StructuralAutocompleteContext
{
    QStringList knownPaths;
    QStringList knownDirectories;
    QStringList knownFiles;
    QStringList snapshotTokens;
    QString currentTargetPath;
    QString currentRootPath;
};

namespace StructuralQueryAutocomplete
{
QStringList buildSuggestions(const QString& input, const StructuralAutocompleteContext& context);
}

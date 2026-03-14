#include "StructuralSuggestionModel.h"

StructuralSuggestionModel::StructuralSuggestionModel(QObject* parent)
    : QStringListModel(parent)
{
}

void StructuralSuggestionModel::setSuggestions(const QStringList& suggestions)
{
    setStringList(suggestions);
}

QStringList StructuralSuggestionModel::suggestions() const
{
    return stringList();
}

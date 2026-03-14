#include "StructuralFilterState.h"

void StructuralFilterState::clear()
{
    categories.clear();
    statuses.clear();
    extensions.clear();
    relationships.clear();
    substring.clear();
}

bool StructuralFilterState::isEmpty() const
{
    return categories.isEmpty()
        && statuses.isEmpty()
        && extensions.isEmpty()
        && relationships.isEmpty()
        && substring.trimmed().isEmpty();
}

namespace StructuralFilterStateUtil
{
QString normalizeToken(const QString& value)
{
    return value.trimmed().toLower();
}
}

#include "StructuralFilterEngine.h"

#include <QFileInfo>

namespace {
bool setMatch(const QSet<QString>& allowed, const QString& value)
{
    return allowed.isEmpty() || allowed.contains(StructuralFilterStateUtil::normalizeToken(value));
}

bool substringMatch(const StructuralResultRow& row, const QString& needle)
{
    if (needle.isEmpty()) {
        return true;
    }

    const Qt::CaseSensitivity cs = Qt::CaseInsensitive;
    return row.primaryPath.contains(needle, cs)
        || row.secondaryPath.contains(needle, cs)
        || row.note.contains(needle, cs)
        || row.sourceFile.contains(needle, cs);
}
}

namespace StructuralFilterEngine
{
QString extensionForPath(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().trimmed().toLower();
    return suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix;
}

QVector<StructuralResultRow> apply(const QVector<StructuralResultRow>& rows, const StructuralFilterState& state)
{
    QVector<StructuralResultRow> out;
    out.reserve(rows.size());

    const QString normalizedSubstring = state.substring.trimmed();
    for (const StructuralResultRow& row : rows) {
        const QString category = StructuralResultRowUtil::categoryToString(row.category);
        const QString extension = extensionForPath(row.primaryPath);

        if (!setMatch(state.categories, category)) {
            continue;
        }
        if (!setMatch(state.statuses, row.status)) {
            continue;
        }
        if (!setMatch(state.extensions, extension)) {
            continue;
        }
        if (!setMatch(state.relationships, row.relationship)) {
            continue;
        }
        if (!substringMatch(row, normalizedSubstring)) {
            continue;
        }

        out.push_back(row);
    }

    return out;
}
}

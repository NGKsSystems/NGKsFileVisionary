#include "StructuralQueryAutocomplete.h"

#include <QRegularExpression>

#include <algorithm>

namespace {
QStringList structuralPrefixes()
{
    return {
        QStringLiteral("history:"),
        QStringLiteral("snapshots:"),
        QStringLiteral("diff:"),
        QStringLiteral("references:"),
        QStringLiteral("usedby:"),
    };
}

QString normalized(const QString& text)
{
    return text.trimmed().toLower();
}

QStringList dedupeSorted(const QStringList& input)
{
    QStringList out = input;
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });
    out.removeDuplicates();
    return out;
}

QStringList filterByPrefix(const QStringList& candidates, const QString& prefix)
{
    const QString token = normalized(prefix);
    QStringList out;
    for (const QString& candidate : candidates) {
        if (normalized(candidate).startsWith(token)) {
            out.push_back(candidate);
        }
    }
    return dedupeSorted(out);
}

QStringList buildPrefixSuggestions(const QString& input)
{
    return filterByPrefix(structuralPrefixes(), input);
}

QStringList buildPathSuggestions(const QString& prefix,
                                 const QString& rawTail,
                                 const StructuralAutocompleteContext& context)
{
    QStringList candidates = context.knownPaths;
    if (!context.currentTargetPath.trimmed().isEmpty()) {
        candidates.push_back(context.currentTargetPath.trimmed());
    }
    candidates = dedupeSorted(candidates);

    const QString tail = normalized(rawTail);
    QStringList out;
    for (const QString& path : candidates) {
        if (tail.isEmpty() || normalized(path).startsWith(tail)) {
            out.push_back(prefix + path);
        }
    }
    return dedupeSorted(out);
}

QStringList buildDiffSuggestions(const QString& input, const StructuralAutocompleteContext& context)
{
    QStringList tokens = dedupeSorted(context.snapshotTokens);
    QStringList ids;
    for (const QString& token : tokens) {
        const QRegularExpressionMatch m = QRegularExpression(QStringLiteral("^(\\d+)")).match(token.trimmed());
        if (m.hasMatch()) {
            ids.push_back(m.captured(1));
        }
    }
    ids = dedupeSorted(ids);

    QStringList candidates;
    for (const QString& token : tokens) {
        candidates.push_back(QStringLiteral("diff:") + token);
    }

    for (int i = 0; i < ids.size(); ++i) {
        for (int j = i + 1; j < ids.size(); ++j) {
            candidates.push_back(QStringLiteral("diff:%1:%2").arg(ids.at(i), ids.at(j)));
        }
    }

    if (candidates.isEmpty()) {
        candidates.push_back(QStringLiteral("diff:"));
    }

    return filterByPrefix(dedupeSorted(candidates), input);
}
}

namespace StructuralQueryAutocomplete
{
QStringList buildSuggestions(const QString& input, const StructuralAutocompleteContext& context)
{
    const QString trimmed = input.trimmed();
    const QString lower = trimmed.toLower();

    if (trimmed.isEmpty()) {
        return structuralPrefixes();
    }

    if (!trimmed.contains(':')) {
        return buildPrefixSuggestions(trimmed);
    }

    if (lower.startsWith(QStringLiteral("history:"))) {
        return buildPathSuggestions(QStringLiteral("history:"), trimmed.mid(QStringLiteral("history:").size()), context);
    }

    if (lower.startsWith(QStringLiteral("snapshots:"))) {
        return buildPathSuggestions(QStringLiteral("snapshots:"), trimmed.mid(QStringLiteral("snapshots:").size()), context);
    }

    if (lower.startsWith(QStringLiteral("references:"))) {
        return buildPathSuggestions(QStringLiteral("references:"), trimmed.mid(QStringLiteral("references:").size()), context);
    }

    if (lower.startsWith(QStringLiteral("usedby:"))) {
        return buildPathSuggestions(QStringLiteral("usedby:"), trimmed.mid(QStringLiteral("usedby:").size()), context);
    }

    if (lower.startsWith(QStringLiteral("diff:"))) {
        return buildDiffSuggestions(trimmed, context);
    }

    return QStringList();
}
}

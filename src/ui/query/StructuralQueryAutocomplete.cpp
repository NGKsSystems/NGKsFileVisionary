#include "StructuralQueryAutocomplete.h"

#include <QRegularExpression>

#include <algorithm>

namespace {
QStringList structuralPrefixes()
{
    return {
        QStringLiteral("ext:"),
        QStringLiteral("under:"),
        QStringLiteral("name:"),
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

QString normalizedPathToken(const QString& raw)
{
    return raw.trimmed().replace('\\', '/');
}

QStringList extensionSuggestions(const QString& tokenText)
{
    const QStringList defaults = {
        QStringLiteral("ext:.cpp"),
        QStringLiteral("ext:.h"),
        QStringLiteral("ext:.hpp"),
        QStringLiteral("ext:.c"),
        QStringLiteral("ext:.cc"),
        QStringLiteral("ext:.js"),
        QStringLiteral("ext:.ts"),
        QStringLiteral("ext:.py"),
    };
    return filterByPrefix(defaults, tokenText);
}

QStringList nameSuggestions(const QString& tokenText, const StructuralAutocompleteContext& context)
{
    QStringList candidates;
    for (const QString& path : context.knownPaths) {
        const QString clean = normalizedPathToken(path);
        if (clean.isEmpty()) {
            continue;
        }
        const int slash = clean.lastIndexOf('/');
        const QString name = slash >= 0 ? clean.mid(slash + 1) : clean;
        if (!name.trimmed().isEmpty()) {
            candidates.push_back(QStringLiteral("name:%1").arg(name));
        }
    }
    if (candidates.isEmpty()) {
        candidates.push_back(QStringLiteral("name:main"));
    }
    return filterByPrefix(dedupeSorted(candidates), tokenText);
}

QStringList pathTokenSuggestions(const QString& prefix,
                                 const QString& tokenText,
                                 const QStringList& pathCandidates)
{
    QStringList candidates;
    for (const QString& path : pathCandidates) {
        const QString clean = normalizedPathToken(path);
        if (!clean.isEmpty()) {
            candidates.push_back(prefix + clean);
        }
    }
    if (candidates.isEmpty()) {
        candidates.push_back(prefix);
    }
    return filterByPrefix(dedupeSorted(candidates), tokenText);
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
    const QString cleaned = input;
    const int lastSpace = cleaned.lastIndexOf(QRegularExpression(QStringLiteral("\\s")));
    const QString token = (lastSpace >= 0) ? cleaned.mid(lastSpace + 1).trimmed() : cleaned.trimmed();
    const QString lower = token.toLower();

    if (token.isEmpty()) {
        return structuralPrefixes();
    }

    if (!token.contains(':')) {
        return buildPrefixSuggestions(token);
    }

    if (lower.startsWith(QStringLiteral("ext:"))) {
        return extensionSuggestions(token);
    }

    if (lower.startsWith(QStringLiteral("under:"))) {
        QStringList underCandidates = context.knownDirectories;
        if (!context.currentRootPath.trimmed().isEmpty()) {
            underCandidates.push_back(context.currentRootPath.trimmed());
        }
        return pathTokenSuggestions(QStringLiteral("under:"), token, underCandidates);
    }

    if (lower.startsWith(QStringLiteral("name:"))) {
        return nameSuggestions(token, context);
    }

    if (lower.startsWith(QStringLiteral("history:"))) {
        return buildPathSuggestions(QStringLiteral("history:"), token.mid(QStringLiteral("history:").size()), context);
    }

    if (lower.startsWith(QStringLiteral("snapshots:"))) {
        return buildPathSuggestions(QStringLiteral("snapshots:"), token.mid(QStringLiteral("snapshots:").size()), context);
    }

    if (lower.startsWith(QStringLiteral("references:"))) {
        return pathTokenSuggestions(QStringLiteral("references:"), token, context.knownFiles);
    }

    if (lower.startsWith(QStringLiteral("usedby:"))) {
        return pathTokenSuggestions(QStringLiteral("usedby:"), token, context.knownFiles);
    }

    if (lower.startsWith(QStringLiteral("diff:"))) {
        return buildDiffSuggestions(token, context);
    }

    return QStringList();
}
}

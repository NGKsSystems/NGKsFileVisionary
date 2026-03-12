#include "QueryParser.h"

#include <QDir>
#include <QRegularExpression>

#include "core/query/QueryTypes.h"

namespace {
struct ParsedToken
{
    QString key;
    QString value;
    bool hasColon = false;
    bool isSizeOp = false;
    bool isModifiedOp = false;
    QueryComparator comparator = QueryComparator::None;
    bool negated = false;
};

bool parseBoolValue(const QString& value, bool* out)
{
    if (!out) {
        return false;
    }
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("true")) {
        *out = true;
        return true;
    }
    if (normalized == QStringLiteral("false")) {
        *out = false;
        return true;
    }
    return false;
}

QString normalizeExt(const QString& raw)
{
    QString ext = raw.trimmed().toLower();
    if (ext.isEmpty()) {
        return QString();
    }
    if (!ext.startsWith('.')) {
        ext.prepend('.');
    }
    return ext;
}

bool parseByteSize(const QString& raw, qint64* out)
{
    if (!out) {
        return false;
    }

    const QRegularExpression re(QStringLiteral("^\\s*(\\d+)\\s*(b|kb|mb|gb)?\\s*$"),
                                QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(raw);
    if (!m.hasMatch()) {
        return false;
    }

    bool ok = false;
    const qint64 value = m.captured(1).toLongLong(&ok);
    if (!ok || value < 0) {
        return false;
    }

    const QString unit = m.captured(2).toLower();
    qint64 multiplier = 1;
    if (unit == QStringLiteral("kb")) {
        multiplier = 1024LL;
    } else if (unit == QStringLiteral("mb")) {
        multiplier = 1024LL * 1024LL;
    } else if (unit == QStringLiteral("gb")) {
        multiplier = 1024LL * 1024LL * 1024LL;
    }

    *out = value * multiplier;
    return true;
}

bool parseAgeDurationSeconds(const QString& raw, qint64* out)
{
    if (!out) {
        return false;
    }

    const QRegularExpression re(QStringLiteral("^\\s*(\\d+)\\s*(s|m|h|d|w)?\\s*$"),
                                QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(raw);
    if (!m.hasMatch()) {
        return false;
    }

    bool ok = false;
    const qint64 value = m.captured(1).toLongLong(&ok);
    if (!ok || value < 0) {
        return false;
    }

    const QString unit = m.captured(2).toLower();
    qint64 multiplier = 1;
    if (unit == QStringLiteral("m")) {
        multiplier = 60LL;
    } else if (unit == QStringLiteral("h")) {
        multiplier = 60LL * 60LL;
    } else if (unit == QStringLiteral("d")) {
        multiplier = 24LL * 60LL * 60LL;
    } else if (unit == QStringLiteral("w")) {
        multiplier = 7LL * 24LL * 60LL * 60LL;
    }

    *out = value * multiplier;
    return true;
}

bool parseComparatorExpression(const QString& token,
                               const QString& prefix,
                               QueryComparator* comparator,
                               QString* value)
{
    if (!comparator || !value) {
        return false;
    }

    if (!token.startsWith(prefix, Qt::CaseInsensitive)) {
        return false;
    }

    const QString suffix = token.mid(prefix.size());
    const QStringList candidates = {QStringLiteral("<="), QStringLiteral(">="), QStringLiteral("<"), QStringLiteral(">")};
    for (const QString& c : candidates) {
        if (suffix.startsWith(c)) {
            QueryComparator parsed = QueryComparator::None;
            if (!QueryTypesUtil::parseComparator(c, &parsed)) {
                return false;
            }
            *comparator = parsed;
            *value = suffix.mid(c.size()).trimmed();
            return !value->isEmpty();
        }
    }

    return false;
}

bool parseSingleToken(const QString& term, bool negated, ParsedToken* out, QString* error)
{
    if (!out) {
        return false;
    }

    ParsedToken token;
    token.negated = negated;

    QueryComparator cmp = QueryComparator::None;
    QString rhs;
    if (parseComparatorExpression(term, QStringLiteral("size"), &cmp, &rhs)) {
        token.key = QStringLiteral("size");
        token.value = rhs;
        token.hasColon = false;
        token.isSizeOp = true;
        token.comparator = cmp;
        *out = token;
        return true;
    }
    if (parseComparatorExpression(term, QStringLiteral("modified"), &cmp, &rhs)) {
        token.key = QStringLiteral("modified");
        token.value = rhs;
        token.hasColon = false;
        token.isModifiedOp = true;
        token.comparator = cmp;
        *out = token;
        return true;
    }

    const int sep = term.indexOf(':');
    if (sep <= 0 || sep >= term.size() - 1) {
        if (error) {
            *error = QStringLiteral("malformed_token:%1").arg(term);
        }
        return false;
    }

    token.key = term.left(sep).trimmed().toLower();
    token.value = term.mid(sep + 1).trimmed();
    token.hasColon = true;
    if (token.value.isEmpty()) {
        if (error) {
            *error = QStringLiteral("missing_value_for:%1").arg(token.key);
        }
        return false;
    }

    *out = token;
    return true;
}

void assignNamePositive(QueryPlan* plan, const QString& value)
{
    plan->nameContains = value;
    plan->nameContainsAny.clear();
}

bool applyParsedToken(const ParsedToken& token, QueryPlan* plan, QString* error)
{
    if (!plan) {
        if (error) {
            *error = QStringLiteral("null_plan");
        }
        return false;
    }

    if (token.isSizeOp) {
        if (token.negated) {
            if (error) {
                *error = QStringLiteral("unsupported_not_for:size");
            }
            return false;
        }
        qint64 bytes = 0;
        if (!parseByteSize(token.value, &bytes)) {
            if (error) {
                *error = QStringLiteral("invalid_size_value:%1").arg(token.value);
            }
            return false;
        }
        plan->sizeComparator = token.comparator;
        plan->sizeBytes = bytes;
        return true;
    }

    if (token.isModifiedOp) {
        if (token.negated) {
            if (error) {
                *error = QStringLiteral("unsupported_not_for:modified");
            }
            return false;
        }
        qint64 seconds = 0;
        if (!parseAgeDurationSeconds(token.value, &seconds)) {
            if (error) {
                *error = QStringLiteral("invalid_modified_value:%1").arg(token.value);
            }
            return false;
        }
        plan->modifiedAgeComparator = token.comparator;
        plan->modifiedAgeSeconds = seconds;
        return true;
    }

    const QString& key = token.key;
    const QString& value = token.value;
    if (key == QStringLiteral("ext")) {
        QStringList extensions;
        const QStringList parts = value.split(',', Qt::SkipEmptyParts);
        if (parts.isEmpty()) {
            if (error) {
                *error = QStringLiteral("invalid_ext_value");
            }
            return false;
        }
        for (const QString& part : parts) {
            const QString ext = normalizeExt(part);
            if (ext.isEmpty()) {
                if (error) {
                    *error = QStringLiteral("invalid_ext_value");
                }
                return false;
            }
            if (!extensions.contains(ext)) {
                extensions.push_back(ext);
            }
        }
        if (token.negated) {
            plan->excludedExtensions = extensions;
        } else {
            plan->extensions = extensions;
        }
        return true;
    }

    if (key == QStringLiteral("under")) {
        const QString normalized = QDir::fromNativeSeparators(QDir::cleanPath(value));
        if (token.negated) {
            if (!plan->excludedUnderPaths.contains(normalized, Qt::CaseInsensitive)) {
                plan->excludedUnderPaths.push_back(normalized);
            }
        } else {
            plan->underPath = normalized;
        }
        return true;
    }

    if (key == QStringLiteral("name")) {
        if (token.negated) {
            plan->excludedNameContains = {value};
        } else {
            assignNamePositive(plan, value);
        }
        return true;
    }

    if (key == QStringLiteral("type")) {
        if (token.negated) {
            if (error) {
                *error = QStringLiteral("unsupported_not_for:type");
            }
            return false;
        }
        const QString normalized = value.toLower();
        if (normalized == QStringLiteral("file")) {
            plan->filesOnly = true;
            plan->directoriesOnly = false;
        } else if (normalized == QStringLiteral("dir")) {
            plan->directoriesOnly = true;
            plan->filesOnly = false;
        } else {
            if (error) {
                *error = QStringLiteral("invalid_type_value:%1").arg(value);
            }
            return false;
        }
        return true;
    }

    if (key == QStringLiteral("sort")) {
        if (token.negated) {
            if (error) {
                *error = QStringLiteral("unsupported_not_for:sort");
            }
            return false;
        }
        QuerySortField parsed = QuerySortField::Name;
        if (!QueryTypesUtil::parseSortField(value, &parsed)) {
            if (error) {
                *error = QStringLiteral("invalid_sort_value:%1").arg(value);
            }
            return false;
        }
        plan->sortField = parsed;
        return true;
    }

    if (key == QStringLiteral("order")) {
        if (token.negated) {
            if (error) {
                *error = QStringLiteral("unsupported_not_for:order");
            }
            return false;
        }
        const QString normalized = value.toLower();
        if (normalized == QStringLiteral("asc")) {
            plan->ascending = true;
        } else if (normalized == QStringLiteral("desc")) {
            plan->ascending = false;
        } else {
            if (error) {
                *error = QStringLiteral("invalid_order_value:%1").arg(value);
            }
            return false;
        }
        return true;
    }

    if (key == QStringLiteral("hidden")) {
        if (token.negated) {
            if (error) {
                *error = QStringLiteral("unsupported_not_for:hidden");
            }
            return false;
        }
        bool parsed = false;
        if (!parseBoolValue(value, &parsed)) {
            if (error) {
                *error = QStringLiteral("invalid_hidden_value:%1").arg(value);
            }
            return false;
        }
        plan->includeHidden = parsed;
        return true;
    }

    if (key == QStringLiteral("system")) {
        if (token.negated) {
            if (error) {
                *error = QStringLiteral("unsupported_not_for:system");
            }
            return false;
        }
        bool parsed = false;
        if (!parseBoolValue(value, &parsed)) {
            if (error) {
                *error = QStringLiteral("invalid_system_value:%1").arg(value);
            }
            return false;
        }
        plan->includeSystem = parsed;
        return true;
    }

    if (error) {
        *error = QStringLiteral("unknown_token:%1").arg(key);
    }
    return false;
}

bool applyOrTokenPair(const ParsedToken& left, const ParsedToken& right, QueryPlan* plan, QString* error)
{
    if (left.negated || right.negated) {
        if (error) {
            *error = QStringLiteral("unsupported_or_with_not");
        }
        return false;
    }
    if (left.isSizeOp || right.isSizeOp || left.isModifiedOp || right.isModifiedOp) {
        if (error) {
            *error = QStringLiteral("unsupported_or_operator_type");
        }
        return false;
    }
    if (left.key != right.key) {
        if (error) {
            *error = QStringLiteral("unsupported_or_mixed_tokens:%1|%2").arg(left.key, right.key);
        }
        return false;
    }

    if (left.key == QStringLiteral("ext")) {
        QStringList merged;
        const QStringList leftParts = left.value.split(',', Qt::SkipEmptyParts);
        const QStringList rightParts = right.value.split(',', Qt::SkipEmptyParts);
        for (const QString& item : leftParts + rightParts) {
            const QString ext = normalizeExt(item);
            if (ext.isEmpty()) {
                if (error) {
                    *error = QStringLiteral("invalid_ext_value");
                }
                return false;
            }
            if (!merged.contains(ext)) {
                merged.push_back(ext);
            }
        }
        plan->extensions = merged;
        return true;
    }

    if (left.key == QStringLiteral("name")) {
        plan->nameContains.clear();
        plan->nameContainsAny = {left.value, right.value};
        return true;
    }

    if (error) {
        *error = QStringLiteral("unsupported_or_token:%1").arg(left.key);
    }
    return false;
}
}

QueryParseResult QueryParser::parse(const QString& queryString) const
{
    QueryParseResult result;
    const QString trimmed = queryString.trimmed();
    if (trimmed.isEmpty()) {
        result.errorMessage = QStringLiteral("empty_query");
        return result;
    }

    QueryPlan plan;
    const QStringList terms = trimmed.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    if (terms.isEmpty()) {
        result.errorMessage = QStringLiteral("empty_query");
        return result;
    }

    bool negateNext = false;
    for (int i = 0; i < terms.size(); ++i) {
        const QString term = terms.at(i);
        if (term.compare(QStringLiteral("NOT"), Qt::CaseInsensitive) == 0) {
            if (negateNext) {
                result.errorMessage = QStringLiteral("duplicate_not_operator");
                return result;
            }
            negateNext = true;
            continue;
        }

        if (term.compare(QStringLiteral("OR"), Qt::CaseInsensitive) == 0) {
            result.errorMessage = QStringLiteral("unexpected_or_operator");
            return result;
        }

        ParsedToken left;
        if (!parseSingleToken(term, negateNext, &left, &result.errorMessage)) {
            return result;
        }
        negateNext = false;

        const bool hasOr = (i + 1) < terms.size()
            && terms.at(i + 1).compare(QStringLiteral("OR"), Qt::CaseInsensitive) == 0;
        if (hasOr) {
            if ((i + 2) >= terms.size()) {
                result.errorMessage = QStringLiteral("dangling_or_operator");
                return result;
            }
            ParsedToken right;
            if (!parseSingleToken(terms.at(i + 2), false, &right, &result.errorMessage)) {
                return result;
            }
            if (!applyOrTokenPair(left, right, &plan, &result.errorMessage)) {
                return result;
            }
            i += 2;
            continue;
        }

        if (!applyParsedToken(left, &plan, &result.errorMessage)) {
            return result;
        }
    }

    if (negateNext) {
        result.errorMessage = QStringLiteral("dangling_not_operator");
        return result;
    }

    result.ok = true;
    result.plan = plan;
    return result;
}

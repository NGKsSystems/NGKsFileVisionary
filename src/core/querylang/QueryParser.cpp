#include "QueryParser.h"

#include <QDir>
#include <QRegularExpression>

#include "core/query/QueryTypes.h"

namespace {
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
    for (const QString& term : terms) {
        const int sep = term.indexOf(':');
        if (sep <= 0 || sep >= term.size() - 1) {
            result.errorMessage = QStringLiteral("malformed_token:%1").arg(term);
            return result;
        }

        const QString key = term.left(sep).trimmed().toLower();
        const QString value = term.mid(sep + 1).trimmed();
        if (value.isEmpty()) {
            result.errorMessage = QStringLiteral("missing_value_for:%1").arg(key);
            return result;
        }

        if (key == QStringLiteral("ext")) {
            QStringList extensions;
            const QStringList parts = value.split(',', Qt::SkipEmptyParts);
            if (parts.isEmpty()) {
                result.errorMessage = QStringLiteral("invalid_ext_value");
                return result;
            }
            for (const QString& part : parts) {
                const QString ext = normalizeExt(part);
                if (ext.isEmpty()) {
                    result.errorMessage = QStringLiteral("invalid_ext_value");
                    return result;
                }
                if (!extensions.contains(ext)) {
                    extensions.push_back(ext);
                }
            }
            plan.extensions = extensions;
            continue;
        }

        if (key == QStringLiteral("under")) {
            plan.underPath = QDir::fromNativeSeparators(QDir::cleanPath(value));
            continue;
        }

        if (key == QStringLiteral("name")) {
            plan.nameContains = value;
            continue;
        }

        if (key == QStringLiteral("type")) {
            const QString normalized = value.toLower();
            if (normalized == QStringLiteral("file")) {
                plan.filesOnly = true;
                plan.directoriesOnly = false;
            } else if (normalized == QStringLiteral("dir")) {
                plan.directoriesOnly = true;
                plan.filesOnly = false;
            } else {
                result.errorMessage = QStringLiteral("invalid_type_value:%1").arg(value);
                return result;
            }
            continue;
        }

        if (key == QStringLiteral("sort")) {
            QuerySortField parsed = QuerySortField::Name;
            if (!QueryTypesUtil::parseSortField(value, &parsed)) {
                result.errorMessage = QStringLiteral("invalid_sort_value:%1").arg(value);
                return result;
            }
            plan.sortField = parsed;
            continue;
        }

        if (key == QStringLiteral("order")) {
            const QString normalized = value.toLower();
            if (normalized == QStringLiteral("asc")) {
                plan.ascending = true;
            } else if (normalized == QStringLiteral("desc")) {
                plan.ascending = false;
            } else {
                result.errorMessage = QStringLiteral("invalid_order_value:%1").arg(value);
                return result;
            }
            continue;
        }

        if (key == QStringLiteral("hidden")) {
            bool parsed = false;
            if (!parseBoolValue(value, &parsed)) {
                result.errorMessage = QStringLiteral("invalid_hidden_value:%1").arg(value);
                return result;
            }
            plan.includeHidden = parsed;
            continue;
        }

        if (key == QStringLiteral("system")) {
            bool parsed = false;
            if (!parseBoolValue(value, &parsed)) {
                result.errorMessage = QStringLiteral("invalid_system_value:%1").arg(value);
                return result;
            }
            plan.includeSystem = parsed;
            continue;
        }

        result.errorMessage = QStringLiteral("unknown_token:%1").arg(key);
        return result;
    }

    result.ok = true;
    result.plan = plan;
    return result;
}

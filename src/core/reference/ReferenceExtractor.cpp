#include "ReferenceExtractor.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QTextStream>

namespace ReferenceGraph
{
namespace
{
const QString kExtractorVersion = QStringLiteral("vie_p16_line_pattern_v1");

QString normalizedPath(const QString& path)
{
    return QDir::fromNativeSeparators(QDir::cleanPath(path));
}

bool looksRelative(const QString& value)
{
    return value.startsWith(QStringLiteral("./"))
        || value.startsWith(QStringLiteral("../"));
}

QStringList candidateTargets(const QString& baseCandidate,
                             const QString& referenceType,
                             const QString& sourcePath,
                             const QString& sourceRoot,
                             const QString& rawTarget)
{
    QStringList out;
    auto add = [&](const QString& item) {
        const QString normalized = normalizedPath(item);
        if (!out.contains(normalized, Qt::CaseInsensitive)) {
            out.push_back(normalized);
        }
    };

    if (!baseCandidate.isEmpty()) {
        add(baseCandidate);
    }

    const QFileInfo baseInfo(baseCandidate);
    if (!baseCandidate.isEmpty() && !baseInfo.suffix().isEmpty()) {
        return out;
    }

    const QStringList exts = {
        QStringLiteral(".h"),
        QStringLiteral(".hpp"),
        QStringLiteral(".c"),
        QStringLiteral(".cpp"),
        QStringLiteral(".py"),
        QStringLiteral(".js"),
        QStringLiteral(".ts"),
        QStringLiteral(".tsx"),
        QStringLiteral(".json")
    };

    for (const QString& ext : exts) {
        add(baseCandidate + ext);
    }

    if (referenceType == QStringLiteral("import_ref") || referenceType == QStringLiteral("require_ref")) {
        add(QDir(baseCandidate).filePath(QStringLiteral("index.js")));
        add(QDir(baseCandidate).filePath(QStringLiteral("index.ts")));
        add(QDir(baseCandidate).filePath(QStringLiteral("index.tsx")));
    }

    if (referenceType == QStringLiteral("include_ref")) {
        const QString rootCandidate = QDir(sourceRoot).filePath(rawTarget);
        add(rootCandidate);
        const QFileInfo sourceInfo(sourcePath);
        add(QDir(sourceInfo.absolutePath()).filePath(rawTarget));
    }

    return out;
}

bool parsePythonFromImport(const QString& line, QString* out)
{
    if (!out) {
        return false;
    }
    const QRegularExpression re(QStringLiteral("^\\s*from\\s+([A-Za-z0-9_\\.]+)\\s+import\\s+"));
    const QRegularExpressionMatch m = re.match(line);
    if (!m.hasMatch()) {
        return false;
    }

    QString module = m.captured(1).trimmed();
    if (module.isEmpty()) {
        return false;
    }
    module.replace('.', '/');
    *out = module;
    return true;
}

bool parsePythonImport(const QString& line, QString* out)
{
    if (!out) {
        return false;
    }
    const QRegularExpression re(QStringLiteral("^\\s*import\\s+([A-Za-z0-9_\\.]+)"));
    const QRegularExpressionMatch m = re.match(line);
    if (!m.hasMatch()) {
        return false;
    }

    QString module = m.captured(1).trimmed();
    if (module.isEmpty()) {
        return false;
    }
    module.replace('.', '/');
    *out = module;
    return true;
}
}

bool ReferenceExtractor::supportsFile(const QString& path) const
{
    const QString fileName = QFileInfo(path).fileName();
    if (fileName.compare(QStringLiteral("CMakeLists.txt"), Qt::CaseInsensitive) == 0) {
        return true;
    }

    const QString ext = QFileInfo(path).suffix().toLower();
    return ext == QStringLiteral("cpp")
        || ext == QStringLiteral("h")
        || ext == QStringLiteral("c")
        || ext == QStringLiteral("hpp")
        || ext == QStringLiteral("py")
        || ext == QStringLiteral("js")
        || ext == QStringLiteral("ts")
        || ext == QStringLiteral("tsx")
        || ext == QStringLiteral("json")
        || ext == QStringLiteral("cmake");
}

bool ReferenceExtractor::extractFromFile(const QString& sourcePath,
                                         const QString& sourceRoot,
                                         QVector<ReferenceEdge>* out,
                                         QString* errorText) const
{
    if (!out) {
        if (errorText) {
            *errorText = QStringLiteral("null_output_vector");
        }
        return false;
    }
    out->clear();

    QFile file(sourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorText) {
            *errorText = QStringLiteral("open_failed:%1").arg(file.errorString());
        }
        return false;
    }

    QTextStream stream(&file);

    const QString normalizedSource = normalizedPath(sourcePath);
    const QString normalizedRoot = normalizedPath(sourceRoot);
    QSet<QString> dedupe;
    int lineNo = 0;

    const QRegularExpression includeRe(QStringLiteral("^\\s*#\\s*include\\s*\"([^\"]+)\""));
    const QRegularExpression importFromJsRe(QStringLiteral("\\bimport\\b[^;\\n]*\\bfrom\\s*[\"']([^\"']+)[\"']"));
    const QRegularExpression importBareJsRe(QStringLiteral("^\\s*import\\s*[\"']([^\"']+)[\"']"));
    const QRegularExpression requireRe(QStringLiteral("\\brequire\\s*\\(\\s*[\"']([^\"']+)[\"']\\s*\\)"));
    const QRegularExpression pathLiteralRe(QStringLiteral("[\"']((?:\\./|\\.\\./)[^\"']+)[\"']"));

    auto appendEdge = [&](const QString& rawTarget, const QString& type, int sourceLine, const QString& confidence) {
        const QString key = QStringLiteral("%1|%2|%3|%4")
                                .arg(normalizedSource)
                                .arg(type)
                                .arg(sourceLine)
                                .arg(rawTarget);
        if (dedupe.contains(key)) {
            return;
        }
        dedupe.insert(key);

        bool resolved = false;
        const QString targetPath = resolveTargetPath(normalizedSource,
                                                     normalizedRoot,
                                                     rawTarget,
                                                     type,
                                                     &resolved);

        ReferenceEdge edge;
        edge.sourceRoot = normalizedRoot;
        edge.sourcePath = normalizedSource;
        edge.targetPath = targetPath;
        edge.rawTarget = rawTarget;
        edge.referenceType = type;
        edge.resolvedFlag = resolved;
        edge.confidence = confidence;
        edge.sourceLine = sourceLine;
        edge.createdUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        edge.extractorVersion = kExtractorVersion;
        out->push_back(edge);
    };

    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        ++lineNo;

        const QRegularExpressionMatch includeMatch = includeRe.match(line);
        if (includeMatch.hasMatch()) {
            appendEdge(includeMatch.captured(1).trimmed(), QStringLiteral("include_ref"), lineNo, QStringLiteral("high"));
        }

        QRegularExpressionMatch m = importFromJsRe.match(line);
        if (m.hasMatch()) {
            appendEdge(m.captured(1).trimmed(), QStringLiteral("import_ref"), lineNo, QStringLiteral("medium"));
        }

        m = importBareJsRe.match(line);
        if (m.hasMatch()) {
            appendEdge(m.captured(1).trimmed(), QStringLiteral("import_ref"), lineNo, QStringLiteral("medium"));
        }

        m = requireRe.match(line);
        if (m.hasMatch()) {
            appendEdge(m.captured(1).trimmed(), QStringLiteral("require_ref"), lineNo, QStringLiteral("high"));
        }

        QString pythonTarget;
        if (parsePythonFromImport(line, &pythonTarget)) {
            appendEdge(pythonTarget, QStringLiteral("import_ref"), lineNo, QStringLiteral("low"));
        } else if (parsePythonImport(line, &pythonTarget)) {
            appendEdge(pythonTarget, QStringLiteral("import_ref"), lineNo, QStringLiteral("low"));
        }

        QRegularExpressionMatchIterator it = pathLiteralRe.globalMatch(line);
        while (it.hasNext()) {
            const QRegularExpressionMatch pathMatch = it.next();
            const QString raw = pathMatch.captured(1).trimmed();
            if (raw.isEmpty()) {
                continue;
            }
            appendEdge(raw, QStringLiteral("path_ref"), lineNo, QStringLiteral("medium"));
        }
    }

    return true;
}

QString ReferenceExtractor::resolveTargetPath(const QString& sourcePath,
                                              const QString& sourceRoot,
                                              const QString& rawTarget,
                                              const QString& referenceType,
                                              bool* resolved) const
{
    if (resolved) {
        *resolved = false;
    }

    const QString trimmed = rawTarget.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    const QFileInfo sourceInfo(sourcePath);
    QString baseCandidate;

    if (QFileInfo(trimmed).isAbsolute()) {
        baseCandidate = normalizedPath(trimmed);
    } else if (looksRelative(trimmed) || referenceType == QStringLiteral("include_ref") || referenceType == QStringLiteral("path_ref")) {
        baseCandidate = normalizedPath(QDir(sourceInfo.absolutePath()).filePath(trimmed));
    } else {
        baseCandidate = normalizedPath(QDir(sourceRoot).filePath(trimmed));
    }

    const QStringList candidates = candidateTargets(baseCandidate, referenceType, sourcePath, sourceRoot, trimmed);
    for (const QString& candidate : candidates) {
        if (QFileInfo::exists(candidate)) {
            if (resolved) {
                *resolved = true;
            }
            return candidate;
        }
    }

    return baseCandidate;
}
}

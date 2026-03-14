#include "StructuralExportEngine.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QTextStream>

#include <algorithm>

namespace {
QString csvEscape(const QString& value)
{
    QString out = value;
    out.replace('"', QStringLiteral("\"\""));
    return QStringLiteral("\"") + out + QStringLiteral("\"");
}

QString markdownEscape(const QString& value)
{
    QString out = value;
    out.replace('|', QStringLiteral("\\|"));
    out.replace('\n', QStringLiteral(" "));
    out.replace('\r', QStringLiteral(" "));
    return out;
}

QString rowField(const StructuralResultRow& row, const QString& field)
{
    if (field == QStringLiteral("category")) {
        return StructuralResultRowUtil::categoryToString(row.category);
    }
    if (field == QStringLiteral("primaryPath")) {
        return row.primaryPath;
    }
    if (field == QStringLiteral("secondaryPath")) {
        return row.secondaryPath;
    }
    if (field == QStringLiteral("relationship")) {
        return row.relationship;
    }
    if (field == QStringLiteral("status")) {
        return row.status;
    }
    if (field == QStringLiteral("snapshotId")) {
        return row.hasSnapshotId ? QString::number(row.snapshotId) : QString();
    }
    if (field == QStringLiteral("timestamp")) {
        return row.timestamp;
    }
    if (field == QStringLiteral("sizeBytes")) {
        return row.hasSizeBytes ? QString::number(row.sizeBytes) : QString();
    }
    if (field == QStringLiteral("sourceFile")) {
        return row.sourceFile;
    }
    if (field == QStringLiteral("symbol")) {
        return row.symbol;
    }
    if (field == QStringLiteral("note")) {
        return row.note;
    }
    return QString();
}

QStringList exportFieldOrder()
{
    return {
        QStringLiteral("category"),
        QStringLiteral("primaryPath"),
        QStringLiteral("secondaryPath"),
        QStringLiteral("relationship"),
        QStringLiteral("status"),
        QStringLiteral("snapshotId"),
        QStringLiteral("timestamp"),
        QStringLiteral("sizeBytes"),
        QStringLiteral("sourceFile"),
        QStringLiteral("symbol"),
        QStringLiteral("note"),
    };
}

QJsonObject rowToJson(const StructuralResultRow& row)
{
    QJsonObject out;
    out.insert(QStringLiteral("category"), StructuralResultRowUtil::categoryToString(row.category));
    out.insert(QStringLiteral("primaryPath"), row.primaryPath);
    out.insert(QStringLiteral("secondaryPath"), row.secondaryPath);
    out.insert(QStringLiteral("relationship"), row.relationship);
    out.insert(QStringLiteral("status"), row.status);
    if (row.hasSnapshotId) {
        out.insert(QStringLiteral("snapshotId"), static_cast<double>(row.snapshotId));
    } else {
        out.insert(QStringLiteral("snapshotId"), QString());
    }
    out.insert(QStringLiteral("timestamp"), row.timestamp);
    if (row.hasSizeBytes) {
        out.insert(QStringLiteral("sizeBytes"), static_cast<double>(row.sizeBytes));
    } else {
        out.insert(QStringLiteral("sizeBytes"), QString());
    }
    out.insert(QStringLiteral("sourceFile"), row.sourceFile);
    out.insert(QStringLiteral("symbol"), row.symbol);
    out.insert(QStringLiteral("note"), row.note);
    return out;
}

QString renderJson(const QVector<StructuralResultRow>& rows)
{
    QJsonArray arr;
    for (const StructuralResultRow& row : rows) {
        arr.push_back(rowToJson(row));
    }
    return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Indented));
}

QString renderCsv(const QVector<StructuralResultRow>& rows)
{
    const QStringList fields = exportFieldOrder();
    QString out;
    out += fields.join(',');
    out += '\n';

    for (const StructuralResultRow& row : rows) {
        QStringList cols;
        cols.reserve(fields.size());
        for (const QString& field : fields) {
            cols.push_back(csvEscape(rowField(row, field)));
        }
        out += cols.join(',');
        out += '\n';
    }

    return out;
}

QString renderMarkdown(const QVector<StructuralResultRow>& rows)
{
    const QStringList fields = exportFieldOrder();
    QString out;
    out += QStringLiteral("| ") + fields.join(QStringLiteral(" | ")) + QStringLiteral(" |\n");

    QStringList separators;
    separators.reserve(fields.size());
    for (int i = 0; i < fields.size(); ++i) {
        separators.push_back(QStringLiteral("---"));
    }
    out += QStringLiteral("| ") + separators.join(QStringLiteral(" | ")) + QStringLiteral(" |\n");

    for (const StructuralResultRow& row : rows) {
        QStringList cols;
        cols.reserve(fields.size());
        for (const QString& field : fields) {
            cols.push_back(markdownEscape(rowField(row, field)));
        }
        out += QStringLiteral("| ") + cols.join(QStringLiteral(" | ")) + QStringLiteral(" |\n");
    }

    return out;
}

QString quoteDot(const QString& raw)
{
    QString out = raw;
    out.replace('\\', QStringLiteral("\\\\"));
    out.replace('"', QStringLiteral("\\\""));
    return QStringLiteral("\"") + out + QStringLiteral("\"");
}

QString edgeSource(const StructuralResultRow& row)
{
    if (!row.sourceFile.trimmed().isEmpty()) {
        return row.sourceFile;
    }
    if (!row.secondaryPath.trimmed().isEmpty()) {
        return row.secondaryPath;
    }
    return QString();
}

QString renderGraphViz(const QVector<StructuralResultRow>& rows, bool* hasEdges)
{
    QSet<QString> nodes;
    struct DotEdge {
        QString from;
        QString to;
        QString relationship;
    };
    QVector<DotEdge> edges;

    for (const StructuralResultRow& row : rows) {
        if (!row.primaryPath.trimmed().isEmpty()) {
            nodes.insert(row.primaryPath);
        }
        const QString from = edgeSource(row);
        const QString to = row.primaryPath;
        if (!from.trimmed().isEmpty() && !to.trimmed().isEmpty() && from != to) {
            nodes.insert(from);
            DotEdge edge;
            edge.from = from;
            edge.to = to;
            edge.relationship = row.relationship.trimmed().isEmpty() ? row.status : row.relationship;
            edges.push_back(edge);
        }
    }

    std::sort(edges.begin(), edges.end(), [](const DotEdge& a, const DotEdge& b) {
        if (a.from != b.from) {
            return QString::compare(a.from, b.from, Qt::CaseInsensitive) < 0;
        }
        if (a.to != b.to) {
            return QString::compare(a.to, b.to, Qt::CaseInsensitive) < 0;
        }
        return QString::compare(a.relationship, b.relationship, Qt::CaseInsensitive) < 0;
    });

    if (hasEdges) {
        *hasEdges = !edges.isEmpty();
    }

    QStringList nodeList = nodes.values();
    std::sort(nodeList.begin(), nodeList.end(), [](const QString& a, const QString& b) {
        return QString::compare(a, b, Qt::CaseInsensitive) < 0;
    });

    QString out;
    out += QStringLiteral("digraph structural_export {\n");
    out += QStringLiteral("  rankdir=LR;\n");
    for (const QString& node : nodeList) {
        out += QStringLiteral("  ") + quoteDot(node) + QStringLiteral(";\n");
    }
    for (const DotEdge& edge : edges) {
        out += QStringLiteral("  ") + quoteDot(edge.from) + QStringLiteral(" -> ") + quoteDot(edge.to)
            + QStringLiteral(" [label=") + quoteDot(edge.relationship) + QStringLiteral("];\n");
    }
    out += QStringLiteral("}\n");
    return out;
}
}

namespace StructuralExportEngine
{
QString renderRows(const QVector<StructuralResultRow>& rows,
                   StructuralExportFormat format,
                   bool* hasGraphEdges)
{
    if (hasGraphEdges) {
        *hasGraphEdges = false;
    }

    switch (format) {
    case StructuralExportFormat::Json:
        return renderJson(rows);
    case StructuralExportFormat::Csv:
        return renderCsv(rows);
    case StructuralExportFormat::Markdown:
        return renderMarkdown(rows);
    case StructuralExportFormat::GraphViz:
        return renderGraphViz(rows, hasGraphEdges);
    }

    return QString();
}

bool writeRowsToFile(const QVector<StructuralResultRow>& rows,
                     StructuralExportFormat format,
                     const QString& outputPath,
                     QString* errorText,
                     bool* hasGraphEdges)
{
    if (outputPath.trimmed().isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("empty_output_path");
        }
        return false;
    }

    QFileInfo info(outputPath);
    if (!QDir().mkpath(info.absolutePath())) {
        if (errorText) {
            *errorText = QStringLiteral("unable_to_create_output_directory");
        }
        return false;
    }

    const QString payload = renderRows(rows, format, hasGraphEdges);

    QSaveFile out(outputPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorText) {
            *errorText = QStringLiteral("open_failed:%1").arg(out.errorString());
        }
        return false;
    }

    QTextStream stream(&out);
    stream.setEncoding(QStringConverter::Utf8);
    stream << payload;
    stream.flush();
    if (!out.commit()) {
        if (errorText) {
            *errorText = QStringLiteral("commit_failed:%1").arg(out.errorString());
        }
        return false;
    }

    return true;
}
}

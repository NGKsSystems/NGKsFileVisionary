#include "StructuralSessionStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>

namespace StructuralSessionStore
{
QString defaultStatePath()
{
    const QString appDataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString base = appDataRoot.trimmed().isEmpty()
        ? QDir::currentPath()
        : appDataRoot;
    return QDir(base).filePath(QStringLiteral("structural_session_state.json"));
}

bool saveToPath(const QString& path, const StructuralSessionState& state, QString* errorText)
{
    if (path.trimmed().isEmpty()) {
        if (errorText) {
            *errorText = QStringLiteral("empty_session_path");
        }
        return false;
    }

    QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        if (errorText) {
            *errorText = QStringLiteral("unable_to_create_session_directory");
        }
        return false;
    }

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (errorText) {
            *errorText = QStringLiteral("open_write_failed:%1").arg(out.errorString());
        }
        return false;
    }

    const QJsonDocument doc(state.toJson());
    out.write(doc.toJson(QJsonDocument::Indented));
    if (!out.commit()) {
        if (errorText) {
            *errorText = QStringLiteral("commit_failed:%1").arg(out.errorString());
        }
        return false;
    }

    return true;
}

bool loadFromPath(const QString& path, StructuralSessionState* state, QString* errorText)
{
    if (!state) {
        if (errorText) {
            *errorText = QStringLiteral("output_state_missing");
        }
        return false;
    }

    QFile in(path);
    if (!in.exists()) {
        if (errorText) {
            *errorText = QStringLiteral("session_file_missing");
        }
        return false;
    }
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorText) {
            *errorText = QStringLiteral("open_read_failed:%1").arg(in.errorString());
        }
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(in.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errorText) {
            *errorText = QStringLiteral("json_parse_failed:%1").arg(parseError.errorString());
        }
        return false;
    }

    return StructuralSessionState::fromJson(doc.object(), state, errorText);
}
}

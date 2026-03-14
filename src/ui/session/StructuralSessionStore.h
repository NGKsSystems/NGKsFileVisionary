#pragma once

#include <QString>

#include "StructuralSessionState.h"

namespace StructuralSessionStore
{
QString defaultStatePath();
bool saveToPath(const QString& path, const StructuralSessionState& state, QString* errorText = nullptr);
bool loadFromPath(const QString& path, StructuralSessionState* state, QString* errorText = nullptr);
}

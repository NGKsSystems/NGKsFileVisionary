#pragma once

#include <QVariant>
#include <QString>

namespace SqlHelpers
{
QString utcNowIso();
QString normalizedName(const QString& name);
int boolToInt(bool value);
QVariant nullableInt64(bool hasValue, qint64 value);
}

#include "SqlHelpers.h"

#include <QDateTime>

namespace SqlHelpers
{
QString utcNowIso()
{
    return QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
}

QString normalizedName(const QString& name)
{
    return name.toLower();
}

int boolToInt(bool value)
{
    return value ? 1 : 0;
}

QVariant nullableInt64(bool hasValue, qint64 value)
{
    return hasValue ? QVariant::fromValue(value) : QVariant(QVariant::LongLong);
}
}

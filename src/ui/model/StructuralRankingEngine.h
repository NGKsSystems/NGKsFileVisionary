#pragma once

#include <QVector>

#include "StructuralResultRow.h"

namespace StructuralRankingEngine
{
void computeRanking(QVector<StructuralResultRow>* rows);
}

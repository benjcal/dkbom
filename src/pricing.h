#pragma once

#include <QPair>
#include <QVector>

/* What a line costs. Break quantities are the only pricing rule Digi-Key
 * publishes, and the MOQ is what actually gets billed below the first break, so
 * the two belong together in one place so every model that shows a price can
 * share them. */
namespace pricing {

double unitPriceAt(const QVector<QPair<int, double>> &breaks, int moq, int quantity);

/* Quantity times unit price, for the quantity that will really be bought. */
double lineTotalAt(const QVector<QPair<int, double>> &breaks, int moq, int quantity);

} // namespace pricing

#include "pricing.h"

#include <algorithm>

double pricing::unitPriceAt(const QVector<QPair<int, double>> &breaks, int moq, int quantity)
{
	if (breaks.isEmpty())
		return 0.0;

	/* Below the first break you pay the first break: MOQ is what actually
	 * gets billed, so clamp and never apply the cheaper break. */
	const int effective = std::max(quantity, std::max(moq, 1));
	double price = breaks.first().second;
	for (const auto &entry : breaks) {
		if (entry.first <= effective)
			price = entry.second;
	}
	return price;
}

double pricing::lineTotalAt(const QVector<QPair<int, double>> &breaks, int moq, int quantity)
{
	return unitPriceAt(breaks, moq, quantity) * std::max(quantity, std::max(moq, 1));
}

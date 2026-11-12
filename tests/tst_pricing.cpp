#include <QtTest>

#include "pricing.h"

class PricingTest : public QObject {
	Q_OBJECT

private slots:
	void noBreaksCostNothing();
	void belowFirstBreakPaysFirstBreak();
	void picksHighestBreakAtOrBelow();
	void moqRaisesTheBilledQuantity();
	void lineTotalIsUnitPriceTimesBilledQuantity();
};

namespace {
QVector<QPair<int, double>> breaks()
{
	return { { 1, 0.10 }, { 10, 0.08 }, { 100, 0.05 } };
}
}

void PricingTest::noBreaksCostNothing()
{
	QCOMPARE(pricing::unitPriceAt({}, 1, 1), 0.0);
	QCOMPARE(pricing::lineTotalAt({}, 1, 1), 0.0);
}

void PricingTest::belowFirstBreakPaysFirstBreak()
{
	QCOMPARE(pricing::unitPriceAt(breaks(), 1, 1), 0.10);
	QCOMPARE(pricing::unitPriceAt(breaks(), 1, 5), 0.10);
}

void PricingTest::picksHighestBreakAtOrBelow()
{
	QCOMPARE(pricing::unitPriceAt(breaks(), 1, 10), 0.08);
	QCOMPARE(pricing::unitPriceAt(breaks(), 1, 99), 0.08);
	QCOMPARE(pricing::unitPriceAt(breaks(), 1, 100), 0.05);
	QCOMPARE(pricing::unitPriceAt(breaks(), 1, 1000), 0.05);
}

void PricingTest::moqRaisesTheBilledQuantity()
{
	/* A reel with a 1000 MOQ at a quantity of 10 is bought, and priced, at
	 * 1000. The unit price is the 1000 break, so the line costs $50. */
	QCOMPARE(pricing::unitPriceAt(breaks(), 1000, 10), 0.05);
	QCOMPARE(pricing::lineTotalAt(breaks(), 1000, 10), 50.0);
}

void PricingTest::lineTotalIsUnitPriceTimesBilledQuantity()
{
	QCOMPARE(pricing::lineTotalAt(breaks(), 1, 100), 5.0);
	QVERIFY(qFuzzyCompare(pricing::lineTotalAt(breaks(), 1, 25), 25 * 0.08));
}

QTEST_APPLESS_MAIN(PricingTest)

#include "tst_pricing.moc"

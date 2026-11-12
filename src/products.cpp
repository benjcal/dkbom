#include "products.h"

#include "api.h"
#include "urls.h"
#include "prefs.h"
#include "pricing.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

namespace {
/* Product data barely moves: the photo and the description never do, the price
 * and the stock do. An hour is the same trade the search results make. */
const int kProductTtl = 3600;
const char *kProductPath = "/products/v4/search/";
const char *kProductSuffix = "/productdetails";
}

Products::Products(Api *api, Prefs *prefs, QObject *parent)
	: QObject(parent)
	, m_api(api)
	, m_prefs(prefs)
	, m_assemblies(prefs->assemblies())
{
}

QVariantMap Products::details(const QString &dkpn) const
{
	const Details detail = m_details.value(dkpn.toUpper());

	QVariantMap map;
	if (!detail.valid)
		return map;

	map.insert(QStringLiteral("mpn"), detail.mpn);
	map.insert(QStringLiteral("mfr"), detail.mfr);
	map.insert(QStringLiteral("description"), detail.description);
	map.insert(QStringLiteral("photoUrl"), detail.photoUrl);
	map.insert(QStringLiteral("productUrl"), detail.productUrl);
	map.insert(QStringLiteral("datasheetUrl"), detail.datasheetUrl);
	map.insert(QStringLiteral("packaging"), detail.packaging);
	map.insert(QStringLiteral("moq"), detail.moq);
	map.insert(QStringLiteral("stock"), detail.stock);
	map.insert(QStringLiteral("hasPrice"), !detail.breaks.isEmpty());
	return map;
}

double Products::unitPrice(const QString &dkpn, int lineQty) const
{
	const Details detail = m_details.value(dkpn.toUpper());
	if (!detail.valid)
		return 0.0;

	return pricing::unitPriceAt(detail.breaks, detail.moq, lineQty * m_assemblies);
}

double Products::lineTotal(const QString &dkpn, int lineQty) const
{
	const Details detail = m_details.value(dkpn.toUpper());
	if (!detail.valid)
		return 0.0;

	return pricing::lineTotalAt(detail.breaks, detail.moq, lineQty * m_assemblies);
}

void Products::setAssemblies(int assemblies)
{
	const int clamped = qBound(1, assemblies, 1000000);
	if (clamped == m_assemblies)
		return;

	m_assemblies = clamped;
	m_prefs->setAssemblies(clamped);
	emit assembliesChanged();
	refreshTotals();
}

void Products::setLines(const QVariantList &lines)
{
	if (lines.isEmpty()) {
		clear();
		return;
	}

	QVector<Line> next;
	next.reserve(lines.size());

	for (const QVariant &entry : lines) {
		const QVariantMap map = entry.toMap();
		Line line;
		line.dkpn = map.value(QStringLiteral("dkpn")).toString().trimmed();
		line.qty = map.value(QStringLiteral("qty")).toInt();
		line.dnp = map.value(QStringLiteral("dnp")).toBool();
		next.append(line);

		if (!line.dkpn.isEmpty())
			enqueue(line.dkpn);
	}

	m_lines = next;
	refreshTotals();
	fetchNext();
}

void Products::clear()
{
	++m_generation;
	m_pending.clear();
	m_requested.clear();
	m_lines.clear();
	setStatus(QString());
	refreshTotals();
	emit detailsChanged();
	emit loadingChanged();
}

void Products::enqueue(const QString &dkpn)
{
	const QString key = dkpn.toUpper();
	if (m_requested.contains(key))
		return;

	/* The cache answers instantly for a number seen before; the request is
	 * queued all the same, so one code path decides what a line knows. */
	m_requested.insert(key, true);
	m_pending.append(key);
}

void Products::fetchNext()
{
	if (m_inFlight || m_pending.isEmpty())
		return;

	const QString dkpn = m_pending.takeFirst();
	m_inFlight = true;
	setStatus(QString());
	emit loadingChanged();

	const QString path = QLatin1String(kProductPath)
			     + QString::fromLatin1(QUrl::toPercentEncoding(dkpn))
			     + QLatin1String(kProductSuffix);
	const int generation = m_generation;

	m_api->get(path,
		   [this, dkpn, generation](const QJsonObject &root, const QString &error) {
			   if (generation != m_generation) {
				   m_inFlight = false;
				   emit loadingChanged();
				   fetchNext();
				   return;
			   }

			   m_inFlight = false;

			   if (!error.isEmpty()) {
				   /* Retried next time the project is read: a failed lookup says
					* nothing about whether the number exists. */
				   m_requested.remove(dkpn);
				   setStatus(tr("pricing %1: %2").arg(dkpn, error));
			   } else {
				   parse(dkpn, root);
				   emit detailsChanged();
			   }

			   refreshTotals();
			   emit loadingChanged();
			   fetchNext();
		   },
		   [this, dkpn, generation](const QString &problem) {
			   if (generation != m_generation) {
				   m_inFlight = false;
				   emit loadingChanged();
				   fetchNext();
				   return;
			   }

			   m_inFlight = false;
			   m_requested.remove(dkpn);
			   setStatus(tr("pricing %1: %2").arg(dkpn, problem));
			   emit loadingChanged();
			   fetchNext();
		   },
		   kProductTtl);
}

void Products::parse(const QString &dkpn, const QJsonObject &root)
{
	Details detail;

	const QJsonObject product = root.value(QStringLiteral("Product")).toObject();
	if (product.isEmpty()) {
		m_details.insert(dkpn, detail);
		return;
	}

	detail.valid = true;
	detail.mpn = product.value(QStringLiteral("ManufacturerProductNumber")).toString();
	detail.mfr = product.value(QStringLiteral("Manufacturer")).toObject()
			     .value(QStringLiteral("Name")).toString();
	detail.description = product.value(QStringLiteral("Description")).toObject()
				     .value(QStringLiteral("ProductDescription")).toString();
	detail.photoUrl = absoluteUrl(product.value(QStringLiteral("PhotoUrl")).toString());
	detail.productUrl = product.value(QStringLiteral("ProductUrl")).toString();
	detail.datasheetUrl = absoluteUrl(product.value(QStringLiteral("DatasheetUrl")).toString());
	detail.stock = product.value(QStringLiteral("QuantityAvailable")).toInt();

	/* The line is one packaging of the part, so its price comes from that
	 * packaging. */
	const QJsonArray variations = product.value(QStringLiteral("ProductVariations")).toArray();
	QJsonObject chosen;
	bool found = false;
	for (const QJsonValue &value : variations) {
		const QJsonObject variation = value.toObject();
		if (variation.value(QStringLiteral("DigiKeyProductNumber")).toString().compare(
			    dkpn, Qt::CaseInsensitive) == 0) {
			chosen = variation;
			found = true;
			break;
		}
	}

	if (!found && !variations.isEmpty())
		chosen = variations.first().toObject();

	if (!chosen.isEmpty()) {
		detail.packaging = chosen.value(QStringLiteral("PackageType")).toObject()
					   .value(QStringLiteral("Name")).toString();
		detail.moq = qMax(chosen.value(QStringLiteral("MinimumOrderQuantity")).toInt(1), 1);
		detail.stock = chosen.value(QStringLiteral("QuantityAvailableforPackageType"))
				       .toInt(detail.stock);

		for (const QJsonValue &value :
		     chosen.value(QStringLiteral("StandardPricing")).toArray()) {
			const QJsonObject entry = value.toObject();
			detail.breaks.append({ entry.value(QStringLiteral("BreakQuantity")).toInt(),
					       entry.value(QStringLiteral("UnitPrice")).toDouble() });
		}
	}

	m_details.insert(dkpn, detail);
}

void Products::refreshTotals()
{
	double total = 0;
	int priced = 0;
	int unpriced = 0;

	for (const Line &line : m_lines) {
		if (line.dnp)
			continue;

		const Details detail = m_details.value(line.dkpn.toUpper());
		if (!detail.valid || detail.breaks.isEmpty()) {
			++unpriced;
			continue;
		}

		total += pricing::lineTotalAt(detail.breaks, detail.moq, line.qty * m_assemblies);
		++priced;
	}

	m_priced = priced;
	m_unpriced = unpriced;
	m_total = total;
	m_perAssembly = m_assemblies > 0 ? total / m_assemblies : 0.0;

	emit totalsChanged();
}

void Products::setStatus(const QString &status)
{
	if (status == m_status)
		return;

	m_status = status;
	emit statusChanged();
}

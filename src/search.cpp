#include "search.h"

#include "api.h"
#include "catalog.h"
#include "urls.h"
#include "resultsmodel.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QVector>

#include <algorithm>

namespace {
const char *kSearchPath = "/products/v4/search/keyword";

/* A repeated search is the common case: a filter ticked then unticked, a page
 * gone forward and back, the same category reopened after lunch. */
const int kSearchTtl = 3600;

/* Prototype ordering: cut tape, then Digi-Reel, then a reel. Anything else
 * (bulk, tube, tray, bag) sorts behind those but is still shown. */
int packagingRank(const QString &name)
{
	if (name.contains(QLatin1String("Cut Tape"), Qt::CaseInsensitive))
		return 0;
	if (name.contains(QLatin1String("Digi-Reel"), Qt::CaseInsensitive))
		return 1;
	if (name.contains(QLatin1String("Tape & Reel"), Qt::CaseInsensitive))
		return 2;
	return 3;
}

/* The completion box matches the way a person types: case, spacing and
 * decorative punctuation are noise. The slash and percent stay because they
 * carry meaning in values like 1/10W and 0.5%. The micro sign counts as a
 * plain "u". */
QString foldText(const QString &text)
{
	QString out;
	out.reserve(text.size());

	for (QChar c : text) {
		c = c.toLower();
		if (c == QChar(0x00b5) || c == QChar(0x03bc))
			out.append(QLatin1Char('u'));
		else if (c.isLetterOrNumber() || c == QLatin1Char('.')
			 || c == QLatin1Char('/') || c == QLatin1Char('%'))
			out.append(c);
	}

	return out;
}

/* 4 is an exact hit, 1 the last resort: the characters appear in order, which
 * is what makes "rst" find "Resistance". 0 is no match at all. */
int scoreText(const QString &needle, const QString &haystack)
{
	if (needle.isEmpty())
		return 0;
	if (haystack == needle)
		return 4;
	if (haystack.startsWith(needle))
		return 3;
	if (haystack.contains(needle))
		return 2;

	int at = 0;
	for (const QChar c : haystack) {
		if (at == needle.size())
			break;
		if (c == needle.at(at))
			++at;
	}

	return at == needle.size() ? 1 : 0;
}

const int kMaxSuggestions = 20;

/* One API page, and how many extra pages the search may pull on its own when
 * the local MOQ filter hides most of what came back. */
const int kPageSize = 50;
const int kAutoPageLimit = 4;

/* A filter the panel has ticks in is the one being worked on, so it outranks the
 * ones that are merely available. */
int tickedCount(const QVariant &filter)
{
	const QVariantList values = filter.toMap().value(QStringLiteral("values")).toList();
	int count = 0;
	for (const QVariant &value : values)
		count += value.toMap().value(QStringLiteral("checked")).toBool() ? 1 : 0;
	return count;
}
}

Search::Search(Api *api, Catalog *catalog, QObject *parent)
	: QObject(parent)
	, m_api(api)
	, m_catalog(catalog)
	, m_results(new ResultsModel(this))
{
	m_status = readyStatus();

	/* Credentials can arrive after the window is up; the hint in the empty table
	 * has to stop claiming there are none. */
	connect(m_api, &Api::credentialsChanged, this, [this] { setStatus(readyStatus()); });
}

QString Search::readyStatus() const
{
	return m_api->hasCredentials()
		? QStringLiteral("Pick a category or type a keyword")
		: QStringLiteral("Paste your Digi-Key API credentials in Settings");
}

int Search::shownCount() const
{
	return m_results->rowCount();
}

int Search::filterCount() const
{
	int count = 0;
	for (const QStringList &values : m_selected)
		count += values.size();
	return count;
}

/* What the rows on screen were actually asked for, spelled out for the summary
 * strip: the applied ticks; pending ticks wait for the next Search. */
QVariantList Search::appliedFilters() const
{
	QVariantList out;

	for (auto it = m_applied.cbegin(); it != m_applied.cend(); ++it) {
		for (const QVariant &filter : m_filters) {
			const QVariantMap map = filter.toMap();
			if (map.value(QStringLiteral("parameterId")).toInt() != it.key())
				continue;

			const QString parameter = map.value(QStringLiteral("name")).toString();
			const QVariantList values = map.value(QStringLiteral("values")).toList();
			for (const QVariant &value : values) {
				const QVariantMap entry = value.toMap();
				if (!it.value().contains(entry.value(QStringLiteral("id")).toString()))
					continue;

				QVariantMap chip;
				chip.insert(QStringLiteral("parameter"), parameter);
				chip.insert(QStringLiteral("value"),
					    entry.value(QStringLiteral("text")).toString());
				out.append(chip);
			}
			break;
		}
	}

	return out;
}

QString Search::categoryName() const
{
	return m_catalog->name(m_categoryId);
}

bool Search::pending() const
{
	return m_selected != m_applied || m_marketplace != m_appliedMarketplace;
}

void Search::setKeywords(const QString &keywords)
{
	if (keywords == m_keywords)
		return;

	m_keywords = keywords;
	emit keywordsChanged();
}

void Search::setQuantity(int quantity)
{
	if (quantity == m_quantity)
		return;

	m_quantity = quantity;
	m_results->setQuantity(quantity);
	emit quantityChanged();
}

void Search::setMinStock(int minStock)
{
	if (minStock == m_minStock)
		return;

	m_minStock = minStock;
	emit minStockChanged();
}

void Search::setMaxMoq(int maxMoq)
{
	if (maxMoq == m_maxMoq)
		return;

	m_maxMoq = maxMoq;
	m_autoPages = 0;
	m_results->setMaxMoq(maxMoq);
	emit maxMoqChanged();
	fillVisible();
}

void Search::setMarketplace(bool marketplace)
{
	if (marketplace == m_marketplace)
		return;

	m_marketplace = marketplace;
	emit marketplaceChanged();
	emit pendingChanged();
}

void Search::setStatus(const QString &status)
{
	if (status == m_status)
		return;

	m_status = status;
	emit statusChanged();
}

void Search::setBusy(bool busy)
{
	if (busy == m_busy)
		return;

	m_busy = busy;
	emit busyChanged();
}

void Search::selectCategory(const QString &id)
{
	if (id == m_categoryId)
		return;

	/* A different category means a different set of parameters, so the ticks
	 * and the parameter ids they were chosen under both go. */
	m_categoryId = id;
	m_parameterCategoryId.clear();
	m_selected.clear();
	m_filters.clear();
	emit filtersChanged();
	search();
}

/* Back to a blank slate: no keyword, no category, no ticks, no rows. The panel
 * goes with it because the filters belong to a category. */
void Search::reset()
{
	m_keywords.clear();
	m_categoryId.clear();
	m_parameterCategoryId.clear();
	m_selected.clear();
	m_applied.clear();
	m_appliedMarketplace = m_marketplace;
	m_filters.clear();
	m_results->clear();
	m_totalCount = 0;
	m_offset = 0;
	m_more = false;
	m_autoPages = 0;

	emit keywordsChanged();
	emit filtersChanged();
	emit resultsChanged();
	emit pendingChanged();
	setStatus(QStringLiteral("Pick a category or type a keyword"));
}

void Search::search()
{
	if (m_keywords.trimmed().isEmpty() && m_categoryId.isEmpty()) {
		setStatus(QStringLiteral("Pick a category or type a keyword"));
		return;
	}

	m_autoPages = 0;
	fetchPage(0);
}

/* Ticks are the panel's business: this drops the category and every tick with it
 * and asks the catalogue about the keyword alone. Two buttons, two jobs. */
void Search::searchKeywords()
{
	m_categoryId.clear();
	m_parameterCategoryId.clear();
	m_selected.clear();
	m_applied.clear();
	m_appliedMarketplace = m_marketplace;
	m_filters.clear();
	m_results->clear();
	m_totalCount = 0;
	m_offset = 0;
	m_more = false;

	emit filtersChanged();
	emit resultsChanged();
	emit pendingChanged();

	if (m_keywords.trimmed().isEmpty()) {
		setStatus(QStringLiteral("Type a keyword, or browse a category"));
		return;
	}

	search();
}

void Search::nextPage()
{
	if (m_busy || !m_more)
		return;

	m_autoPages = 0;
	fetchPage(m_offset);
}

bool Search::fillVisible()
{
	if (m_busy || !m_more || m_results->rowCount() >= kPageSize)
		return false;
	if (m_autoPages >= kAutoPageLimit)
		return false;

	++m_autoPages;
	fetchPage(m_offset);
	return true;
}

void Search::fetchPage(int offset)
{
	if (offset == 0)
		setStatus(QStringLiteral("Searching"));
	else
		setStatus(QStringLiteral("Loading more"));

	setBusy(true);

	/* Whichever request was issued last is the one whose answer counts: two
	 * clicks on different categories leave two searches in flight, and replies
	 * can arrive out of order. */
	const int generation = ++m_generation;

	m_api->post(QLatin1String(kSearchPath), searchBody(offset),
		    [this, offset, generation](const QJsonObject &root, const QString &error) {
			    if (generation != m_generation)
				    return;

			    setBusy(false);

			    if (!error.isEmpty()) {
				    setStatus(QStringLiteral("Search failed: ") + error);
				    return;
			    }

			    parseSearch(root, offset);
			    if (fillVisible())
				    return;
			    setStatus(QStringLiteral("%1 matches").arg(m_totalCount));
		    },
		    [this, generation](const QString &problem) {
			    if (generation != m_generation)
				    return;

			    setBusy(false);
			    setStatus(QStringLiteral("Search failed: ") + problem);
		    },
		    kSearchTtl);
}

/* The category the panel's parameter ids belong to. If the user picked one, that
 * is the answer; a keyword-only search falls back to what the response said. */
QString Search::filterCategoryId() const
{
	return m_categoryId.isEmpty() ? m_parameterCategoryId : m_categoryId;
}

QJsonObject Search::searchBody(int offset) const
{
	QJsonObject filters;
	if (!m_categoryId.isEmpty()) {
		QJsonArray categories;
		QJsonObject id;
		id.insert(QStringLiteral("Id"), m_categoryId);
		categories.append(id);
		filters.insert(QStringLiteral("CategoryFilter"), categories);
	}

	filters.insert(QStringLiteral("MarketPlaceFilter"),
		       m_marketplace ? QStringLiteral("NoFilter")
				     : QStringLiteral("ExcludeMarketPlace"));

	QJsonArray options;
	options.append(QStringLiteral("InStock"));
	filters.insert(QStringLiteral("SearchOptions"), options);

	if (m_minStock > 0)
		filters.insert(QStringLiteral("MinimumQuantityAvailable"), m_minStock);

	if (!m_selected.isEmpty() && !filterCategoryId().isEmpty()) {
		QJsonObject category;
		category.insert(QStringLiteral("Id"), filterCategoryId());

		QJsonArray parameters;
		for (auto it = m_selected.cbegin(); it != m_selected.cend(); ++it) {
			QJsonArray values;
			for (const QString &valueId : it.value()) {
				QJsonObject id;
				id.insert(QStringLiteral("Id"), valueId);
				values.append(id);
			}

			QJsonObject parameter;
			parameter.insert(QStringLiteral("ParameterId"), it.key());
			parameter.insert(QStringLiteral("FilterValues"), values);
			parameters.append(parameter);
		}

		QJsonObject request;
		request.insert(QStringLiteral("CategoryFilter"), category);
		request.insert(QStringLiteral("ParameterFilters"), parameters);
		filters.insert(QStringLiteral("ParameterFilterRequest"), request);
	}

	QJsonObject body;
	body.insert(QStringLiteral("Keywords"), m_keywords.trimmed());
	body.insert(QStringLiteral("Limit"), kPageSize);
	body.insert(QStringLiteral("Offset"), offset);
	body.insert(QStringLiteral("FilterOptionsRequest"), filters);

	QJsonObject sort;
	sort.insert(QStringLiteral("Field"), QStringLiteral("Price"));
	sort.insert(QStringLiteral("SortOrder"), QStringLiteral("Ascending"));
	body.insert(QStringLiteral("SortOptions"), sort);

	return body;
}

void Search::parseSearch(const QJsonObject &root, int offset)
{
	if (offset == 0)
		m_results->clear();

	const QJsonArray products = root.value(QStringLiteral("Products")).toArray();
	QVector<ResultsModel::Row> rows;

	for (const QJsonValue &value : products) {
		const QJsonObject product = value.toObject();
		const QJsonObject description = product.value(QStringLiteral("Description")).toObject();

		const int productStock = product.value(QStringLiteral("QuantityAvailable")).toInt();

		/* One row per product. Cut tape is what a prototype wants, Digi-Reel
		 * next, a full reel last: same part, and the choice is the packaging.
		 * Every packaging is named in the row so nothing is hidden. */
		const QJsonArray variations = product.value(QStringLiteral("ProductVariations")).toArray();
		QJsonObject chosen;
		int chosenRank = 99;
		int chosenMoq = 0;

		QVector<QPair<int, QString>> packagingNames;
		for (const QJsonValue &variationValue : variations) {
			const QJsonObject variation = variationValue.toObject();
			const QString name = variation.value(QStringLiteral("PackageType"))
						     .toObject()
						     .value(QStringLiteral("Name"))
						     .toString();
			const int rank = packagingRank(name);
			const int moq = qMax(variation.value(QStringLiteral("MinimumOrderQuantity")).toInt(1), 1);

			if (rank < chosenRank || (rank == chosenRank && moq < chosenMoq)) {
				chosen = variation;
				chosenRank = rank;
				chosenMoq = moq;
			}
			bool known = false;
			for (const auto &entry : packagingNames)
				known = known || entry.second == name;
			if (!known)
				packagingNames.append({ rank, name });
		}

		if (chosen.isEmpty())
			continue;

		ResultsModel::Row row;
		row.dkpn = chosen.value(QStringLiteral("DigiKeyProductNumber")).toString();
		row.mpn = product.value(QStringLiteral("ManufacturerProductNumber")).toString();
		row.mfr = product.value(QStringLiteral("Manufacturer"))
				  .toObject()
				  .value(QStringLiteral("Name"))
				  .toString();
		row.packaging = chosen.value(QStringLiteral("PackageType"))
					.toObject()
					.value(QStringLiteral("Name"))
					.toString();
		std::stable_sort(packagingNames.begin(), packagingNames.end(),
				 [](const QPair<int, QString> &a, const QPair<int, QString> &b) {
					 return a.first < b.first;
				 });
		QStringList names;
		for (const auto &entry : packagingNames)
			names.append(entry.second);
		row.packagings = names.join(QStringLiteral(" \u00b7 "));
		row.productUrl = product.value(QStringLiteral("ProductUrl")).toString();
		row.datasheetUrl = absoluteUrl(product.value(QStringLiteral("DatasheetUrl")).toString());
		row.photoUrl = absoluteUrl(product.value(QStringLiteral("PhotoUrl")).toString());
		row.description = description.value(QStringLiteral("ProductDescription")).toString();
		row.stock = chosen.value(QStringLiteral("QuantityAvailableforPackageType")).toInt(productStock);
		row.moq = chosenMoq;

		for (const QJsonValue &breakValue :
		     chosen.value(QStringLiteral("StandardPricing")).toArray()) {
			const QJsonObject entry = breakValue.toObject();
			row.breaks.append({ entry.value(QStringLiteral("BreakQuantity")).toInt(),
					    entry.value(QStringLiteral("UnitPrice")).toDouble() });
		}

		rows.append(row);
	}

	m_results->append(rows);
	m_results->setQuantity(m_quantity);

	m_totalCount = root.value(QStringLiteral("ProductsCount")).toInt();
	m_offset = offset + products.size();
	m_more = !products.isEmpty() && m_offset < m_totalCount;

	const QJsonObject filterOptions = root.value(QStringLiteral("FilterOptions")).toObject();
	rebuildFilters(filterOptions.value(QStringLiteral("ParametricFilters")), filterCategoryId());

	emit resultsChanged();
}

void Search::rebuildFilters(const QJsonValue &parametric, const QString &fallbackCategory)
{
	QVariantList filters;

	for (const QJsonValue &value : parametric.toArray()) {
		const QJsonObject parameter = value.toObject();
		const int parameterId = parameter.value(QStringLiteral("ParameterId")).toInt();
		const QJsonArray values = parameter.value(QStringLiteral("FilterValues")).toArray();
		const QStringList chosen = m_selected.value(parameterId);

		QVariantList entries;
		bool anyChosen = false;
		for (const QJsonValue &valueEntry : values) {
			const QJsonObject entry = valueEntry.toObject();
			const QString id = entry.value(QStringLiteral("ValueId")).toString();
			const bool checked = chosen.contains(id);
			anyChosen = anyChosen || checked;

			QVariantMap item;
			item.insert(QStringLiteral("id"), id);
			item.insert(QStringLiteral("text"),
				    entry.value(QStringLiteral("ValueName"))
					    .toString(entry.value(QStringLiteral("ValueText")).toString()));
			item.insert(QStringLiteral("count"),
				    entry.value(QStringLiteral("ProductCount")).toInteger());
			item.insert(QStringLiteral("checked"), checked);
			entries.append(item);
		}

		if (entries.size() < 2 && !anyChosen)
			continue;

		std::stable_sort(entries.begin(), entries.end(), [](const QVariant &a, const QVariant &b) {
			return a.toMap().value(QStringLiteral("count")).toLongLong()
				> b.toMap().value(QStringLiteral("count")).toLongLong();
		});

		const QJsonObject category = parameter.value(QStringLiteral("Category")).toObject();
		const QString categoryId = category.contains(QStringLiteral("Id"))
			? QString::number(category.value(QStringLiteral("Id")).toInt())
			: fallbackCategory;
		if (!categoryId.isEmpty())
			m_parameterCategoryId = categoryId;

		QVariantMap filter;
		filter.insert(QStringLiteral("parameterId"), parameterId);
		filter.insert(QStringLiteral("name"),
			      parameter.value(QStringLiteral("ParameterName")).toString());
		filter.insert(QStringLiteral("categoryId"), categoryId);
		filter.insert(QStringLiteral("values"), entries);
		filters.append(filter);
	}

	/* The filters carrying ticks are the ones being worked on, so they sit above
	 * the ones that are only on offer; everything else keeps the catalogue order. */
	std::stable_sort(filters.begin(), filters.end(), [](const QVariant &a, const QVariant &b) {
		return tickedCount(a) > tickedCount(b);
	});

	m_filters = filters;
	m_applied = m_selected;
	m_appliedMarketplace = m_marketplace;
	emit filtersChanged();
	emit pendingChanged();
}

void Search::toggleFilter(int parameterId, const QString &valueId)
{
	QStringList values = m_selected.value(parameterId);
	if (values.contains(valueId))
		values.removeAll(valueId);
	else
		values.append(valueId);

	values.sort();
	if (values.isEmpty())
		m_selected.remove(parameterId);
	else
		m_selected.insert(parameterId, values);

	/* Selecting a filter is local. Nothing is asked of Digi-Key until the user
	 * sends the request, so the panel restates the ticks it already has and
	 * repaints without waiting for a response. */
	for (QVariant &filter : m_filters) {
		QVariantMap map = filter.toMap();
		if (map.value(QStringLiteral("parameterId")).toInt() != parameterId)
			continue;

		QVariantList entries = map.value(QStringLiteral("values")).toList();
		for (QVariant &entry : entries) {
			QVariantMap value = entry.toMap();
			value.insert(QStringLiteral("checked"),
				     values.contains(value.value(QStringLiteral("id")).toString()));
			entry = value;
		}
		map.insert(QStringLiteral("values"), entries);
		filter = map;
	}

	emit filtersChanged();
	emit pendingChanged();
}

void Search::clearFilters()
{
	if (m_selected.isEmpty())
		return;

	m_selected.clear();

	for (QVariant &filter : m_filters) {
		QVariantMap map = filter.toMap();
		QVariantList entries = map.value(QStringLiteral("values")).toList();
		for (QVariant &entry : entries) {
			QVariantMap value = entry.toMap();
			value.insert(QStringLiteral("checked"), false);
			entry = value;
		}
		map.insert(QStringLiteral("values"), entries);
		filter = map;
	}

	emit filtersChanged();
	emit pendingChanged();
}

/* One box over every value the current category response offered: the value is
 * what gets matched, the filter name is only the label. Nothing is asked of
 * Digi-Key and nothing is ticked until the caller says so. */
QVariantList Search::complete(const QString &text) const
{
	const QString needle = foldText(text);
	QVariantList hits;
	if (needle.isEmpty())
		return hits;

	for (const QVariant &filter : m_filters) {
		const QVariantMap map = filter.toMap();
		const QVariantList values = map.value(QStringLiteral("values")).toList();

		/* Typing a filter's own name answers with that filter's values, which
		 * is what "power" should show. Those hits rank one step under a value
		 * that says the word itself. */
		const int nameScore = scoreText(needle, foldText(
			map.value(QStringLiteral("name")).toString()));
		const int nameHit = nameScore > 1 ? nameScore - 1 : 0;

		for (const QVariant &value : values) {
			const QVariantMap entry = value.toMap();
			const QString label = entry.value(QStringLiteral("text")).toString();
			const int score = qMax(scoreText(needle, foldText(label)), nameHit);
			if (score == 0)
				continue;

			QVariantMap hit;
			hit.insert(QStringLiteral("parameterId"),
				   map.value(QStringLiteral("parameterId")));
			hit.insert(QStringLiteral("filter"), map.value(QStringLiteral("name")));
			hit.insert(QStringLiteral("valueId"),
				   entry.value(QStringLiteral("id")));
			hit.insert(QStringLiteral("text"), label);
			hit.insert(QStringLiteral("count"),
				   entry.value(QStringLiteral("count")));
			hit.insert(QStringLiteral("checked"),
				   entry.value(QStringLiteral("checked")));
			hit.insert(QStringLiteral("score"), score);
			hit.insert(QStringLiteral("length"), label.size());
			hits.append(hit);
		}
	}

	/* Strongest match first. Among equals the shorter label is the tighter
	 * fit, and the catalogue's count breaks whatever is left. */
	std::stable_sort(hits.begin(), hits.end(), [](const QVariant &a, const QVariant &b) {
		const QVariantMap left = a.toMap();
		const QVariantMap right = b.toMap();

		const int leftScore = left.value(QStringLiteral("score")).toInt();
		const int rightScore = right.value(QStringLiteral("score")).toInt();
		if (leftScore != rightScore)
			return leftScore > rightScore;

		const int leftLength = left.value(QStringLiteral("length")).toInt();
		const int rightLength = right.value(QStringLiteral("length")).toInt();
		if (leftLength != rightLength)
			return leftLength < rightLength;

		return left.value(QStringLiteral("count")).toLongLong()
		       > right.value(QStringLiteral("count")).toLongLong();
	});

	if (hits.size() > kMaxSuggestions)
		hits.resize(kMaxSuggestions);

	return hits;
}


#include "resultsmodel.h"

#include "pricing.h"

#include <QStringList>

#include <algorithm>

double ResultsModel::Row::unitPriceAt(int quantity) const
{
	return pricing::unitPriceAt(breaks, moq, quantity);
}

double ResultsModel::Row::lineTotalAt(int quantity) const
{
	return pricing::lineTotalAt(breaks, moq, quantity);
}

ResultsModel::ResultsModel(QObject *parent)
	: QAbstractListModel(parent)
{
}

int ResultsModel::rowCount(const QModelIndex &parent) const
{
	return parent.isValid() ? 0 : m_visible.size();
}

QVariant ResultsModel::data(const QModelIndex &index, int role) const
{
	if (!index.isValid() || index.row() >= m_visible.size())
		return {};

	const Row &row = m_rows.at(m_visible.at(index.row()));

	if (role != RowRole)
		return {};

	QStringList breaks;
	for (int i = 0; i < row.breaks.size() && i < 4; ++i)
		breaks << QStringLiteral("%1/%2").arg(row.breaks.at(i).first).arg(
			row.breaks.at(i).second, 0, 'f', 4);

	QVariantMap map;
	map.insert(QStringLiteral("dkpn"), row.dkpn);
	map.insert(QStringLiteral("mpn"), row.mpn);
	map.insert(QStringLiteral("mfr"), row.mfr);
	map.insert(QStringLiteral("packaging"), row.packaging);
	map.insert(QStringLiteral("packagings"), row.packagings);
	map.insert(QStringLiteral("stock"), row.stock);
	map.insert(QStringLiteral("moq"), row.moq);
	map.insert(QStringLiteral("price"), row.unitPriceAt(m_quantity));
	map.insert(QStringLiteral("lineTotal"), row.lineTotalAt(m_quantity));
	map.insert(QStringLiteral("breaks"), breaks.join(QStringLiteral("  ")));
	map.insert(QStringLiteral("url"), row.productUrl);
	map.insert(QStringLiteral("datasheetUrl"), row.datasheetUrl);
	map.insert(QStringLiteral("photoUrl"), row.photoUrl);
	map.insert(QStringLiteral("description"), row.description);
	return map;
}

QHash<int, QByteArray> ResultsModel::roleNames() const
{
	return { { RowRole, "modelData" } };
}

void ResultsModel::clear()
{
	beginResetModel();
	m_rows.clear();
	m_visible.clear();
	endResetModel();
}

void ResultsModel::append(const QVector<Row> &rows)
{
	if (rows.isEmpty())
		return;

	/* Only the rows that survive the MOQ filter are visible, so the insert has
	 * to count them first. */
	QVector<int> added;
	for (int i = 0; i < rows.size(); ++i) {
		if (m_maxMoq <= 0 || rows.at(i).moq <= m_maxMoq)
			added.append(m_rows.size() + i);
	}

	if (added.isEmpty()) {
		m_rows += rows;
		return;
	}

	beginInsertRows(QModelIndex(), m_visible.size(), m_visible.size() + added.size() - 1);
	m_rows += rows;
	m_visible += added;
	endInsertRows();
	resort();
}

void ResultsModel::setQuantity(int quantity)
{
	if (quantity == m_quantity)
		return;

	m_quantity = quantity;
	if (!m_visible.isEmpty())
		emit dataChanged(index(0), index(m_visible.size() - 1), { RowRole });
}

void ResultsModel::sortBy(const QString &field)
{
	if (field == m_sortField)
		m_sortAscending = !m_sortAscending;
	else {
		m_sortField = field;
		m_sortAscending = true;
	}
	resort();
	emit sortChanged();
}

QVariantMap ResultsModel::rowAt(int index) const
{
	return data(this->index(index), RowRole).toMap();
}

void ResultsModel::setMaxMoq(int maxMoq)
{
	if (maxMoq == m_maxMoq)
		return;

	m_maxMoq = maxMoq;
	beginResetModel();
	rebuildVisible();
	endResetModel();
	emit maxMoqChanged();
}

void ResultsModel::rebuildVisible()
{
	m_visible.clear();
	for (int i = 0; i < m_rows.size(); ++i) {
		if (m_maxMoq <= 0 || m_rows.at(i).moq <= m_maxMoq)
			m_visible.append(i);
	}
}

/* Re-sorting only reorders rows: using a model reset would destroy
 * every delegate and throw the scrollbar and the selection away, which is what
 * "load 50 more" used to do. */
void ResultsModel::resort()
{
	if (m_rows.isEmpty())
		return;

	emit layoutAboutToBeChanged();

	const QModelIndexList oldIndexes = persistentIndexList();
	QVector<QString> identities;
	identities.reserve(oldIndexes.size());
	for (const QModelIndex &old : oldIndexes) {
		const int row = old.row();
		identities.append(row >= 0 && row < m_visible.size()
					  ? m_rows.at(m_visible.at(row)).dkpn : QString());
	}

	resortRows();
	rebuildVisible();

	QHash<QString, int> position;
	for (int i = 0; i < m_visible.size(); ++i)
		position.insert(m_rows.at(m_visible.at(i)).dkpn, i);

	QModelIndexList newIndexes;
	newIndexes.reserve(identities.size());
	for (const QString &identity : identities) {
		const int row = position.value(identity, -1);
		newIndexes.append(row >= 0 ? index(row) : QModelIndex());
	}
	changePersistentIndexList(oldIndexes, newIndexes);

	emit layoutChanged();
}

void ResultsModel::resortRows()
{
	if (m_rows.size() < 2)
		return;

	const QString field = m_sortField;
	const bool ascending = m_sortAscending;
	const int quantity = m_quantity;

	auto value = [field, quantity](const Row &row) {
		if (field == QLatin1String("price"))
			return row.unitPriceAt(quantity);
		if (field == QLatin1String("stock"))
			return double(row.stock);
		if (field == QLatin1String("moq"))
			return double(row.moq);
		if (field == QLatin1String("total"))
			return row.lineTotalAt(quantity);
		return 0.0;
	};

	auto text = [field](const Row &row) -> QString {
		if (field == QLatin1String("dkpn"))
			return row.dkpn;
		if (field == QLatin1String("mpn"))
			return row.mpn;
		if (field == QLatin1String("mfr"))
			return row.mfr;
		if (field == QLatin1String("packaging"))
			return row.packaging;
		return {};
	};

	const bool numeric = field != QLatin1String("dkpn") && field != QLatin1String("mpn")
		&& field != QLatin1String("mfr") && field != QLatin1String("packaging");

	std::stable_sort(m_rows.begin(), m_rows.end(), [&](const Row &a, const Row &b) {
		if (ascending)
			return numeric ? value(a) < value(b)
				       : text(a).compare(text(b), Qt::CaseInsensitive) < 0;
		return numeric ? value(b) < value(a)
			       : text(b).compare(text(a), Qt::CaseInsensitive) < 0;
	});
}

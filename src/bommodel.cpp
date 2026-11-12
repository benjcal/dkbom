#include "bommodel.h"

BomModel::BomModel(QObject *parent)
	: QAbstractListModel(parent)
{
}

int BomModel::rowCount(const QModelIndex &parent) const
{
	return parent.isValid() ? 0 : m_visible.size();
}

int BomModel::assignedCount() const
{
	int count = 0;
	for (const Row &row : m_rows)
		count += row.dkpn.isEmpty() ? 0 : 1;
	return count;
}

QVariant BomModel::data(const QModelIndex &index, int role) const
{
	const Row *row = at(index.row());
	if (!row)
		return {};

	switch (role) {
	case RefTextRole:
		return row->refs.join(QLatin1Char(' '));
	case RefsRole:
		return row->refs;
	case SheetsRole:
		return row->sheets;
	case ValueRole:
		return row->value;
	case FootprintRole:
		return row->footprint;
	case DkpnRole:
		return row->staged.isEmpty() ? row->dkpn : row->staged;
	case StagedRole:
		return !row->staged.isEmpty();
	case QtyRole:
		return row->qty;
	case AssignedRole:
		return !row->dkpn.isEmpty();
	case DnpRole:
		return row->dnp;
	case ProblemRole:
		return row->problem;
	default:
		return {};
	}
}

QHash<int, QByteArray> BomModel::roleNames() const
{
	return {
		{ RefTextRole, "refText" }, { RefsRole, "refs" },     { SheetsRole, "sheets" },
		{ ValueRole, "value" },     { FootprintRole, "footprint" },
		{ DkpnRole, "dkpn" },       { StagedRole, "staged" },   { QtyRole, "qty" },       { AssignedRole, "assigned" },
		{ DnpRole, "dnp" },         { ProblemRole, "problem" },
	};
}

void BomModel::setShowAssigned(bool showAssigned)
{
	if (m_showAssigned == showAssigned)
		return;

	m_showAssigned = showAssigned;
	beginResetModel();
	rebuildVisible();
	endResetModel();
	refresh();
}

void BomModel::setRows(const QVector<Row> &rows)
{
	beginResetModel();
	m_rows = rows;
	rebuildVisible();
	endResetModel();
	refresh();
}

void BomModel::setStaged(const QHash<QString, QString> &byRef)
{
	beginResetModel();
	for (Row &row : m_rows) {
		QString staged = row.refs.isEmpty() ? QString() : byRef.value(row.refs.first());
		for (const QString &ref : row.refs) {
			if (staged.isEmpty() || byRef.value(ref) != staged) {
				staged.clear();
				break;
			}
		}
		row.staged = staged;
	}
	rebuildVisible();
	endResetModel();
	refresh();
}

void BomModel::rebuildVisible()
{
	m_visible.clear();
	for (int i = 0; i < m_rows.size(); ++i) {
		if (m_showAssigned || m_rows.at(i).dkpn.isEmpty())
			m_visible.append(i);
	}
}

void BomModel::refresh()
{
	emit changed();
}

const BomModel::Row *BomModel::at(int visibleIndex) const
{
	if (visibleIndex < 0 || visibleIndex >= m_visible.size())
		return nullptr;

	return &m_rows.at(m_visible.at(visibleIndex));
}

/* Containment: assigning the same number to two lines
 * merges them, and the line the user is standing on should still be the one
 * selected afterwards. */
int BomModel::indexOfRefsIn(const QVector<int> &order, const QStringList &refs) const
{
	if (refs.isEmpty())
		return -1;

	int best = -1;
	for (int i = 0; i < order.size(); ++i) {
		const Row &row = m_rows.at(order.at(i));
		bool holds = true;
		for (const QString &ref : refs) {
			if (!row.refs.contains(ref)) {
				holds = false;
				break;
			}
		}
		if (!holds)
			continue;

		if (row.refs.size() == refs.size())
			return i;
		if (best < 0)
			best = i;
	}

	return best;
}

int BomModel::indexOfRefs(const QStringList &refs) const
{
	return indexOfRefsIn(m_visible, refs);
}

QVariantMap BomModel::findRow(const QStringList &refs) const
{
	QVector<int> all;
	all.reserve(m_rows.size());
	for (int i = 0; i < m_rows.size(); ++i)
		all.append(i);

	const int index = indexOfRefsIn(all, refs);
	return index < 0 ? QVariantMap() : toMap(m_rows.at(all.at(index)));
}

QVariantMap BomModel::rowAt(int visibleIndex) const
{
	const Row *row = at(visibleIndex);
	return row ? toMap(*row) : QVariantMap();
}

QVariantList BomModel::allRows() const
{
	QVariantList out;
	out.reserve(m_rows.size());
	for (const Row &row : m_rows)
		out.append(toMap(row));
	return out;
}

QVariantMap BomModel::toMap(const Row &row)
{
	QVariantMap map;
	map.insert(QStringLiteral("refs"), row.refs);
	map.insert(QStringLiteral("refText"), row.refs.join(QLatin1Char(' ')));
	map.insert(QStringLiteral("sheets"), row.sheets);
	map.insert(QStringLiteral("value"), row.value);
	map.insert(QStringLiteral("footprint"), row.footprint);
	map.insert(QStringLiteral("dkpn"), row.staged.isEmpty() ? row.dkpn : row.staged);
	map.insert(QStringLiteral("staged"), !row.staged.isEmpty());
	map.insert(QStringLiteral("qty"), row.qty);
	map.insert(QStringLiteral("assigned"), !row.dkpn.isEmpty());
	map.insert(QStringLiteral("dnp"), row.dnp);
	map.insert(QStringLiteral("problem"), row.problem);
	return map;
}

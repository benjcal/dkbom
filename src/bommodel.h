#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVector>

/* One row per BOM line: symbols that share a value, a footprint and a DigiKey
 * number, on any sheet. The number is what this tool writes, so a line that
 * has one is a line that is done. */
class BomModel : public QAbstractListModel {
	Q_OBJECT
	Q_PROPERTY(int count READ count NOTIFY changed)
	Q_PROPERTY(int totalCount READ totalCount NOTIFY changed)
	Q_PROPERTY(int assignedCount READ assignedCount NOTIFY changed)
	Q_PROPERTY(bool showAssigned READ showAssigned WRITE setShowAssigned NOTIFY changed)

public:
	struct Row {
		QStringList refs;
		QStringList sheets;
		QString value;
		QString footprint;
		QString dkpn;
		/* The number staged here and not yet in the schematics: shown, priced
		 * and never mistaken for something that is already written. */
		QString staged;
		/* Set when a reference in the line has no symbol the writer can find,
		 * the one way a line can look fine and still be unassignable. */
		QString problem;
		int qty = 0;
		bool dnp = false;
	};

	enum Roles {
		RefTextRole = Qt::UserRole + 1,
		RefsRole,
		SheetsRole,
		ValueRole,
		FootprintRole,
		DkpnRole,
		StagedRole,
		QtyRole,
		AssignedRole,
		DnpRole,
		ProblemRole
	};

	explicit BomModel(QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;

	int count() const { return m_visible.size(); }
	int totalCount() const { return m_rows.size(); }
	int assignedCount() const;

	bool showAssigned() const { return m_showAssigned; }
	void setShowAssigned(bool showAssigned);

	void setRows(const QVector<Row> &rows);
	/* Which references have an unsaved number, by reference. A line whose
	 * references disagree is left unstaged. */
	void setStaged(const QHash<QString, QString> &byRef);
	const Row *at(int visibleIndex) const;

	/* The line a reference belongs to, which is how a selection survives the
	 * re-read that follows a write: the line may have been merged with
	 * another one that had the same number already. */
	int indexOfRefs(const QStringList &refs) const;

	/* The same lookup across every row, assigned ones included: after a write
	 * the line has just left the unassigned view, and that is exactly the line
	 * whose number has to be read back. */
	QVariantMap findRow(const QStringList &refs) const;

	Q_INVOKABLE QVariantMap rowAt(int visibleIndex) const;

	/* Every line, assigned ones and the ones the dock is hiding, as maps. The
	 * pricing side works on the whole BOM even when the dock hides lines. */
	Q_INVOKABLE QVariantList allRows() const;

signals:
	void changed();

private:
	void rebuildVisible();
	void refresh();
	int indexOfRefsIn(const QVector<int> &order, const QStringList &refs) const;
	static QVariantMap toMap(const Row &row);

	QVector<Row> m_rows;
	QVector<int> m_visible;
	bool m_showAssigned = false;
};

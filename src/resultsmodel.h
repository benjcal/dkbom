#pragma once

#include <QAbstractListModel>
#include <QString>
#include <QVector>

/* One row per product. A product arrives with up to three packagings, each its
 * own DigiKey number; we keep the one worth buying for a prototype (Cut Tape,
 * then Digi-Reel, then whatever is left) and remember the others by name so the
 * row can say what else exists. Prices are recomputed here because the
 * quantity changes with every spin of the toolbar. */
class ResultsModel : public QAbstractListModel {
	Q_OBJECT
	Q_PROPERTY(QString sortField READ sortField NOTIFY sortChanged)
	Q_PROPERTY(bool sortAscending READ sortAscending NOTIFY sortChanged)
	Q_PROPERTY(int maxMoq READ maxMoq NOTIFY maxMoqChanged)

public:
	struct Row {
		QString dkpn;
		QString mpn;
		QString mfr;
		QString packaging;
		QString packagings;
		QString productUrl;
		QString datasheetUrl;
		QString photoUrl;
		QString description;
		int stock = 0;
		int moq = 1;
		QVector<QPair<int, double>> breaks;

		double unitPriceAt(int quantity) const;
		double lineTotalAt(int quantity) const;
	};

	enum Roles { RowRole = Qt::UserRole + 1 };

	explicit ResultsModel(QObject *parent = nullptr);

	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash<int, QByteArray> roleNames() const override;

	void clear();
	void append(const QVector<Row> &rows);
	void setQuantity(int quantity);

	QString sortField() const { return m_sortField; }
	bool sortAscending() const { return m_sortAscending; }
	int maxMoq() const { return m_maxMoq; }

	Q_INVOKABLE void sortBy(const QString &field);
	Q_INVOKABLE QVariantMap rowAt(int index) const;

	/* MOQ is a view-side filter: the response is kept whole so raising the
	 * limit can bring rows back without another request. */
	void setMaxMoq(int maxMoq);

signals:
	void sortChanged();
	void maxMoqChanged();

private:
	void resort();
	void resortRows();
	void rebuildVisible();

	QVector<Row> m_rows;
	QVector<int> m_visible;
	int m_maxMoq = 1;
	int m_quantity = 1;
	QString m_sortField = QStringLiteral("price");
	bool m_sortAscending = true;
};

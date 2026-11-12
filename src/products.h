#pragma once

#include <QHash>
#include <QObject>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class Api;
class Prefs;

/* What each BOM line actually is, from Digi-Key, for the check that matters:
 * does this number point at the part I meant to buy.
 *
 * A BOM line is a DigiKey number and a quantity; everything else here — photo,
 * description, packaging, price at the break the quantity lands in — comes from
 * one product call per number, kept on disk for an hour. The totals are the
 * sum of the lines, times however many boards are being built. */
class Products : public QObject {
	Q_OBJECT
	Q_PROPERTY(int assemblies READ assemblies WRITE setAssemblies NOTIFY assembliesChanged)
	Q_PROPERTY(double total READ total NOTIFY totalsChanged)
	Q_PROPERTY(double perAssembly READ perAssembly NOTIFY totalsChanged)
	Q_PROPERTY(int priced READ priced NOTIFY totalsChanged)
	Q_PROPERTY(int unpriced READ unpriced NOTIFY totalsChanged)
	Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
	Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
	struct Details {
		bool valid = false;
		QString mpn;
		QString mfr;
		QString description;
		QString photoUrl;
		QString productUrl;
		QString datasheetUrl;
		QString packaging;
		int moq = 1;
		int stock = 0;
		QVector<QPair<int, double>> breaks;
	};

	explicit Products(Api *api, Prefs *prefs, QObject *parent = nullptr);

	/* Empty until the number has been looked up; the fields are what QML reads,
	 * so a missing one is simply blank. */
	Q_INVOKABLE QVariantMap details(const QString &dkpn) const;
	Q_INVOKABLE double unitPrice(const QString &dkpn, int lineQty) const;
	Q_INVOKABLE double lineTotal(const QString &dkpn, int lineQty) const;

	/* One entry per BOM line, {dkpn, qty, dnp}; everything else is fetched. */
	Q_INVOKABLE void setLines(const QVariantList &lines);

	int assemblies() const { return m_assemblies; }
	void setAssemblies(int assemblies);

	double total() const { return m_total; }
	double perAssembly() const { return m_perAssembly; }
	int priced() const { return m_priced; }
	int unpriced() const { return m_unpriced; }
	bool loading() const { return m_inFlight || !m_pending.isEmpty(); }
	QString status() const { return m_status; }

signals:
	void totalsChanged();
	void detailsChanged();
	void assembliesChanged();
	void loadingChanged();
	void statusChanged();

private:
	struct Line {
		QString dkpn;
		int qty = 0;
		bool dnp = false;
	};

	void enqueue(const QString &dkpn);
	void fetchNext();
	void parse(const QString &dkpn, const QJsonObject &root);
	void refreshTotals();
	void setStatus(const QString &status);

	Api *m_api;
	Prefs *m_prefs;

	QHash<QString, Details> m_details;
	QHash<QString, bool> m_requested;
	QStringList m_pending;
	bool m_inFlight = false;

	QVector<Line> m_lines;
	int m_assemblies = 1;
	double m_total = 0;
	double m_perAssembly = 0;
	int m_priced = 0;
	int m_unpriced = 0;
	QString m_status;
};

#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <QVariantList>

class Api;
class Catalog;
class ResultsModel;

/* One search and its state: what was asked for, what came back, which filter
 * ticks are pending, which are applied, and how to page. Talking to the network
 * is Api's job and knowing the category tree is Catalog's. */
class Search : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString status READ status NOTIFY statusChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
	Q_PROPERTY(int totalCount READ totalCount NOTIFY resultsChanged)
	Q_PROPERTY(int shownCount READ shownCount NOTIFY resultsChanged)
	Q_PROPERTY(bool more READ more NOTIFY resultsChanged)
	Q_PROPERTY(QString keywords READ keywords WRITE setKeywords NOTIFY keywordsChanged)
	Q_PROPERTY(QString categoryId READ categoryId NOTIFY resultsChanged)
	Q_PROPERTY(QString categoryName READ categoryName NOTIFY resultsChanged)
	Q_PROPERTY(QVariantList filters READ filters NOTIFY filtersChanged)
	Q_PROPERTY(QVariantList appliedFilters READ appliedFilters NOTIFY filtersChanged)
	Q_PROPERTY(int filterCount READ filterCount NOTIFY filtersChanged)
	Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged)
	Q_PROPERTY(int quantity READ quantity WRITE setQuantity NOTIFY quantityChanged)
	Q_PROPERTY(int minStock READ minStock WRITE setMinStock NOTIFY minStockChanged)
	Q_PROPERTY(int maxMoq READ maxMoq WRITE setMaxMoq NOTIFY maxMoqChanged)
	Q_PROPERTY(bool marketplace READ marketplace WRITE setMarketplace NOTIFY marketplaceChanged)
	Q_PROPERTY(ResultsModel *results READ results CONSTANT)

public:
	Search(Api *api, Catalog *catalog, QObject *parent = nullptr);

	QString status() const { return m_status; }
	bool busy() const { return m_busy; }
	int totalCount() const { return m_totalCount; }
	int shownCount() const;
	bool more() const { return m_more; }
	QString keywords() const { return m_keywords; }
	QString categoryId() const { return m_categoryId; }
	QString categoryName() const;
	QVariantList filters() const { return m_filters; }
	QVariantList appliedFilters() const;
	int filterCount() const;
	bool pending() const;
	int quantity() const { return m_quantity; }
	int minStock() const { return m_minStock; }
	int maxMoq() const { return m_maxMoq; }
	bool marketplace() const { return m_marketplace; }
	ResultsModel *results() const { return m_results; }

	void setKeywords(const QString &keywords);
	void setQuantity(int quantity);
	void setMinStock(int minStock);
	void setMaxMoq(int maxMoq);
	void setMarketplace(bool marketplace);

	Q_INVOKABLE void search();

	/* The toolbar's button: keywords across the whole catalogue. The category
	 * and its ticks belong to the rail's Apply, so a fresh start means both go. */
	Q_INVOKABLE void searchKeywords();
	Q_INVOKABLE void nextPage();
	Q_INVOKABLE void selectCategory(const QString &id);
	Q_INVOKABLE void toggleFilter(int parameterId, const QString &valueId);
	Q_INVOKABLE void clearFilters();
	Q_INVOKABLE void reset();

	/* Value hits across every filter the current response offered. Matching
	 * is local and ticks nothing. */
	Q_INVOKABLE QVariantList complete(const QString &text) const;

signals:
	void statusChanged();
	void busyChanged();
	void resultsChanged();
	void keywordsChanged();
	void filtersChanged();
	void pendingChanged();
	void quantityChanged();
	void minStockChanged();
	void maxMoqChanged();
	void marketplaceChanged();

private:
	void fetchPage(int offset);
	bool fillVisible();
	void parseSearch(const QJsonObject &root, int offset);
	void rebuildFilters(const QJsonValue &parametric, const QString &fallbackCategory);
	QJsonObject searchBody(int offset) const;
	QString filterCategoryId() const;
	void setStatus(const QString &status);
	void setBusy(bool busy);
	QString readyStatus() const;

	Api *m_api;
	Catalog *m_catalog;

	QString m_status;
	bool m_busy = false;
	QString m_keywords;
	QString m_categoryId;
	QString m_parameterCategoryId;
	QVariantList m_filters;
	QHash<int, QStringList> m_selected;
	QHash<int, QStringList> m_applied;
	bool m_appliedMarketplace = false;
	int m_quantity = 1;
	int m_minStock = 1;
	int m_maxMoq = 1;
	bool m_marketplace = false;
	int m_totalCount = 0;
	int m_offset = 0;
	int m_generation = 0;
	bool m_more = false;
	int m_autoPages = 0;
	ResultsModel *m_results;
};

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class Api;
class QJsonArray;

/* The category tree, flattened once and kept for the life of the process. It is
 * the slowest thing to fetch and changes about never, so nothing else in the app
 * has to think about it. */
class Catalog : public QObject {
	Q_OBJECT
	Q_PROPERTY(QVariantList categories READ categories NOTIFY changed)
	Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
	Q_PROPERTY(bool loading READ loading NOTIFY changed)
	Q_PROPERTY(QString status READ status NOTIFY changed)

public:
	explicit Catalog(Api *api, QObject *parent = nullptr);

	QVariantList categories() const { return m_categories; }
	bool loaded() const { return !m_categories.isEmpty(); }
	bool loading() const { return m_loading; }

	/* Empty while all is well; otherwise the reason the tree is not there, which
	 * for bad credentials is the token endpoint's own words. */
	QString status() const { return m_status; }

	QString name(const QString &categoryId) const;

	Q_INVOKABLE void load();

signals:
	void changed();

private:
	void flatten(const QJsonArray &nodes, int depth, const QString &parentId);

	Api *m_api;
	QVariantList m_categories;
	bool m_loading = false;
	QString m_status;
};

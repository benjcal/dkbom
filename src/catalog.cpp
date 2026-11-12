#include "catalog.h"

#include "api.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace {
const char *kCategoriesPath = "/products/v4/search/categories";

/* The category tree is the slowest thing to fetch and changes about never. */
const int kCategoriesTtl = 30 * 24 * 3600;
}

Catalog::Catalog(Api *api, QObject *parent)
	: QObject(parent)
	, m_api(api)
{
}

QString Catalog::name(const QString &categoryId) const
{
	for (const QVariant &entry : m_categories) {
		const QVariantMap map = entry.toMap();
		if (map.value(QStringLiteral("id")).toString() == categoryId)
			return map.value(QStringLiteral("name")).toString();
	}
	return {};
}

void Catalog::load()
{
	if (m_loading)
		return;

	m_loading = true;
	m_status.clear();
	emit changed();

	m_api->get(QLatin1String(kCategoriesPath),
		   [this](const QJsonObject &root, const QString &error) {
			   m_loading = false;

			   if (!error.isEmpty()) {
				   m_status = error;
				   emit changed();
				   return;
			   }

			   /* The tree arrives nested; a flat list with a depth is all QML
				* needs to draw an indented picker without a model of its own. */
			   m_categories.clear();
			   flatten(root.value(QStringLiteral("Categories")).toArray(), 0, QString());
			   emit changed();
		   },
		   [this](const QString &problem) {
			   m_loading = false;
			   m_status = problem;
			   emit changed();
		   },
		   kCategoriesTtl);
}

void Catalog::flatten(const QJsonArray &nodes, int depth, const QString &parentId)
{
	for (const QJsonValue &value : nodes) {
		const QJsonObject node = value.toObject();
		const QJsonArray children = node.contains(QStringLiteral("Children"))
			? node.value(QStringLiteral("Children")).toArray()
			: node.value(QStringLiteral("ChildCategories")).toArray();

		const QString id = QString::number(node.value(QStringLiteral("CategoryId")).toInt());

		QVariantMap entry;
		entry.insert(QStringLiteral("id"), id);
		entry.insert(QStringLiteral("name"), node.value(QStringLiteral("Name")).toString());
		entry.insert(QStringLiteral("count"),
			     node.value(QStringLiteral("ProductCount")).toInteger());
		entry.insert(QStringLiteral("depth"), depth);
		/* The parent lets the panel unfold one branch; depth alone cannot tell
		 * siblings apart. */
		entry.insert(QStringLiteral("parent"), parentId);
		m_categories.append(entry);

		flatten(children, depth + 1, id);
	}
}

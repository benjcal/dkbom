#include "prefs.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace {
/* This tool is its own thing: it keeps its file in its own directory and shares
 * nothing with the portcullis project directories. */
QString configPath()
{
	const QDir base(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
	const QString folder = base.filePath(QStringLiteral("dkbom"));
	QDir().mkpath(folder);
	return QDir(folder).filePath(QStringLiteral("config.conf"));
}
}

Prefs::Prefs(QObject *parent)
	: QObject(parent)
	, m_settings(configPath(), QSettings::IniFormat)
{
}

QVariantMap Prefs::columnWidths() const
{
	QVariantMap widths;
	m_settings.beginGroup(QStringLiteral("widths"));
	const QStringList keys = m_settings.childKeys();
	for (const QString &key : keys)
		widths.insert(key, m_settings.value(key).toInt());
	m_settings.endGroup();
	return widths;
}

QStringList Prefs::hiddenColumns() const
{
	return m_settings.value(QStringLiteral("columns/hidden")).toStringList();
}

QString Prefs::clientId() const
{
	return m_settings.value(QStringLiteral("api/clientId")).toString();
}

QString Prefs::clientSecret() const
{
	return m_settings.value(QStringLiteral("api/clientSecret")).toString();
}

bool Prefs::hasCredentials() const
{
	return !clientId().isEmpty() && !clientSecret().isEmpty();
}

int Prefs::railWidth() const
{
	return m_settings.value(QStringLiteral("window/railWidth"), 340).toInt();
}

void Prefs::setRailWidth(int width)
{
	const int clamped = qBound(160, width, 1200);
	if (m_settings.value(QStringLiteral("window/railWidth"), 340).toInt() == clamped)
		return;

	m_settings.setValue(QStringLiteral("window/railWidth"), clamped);
	emit changed();
}

int Prefs::bomWidth() const
{
	return m_settings.value(QStringLiteral("window/bomWidth"), 360).toInt();
}

void Prefs::setBomWidth(int width)
{
	const int clamped = qBound(220, width, 900);
	if (m_settings.value(QStringLiteral("window/bomWidth"), 360).toInt() == clamped)
		return;

	m_settings.setValue(QStringLiteral("window/bomWidth"), clamped);
	emit changed();
}

int Prefs::assemblies() const
{
	return qBound(1, m_settings.value(QStringLiteral("bom/assemblies"), 1).toInt(), 1000000);
}

void Prefs::setAssemblies(int assemblies)
{
	const int clamped = qBound(1, assemblies, 1000000);
	if (m_settings.value(QStringLiteral("bom/assemblies"), 1).toInt() == clamped)
		return;

	m_settings.setValue(QStringLiteral("bom/assemblies"), clamped);
	emit assembliesChanged();
}

QStringList Prefs::recentProjects() const
{
	QStringList paths = m_settings.value(QStringLiteral("project/recent")).toStringList();

	/* Drop projects that were moved or deleted. */
	QStringList alive;
	for (const QString &path : paths) {
		if (QFileInfo::exists(path))
			alive.append(path);
	}
	return alive;
}

QString Prefs::lastProject() const
{
	return m_settings.value(QStringLiteral("project/last")).toString();
}

void Prefs::clearLastProject()
{
	if (!m_settings.contains(QStringLiteral("project/last")))
		return;

	m_settings.remove(QStringLiteral("project/last"));
	m_settings.sync();
	emit recentChanged();
}

void Prefs::addRecentProject(const QString &path)
{
	QStringList paths = recentProjects();
	paths.removeAll(path);
	paths.prepend(path);
	while (paths.size() > 12)
		paths.removeLast();

	m_settings.setValue(QStringLiteral("project/recent"), paths);
	m_settings.setValue(QStringLiteral("project/last"), path);
	m_settings.sync();
	emit recentChanged();
}

void Prefs::forgetProjects()
{
	m_settings.remove(QStringLiteral("project"));
	m_settings.sync();
	emit recentChanged();
}

void Prefs::setCredentials(const QString &clientId, const QString &clientSecret)
{
	m_settings.setValue(QStringLiteral("api/clientId"), clientId);
	m_settings.setValue(QStringLiteral("api/clientSecret"), clientSecret);
	m_settings.sync();

	emit credentialsChanged();
}

void Prefs::setColumnWidth(const QString &key, int width)
{
	const QString path = QStringLiteral("widths/") + key;
	if (m_settings.contains(path) && m_settings.value(path).toInt() == width)
		return;

	m_settings.setValue(path, width);
	emit changed();
}

void Prefs::setColumnHidden(const QString &key, bool hidden)
{
	QStringList columns = hiddenColumns();
	if (columns.contains(key) == hidden)
		return;

	if (hidden)
		columns.append(key);
	else
		columns.removeAll(key);

	m_settings.setValue(QStringLiteral("columns/hidden"), columns);
	emit changed();
}

void Prefs::sync()
{
	m_settings.sync();
}

void Prefs::resetColumns()
{
	m_settings.remove(QStringLiteral("columns"));
	m_settings.remove(QStringLiteral("widths"));
	emit changed();
	m_settings.sync();
}

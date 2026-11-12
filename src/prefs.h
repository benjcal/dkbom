#pragma once

#include <QObject>
#include <QSettings>
#include <QStringList>
#include <QVariantMap>

/* Everything the tool remembers between runs, in ~/.config/dkbom/config.conf.
 * Column state is keyed by column name, so hiding a column or the
 * catalogue growing a new parameter column cannot shuffle the widths onto the
 * wrong one. */
class Prefs : public QObject {
	Q_OBJECT
	Q_PROPERTY(QVariantMap columnWidths READ columnWidths NOTIFY changed)
	Q_PROPERTY(QStringList hiddenColumns READ hiddenColumns NOTIFY changed)
	Q_PROPERTY(int railWidth READ railWidth NOTIFY changed)
	Q_PROPERTY(int bomWidth READ bomWidth NOTIFY changed)
	Q_PROPERTY(int assemblies READ assemblies NOTIFY assembliesChanged)
	Q_PROPERTY(QStringList recentProjects READ recentProjects NOTIFY recentChanged)
	Q_PROPERTY(QString lastProject READ lastProject NOTIFY recentChanged)
	Q_PROPERTY(QString clientId READ clientId NOTIFY credentialsChanged)
	Q_PROPERTY(QString clientSecret READ clientSecret NOTIFY credentialsChanged)
	Q_PROPERTY(bool hasCredentials READ hasCredentials NOTIFY credentialsChanged)

public:
	explicit Prefs(QObject *parent = nullptr);

	QVariantMap columnWidths() const;
	QStringList hiddenColumns() const;

	/* The filter rail is dragged, so its width is a window setting. The BOM dock
	 * is the same kind of thing on the other side. */
	int railWidth() const;
	int bomWidth() const;

	/* How many boards the BOM is being priced for. It belongs to the window: the
	 * same project is built in ones and in hundreds. */
	int assemblies() const;

	/* Most recent first, and whatever was opened last is what the next start
	 * opens. */
	QStringList recentProjects() const;
	QString lastProject() const;
	Q_INVOKABLE void clearLastProject();

	/* Digi-Key credentials belong to the user and are pasted into the settings
	 * dialog, then kept here. QSettings is plain text, which is a bench tool's
	 * trade-off. */
	QString clientId() const;
	QString clientSecret() const;
	bool hasCredentials() const;

	Q_INVOKABLE void setCredentials(const QString &clientId, const QString &clientSecret);

	Q_INVOKABLE void setColumnWidth(const QString &key, int width);
	Q_INVOKABLE void setColumnHidden(const QString &key, bool hidden);
	Q_INVOKABLE void setRailWidth(int width);
	Q_INVOKABLE void setBomWidth(int width);
	Q_INVOKABLE void setAssemblies(int assemblies);
	Q_INVOKABLE void addRecentProject(const QString &path);
	Q_INVOKABLE void forgetProjects();
	Q_INVOKABLE void resetColumns();

	/* QSettings buffers until sync; dragging a column would otherwise be lost
	 * on any exit that is not a clean quit. */
	Q_INVOKABLE void sync();

signals:
	void changed();
	void recentChanged();
	void credentialsChanged();
	void assembliesChanged();

private:
	mutable QSettings m_settings;
};

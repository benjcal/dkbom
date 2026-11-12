#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QQmlApplicationEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QQuickStyle>
#include <QStandardPaths>
#include <qqml.h>

#include "api.h"
#include "catalog.h"
#include "prefs.h"
#include "products.h"
#include "project.h"
#include "search.h"
#include "shell.h"

namespace {
/* Part photos are plain GETs off Digi-Key's media host and cost no API quota,
 * so the cheap thing to do is keep them: the second visit to the same part is
 * instant, and Qt already speaks the HTTP caching rules for us. */
class MediaCacheFactory : public QQmlNetworkAccessManagerFactory {
public:
	explicit MediaCacheFactory(const QString &folder)
		: m_folder(folder)
	{
	}

	QNetworkAccessManager *create(QObject *parent) override
	{
		auto *manager = new QNetworkAccessManager(parent);
		auto *cache = new QNetworkDiskCache(manager);
		cache->setCacheDirectory(m_folder);
		cache->setMaximumCacheSize(256ll * 1024 * 1024);
		manager->setCache(cache);
		return manager;
	}

private:
	QString m_folder;
};
}

int main(int argc, char *argv[])
{
	QGuiApplication app(argc, argv);
	QFontDatabase::addApplicationFont(QStringLiteral(":/assets/Phosphor.ttf"));
	app.setWindowIcon(QIcon(QStringLiteral(":/assets/icons/dkbom.svg")));
	app.setApplicationName(QStringLiteral("dkbom"));
	app.setDesktopFileName(QStringLiteral("dkbom"));

	/* Pin the style. Left to the platform theme, the toolbar and the spin
	 * boxes come out in whatever the desktop ships. */
	QQuickStyle::setStyle(QStringLiteral("Fusion"));

	/* Saved credentials win over the environment; the environment is what makes
	 * `make run` work during development. */
	Prefs prefs;
	Api api;
	if (prefs.hasCredentials())
		api.setCredentials(prefs.clientId(), prefs.clientSecret());

	Catalog catalog(&api);
	Search search(&api, &catalog);
	Project project(&prefs);
	Products products(&api, &prefs);
	Shell shell;

	/* Singletons give QML the real property names, so a typo is a load error. */
	qmlRegisterSingletonInstance("Dk", 1, 0, "Api", &api);
	qmlRegisterSingletonInstance("Dk", 1, 0, "Catalog", &catalog);
	qmlRegisterSingletonInstance("Dk", 1, 0, "Search", &search);
	qmlRegisterSingletonInstance("Dk", 1, 0, "Project", &project);
	qmlRegisterSingletonInstance("Dk", 1, 0, "Products", &products);
	qmlRegisterSingletonInstance("Dk", 1, 0, "Prefs", &prefs);
	qmlRegisterSingletonInstance("Dk", 1, 0, "Shell", &shell);

	QQmlApplicationEngine engine;

	const QDir cacheRoot(
		QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation));
	const QString mediaFolder = cacheRoot.filePath(QStringLiteral("dkbom/media"));
	QDir().mkpath(mediaFolder);
	engine.setNetworkAccessManagerFactory(new MediaCacheFactory(mediaFolder));

	QObject::connect(
		&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
		[] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

	QObject::connect(&app, &QCoreApplication::aboutToQuit, &prefs, &Prefs::sync);

	engine.loadFromModule("Dkbom", "Main");

	if (api.hasCredentials())
		catalog.load();

	/* The project opened last time is what a bench tool is expected to come
	 * back to; a missing one is silently forgotten and the pane offers the
	 * file dialog. */
	const QString last = prefs.lastProject();
	if (!last.isEmpty() && QFileInfo::exists(last))
		project.openPath(last);

	return app.exec();
}

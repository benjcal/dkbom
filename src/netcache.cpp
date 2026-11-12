#include "netcache.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {
const char *kFolder = "http";
const char *kSuffix = ".json";
}

NetCache::NetCache()
{
	const QDir base(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation));
	m_root = base.filePath(QStringLiteral("dkbom"));
	QDir().mkpath(QDir(m_root).filePath(QLatin1String(kFolder)));
}

QString NetCache::key(const QString &method, const QString &path, const QByteArray &body)
{
	const QByteArray raw = method.toUtf8() + '\n' + path.toUtf8() + '\n' + body;
	return QString::fromLatin1(QCryptographicHash::hash(raw, QCryptographicHash::Sha1).toHex());
}

QString NetCache::pathOf(const QString &key) const
{
	const QDir folder(QDir(m_root).filePath(QLatin1String(kFolder)));
	return folder.filePath(key + QLatin1String(kSuffix));
}

bool NetCache::get(const QString &key, int ttlSeconds, QByteArray *body) const
{
	if (ttlSeconds <= 0)
		return false;

	QFile file(pathOf(key));
	if (!file.open(QIODevice::ReadOnly))
		return false;

	const QJsonObject entry = QJsonDocument::fromJson(file.readAll()).object();
	const qint64 fetched = entry.value(QStringLiteral("fetched")).toInteger();
	if (fetched <= 0)
		return false;

	const qint64 age = QDateTime::currentSecsSinceEpoch() - fetched;
	if (age < 0 || age > ttlSeconds)
		return false;

	const QString text = entry.value(QStringLiteral("body")).toString();
	if (text.isEmpty())
		return false;

	*body = text.toUtf8();
	return true;
}

void NetCache::put(const QString &key, const QByteArray &body)
{
	QJsonObject entry;
	entry.insert(QStringLiteral("key"), key);
	entry.insert(QStringLiteral("fetched"), QDateTime::currentSecsSinceEpoch());
	entry.insert(QStringLiteral("body"), QString::fromUtf8(body));

	QFile file(pathOf(key));
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return;

	file.write(QJsonDocument(entry).toJson(QJsonDocument::Compact));
}

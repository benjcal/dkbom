#include "ratelimit.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QStandardPaths>

namespace {
/* The minute is a sliding window of request timestamps; two minutes of them is
 * plenty of history and keeps the file small. */
const int kWindowSeconds = 60;

QString cacheRoot()
{
	const QDir base(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation));
	const QString root = base.filePath(QStringLiteral("dkbom"));
	QDir().mkpath(root);
	return root;
}
}

RateLimiter::RateLimiter()
{
	const QString root = cacheRoot();
	m_file = QDir(root).filePath(QStringLiteral("quota.json"));
	m_lockFile = QDir(root).filePath(QStringLiteral("quota.lock"));

	m_minuteLimit = qEnvironmentVariableIntValue("DK_BROWSE_MAX_PER_MIN");
	if (m_minuteLimit <= 0)
		m_minuteLimit = 60;

	m_dayLimit = qEnvironmentVariableIntValue("DK_BROWSE_MAX_PER_DAY");
	if (m_dayLimit <= 0)
		m_dayLimit = 800;

	load();
}

void RateLimiter::load()
{
	QFile file(m_file);
	if (!file.open(QIODevice::ReadOnly))
		return;

	const QJsonObject state = QJsonDocument::fromJson(file.readAll()).object();

	m_day = QDate::fromString(state.value(QStringLiteral("day")).toString(), Qt::ISODate);
	m_dayCount = state.value(QStringLiteral("dayCount")).toInt();
	m_serverLimit = state.value(QStringLiteral("serverLimit")).toInt();
	m_serverRemaining = state.contains(QStringLiteral("serverRemaining"))
				  ? state.value(QStringLiteral("serverRemaining")).toInt()
				  : -1;

	/* Each read replaces whatever was already in memory. */
	m_window.clear();

	const qint64 oldest = QDateTime::currentSecsSinceEpoch() - kWindowSeconds;
	for (const QJsonValue &value : state.value(QStringLiteral("window")).toArray()) {
		const qint64 at = value.toInteger();
		if (at > oldest)
			m_window.append(at);
	}
}

void RateLimiter::save() const
{
	QJsonArray window;
	for (const qint64 at : m_window)
		window.append(at);

	QJsonObject state;
	state.insert(QStringLiteral("day"), m_day.toString(Qt::ISODate));
	state.insert(QStringLiteral("dayCount"), m_dayCount);
	state.insert(QStringLiteral("window"), window);
	state.insert(QStringLiteral("serverLimit"), m_serverLimit);
	state.insert(QStringLiteral("serverRemaining"), m_serverRemaining);

	QFile file(m_file);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return;

	file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
}

int RateLimiter::localRemaining() const
{
	const QDate today = QDate::currentDate();
	const int spent = m_day == today ? m_dayCount : 0;

	const qint64 oldest = QDateTime::currentSecsSinceEpoch() - kWindowSeconds;
	int inWindow = 0;
	for (const qint64 at : m_window) {
		if (at > oldest)
			++inWindow;
	}

	return qMax(0, qMin(m_dayLimit - spent, m_minuteLimit - inWindow));
}

bool RateLimiter::spend()
{
	/* Everything under one lock, including the re-read: another process may
	 * have spent the slot between our load and our write. */
	QLockFile lock(m_lockFile);
	lock.setStaleLockTime(5000);
	if (!lock.tryLock(2000))
		return false;

	load();

	const QDate today = QDate::currentDate();
	if (m_day != today) {
		m_day = today;
		m_dayCount = 0;
	}

	const qint64 now = QDateTime::currentSecsSinceEpoch();
	const qint64 oldest = now - kWindowSeconds;

	QVector<qint64> window;
	window.reserve(m_window.size());
	for (const qint64 at : m_window) {
		if (at > oldest)
			window.append(at);
	}
	m_window = window;

	const bool fits = m_dayCount < m_dayLimit && m_window.size() < m_minuteLimit;
	if (fits) {
		++m_dayCount;
		m_window.append(now);
	}

	save();
	lock.unlock();
	return fits;
}

bool RateLimiter::dayExhausted() const
{
	return m_day == QDate::currentDate() && m_dayCount >= m_dayLimit;
}

void RateLimiter::recordServer(int limit, int remaining)
{
	if (limit <= 0 && remaining < 0)
		return;

	QLockFile lock(m_lockFile);
	lock.setStaleLockTime(5000);
	if (!lock.tryLock(2000))
		return;

	load();
	if (limit > 0)
		m_serverLimit = limit;
	if (remaining >= 0)
		m_serverRemaining = remaining;
	save();
	lock.unlock();
}

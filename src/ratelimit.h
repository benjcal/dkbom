#pragma once

#include <QDateTime>
#include <QString>
#include <QVector>

/* A local budget in front of Digi-Key's own. The headers on every response say
 * what is really left; this keeps the tool from finding out the hard way, and
 * it survives a restart because a spent day stays spent.
 *
 * The counters live in the same cache directory as the responses, updated under
 * a lock file so a second copy of the window cannot spend the same slot twice. */
class RateLimiter {
public:
	RateLimiter();

	/* True when a request fits both the per-minute window and the day's
	 * allowance; spends a slot when it does. */
	bool spend();

	int localRemaining() const;

	/* The minute window refills by itself; the day does not. */
	bool dayExhausted() const;
	int dayLimit() const { return m_dayLimit; }
	int minuteLimit() const { return m_minuteLimit; }

	/* What the server said, which is authoritative once known. */
	void recordServer(int limit, int remaining);
	int serverRemaining() const { return m_serverRemaining; }
	int serverLimit() const { return m_serverLimit; }

private:
	void load();
	void save() const;

	QString m_file;
	QString m_lockFile;

	QDate m_day;
	int m_dayCount = 0;
	QVector<qint64> m_window;

	int m_serverLimit = 0;
	int m_serverRemaining = -1;

	/* Deliberately below the advertised 1000/day and 120/minute: a bench tool
	 * that stops for the day is worse than one that throttles itself. */
	int m_minuteLimit;
	int m_dayLimit;
};

#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QString>

/* Responses on disk, so a search that was already run costs nothing to run
 * again. Digi-Key's quota is per client and per day, and browsing is exactly
 * the behaviour that spends it: paging back and forth, re-ticking a filter to
 * see what it did, reopening a project next morning.
 *
 * One file per request, named by the digest of its key, so two processes never
 * write the same file and a partial write is only ever one stale entry. */
class NetCache {
public:
	NetCache();

	/* Method, path and body: a POST search is only the same request when the
	 * body matches, and the body is where the filters and the page live. */
	static QString key(const QString &method, const QString &path, const QByteArray &body);

	/* True when the entry exists and is younger than ttlSeconds. */
	bool get(const QString &key, int ttlSeconds, QByteArray *body) const;
	void put(const QString &key, const QByteArray &body);

	QString root() const { return m_root; }

private:
	QString pathOf(const QString &key) const;

	QString m_root;
};

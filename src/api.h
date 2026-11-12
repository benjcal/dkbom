#pragma once

#include "netcache.h"
#include "ratelimit.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>

#include <functional>

class QNetworkReply;
class QNetworkRequest;

/* The mechanics of api.digikey.com and nothing else: base address, locale
 * headers, the client-credentials token shared by every call, and a reply
 * delivered as JSON or as a readable problem string. It knows nothing about
 * products, filters or paging.
 *
 * It does know about the quota, because every call costs one: responses are
 * kept on disk for a caller-chosen while, the token survives a restart, and
 * requests are spaced out and counted so browsing cannot spend the day's
 * allowance in a minute. */
class Api : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool hasCredentials READ hasCredentials NOTIFY credentialsChanged)
	Q_PROPERTY(QString quotaText READ quotaText NOTIFY quotaChanged)
	Q_PROPERTY(int serverRemaining READ serverRemaining NOTIFY quotaChanged)
	Q_PROPERTY(int serverLimit READ serverLimit NOTIFY quotaChanged)
	Q_PROPERTY(int cachedHits READ cachedHits NOTIFY quotaChanged)

public:
	/* root is empty when the request failed; error then carries the reason. */
	using Done = std::function<void(const QJsonObject &root, const QString &error)>;
	using Fail = std::function<void(const QString &problem)>;

	explicit Api(QObject *parent = nullptr);

	bool hasCredentials() const;

	/* The toolbar's settings dialog is the usual source; the environment stays
	 * as the dev path. Either way the token is dropped, since it was issued to
	 * the credentials that just went away. */
	Q_INVOKABLE void setCredentials(const QString &clientId, const QString &clientSecret);

	/* ttlSeconds > 0 keeps the response on disk and answers from there while it
	 * is younger; 0 always asks Digi-Key. */
	void get(const QString &path, Done done, Fail fail, int ttlSeconds = 0);
	void post(const QString &path, const QJsonObject &body, Done done, Fail fail,
		  int ttlSeconds = 0);

	QString quotaText() const;
	int serverRemaining() const { return m_limiter.serverRemaining(); }
	int serverLimit() const { return m_limiter.serverLimit(); }
	int cachedHits() const { return m_cachedHits; }

	/* Both endpoints report failures inside a 200 body as often as with a bad
	 * status, so the message has to be fished out of whichever shape arrived. */
	static QString problemText(const QJsonObject &root);

signals:
	void credentialsChanged();
	void quotaChanged();

private:
	void ensureToken(std::function<void()> then, Fail fail);
	void authorize(QNetworkRequest &request) const;
	void request(const QString &method, const QString &path, const QByteArray &body, Done done,
		     Fail fail, int ttlSeconds);
	void send(const QString &method, const QString &path, const QByteArray &body,
		  const QString &cacheKey, int ttlSeconds, Done done, Fail fail);
	void noteQuota(QNetworkReply *reply);

	void loadToken();
	void saveToken() const;
	void forgetToken();

	QString m_clientId;
	QString m_clientSecret;
	QString m_token;
	QDateTime m_tokenExpiry;

	/* Callers needing a token at the same moment queue behind one request. */
	bool m_tokenInFlight = false;
	QVector<QPair<std::function<void()>, Fail>> m_waiters;

	NetCache m_cache;
	RateLimiter m_limiter;
	int m_cachedHits = 0;

	/* A burst of enrichment requests is exactly what a rate limit punishes, so
	 * consecutive calls are spaced; the server sees a steady trickle. */
	static constexpr qint64 kSpacingMs = 250;
	QElapsedTimer m_sinceLastCall;

	QNetworkAccessManager m_network;
};

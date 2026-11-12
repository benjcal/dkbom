#include "api.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

namespace {
const char *kBase = "https://api.digikey.com";
const char *kTokenPath = "/v1/oauth2/token";
const char *kSite = "US";
const char *kLanguage = "en";
const char *kCurrency = "USD";

/* The token is worth a day of not asking for it again: a restart in the middle
 * of a BOM should not cost an extra request, and Digi-Key expires it anyway. */
QString tokenPath()
{
	const QDir base(QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation));
	const QString root = base.filePath(QStringLiteral("dkbom"));
	QDir().mkpath(root);
	return QDir(root).filePath(QStringLiteral("token.json"));
}
}

Api::Api(QObject *parent)
	: QObject(parent)
{
	m_clientId = qEnvironmentVariable("DIGIKEY_CLIENT_ID");
	m_clientSecret = qEnvironmentVariable("DIGIKEY_CLIENT_SECRET");
	loadToken();
}

bool Api::hasCredentials() const
{
	return !m_clientId.isEmpty() && !m_clientSecret.isEmpty();
}

void Api::setCredentials(const QString &clientId, const QString &clientSecret)
{
	if (clientId == m_clientId && clientSecret == m_clientSecret)
		return;

	m_clientId = clientId;
	m_clientSecret = clientSecret;
	m_token.clear();
	m_tokenExpiry = QDateTime();
	forgetToken();

	emit credentialsChanged();
}

QString Api::problemText(const QJsonObject &root)
{
	for (const char *key : { "ErrorMessage", "error_description", "detail", "title" }) {
		const QString text = root.value(QLatin1String(key)).toString();
		if (!text.isEmpty())
			return text;
	}
	return {};
}

QString Api::quotaText() const
{
	if (m_limiter.serverRemaining() >= 0 && m_limiter.serverLimit() > 0) {
		return QStringLiteral("%1/%2 left")
			.arg(m_limiter.serverRemaining())
			.arg(m_limiter.serverLimit());
	}

	return QStringLiteral("%1/%2 budget")
		.arg(m_limiter.localRemaining())
		.arg(m_limiter.dayLimit());
}

void Api::authorize(QNetworkRequest &request) const
{
	request.setRawHeader("Authorization", "Bearer " + m_token.toLatin1());
	request.setRawHeader("X-DIGIKEY-Client-Id", m_clientId.toLatin1());
	request.setRawHeader("X-DIGIKEY-Locale-Site", kSite);
	request.setRawHeader("X-DIGIKEY-Locale-Language", kLanguage);
	request.setRawHeader("X-DIGIKEY-Locale-Currency", kCurrency);
}

void Api::loadToken()
{
	if (!hasCredentials())
		return;

	QFile file(tokenPath());
	if (!file.open(QIODevice::ReadOnly))
		return;

	const QJsonObject entry = QJsonDocument::fromJson(file.readAll()).object();
	if (entry.value(QStringLiteral("clientId")).toString() != m_clientId)
		return;

	const qint64 expiry = entry.value(QStringLiteral("expiry")).toInteger();
	if (expiry <= QDateTime::currentSecsSinceEpoch())
		return;

	m_token = entry.value(QStringLiteral("token")).toString();
	m_tokenExpiry = QDateTime::fromSecsSinceEpoch(expiry);
}

void Api::saveToken() const
{
	QJsonObject entry;
	entry.insert(QStringLiteral("clientId"), m_clientId);
	entry.insert(QStringLiteral("token"), m_token);
	entry.insert(QStringLiteral("expiry"), m_tokenExpiry.toSecsSinceEpoch());

	QFile file(tokenPath());
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return;

	file.write(QJsonDocument(entry).toJson(QJsonDocument::Compact));
}

void Api::forgetToken()
{
	QFile::remove(tokenPath());
}

void Api::ensureToken(std::function<void()> then, Fail fail)
{
	if (!m_token.isEmpty() && m_tokenExpiry > QDateTime::currentDateTime()) {
		then();
		return;
	}

	if (!hasCredentials()) {
		fail(QStringLiteral("no credentials: set DIGIKEY_CLIENT_ID and DIGIKEY_CLIENT_SECRET"));
		return;
	}

	m_waiters.append({ then, fail });
	if (m_tokenInFlight)
		return;

	m_tokenInFlight = true;

	QUrlQuery form;
	form.addQueryItem(QStringLiteral("grant_type"), QStringLiteral("client_credentials"));
	form.addQueryItem(QStringLiteral("client_id"), m_clientId);
	form.addQueryItem(QStringLiteral("client_secret"), m_clientSecret);

	QNetworkRequest request{QUrl(QLatin1String(kBase) + QLatin1String(kTokenPath))};
	request.setHeader(QNetworkRequest::ContentTypeHeader,
			  QStringLiteral("application/x-www-form-urlencoded"));

	QNetworkReply *reply = m_network.post(request, form.toString(QUrl::FullyEncoded).toLatin1());
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		reply->deleteLater();
		m_tokenInFlight = false;

		const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
		const QString token = root.value(QStringLiteral("access_token")).toString();

		const auto waiters = m_waiters;
		m_waiters.clear();

		if (token.isEmpty()) {
			QString problem = problemText(root);
			if (problem.isEmpty())
				problem = reply->errorString();
			for (const auto &waiter : waiters)
				waiter.second(problem);
			return;
		}

		m_token = token;
		const int seconds = root.value(QStringLiteral("expires_in")).toInt(600);
		m_tokenExpiry = QDateTime::currentDateTime().addSecs(qMax(seconds - 60, 30));
		saveToken();
		for (const auto &waiter : waiters)
			waiter.first();
	});
}

void Api::noteQuota(QNetworkReply *reply)
{
	const QByteArray limit = reply->rawHeader("x-ratelimit-limit");
	const QByteArray remaining = reply->rawHeader("x-ratelimit-remaining");
	if (limit.isEmpty() && remaining.isEmpty())
		return;

	m_limiter.recordServer(limit.toInt(), remaining.isEmpty() ? -1 : remaining.toInt());
	emit quotaChanged();
}

void Api::request(const QString &method, const QString &path, const QByteArray &body, Done done,
		  Fail fail, int ttlSeconds)
{
	const QString key = NetCache::key(method, path, body);

	if (ttlSeconds > 0) {
		QByteArray cached;
		if (m_cache.get(key, ttlSeconds, &cached)) {
			++m_cachedHits;
			emit quotaChanged();
			done(QJsonDocument::fromJson(cached).object(), QString());
			return;
		}
	}

	ensureToken([this, method, path, body, key, ttlSeconds, done, fail] {
		send(method, path, body, key, ttlSeconds, done, fail);
	}, fail);
}

void Api::send(const QString &method, const QString &path, const QByteArray &body,
	       const QString &cacheKey, int ttlSeconds, Done done, Fail fail)
{
	if (!m_limiter.spend()) {
		emit quotaChanged();

		/* The minute refills by itself: wait for it and retry, since only the
		 * day's end is final. */
		if (!m_limiter.dayExhausted()) {
			QTimer::singleShot(2000, this, [this, method, path, body, cacheKey,
							ttlSeconds, done, fail] {
				send(method, path, body, cacheKey, ttlSeconds, done, fail);
			});
			return;
		}

		fail(QStringLiteral("local request budget spent (%1/day, %2/minute); "
				    "responses already fetched are still served")
			     .arg(m_limiter.dayLimit())
			     .arg(m_limiter.minuteLimit()));
		return;
	}
	emit quotaChanged();

	const qint64 wait = m_sinceLastCall.isValid()
				  ? qMax<qint64>(0, kSpacingMs - m_sinceLastCall.elapsed())
				  : 0;
	m_sinceLastCall.start();

	auto fire = [this, method, path, body, cacheKey, ttlSeconds, done, fail] {
		QNetworkRequest request{QUrl(QLatin1String(kBase) + path)};
		authorize(request);

		QNetworkReply *reply = nullptr;
		if (method == QLatin1String("POST")) {
			request.setHeader(QNetworkRequest::ContentTypeHeader,
					  QStringLiteral("application/json"));
			reply = m_network.post(request, body);
		} else {
			reply = m_network.get(request);
		}

		connect(reply, &QNetworkReply::finished, this,
			[this, reply, cacheKey, ttlSeconds, done, fail] {
				reply->deleteLater();
				noteQuota(reply);

				const QByteArray payload = reply->readAll();
				const QJsonObject root =
					QJsonDocument::fromJson(payload).object();
				QString error = problemText(root);
				if (error.isEmpty() && root.isEmpty())
					error = reply->errorString();

				/* Only a usable answer is worth keeping: a throttle or a
				 * failure must not be replayed for the next hour. */
				if (error.isEmpty() && ttlSeconds > 0 && !root.isEmpty())
					m_cache.put(cacheKey, payload);

				done(root, error);
			});
	};

	if (wait > 0)
		QTimer::singleShot(int(wait), this, fire);
	else
		fire();
}

void Api::get(const QString &path, Done done, Fail fail, int ttlSeconds)
{
	request(QStringLiteral("GET"), path, QByteArray(), done, fail, ttlSeconds);
}

void Api::post(const QString &path, const QJsonObject &body, Done done, Fail fail, int ttlSeconds)
{
	request(QStringLiteral("POST"), path, QJsonDocument(body).toJson(QJsonDocument::Compact), done,
		fail, ttlSeconds);
}

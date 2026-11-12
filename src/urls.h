#pragma once

#include <QString>

/* Digi-Key hands back protocol-relative URLs for its own media host
 * ("//mm.digikey.com/..."). Without a scheme Qt resolves one against the
 * binary's qrc: base and the viewer is handed a file it cannot read, so an
 * absolute URL or nothing. */
inline QString absoluteUrl(const QString &url)
{
	const QString trimmed = url.trimmed();
	if (trimmed.startsWith(QLatin1String("//")))
		return QLatin1String("https:") + trimmed;
	if (!trimmed.contains(QLatin1String("://")))
		return {};
	return trimmed;
}

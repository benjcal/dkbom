#include "bomcsv.h"

namespace {

/* One row per line, `"` quoting with `""` for a literal quote, which is what
 * KiCad's exporter writes. Embedded line breaks inside a quoted field stay in
 * the field. */
QVector<QStringList> table(const QString &text, QChar delimiter)
{
	QVector<QStringList> rows;
	QStringList row;
	QString field;
	bool quoted = false;
	bool started = false;

	auto endField = [&] {
		row.append(field);
		field.clear();
		started = true;
	};

	auto endRow = [&] {
		endField();
		rows.append(row);
		row.clear();
		started = false;
	};

	for (int i = 0; i < text.size(); ++i) {
		const QChar c = text.at(i);

		if (quoted) {
			if (c != '"') {
				field.append(c);
			} else if (i + 1 < text.size() && text.at(i + 1) == '"') {
				field.append(c);
				++i;
			} else {
				quoted = false;
			}
			continue;
		}

		if (c == '"') {
			quoted = true;
			started = true;
		} else if (c == delimiter) {
			endField();
		} else if (c == '\n') {
			endRow();
		} else if (c != '\r') {
			field.append(c);
			started = true;
		}
	}

	if (started || !field.isEmpty() || !row.isEmpty())
		endRow();

	return rows;
}

} // namespace

QVector<bom::Row> bom::parse(const QByteArray &data, QChar delimiter, QString *error)
{
	QVector<Row> rows;

	const QVector<QStringList> cells = table(QString::fromUtf8(data), delimiter);
	if (cells.isEmpty()) {
		*error = QStringLiteral("the BOM export is empty");
		return rows;
	}

	const QStringList &header = cells.first();
	const int refsAt = header.indexOf(QStringLiteral("Refs"));
	const int valueAt = header.indexOf(QStringLiteral("Value"));

	if (refsAt < 0 || valueAt < 0) {
		*error = QStringLiteral("unexpected BOM header: %1").arg(header.join(QStringLiteral(", ")));
		return rows;
	}

	const int footprintAt = header.indexOf(QStringLiteral("Footprint"));
	const int dkpnAt = header.indexOf(QStringLiteral("DKPN"));
	const int dnpAt = header.indexOf(QStringLiteral("DNP"));
	const int qtyAt = header.indexOf(QStringLiteral("Qty"));

	auto cell = [](const QStringList &line, int at) -> QString {
		return at >= 0 && at < line.size() ? line.at(at) : QString();
	};

	for (int i = 1; i < cells.size(); ++i) {
		const QStringList &line = cells.at(i);

		Row row;
		row.value = cell(line, valueAt);
		row.footprint = cell(line, footprintAt);
		row.dkpn = cell(line, dkpnAt);
		row.dnp = !cell(line, dnpAt).trimmed().isEmpty();

		/* KiCad writes ranges as "R1-R4" unless told not to; the writer needs
		 * the individual references, and we ask for them. */
		for (const QString &ref : cell(line, refsAt).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
			const QString trimmed = ref.trimmed();
			if (!trimmed.isEmpty())
				row.refs.append(trimmed);
		}

		if (row.refs.isEmpty())
			continue;

		row.qty = cell(line, qtyAt).trimmed().toInt();
		if (row.qty <= 0)
			row.qty = row.refs.size();

		rows.append(row);
	}

	return rows;
}

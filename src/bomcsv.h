#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

/* The BOM as `kicad-cli sch export bom` writes it. KiCad does the hierarchy,
 * the exclude-from-BOM flag and the grouping; this only reads the table it
 * hands back. */
namespace bom {

struct Row {
	QStringList refs;
	QString value;
	QString footprint;
	QString dkpn;
	bool dnp = false;
	int qty = 0;
};

/* Rows of the exported table, keyed by its own header names, so a column
 * added to the export does not shift the others. */
QVector<Row> parse(const QByteArray &data, QChar delimiter, QString *error);

} // namespace bom

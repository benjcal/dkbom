#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

/* KiCad's schematic format is an S-expression tree, but nothing here needs a
 * tree: a BOM needs the placed symbols and their fields, and an assignment
 * needs to replace one string inside one block. So this reads the file as
 * text: it finds the byte range of each placed symbol, and edits inside that
 * range. Copying every other byte through is what keeps `lib_symbols`, UUIDs
 * and any token this code has never heard of intact. */
namespace sch {

/* One placed symbol, with the byte range of its block in the file text. */
struct Symbol {
	QByteArray uuid;
	QString ref;
	QString value;
	int start = 0;
	int end = 0;
};

/* Every placed symbol in one schematic file, in file order. Library
 * definitions live under `lib_symbols` one tab deeper, and so does every
 * embedded unit; the indentation is what tells a placed part from a library
 * part. Symbols without a reference (graphic shapes in a symbol library
 * dropped into the sheet) are skipped. */
QVector<Symbol> scan(const QByteArray &text);

struct Edit {
	bool ok = false;
	QByteArray block;
	QString error;
};

/* Set a property on one symbol block. An existing property keeps its position
 * and every other byte of the block; a missing one is inserted in KiCad's own
 * layout, before the symbol's pins, hidden, at the symbol's position. */
Edit setProperty(const QByteArray &block, const QString &name, const QString &value);

} // namespace sch

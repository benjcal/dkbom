#include "schematic.h"

namespace {

bool isSpaceChar(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Offset one past the ')' closing the list opened at `start`. Quoted strings
 * are skipped, so a parenthesis inside a pin name or a value cannot end the
 * block early. */
int balancedEnd(const QByteArray &text, int start, bool *ok)
{
	int depth = 0;
	int i = start;
	const int size = text.size();

	while (i < size) {
		const char c = text.at(i);

		if (c == '"') {
			++i;
			while (i < size) {
				if (text.at(i) == '\\') {
					i += 2;
					continue;
				}
				if (text.at(i) == '"') {
					++i;
					break;
				}
				++i;
			}
			continue;
		}

		if (c == '(') {
			++depth;
		} else if (c == ')') {
			if (--depth == 0) {
				*ok = true;
				return i + 1;
			}
		}
		++i;
	}

	*ok = false;
	return size;
}

/* The first string after `key` in `block`, starting the search at `from`. */
QString quotedAfter(const QByteArray &block, int from, const QByteArray &key)
{
	const int at = block.indexOf(key, from);
	if (at < 0)
		return {};

	const int start = at + key.size();
	const int end = block.indexOf('"', start);
	if (end < 0)
		return {};

	return QString::fromUtf8(block.mid(start, end - start));
}

/* The symbol's own UUID is the first one in the block: pins carry theirs
 * further down. */
QByteArray uuidOf(const QByteArray &block)
{
	const QByteArray key = "(uuid \"";
	const int at = block.indexOf(key);
	if (at < 0)
		return {};

	const int start = at + key.size();
	const int end = block.indexOf('"', start);
	if (end < 0)
		return {};

	return block.mid(start, end - start);
}

/* KiCad 7 and later keep the annotated reference per sheet instance, under
 * `(instances ... (path ... (reference "R27")))`; the Reference property is
 * the last value written there and is what older files only have. */
QString refOf(const QByteArray &block)
{
	const int instances = block.indexOf("(instances");
	if (instances >= 0) {
		const QString ref = quotedAfter(block, instances, "(reference \"");
		if (!ref.isEmpty())
			return ref;
	}

	return quotedAfter(block, 0, "(property \"Reference\" \"");
}

QByteArray propertyText(const QString &name, const QByteArray &value, const QByteArray &position)
{
	QByteArray out;
	out += "\n\t\t(property \"" + name.toUtf8() + "\" \"" + value + "\"";
	out += "\n\t\t\t(at " + position + ")";
	out += "\n\t\t\t(hide yes)";
	out += "\n\t\t\t(show_name no)";
	out += "\n\t\t\t(do_not_autoplace no)";
	out += "\n\t\t\t(effects";
	out += "\n\t\t\t\t(font";
	out += "\n\t\t\t\t\t(size 1.27 1.27)";
	out += "\n\t\t\t\t)";
	out += "\n\t\t\t)";
	out += "\n\t\t)";
	return out;
}

} // namespace

QVector<sch::Symbol> sch::scan(const QByteArray &text)
{
	QVector<Symbol> symbols;
	int lineStart = 0;

	while (lineStart < text.size()) {
		int lineEnd = text.indexOf('\n', lineStart);
		if (lineEnd < 0)
			lineEnd = text.size();

		const QByteArray line = text.mid(lineStart, lineEnd - lineStart);
		const bool placed = line.startsWith("\t(symbol")
				    && (line.size() == 8 || isSpaceChar(line.at(8)));

		if (!placed) {
			lineStart = lineEnd + 1;
			continue;
		}

		bool closed = false;
		const int end = balancedEnd(text, lineStart + 1, &closed);

		if (closed) {
			const QByteArray block = text.mid(lineStart, end - lineStart);

			Symbol symbol;
			symbol.start = lineStart;
			symbol.end = end;
			symbol.uuid = uuidOf(block);
			symbol.ref = refOf(block);
			symbol.value = quotedAfter(block, 0, "(property \"Value\" \"");

			if (!symbol.ref.isEmpty() && !symbol.uuid.isEmpty())
				symbols.append(symbol);
		}

		lineStart = end > lineStart ? end : lineEnd + 1;
	}

	return symbols;
}

sch::Edit sch::setProperty(const QByteArray &block, const QString &name, const QString &value)
{
	Edit result;

	const QByteArray raw = value.toUtf8();
	if (raw.contains('"') || raw.contains('\\') || raw.contains('\n')) {
		result.error = QStringLiteral("value holds a character a schematic string cannot");
		return result;
	}

	const QByteArray key = "(property \"" + name.toUtf8() + "\" \"";
	const int at = block.indexOf(key);

	if (at >= 0) {
		const int start = at + key.size();
		const int end = block.indexOf('"', start);
		if (end < 0) {
			result.error = QStringLiteral("property %1 is not closed").arg(name);
			return result;
		}

		QByteArray edited = block;
		edited.replace(start, end - start, raw);
		result.ok = true;
		result.block = edited;
		return result;
	}

	/* A new field goes before the pins, which is where KiCad puts the fields
	 * it knows about; the closing paren is the fallback for a symbol with no
	 * pins at all. */
	const int pin = block.indexOf("\n\t\t(pin ");
	const int instances = block.indexOf("\n\t\t(instances");
	int anchor = (pin >= 0 && (instances < 0 || pin < instances)) ? pin : instances;
	if (anchor < 0)
		anchor = block.lastIndexOf("\n\t)");
	if (anchor < 0) {
		result.error = QStringLiteral("no place inside %1's block for a property").arg(name);
		return result;
	}

	/* The anchor is the '\n' of the line break above the pins. On a CRLF sheet
	 * that would split the pair, leaving the '\r' behind. */
	if (anchor > 0 && block.at(anchor - 1) == '\r')
		--anchor;

	/* Hidden or not, a field with no position lands on the sheet origin, so
	 * the symbol's own position is the one to copy. */
	QByteArray position = "0 0 0";
	const int atPos = block.indexOf("\n\t\t(at ");
	if (atPos >= 0) {
		const int close = block.indexOf(')', atPos);
		if (close > atPos)
			position = block.mid(atPos + 7, close - (atPos + 7)).trimmed();
	}

	/* The inserted text follows the sheet's own endings: KiCad reads a bare LF,
	 * but a sheet that comes back half converted is a diff nobody asked for. */
	QByteArray added = propertyText(name, raw, position);
	if (block.contains("\r\n"))
		added.replace('\n', "\r\n");

	QByteArray edited = block;
	edited.insert(anchor, added);
	result.ok = true;
	result.block = edited;
	return result;
}

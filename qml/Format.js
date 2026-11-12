.pragma library

/* One palette for the whole tool. Partitions have to be told apart at a glance:
 * chrome, panels, table head, rows, and the things that are ticked or stale. */
var appBg = "#e9edf2"
var surface = "#ffffff"
var panelBg = "#f5f8fb"
var sectionBg = "#d9e2ec"
var sectionText = "#2b3947"
var blockBg = "#eaf0f6"
var tableHeadBg = "#ccd8e5"
var rowA = "#ffffff"
var rowB = "#f4f7fa"
var rowSelected = "#b7d4ef"
var rowHover = "#e4eef8"
var text = "#161b21"
var muted = "#5f6b78"
var accent = "#1a6fc0"
var tickBg = "#bfdcf6"
var pending = "#a05a00"
var separator = "#cdd8e3"

function money(value) {
	var v = Number(value);
	if (!isFinite(v) || v <= 0)
		return "\u2014";
	return "$" + v.toFixed(v < 1 ? 4 : 3);
}

/* Extended price: two decimals once it is a real amount of money, four while it
 * is still fractions of a cent. */
function total(value) {
	var v = Number(value);
	if (!isFinite(v) || v <= 0)
		return "\u2014";
	return "$" + v.toFixed(v < 1 ? 4 : 2);
}

function count(value) {
	var n = Number(value);
	if (!isFinite(n))
		return "";
	return n.toLocaleString(Qt.locale(), "f", 0);
}

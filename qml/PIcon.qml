import QtQuick
import "Format.js" as Format

Text {
	property string glyph
	property int size: 14
	property color tint: Format.text

	text: glyph
	font.family: "Phosphor"
	font.pixelSize: size
	color: tint
	verticalAlignment: Text.AlignVCenter
	horizontalAlignment: Text.AlignHCenter
}

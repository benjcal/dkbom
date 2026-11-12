import QtQuick
import "Format.js" as Format

/* Hovering a thumbnail blows it up. Shared by the results table and the BOM
 * dock so a part looks the same wherever it is being checked; the caller owns
 * the timer that decides when it appears, since sliding down a column of
 * thumbnails must not flash a picture on every row it crosses. */
Rectangle {
	id: peek

	property var row
	property bool active: false
	property string caption

	visible: active && row !== null
	width: 320
	height: 320
	color: Format.surface
	border.color: Format.accent
	z: 10

	Image {
		anchors.fill: parent
		anchors.margins: 10
		anchors.bottomMargin: 34
		source: peek.row ? peek.row.photoUrl : ""
		asynchronous: true
		fillMode: Image.PreserveAspectFit
		sourceSize.width: 600
		sourceSize.height: 600
		cache: true
	}

	Text {
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.bottom: parent.bottom
		anchors.margins: 10
		text: peek.caption !== "" ? peek.caption
					  : (peek.row ? (peek.row.mpn || peek.row.dkpn || "") : "")
		font.pixelSize: 11
		font.bold: true
		color: Format.text
		elide: Text.ElideRight
	}
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Dk 1.0
import "Format.js" as Format
import "Phosphor.js" as Phosphor

Rectangle {
	id: view
	color: Format.surface

	signal notify(string message)

	/* What the target strip shows, and what Enter writes: the row the table is
	 * standing on. Bound through list.currentIndex so it follows the keyboard. */
	readonly property var currentRowData: list.currentIndex >= 0
					       ? Search.results.rowAt(list.currentIndex) : null

	/* Column definitions are declared once and shared by the header and the
	 * rows. Widths and visibility live beside them and are addressed by key,
	 * because both are dragged, toggled and persisted by name. */
	readonly property var definitions: [
		{ key: "photo", title: "Photo", width: 52 },
		{ key: "mpn", title: "Part", sort: "mpn", width: 276 },
		{ key: "datasheet", title: "Datasheet", width: 70 },
		{ key: "dkpn", title: "DigiKey #", sort: "dkpn", width: 164 },
		{ key: "stock", title: "Stock", numeric: true, sort: "stock", width: 74 },
		{ key: "moq", title: "MOQ", numeric: true, sort: "moq", width: 50 },
		{ key: "price", title: "Unit", numeric: true, sort: "price", width: 62 },
		{ key: "lineTotal", title: "Total", numeric: true, sort: "total", width: 70 },
		{ key: "breaks", title: "Breaks", width: -1 }
	]

	/* Hovering a thumbnail blows it up; the timer keeps a mouse sweeping down
	 * the column from flashing a picture on every row it crosses. It sits in the
	 * middle of the pane, clear of the pointer, so reaching for the copy icon on
	 * the right never runs into the enlarged image. */
	property var peekRow: null
	property int peekIndex: -1
	property bool peekActive: false

	property var widths: ({})
	property var hidden: ({})
	property int revision: 0

	property var shown: {
		revision;
		var out = [];
		for (var i = 0; i < definitions.length; ++i) {
			if (hidden[definitions[i].key] !== true)
				out.push(definitions[i]);
		}
		return out;
	}

	readonly property int lastIndex: shown.length - 1

	readonly property int fixedWidth: {
		revision;
		var total = 0;
		for (var i = 0; i < shown.length - 1; ++i)
			total += columnWidth(i);
		return total;
	}

	function defaultWidth(key) {
		for (var i = 0; i < definitions.length; ++i) {
			if (definitions[i].key === key)
				return definitions[i].width;
		}
		return 80;
	}

	/* The last visible column takes whatever is left, so the table always
	 * reaches the edge of its pane however the others are dragged or hidden. */
	function columnWidth(index) {
		revision;
		if (index === lastIndex)
			return Math.max(60, view.width - fixedWidth);
		return columnWidthOf(shown[index].key);
	}

	function columnWidthOf(key) {
		var stored = widths[key];
		return stored === undefined ? defaultWidth(key) : stored;
	}

	function setColumnWidth(index, width) {
		var key = shown[index].key;
		var next = Math.max(36, Math.round(width));
		if (next === columnWidthOf(key))
			return;

		widths[key] = next;
		Prefs.setColumnWidth(key, next);
		saveTimer.restart();
		revision++;
	}

	function isHidden(key) {
		revision;
		return hidden[key] === true;
	}

	function toggleColumn(key) {
		if (hidden[key] === true)
			delete hidden[key];
		else
			hidden[key] = true;

		Prefs.setColumnHidden(key, hidden[key] === true);
		saveTimer.restart();
		revision++;
	}

	function resetColumns() {
		widths = ({});
		hidden = ({});
		Prefs.resetColumns();
		revision++;
	}

	Timer {
		id: saveTimer
		interval: 400
		onTriggered: Prefs.sync()
	}

	Component.onCompleted: {
		var stored = Prefs.columnWidths;
		for (var key in stored)
			widths[key] = stored[key];

		var off = Prefs.hiddenColumns;
		for (var i = 0; i < off.length; ++i)
			hidden[off[i]] = true;

		revision++;
	}

	/* One strip that answers "what am I looking at": the category, the ticks the
	 * visible rows were actually searched with, and the refresh flags. */
	function scopeChips() {
		var out = [];

		out.push({ text: Search.categoryName ? Search.categoryName : "all categories",
			   strong: true });

		if (Search.keywords)
			out.push({ text: "\u201c" + Search.keywords + "\u201d", strong: true });

		var filters = Search.appliedFilters;
		for (var i = 0; i < filters.length; ++i)
			out.push({ note: filters[i].parameter, text: filters[i].value });

		out.push({ text: "stock \u2265 " + Search.minStock });
		out.push({ text: Search.maxMoq > 0 ? "MOQ \u2264 " + Search.maxMoq : "any MOQ" });
		out.push({ text: Search.marketplace ? "marketplace included" : "marketplace excluded" });

		if (Search.pending)
			out.push({ text: "filters changed \u2014 press Apply", warn: true });

		return out;
	}

	function quantityNote(column) {
		if (column.key === "price")
			return "each @" + Search.quantity;
		if (column.key === "lineTotal")
			return "for " + Search.quantity;
		return "";
	}

	function cellText(row, column) {
		if (!row)
			return "";
		if (column.key === "price")
			return Format.money(row.price);
		if (column.key === "lineTotal")
			return Format.total(row.lineTotal);
		if (column.key === "stock" || column.key === "moq")
			return Format.count(row[column.key]);
		var value = row[column.key];
		return value === undefined ? "" : String(value);
	}

	Timer {
		id: peekTimer
		interval: 220
		onTriggered: view.peekActive = true
	}

	PhotoPeek {
		row: view.peekRow
		active: view.peekActive
		caption: view.peekRow ? view.peekRow.mpn : ""
		anchors.centerIn: parent
	}

	/* Double-click is the bulk action: the DigiKey number is what goes into an
	 * order or a search box, and it is the one string per row worth a shortcut. */
	function copyDkpn(row) {
		if (!row || !row.dkpn)
			return;
		Shell.copy(row.dkpn);
		view.notify("Copied " + row.dkpn);
	}

	function currentRow() {
		return list.currentIndex >= 0 ? Search.results.rowAt(list.currentIndex) : null;
	}

	/* Enter means "put this part on the line I picked"; with no line picked
	 * there is nothing to write into, so it stays the copy it always was. */
	function assignOrCopy() {
		var row = view.currentRow();
		if (!row)
			return;

		if (Project.hasTarget && row.dkpn) {
			Project.assign(row.dkpn);
			return;
		}

		view.copyDkpn(row);
	}

	function openCurrent() {
		var row = view.currentRow();
		if (!row || !row.url)
			return;
		Qt.openUrlExternally(row.url);
		view.notify("Opening digikey.com");
	}

	/* In flight: the Search button renames itself, and this turns in the middle
	 * of the table so there is no doubt the old rows are stale. It overlays, so
	 * the table does not reflow when a search starts. */
	Rectangle {
		anchors.centerIn: parent
		width: 96
		height: 96
		radius: 8
		color: Format.surface
		border.color: Format.separator
		opacity: Search.busy ? 0.95 : 0
		visible: opacity > 0
		z: 2

		BusyIndicator {
			anchors.centerIn: parent
			running: Search.busy
			width: 48
			height: 48
		}
	}

	ColumnLayout {
		anchors.fill: parent
		spacing: 0

		/* Which BOM line the pick lands on. It has to be visible from here:
		 * lines are scrolled out of sight in the dock, and writing a number
		 * into the wrong one is the mistake this exists to prevent. */
		Rectangle {
			id: targetStrip
			Layout.fillWidth: true
			Layout.preferredHeight: visible ? 34 : 0
			visible: Project.hasTarget
			color: Format.tickBg

			RowLayout {
				anchors.fill: parent
				anchors.leftMargin: 8
				anchors.rightMargin: 4
				spacing: 8

				RowLayout {
					spacing: 2

					PIcon {
						glyph: Phosphor.arrowRight
						size: 11
						tint: Format.text
					}

					Label {
						text: (Project.target.refText || "")
						      + " (" + (Project.target.qty || 0) + ")"
						font.bold: true
					}
				}

				Label {
					Layout.fillWidth: true
					text: (Project.target.value || "")
					      + (Project.target.footprint
						 ? " \u00b7 " + Project.target.footprint.split(":").pop() : "")
					elide: Text.ElideRight
					opacity: 0.75
				}

				Label {
					text: view.currentRowData && view.currentRowData.dkpn
					      ? view.currentRowData.dkpn : "pick a part"
					elide: Text.ElideMiddle
					Layout.maximumWidth: 190
					font.pixelSize: 11
					color: view.currentRowData && view.currentRowData.dkpn
					       ? Format.text : Format.muted
				}

				Button {
					text: "Assign"
					font.pixelSize: 11
					icon.source: "qrc:/assets/icons/arrow-right.svg"
					icon.color: Format.text
					display: AbstractButton.TextBesideIcon
					enabled: view.currentRowData && view.currentRowData.dkpn && !Project.busy
					onClicked: view.assignOrCopy()
				}

				ToolButton {
					icon.source: "qrc:/assets/icons/x.svg"
					icon.color: Format.text
					onClicked: Project.clearTarget()
					ToolTip.text: "Clear target"
					ToolTip.visible: hovered
				}
			}
		}

		Rectangle {
			Layout.fillWidth: true
			color: Format.panelBg
			Layout.preferredHeight: scope.implicitHeight + 10

			Flow {
				id: scope
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.top: parent.top
				anchors.margins: 5
				spacing: 5

				Repeater {
					model: view.scopeChips()

					delegate: Rectangle {
						id: chip
						required property var modelData

						height: chipRow.implicitHeight + 6
						width: chipRow.implicitWidth + 14
						radius: 4
						color: modelData.warn ? "#f7e3c4"
						     : (modelData.strong ? Format.tickBg : Format.blockBg)
						border.color: modelData.warn ? Format.pending : Format.separator

						Row {
							id: chipRow
							anchors.centerIn: parent
							spacing: 5

							Text {
								anchors.verticalCenter: parent.verticalCenter
								text: chip.modelData.note ? chip.modelData.note + ":" : ""
								font.pixelSize: 10
								color: Format.muted
								visible: !!chip.modelData.note
							}

							Text {
								anchors.verticalCenter: parent.verticalCenter
								text: chip.modelData.text
								font.pixelSize: 11
								font.bold: chip.modelData.strong === true
								color: chip.modelData.warn ? Format.pending : Format.text
							}
						}
					}
				}
			}
		}

		Rectangle {
			Layout.fillWidth: true
			implicitHeight: 26
			color: Format.tableHeadBg

			Row {
				anchors.fill: parent
				Repeater {
					model: view.shown

					delegate: Item {
						id: headerCell
						width: view.columnWidth(index)
						height: parent.height

						readonly property var column: view.shown[index]

						RowLayout {
							anchors.left: parent.left
							anchors.leftMargin: 8
							anchors.right: grip.left
							anchors.verticalCenter: parent.verticalCenter
							spacing: 2

							Text {
								Layout.fillWidth: true
								text: headerCell.column.key === "lineTotal"
								      ? "Total @" + Search.quantity
								      : headerCell.column.title
								font.pixelSize: 11
								font.bold: true
								font.letterSpacing: 0.4
								color: Format.sectionText
								elide: Text.ElideRight
							}

							PIcon {
								visible: Search.results.sortField === headerCell.column.sort
								glyph: Search.results.sortAscending
								       ? Phosphor.caretUp : Phosphor.caretDown
								size: 9
								tint: Format.sectionText
							}
						}

						MouseArea {
							anchors.fill: parent
							anchors.rightMargin: 7
							onClicked: {
								if (headerCell.column.sort)
									Search.results.sortBy(headerCell.column.sort);
							}
						}

						/* Grab lane: wide enough to hit, narrow enough to stay
						 * out of the way of the sort click. */
						Item {
							id: grip
							anchors.right: parent.right
							width: 7
							height: parent.height
							visible: index < view.shown.length - 1

							Rectangle {
								anchors.right: parent.right
								width: 1
								height: parent.height
								color: Format.sectionText
								opacity: gripArea.containsMouse || gripArea.pressed ? 0.55 : 0.18
							}

							MouseArea {
								id: gripArea
								anchors.fill: parent
								hoverEnabled: true
								cursorShape: Qt.SizeHorCursor
								preventStealing: true

								property real grabX: 0
								property real grabWidth: 0

								onPressed: (mouse) => {
									grabX = mapToItem(view, mouse.x, 0).x;
									grabWidth = view.columnWidth(index);
								}
								onPositionChanged: (mouse) => {
									if (pressed)
										view.setColumnWidth(index, grabWidth
											+ mapToItem(view, mouse.x, 0).x - grabX);
								}
							}
						}
					}
				}
			}
		}

		ListView {
			id: list
			Layout.fillWidth: true
			Layout.fillHeight: true
			clip: true
			model: Search.results
			spacing: 0
			currentIndex: count > 0 ? 0 : -1
			focus: true
			ScrollBar.vertical: ScrollBar {}

			Keys.onPressed: (event) => {
				if (event.key === Qt.Key_Down) {
					list.currentIndex = Math.min(list.currentIndex + 1, list.count - 1);
					event.accepted = true;
				} else if (event.key === Qt.Key_Up) {
					list.currentIndex = Math.max(list.currentIndex - 1, 0);
					event.accepted = true;
				} else if (event.key === Qt.Key_PageDown) {
					list.currentIndex = Math.min(list.currentIndex + 10, list.count - 1);
					event.accepted = true;
				} else if (event.key === Qt.Key_PageUp) {
					list.currentIndex = Math.max(list.currentIndex - 10, 0);
					event.accepted = true;
				} else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
					view.assignOrCopy();
					event.accepted = true;
				} else if (event.text === "o") {
					view.openCurrent();
					event.accepted = true;
				}
			}

			delegate: Rectangle {
				id: rowItem
				required property var modelData
				required property int index

				width: list.width
				height: 68

				readonly property bool current: index === list.currentIndex
				readonly property bool hovered: rowHover.containsMouse

				color: current ? Format.rowSelected
					       : (hovered ? Format.rowHover
							  : (index % 2 ? Format.rowB : Format.rowA))

				/* Declared before the cells: nothing but the thumbnail takes
				 * mouse events, so clicks land here while the photo cell still
				 * gets its own hover. */
				MouseArea {
					id: rowHover
					anchors.fill: parent
					hoverEnabled: true
					onClicked: {
						list.forceActiveFocus();
						list.currentIndex = rowItem.index;
					}
					onDoubleClicked: view.assignOrCopy()
				}

				Row {
					anchors.fill: parent

					Repeater {
						model: view.shown

						delegate: Item {
							id: cell
							width: view.columnWidth(index)
							height: parent.height

							readonly property var column: view.shown[index]

							/* Part is three lines so a row answers "what is it,
							 * who makes it, and which packaging do I get"
							 * without a detail pane. */
							Item {
								anchors.fill: parent
								visible: cell.column.key === "photo"

								Image {
									anchors.fill: parent
									anchors.margins: 8
									source: rowItem.modelData.photoUrl
									asynchronous: true
									fillMode: Image.PreserveAspectFit
									sourceSize.width: 88
									sourceSize.height: 88
								}

								MouseArea {
									id: photoHover
									anchors.fill: parent
									hoverEnabled: true
									acceptedButtons: Qt.NoButton
									onContainsMouseChanged: {
										if (containsMouse) {
											view.peekIndex = rowItem.index;
											view.peekRow = rowItem.modelData;
											view.peekActive = false;
											peekTimer.restart();
										} else if (view.peekIndex === rowItem.index) {
											/* Sliding straight down the column enters the next row
											 * before it leaves this one, so a row only clears
											 * the peek that is its own. */
											peekTimer.stop();
											view.peekIndex = -1;
											view.peekRow = null;
											view.peekActive = false;
										}
									}
								}
							}

							Column {
								anchors.fill: parent
								anchors.leftMargin: 8
								anchors.rightMargin: 6
								anchors.topMargin: 9
								spacing: 2
								visible: cell.column.key === "mpn"

								Text {
									width: parent.width - 22
									text: rowItem.modelData.mpn
									font.pixelSize: 13
									font.bold: true
									color: Format.accent
									elide: Text.ElideRight
								}

								Text {
									width: parent.width
									text: rowItem.modelData.description
									font.pixelSize: 11
									color: Format.text
									elide: Text.ElideRight
								}

								Text {
									width: parent.width
									text: rowItem.modelData.mfr
									font.pixelSize: 11
									color: Format.muted
									elide: Text.ElideRight
								}
							}

							/* Copying the manufacturer number is the only string copied per
							 * row, so it is the only one that needs an affordance. */
							Item {
								anchors.right: parent.right
								anchors.rightMargin: 6
								anchors.top: parent.top
								anchors.topMargin: 5
								width: 11
								height: 11
								visible: cell.column.key === "mpn"
									 && (rowItem.hovered || copyMpnHover.containsMouse)

								Rectangle {
									x: 0
									y: 3
									width: 8
									height: 8
									color: "transparent"
									border.width: 1
									border.color: copyMpnHover.containsMouse
										      ? Format.accent : Format.muted
								}

								Rectangle {
									x: 3
									y: 0
									width: 8
									height: 8
									color: rowItem.color
									border.width: 1
									border.color: copyMpnHover.containsMouse
										      ? Format.accent : Format.muted
								}

								MouseArea {
									id: copyMpnHover
									anchors.fill: parent
									hoverEnabled: true
									cursorShape: Qt.PointingHandCursor
									onClicked: {
										Shell.copy(rowItem.modelData.mpn);
										view.notify("Copied " + rowItem.modelData.mpn);
									}
								}
							}

							Item {
								anchors.fill: parent
								visible: cell.column.key === "datasheet"

								/* A word, because "PDF" says what opens. */
								Rectangle {
									anchors.centerIn: parent
									width: 28
									height: 15
									radius: 3
									visible: !!rowItem.modelData.datasheetUrl
									color: datasheetHover.containsMouse ? Format.accent
													: "transparent"
									border.width: 1
									border.color: datasheetHover.containsMouse ? Format.accent
													: Format.muted

									Text {
										anchors.centerIn: parent
										text: "PDF"
										font.pixelSize: 9
										font.bold: true
										color: datasheetHover.containsMouse ? "white"
															: Format.text
									}

									MouseArea {
										id: datasheetHover
										anchors.fill: parent
										hoverEnabled: true
										cursorShape: Qt.PointingHandCursor
										onClicked: {
											Qt.openUrlExternally(
												rowItem.modelData.datasheetUrl);
											view.notify("Opening datasheet");
										}
									}
								}
							}

							Column {
								anchors.fill: parent
								anchors.leftMargin: 8
								anchors.rightMargin: 6
								anchors.topMargin: 9
								spacing: 2
								visible: cell.column.key === "dkpn"

								Text {
									width: parent.width - 20
									text: rowItem.modelData.dkpn
									font.pixelSize: 12
									font.bold: rowItem.current
									color: Format.text
									elide: Text.ElideRight
								}

								Text {
									width: parent.width
									text: rowItem.modelData.packaging
									font.pixelSize: 11
									color: Format.text
									elide: Text.ElideRight
								}

								Text {
									width: parent.width
									text: rowItem.modelData.packagings
									font.pixelSize: 10
									color: Format.muted
									elide: Text.ElideRight
								}
							}

							Item {
								anchors.right: parent.right
								anchors.rightMargin: 6
								anchors.top: parent.top
								anchors.topMargin: 5
								width: 11
								height: 11
								visible: cell.column.key === "dkpn"
									 && (rowItem.hovered || copyDkpnHover.containsMouse)

								Rectangle {
									x: 0
									y: 3
									width: 8
									height: 8
									color: "transparent"
									border.width: 1
									border.color: copyDkpnHover.containsMouse
										      ? Format.accent : Format.muted
								}

								Rectangle {
									x: 3
									y: 0
									width: 8
									height: 8
									color: rowItem.color
									border.width: 1
									border.color: copyDkpnHover.containsMouse
										      ? Format.accent : Format.muted
								}

								MouseArea {
									id: copyDkpnHover
									anchors.fill: parent
									hoverEnabled: true
									cursorShape: Qt.PointingHandCursor
									onClicked: {
										Shell.copy(rowItem.modelData.dkpn);
										view.notify("Copied " + rowItem.modelData.dkpn);
									}
								}
							}

							Column {
								anchors.fill: parent
								anchors.leftMargin: 8
								anchors.rightMargin: 6
								anchors.topMargin: 12
								spacing: 2
								visible: cell.column.key !== "mpn"
									 && cell.column.key !== "dkpn"
									 && cell.column.key !== "datasheet"
									 && cell.column.key !== "photo"

								Text {
									width: parent.width
									text: view.cellText(rowItem.modelData, cell.column)
									font.pixelSize: 12
									font.bold: rowItem.current
									color: Format.text
									elide: Text.ElideRight
								}

								Text {
									width: parent.width
									visible: cell.column.key === "stock"
									text: "in stock"
									font.pixelSize: 10
									color: Format.muted
								}

								Text {
									width: parent.width
									visible: cell.column.key === "price" || cell.column.key === "lineTotal"
									text: view.quantityNote(cell.column)
									font.pixelSize: 10
									color: Format.muted
									elide: Text.ElideRight
								}
							}
						}
					}
				}

			}

			Label {
				anchors.centerIn: parent
				visible: list.count === 0
				text: Search.busy ? "searching\u2026" : Search.status
				font.pixelSize: 13
				color: Search.busy ? Format.accent : Format.muted
			}
		}

		Rectangle {
			Layout.fillWidth: true
			implicitHeight: Search.more ? 30 : 0
			visible: Search.more
			color: "#f4f5f7"

			Button {
				anchors.centerIn: parent
				flat: true
				font.pixelSize: 11
				text: "load more of " + Format.count(Search.totalCount)
				onClicked: Search.nextPage()
			}
		}
	}
}

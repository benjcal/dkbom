import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Dk 1.0
import "Format.js" as Format
import "Phosphor.js" as Phosphor

/* The BOM of the open project, as a dock beside the results. It is a queue:
 * one line per (value, footprint, DigiKey #) group, and the number is what an
 * assignment produces. Clicking a line makes it the target the results pane
 * assigns into; today's work is the unassigned lines, so that is all it shows
 * until "show assigned" is ticked. */
Rectangle {
	id: view
	color: Format.railBg

	signal notify(string message)

	/* Which lines are unfolded, by their references: the model is rebuilt
	 * after every write, so an index would point at a different line. */
	property var unfolded: ({})

	/* What Digi-Key says about each line arrives one cached call at a time; the
	 * revision is what makes every delegate re-read it when it lands. */
	property int detailRevision: 0

	property var peekRow: null
	property int peekIndex: -1
	property bool peekActive: false

	function openProduct(url) {
		if (!url)
			return;
		Qt.openUrlExternally(url);
		view.notify("Opening digikey.com");
	}

	Timer {
		id: peekTimer
		interval: 220
		onTriggered: view.peekActive = true
	}

	Connections {
		target: Products

		function onDetailsChanged() {
			view.detailRevision++;
		}
	}

	function keyOf(refs) {
		return refs.join(" ");
	}

	function isUnfolded(refs) {
		return unfolded[keyOf(refs)] === true;
	}

	function toggleUnfolded(refs) {
		var next = {};
		for (var k in unfolded)
			next[k] = unfolded[k];
		next[keyOf(refs)] = !next[keyOf(refs)];
		unfolded = next;
	}

	/* The footprint's library prefix is noise in a pane this narrow; the
	 * footprint name is the part somebody is looking for. */
	function shortFootprint(footprint) {
		if (!footprint)
			return "no footprint";
		var parts = footprint.split(":");
		return parts[parts.length - 1];
	}

	FileDialog {
		id: projectDialog
		title: "Open a KiCad project"
		nameFilters: ["KiCad schematic (*.kicad_sch)", "All files (*)"]
		onAccepted: Project.open(selectedFile)
	}

	ColumnLayout {
		anchors.fill: parent
		spacing: 0

		Rectangle {
			Layout.fillWidth: true
			color: Format.sectionBg
			implicitHeight: head.implicitHeight + 12

			ColumnLayout {
				id: head
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.top: parent.top
				anchors.margins: 6
				spacing: 3

				RowLayout {
					Layout.fillWidth: true
					spacing: 2

					Label {
						text: "BOM"
						font.bold: true
					}

					Label {
						Layout.fillWidth: true
						Layout.leftMargin: 4
						text: Project.isOpen ? Project.fileName : "no project"
						elide: Text.ElideMiddle
						font.pixelSize: 11
						color: Format.sectionText
						opacity: 0.75
					}

					Button {
						text: "Open\u2026"
						font.pixelSize: 11
						icon.source: "qrc:/assets/icons/folder-open.svg"
						icon.color: Format.text
						display: AbstractButton.TextBesideIcon
						onClicked: projectDialog.open()
					}

					Button {
						icon.source: "qrc:/assets/icons/arrow-clockwise.svg"
						icon.color: Format.text
						enabled: Project.isOpen && !Project.busy
						onClicked: Project.reload()
						ToolTip.text: "Reload"
						ToolTip.visible: hovered
					}
				}

				RowLayout {
					Layout.fillWidth: true
					spacing: 6

					Label {
						text: Project.rows.assignedCount + "/" + Project.rows.totalCount + " assigned"
						font.pixelSize: 11
						opacity: 0.75
					}

					CheckBox {
						text: "show assigned"
						font.pixelSize: 11
						checked: Project.rows.showAssigned
						onToggled: Project.rows.showAssigned = checked
					}

					Item { Layout.fillWidth: true }

					/* What is picked but not yet in the schematics. Save is the only
					 * thing that writes, so this count is what it writes. */
					Label {
						visible: Project.pendingCount > 0
						text: Project.pendingCount + " unsaved"
						font.pixelSize: 11
						font.bold: true
						color: Format.accent
					}
				}

				RowLayout {
					Layout.fillWidth: true
					spacing: 4

					Item { Layout.fillWidth: true }

					Button {
						text: "Undo"
						font.pixelSize: 11
						icon.source: "qrc:/assets/icons/arrow-u-up-left.svg"
						icon.color: Format.text
						display: AbstractButton.TextBesideIcon
						enabled: Project.canUndo && !Project.busy
						onClicked: Project.undo()
					}

					Button {
						text: "Discard"
						font.pixelSize: 11
						icon.source: "qrc:/assets/icons/trash.svg"
						icon.color: Format.text
						display: AbstractButton.TextBesideIcon
						enabled: Project.pendingCount > 0 && !Project.busy
						onClicked: Project.discard()
					}

					Button {
						text: "Save"
						font.pixelSize: 11
						icon.source: "qrc:/assets/icons/floppy-disk.svg"
						icon.color: Format.text
						display: AbstractButton.TextBesideIcon
						highlighted: Project.pendingCount > 0
						enabled: Project.pendingCount > 0 && !Project.busy && !Project.locked
						onClicked: Project.save()
					}
				}

				/* The part of the Digi-Key list this pane was missing: what the
				 * BOM costs, at the break the quantity lands in, for however many
				 * boards are being built. Unpriced lines stay counted and are never
				 * priced at nothing. */
				RowLayout {
					Layout.fillWidth: true
					spacing: 5

					Label {
						text: "assemblies"
						font.pixelSize: 11
						color: Format.muted
					}

					SpinBox {
						from: 1
						to: 100000
						editable: true
						value: Products.assemblies
						onValueModified: Products.assemblies = value
						font.pixelSize: 11
						implicitWidth: 76
						implicitHeight: 22
					}

					Item { Layout.fillWidth: true }

					Label {
						text: Format.total(Products.total)
						font.bold: true
						font.pixelSize: 12
					}

					Label {
						text: "total"
						font.pixelSize: 10
						color: Format.muted
					}
				}

				RowLayout {
					Layout.fillWidth: true
					spacing: 5

					Label {
						text: Products.assemblies > 1
						      ? Format.total(Products.perAssembly) + " per board" : ""
						font.pixelSize: 10
						color: Format.muted
					}

					Item { Layout.fillWidth: true }

					Label {
						visible: Products.unpriced > 0
						text: Products.unpriced
						      + (Products.loading ? " pricing\u2026" : " unpriced")
						font.pixelSize: 10
						color: Format.pending
					}

					Label {
						Layout.maximumWidth: 200
						visible: Products.status.length > 0
						text: Products.status
						elide: Text.ElideRight
						font.pixelSize: 10
						color: Format.pending
					}
				}
			}
		}

		Rectangle {
			Layout.fillWidth: true
			Layout.preferredHeight: warning.visible ? warning.implicitHeight + 8 : 0
			visible: Project.locked
			color: "#f7e3c4"

			Label {
				id: warning
				anchors.left: parent.left
				anchors.right: parent.right
				anchors.verticalCenter: parent.verticalCenter
				anchors.margins: 5
				text: "KiCad has this project open: saving is refused until it is closed."
				wrapMode: Text.WordWrap
				font.pixelSize: 10
				color: Format.pending
			}
		}

		/* Nothing open yet: the way in, and the way back in. */
		ColumnLayout {
			Layout.fillWidth: true
			Layout.fillHeight: true
			Layout.margins: 12
			spacing: 6
			visible: !Project.isOpen

			Label {
				Layout.fillWidth: true
				text: "No project open"
				font.bold: true
			}

			Label {
				Layout.fillWidth: true
				text: "Open a .kicad_sch and every sheet under it becomes part of "
				      + "the BOM."
				wrapMode: Text.WordWrap
				font.pixelSize: 11
				color: Format.muted
			}

			Button {
				Layout.fillWidth: true
				text: "Open project\u2026"
				icon.source: "qrc:/assets/icons/folder-open.svg"
				icon.color: Format.text
				display: AbstractButton.TextBesideIcon
				onClicked: projectDialog.open()
			}

			Label {
				Layout.fillWidth: true
				Layout.topMargin: 6
				text: "Recent"
				font.pixelSize: 11
				color: Format.muted
				visible: Project.recent.length > 0
			}

			Repeater {
				model: Project.recent

				delegate: Button {
					required property string modelData
					Layout.fillWidth: true
					flat: true
					font.pixelSize: 11
					text: modelData.split("/").pop()
					onClicked: Project.openPath(modelData)
				}
			}

			Item { Layout.fillHeight: true }
		}

		Label {
			Layout.fillWidth: true
			Layout.fillHeight: true
			Layout.margins: 16
			visible: Project.isOpen && Project.rows.count === 0
			horizontalAlignment: Text.AlignHCenter
			verticalAlignment: Text.AlignTop
			wrapMode: Text.WordWrap
			text: Project.busy ? "Reading the project\u2026"
					   : "Nothing left to assign."
			color: Format.muted
			font.pixelSize: 11
		}

		ListView {
			id: list
			Layout.fillWidth: true
			Layout.fillHeight: true
			clip: true
			spacing: 0
			model: Project.rows
			currentIndex: Project.hasTarget ? Project.target.index : -1
			ScrollBar.vertical: ScrollBar {}
			visible: Project.isOpen && Project.rows.count > 0

			delegate: Rectangle {
				id: rowItem

				required property int index
				required property string refText
				required property var refs
				required property var sheets
				required property string value
				required property string footprint
				required property string dkpn
				required property string problem
				required property int qty
				required property bool assigned
				required property bool staged
				required property bool dnp

				readonly property bool current: index === list.currentIndex
				readonly property bool hovered: rowHover.containsMouse
				readonly property bool unfolded: view.isUnfolded(refs)
				readonly property var details: {
					view.detailRevision;
					return Products.details(rowItem.dkpn);
				}

				width: list.width
				height: layout.implicitHeight
				color: current ? Format.rowSelected
					       : (hovered ? Format.rowHover
							  : (index % 2 ? Format.rowB : Format.rowA))

				/* Clicking here picks the target but leaves the keyboard in
				 * the results table, where Enter does the assigning. */
				MouseArea {
					id: rowHover
					anchors.fill: parent
					hoverEnabled: true
					onClicked: Project.selectRow(rowItem.index)
				}

				ColumnLayout {
					id: layout
					anchors.left: parent.left
					anchors.right: parent.right
					anchors.top: parent.top
					spacing: 0

					RowLayout {
						Layout.fillWidth: true
						Layout.margins: 6
						spacing: 6

						/* The visual check: this is what the number actually buys. */
						Item {
							Layout.preferredWidth: 44
							Layout.preferredHeight: 44
							Layout.alignment: Qt.AlignTop
							visible: rowItem.assigned || rowItem.staged

							Image {
								anchors.fill: parent
								anchors.margins: 2
								source: rowItem.details.photoUrl || ""
								asynchronous: true
								fillMode: Image.PreserveAspectFit
								sourceSize.width: 88
								sourceSize.height: 88
							}

							MouseArea {
								id: thumbHover
								anchors.fill: parent
								hoverEnabled: true
								cursorShape: Qt.PointingHandCursor
								onClicked: view.openProduct(rowItem.details.productUrl)
								onContainsMouseChanged: {
									if (containsMouse && rowItem.details.photoUrl) {
										view.peekIndex = rowItem.index;
										view.peekRow = rowItem.details;
										view.peekActive = false;
										peekTimer.restart();
									} else if (view.peekIndex === rowItem.index) {
										peekTimer.stop();
										view.peekIndex = -1;
										view.peekRow = null;
										view.peekActive = false;
									}
								}
							}
						}

						/* Filled, ringed, or hollow: assigned, skipped, or still
						 * to do. */
						Rectangle {
							Layout.alignment: Qt.AlignTop
							Layout.topMargin: 6
							width: 8
							height: 8
							radius: 4
							color: (rowItem.assigned || rowItem.staged) ? Format.accent
							     : (rowItem.dnp ? Format.muted : Format.pending)
						}

						ColumnLayout {
							Layout.fillWidth: true
							spacing: 0

							Label {
								Layout.fillWidth: true
								text: rowItem.value
								elide: Text.ElideRight
								font.bold: true
								font.pixelSize: 12
								opacity: rowItem.dnp ? 0.5 : 1
							}

							Label {
								Layout.fillWidth: true
								visible: text.length > 0
								text: rowItem.details.description || ""
								elide: Text.ElideRight
								font.pixelSize: 10
								color: Format.muted
							}

							Label {
								Layout.fillWidth: true
								text: view.shortFootprint(rowItem.footprint)
								      + (rowItem.dnp ? " · DNP" : "")
								elide: Text.ElideRight
								font.pixelSize: 10
								color: rowItem.footprint ? Format.muted : Format.pending
							}

							/* Staged here, still waiting for the schematics: the one line of
							 * the pane that says Save has something to do. */
							RowLayout {
								Layout.fillWidth: true
								visible: rowItem.staged
								spacing: 2

								PIcon {
									glyph: Phosphor.arrowRight
									size: 10
									tint: Format.accent
								}

								Label {
									Layout.fillWidth: true
									text: rowItem.dkpn + " unsaved"
									elide: Text.ElideRight
									font.pixelSize: 10
									color: Format.accent
								}
							}
						}

						ColumnLayout {
							Layout.alignment: Qt.AlignTop
							spacing: 0

							Label {
								text: "\u00d7" + rowItem.qty
								font.pixelSize: 11
								color: Format.muted
							}

							Label {
								visible: rowItem.details.hasPrice === true
								text: {
									Products.assemblies;
									return Format.total(Products.lineTotal(
										rowItem.dkpn, rowItem.qty));
								}
								font.pixelSize: 10
								color: Format.text
							}
						}

						ToolButton {
							icon.source: rowItem.unfolded
								     ? "qrc:/assets/icons/caret-down.svg"
								     : "qrc:/assets/icons/caret-right.svg"
							icon.color: Format.muted
							icon.width: 10
							icon.height: 10
							implicitWidth: 18
							implicitHeight: 18
							onClicked: view.toggleUnfolded(rowItem.refs)
						}
					}

					/* The at-a-glance check: what this line is, where it is,
					 * and the number that was written into it. */
					ColumnLayout {
						id: detail
						visible: rowItem.unfolded
						Layout.fillWidth: true
						Layout.leftMargin: 20
						Layout.rightMargin: 8
						Layout.bottomMargin: 6
						spacing: 1

						Label {
							Layout.fillWidth: true
							text: rowItem.refText
							wrapMode: Text.Wrap
							font.pixelSize: 10
							color: Format.muted
						}

						Label {
							Layout.fillWidth: true
							visible: rowItem.sheets.length > 0
							text: rowItem.sheets.join(", ")
							wrapMode: Text.Wrap
							font.pixelSize: 10
							color: Format.muted
						}

						Label {
							Layout.fillWidth: true
							visible: rowItem.footprint.length > 0
							text: rowItem.footprint
							wrapMode: Text.Wrap
							font.pixelSize: 10
							color: Format.muted
						}

						Label {
							Layout.fillWidth: true
							visible: rowItem.problem.length > 0
							text: rowItem.problem
							wrapMode: Text.Wrap
							font.pixelSize: 10
							color: Format.pending
						}

						RowLayout {
							Layout.fillWidth: true
							Layout.topMargin: 3
							spacing: 4

							Label {
								Layout.fillWidth: true
								text: rowItem.dkpn.length > 0
								      ? rowItem.dkpn + (rowItem.staged ? " (unsaved)" : "")
								      : "no Digi-Key # yet"
								elide: Text.ElideRight
								font.pixelSize: 10
								color: rowItem.dkpn.length > 0 ? Format.text : Format.muted

								MouseArea {
									anchors.fill: parent
									enabled: !!rowItem.details.productUrl
									cursorShape: enabled ? Qt.PointingHandCursor
											: Qt.ArrowCursor
									onClicked: view.openProduct(rowItem.details.productUrl)
								}
							}

							Button {
								text: "open"
								font.pixelSize: 10
								enabled: !!rowItem.details.productUrl
								onClicked: view.openProduct(rowItem.details.productUrl)
							}

							Button {
								text: "copy"
								font.pixelSize: 10
								enabled: rowItem.dkpn.length > 0
								onClicked: {
									Shell.copy(rowItem.dkpn);
									view.notify("Copied " + rowItem.dkpn);
								}
							}

							Button {
								text: "target"
								font.pixelSize: 10
								onClicked: Project.selectRow(rowItem.index)
							}
						}
					}
				}
			}
		}

		Rectangle {
			Layout.fillWidth: true
			Layout.preferredHeight: 22
			color: Format.sectionBg

			Label {
				anchors.fill: parent
				anchors.leftMargin: 6
				anchors.rightMargin: 6
				verticalAlignment: Text.AlignVCenter
				text: Project.status
				elide: Text.ElideRight
				font.pixelSize: 10
				color: Format.muted
			}
		}
	}

	PhotoPeek {
		row: view.peekRow
		active: view.peekActive
		anchors.centerIn: parent
		height: Math.min(320, view.height - 40)
		width: height
	}
}

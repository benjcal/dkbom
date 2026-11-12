import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Dk 1.0
import "Format.js" as Format

ApplicationWindow {
	id: root
	width: 1440
	height: 900
	visible: true
	title: "dkbom"

	color: Format.appBg

	readonly property color accent: Format.accent

	function searchAllProducts() {
		filterPanel.reset();
		Search.searchKeywords();
	}

	/* One product call per DigiKey number in the BOM, from cache where it was
	 * seen before: the photo, the description and the price all come from it. */
	function syncProducts() {
		Products.setLines(Project.rows.allRows());
	}

	Component.onCompleted: syncProducts()

	Connections {
		target: Project.rows

		function onChanged() {
			root.syncProducts();
		}
	}

	property string toastText: ""
	property bool toastVisible: false
	property bool toastWarn: false

	function showToast(message, warn) {
		toastText = message;
		toastWarn = warn === true;
		toastVisible = true;
		toastTimer.restart();
	}

	Timer {
		id: toastTimer
		interval: 2600
		onTriggered: root.toastVisible = false
	}

	/* Whatever happened to the schematics says so out loud, in both
	 * directions: a write counts only when KiCad can read it back. */
	Connections {
		target: Project

		function onNotice(text, ok) {
			root.showToast(text, !ok);
		}
	}

	header: ToolBar {
		RowLayout {
			anchors.fill: parent
			spacing: 8

			Label {
				text: "dkbom"
				font.bold: true
			}

			ToolSeparator {}

			TextField {
				Layout.preferredWidth: 210
				placeholderText: "all products \u2014 optional"
				text: Search.keywords
				selectByMouse: true
				onTextEdited: Search.keywords = text
				onAccepted: root.searchAllProducts()
			}

			/* Keywords across the whole catalogue: it drops the category and its
			 * ticks, because applying those is the filter panel's Apply button. */
			Button {
				Layout.preferredWidth: 90
				text: "Search"
				icon.source: "qrc:/assets/icons/magnifying-glass.svg"
				icon.color: Format.text
				display: AbstractButton.TextBesideIcon
				highlighted: Search.busy
				enabled: !Search.busy
				onClicked: root.searchAllProducts()
			}

			Button {
				text: "Reset"
				icon.source: "qrc:/assets/icons/arrow-counter-clockwise.svg"
				icon.color: Format.text
				display: AbstractButton.TextBesideIcon
				enabled: !Search.busy
				onClicked: {
					filterPanel.reset();
					Search.reset();
				}
			}

			/* Browsing is the point, so the knobs that only adjust a search live
			 * behind one button, which keeps the bar short. */
			ToolButton {
				id: optionsButton
				text: "Options"
				icon.source: "qrc:/assets/icons/sliders.svg"
				icon.color: Format.text
				display: AbstractButton.TextBesideIcon
				onClicked: optionsPopup.open()

				Popup {
					id: optionsPopup
					parent: optionsButton
					y: optionsButton.height
					width: 250
					padding: 10
					closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

					background: Rectangle {
						color: Format.surface
						border.width: 1
						border.color: Format.separator
					}

					ColumnLayout {
						spacing: 6

						RowLayout {
							Label {
								text: "quantity"
								Layout.preferredWidth: 92
								opacity: 0.7
							}
							SpinBox {
								from: 1
								to: 100000
								editable: true
								value: Search.quantity
								onValueModified: Search.quantity = value
							}
						}

						RowLayout {
							Label {
								text: "min stock"
								Layout.preferredWidth: 92
								opacity: 0.7
							}
							SpinBox {
								from: 0
								to: 1000000
								stepSize: 100
								editable: true
								value: Search.minStock
								onValueModified: Search.minStock = value
							}
						}

						RowLayout {
							Label {
								text: "max MOQ"
								Layout.preferredWidth: 92
								opacity: 0.7
							}
							SpinBox {
								from: 0
								to: 1000000
								editable: true
								value: Search.maxMoq
								onValueModified: Search.maxMoq = value
							}
						}

						CheckBox {
							text: "include marketplace"
							checked: Search.marketplace
							onToggled: Search.marketplace = checked
						}
					}
				}
			}

			ToolButton {
				id: columnsButton
				text: "Columns"
				icon.source: "qrc:/assets/icons/table.svg"
				icon.color: Format.text
				display: AbstractButton.TextBesideIcon
				onClicked: columnsPopup.open()

				/* Same shape as the options popover: anchored under its button, and
				 * any click outside puts it away. */
				Popup {
					id: columnsPopup
					parent: columnsButton
					y: columnsButton.height
					width: 220
					padding: 8
					closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

					background: Rectangle {
						color: Format.surface
						border.width: 1
						border.color: Format.separator
					}

					ColumnLayout {
						spacing: 2

						Repeater {
							model: results.definitions

							delegate: CheckBox {
								required property var modelData
								Layout.fillWidth: true
								text: modelData.title
								checked: !results.isHidden(modelData.key)
								onToggled: results.toggleColumn(modelData.key)
							}
						}

						Rectangle {
							Layout.fillWidth: true
							Layout.topMargin: 4
							Layout.bottomMargin: 4
							height: 1
							color: Format.separator
						}

						Button {
							Layout.fillWidth: true
							flat: true
							font.pixelSize: 11
							text: "reset widths and columns"
							onClicked: {
								results.resetColumns();
								columnsPopup.close();
							}
						}
					}
				}
			}

			Item { Layout.fillWidth: true }

			ToolButton {
				text: "Settings"
				icon.source: "qrc:/assets/icons/gear.svg"
				icon.color: Format.text
				display: AbstractButton.TextBesideIcon
				onClicked: settingsDialog.open()
			}

			BusyIndicator {
				running: Search.busy
				visible: running
				implicitWidth: 22
				implicitHeight: 22
			}
		}
	}

	footer: ToolBar {
		RowLayout {
			anchors.fill: parent
			spacing: 12

			/* One fact worth a permanent line: how many products matched. The
			 * filters themselves are already written across the top of the table. */
			Label {
				text: Format.count(Search.totalCount)
				      + (Search.totalCount === 1 ? " match" : " matches")
				font.bold: true
			}

			Item { Layout.fillWidth: true }

			/* What the quota has left, as Digi-Key's own headers report it, and
			 * how many answers this session got from disk instead. */
			Label {
				text: Api.quotaText
				      + (Api.cachedHits > 0 ? " \u00b7 " + Api.cachedHits + " cached" : "")
				font.pixelSize: 11
				opacity: 0.7
				color: Api.serverRemaining >= 0 && Api.serverRemaining < 50
				       ? Format.pending : Format.muted
			}

			Label {
				Layout.maximumWidth: 620
				text: Search.status
				elide: Text.ElideRight
				opacity: 0.7
			}
		}
	}

	/* Digi-Key credentials belong to whoever runs the tool: nothing secret is
	 * built into the binary, so a fresh install starts here. QSettings keeps them
	 * in plain text, which the dialog says out loud. */
	Dialog {
		id: settingsDialog
		width: 520
		modal: true
		anchors.centerIn: parent
		title: "Digi-Key API access"
		standardButtons: Dialog.Ok | Dialog.Cancel

		onOpened: {
			clientIdField.text = Prefs.clientId;
			clientSecretField.text = Prefs.clientSecret;
		}

		onAccepted: {
			var id = clientIdField.text.trim();
			var secret = clientSecretField.text.trim();

			Prefs.setCredentials(id, secret);
			Api.setCredentials(id, secret);

			if (!Catalog.loaded && Api.hasCredentials)
				Catalog.load();
		}

		ColumnLayout {
			anchors.fill: parent
			spacing: 8

			Label {
				Layout.fillWidth: true
				wrapMode: Text.WordWrap
				text: "Create an app at developer.digikey.com and paste its client ID "
				      + "and secret here. They are kept in ~/.config/dkbom/config.conf, "
				      + "in plain text, on this machine."
				font.pixelSize: 11
				color: Format.muted
			}

			TextField {
				id: clientIdField
				Layout.fillWidth: true
				placeholderText: "Client ID"
				selectByMouse: true
			}

			TextField {
				id: clientSecretField
				Layout.fillWidth: true
				placeholderText: "Client secret"
				echoMode: TextInput.Password
				selectByMouse: true
			}
		}
	}

	SplitView {
		anchors.fill: parent

		handle: Rectangle {
			implicitWidth: 4
			color: handleHover.hovered ? Format.accent : Format.separator

			HoverHandler {
				id: handleHover
			}
		}

		FilterPanel {
			id: filterPanel
			SplitView.minimumWidth: 220
			SplitView.maximumWidth: 640
			SplitView.preferredWidth: Prefs.filterPanelWidth
			SplitView.fillHeight: true

			onWidthChanged: panelSaveTimer.restart()
		}

		ResultsView {
			id: results
			SplitView.fillWidth: true
			onNotify: (message) => root.showToast(message)
		}

		/* The BOM is a queue on the other side of the results: the filter panel
		 * narrows what is browsable, this is the list being filled. */
		BomView {
			id: bom
			SplitView.minimumWidth: 240
			SplitView.maximumWidth: 900
			SplitView.preferredWidth: Prefs.bomWidth
			SplitView.fillHeight: true

			onNotify: (message) => root.showToast(message)
			onWidthChanged: bomSaveTimer.restart()
		}
	}

	Timer {
		id: bomSaveTimer
		interval: 400
		onTriggered: Prefs.setBomWidth(Math.round(bom.width))
	}

	Timer {
		id: panelSaveTimer
		interval: 400
		onTriggered: Prefs.setFilterPanelWidth(Math.round(filterPanel.width))
	}

	/* Copying and opening are the two actions that leave no trace in the window,
	 * so they say so out loud and then get out of the way. */
	Rectangle {
		id: toast
		anchors.horizontalCenter: parent.horizontalCenter
		anchors.bottom: parent.bottom
		anchors.bottomMargin: 52
		width: toastLabel.implicitWidth + 24
		height: 28
		radius: 6
		color: root.toastWarn ? "#7a3b1e" : "#2b3947"
		z: 100
		visible: opacity > 0
		opacity: root.toastVisible ? 0.95 : 0

		Behavior on opacity {
			NumberAnimation { duration: 160 }
		}

		Label {
			id: toastLabel
			anchors.centerIn: parent
			text: root.toastText
			color: "white"
			font.pixelSize: 11
		}
	}
}

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Dk 1.0
import "Format.js" as Format
import "Phosphor.js" as Phosphor

Rectangle {
	id: rail
	color: Format.railBg

	/* Per-block state: which panels are unrolled, and what has been typed into
	 * each panel's own narrow field. Keyed by parameter id, because the rail is
	 * rebuilt from every response and the ids are the only stable handle. */
	property var expanded: ({})
	property var expandedCategories: ({})
	property var queries: ({})
	property int revision: 0
	property string categoryQuery: ""
	property var hits: []
	property real suggestionWidth: 0

	/* Both suggestions are 12px, so one measurer at that size is enough to know
	 * how wide the list has to be before a value gets cut off. */
	TextMetrics {
		id: suggestionMeasurer
		font.pixelSize: 12
	}

	/* Reset is global: the boxes in here are part of the search, so they go
	 * back to empty with everything else. */
	function reset() {
		categoryQuery = "";
		queries = ({});
		expanded = ({});
		expandedCategories = ({});
		hits = [];
		completionField.text = "";
		completionPopup.close();
		revision++;
	}

	function toggleExpanded(parameterId) {
		expanded[parameterId] = !expanded[parameterId];
		revision++;
	}

	function isExpanded(parameterId) {
		revision;
		return expanded[parameterId] === true;
	}

	function queryFor(parameterId) {
		revision;
		return String(queries[parameterId] || "").trim();
	}

	/* Values are printed the way the catalogue prints them — "10 kOhms",
	 * "0.1 \u00b5F" — so the match ignores case, spacing, punctuation and the
	 * micro sign. "10k" has to find "10 kOhms" or nobody will use the box. */
	function fold(textValue) {
		return String(textValue).toLowerCase()
			.replace(/\u00b5/g, "u")
			.replace(/[^a-z0-9.]/g, "");
	}

	function setQuery(parameterId, text) {
		queries[parameterId] = text;
		revision++;
	}

	/* The whole point of the box: one token at a time, matched against every
	 * value the last response offered, whichever filter it belongs to. */
	function refreshCompletion() {
		hits = completionField.text.trim() === "" ? [] : Search.complete(completionField.text);

		var widest = 0;
		for (var i = 0; i < hits.length; ++i) {
			suggestionMeasurer.text = hits[i].filter + ": " + hits[i].text;
			widest = Math.max(widest, suggestionMeasurer.width);
		}
		suggestionWidth = widest + 60;

		if (hits.length === 0)
			completionPopup.close();
		else
			completionPopup.open();
	}

	/* A pick is a tick, no more: it stays pending until Search is pressed, and
	 * the box empties so the next token can be typed straight away. */
	function chooseSuggestion(hit) {
		Search.toggleFilter(hit.parameterId, hit.valueId);
		completionField.text = "";
		refreshCompletion();
		completionField.forceActiveFocus();
	}

	/* No tree widget: the flattened list carries a depth, and typing narrows
	 * it to matches anywhere in the catalogue. Without a needle it is a tree:
	 * a branch shows only when every ancestor above it has been opened. */
	function visibleCategories() {
		revision;

		var needle = categoryQuery.trim().toLowerCase();
		var list = Catalog.categories;
		var out = [];

		var byId = ({});
		for (var i = 0; i < list.length; ++i)
			byId[list[i].id] = list[i];

		for (var j = 0; j < list.length; ++j) {
			var entry = list[j];

			if (needle !== "") {
				if (entry.name.toLowerCase().indexOf(needle) >= 0)
					out.push(entry);
				continue;
			}

			var open = true;
			var up = byId[entry.parent];
			while (up) {
				if (expandedCategories[up.id] !== true) {
					open = false;
					break;
				}
				up = byId[up.parent];
			}

			if (open)
				out.push(entry);
		}

		return out;
	}

	function hasChildren(id) {
		revision;
		var list = Catalog.categories;
		for (var i = 0; i < list.length; ++i) {
			if (list[i].parent === id)
				return true;
		}
		return false;
	}

	function isCategoryOpen(id) {
		revision;
		return expandedCategories[id] === true;
	}

	function toggleCategory(id) {
		if (expandedCategories[id] === true)
			delete expandedCategories[id];
		else
			expandedCategories[id] = true;
		revision++;
	}

	function valuesFor(filter) {
		var needle = fold(queryFor(filter.parameterId));
		if (needle === "")
			return filter.values.slice(0,
				isExpanded(filter.parameterId) ? filter.values.length : 8);

		var out = [];
		for (var i = 0; i < filter.values.length; ++i) {
			var value = filter.values[i];
			if (fold(value.text).indexOf(needle) >= 0)
				out.push(value);
		}
		return out;
	}

	ColumnLayout {
		anchors.fill: parent
		spacing: 0

		Rectangle {
			Layout.fillWidth: true
			implicitHeight: 30
			color: Format.sectionBg

			Text {
				anchors.left: parent.left
				anchors.leftMargin: 10
				anchors.verticalCenter: parent.verticalCenter
				text: "CATEGORY"
				font.pixelSize: 10
				font.bold: true
				font.letterSpacing: 1
				color: Format.sectionText
			}
		}

		TextField {
			Layout.fillWidth: true
			Layout.margins: 6
			placeholderText: "filter categories"
			font.pixelSize: 12
			selectByMouse: true
			text: rail.categoryQuery
			onTextEdited: rail.categoryQuery = text
		}

		Item {
			Layout.fillWidth: true
			Layout.preferredHeight: 240

			ListView {
				id: categoryList
				anchors.fill: parent
				clip: true
				model: rail.visibleCategories()
				ScrollBar.vertical: ScrollBar {}

				delegate: Item {
					id: categoryRow
					required property var modelData
					width: categoryList.width
					height: 21

					readonly property bool current: Search.categoryId === modelData.id
					readonly property bool hovered: categoryHover.containsMouse
					readonly property bool kids: rail.hasChildren(modelData.id)
					readonly property bool unfolded: rail.isCategoryOpen(modelData.id)

					Rectangle {
						anchors.fill: parent
						color: categoryRow.current ? Format.accent
									   : (categoryRow.hovered ? Format.rowHover
												  : "transparent")
					}

					Text {
						anchors.left: parent.left
						anchors.leftMargin: 20 + categoryRow.modelData.depth * 12
						anchors.right: countLabel.left
						anchors.verticalCenter: parent.verticalCenter
						text: categoryRow.modelData.name
						font.pixelSize: 12
						font.bold: categoryRow.current
						color: categoryRow.current ? "white" : Format.text
						elide: Text.ElideRight
					}

					Text {
						id: countLabel
						anchors.right: parent.right
						anchors.rightMargin: 8
						anchors.verticalCenter: parent.verticalCenter
						text: Format.count(categoryRow.modelData.count)
						font.pixelSize: 10
						color: categoryRow.current ? "#d5e6f7" : Format.muted
					}

					MouseArea {
						id: categoryHover
						anchors.fill: parent
						hoverEnabled: true
						onClicked: Search.selectCategory(categoryRow.modelData.id)
					}

					/* Declared last so it sits above the row's own click target: the
					 * caret folds the branch, the rest of the row picks the category. */
					PIcon {
						anchors.left: parent.left
						anchors.leftMargin: 8 + categoryRow.modelData.depth * 12
						anchors.verticalCenter: parent.verticalCenter
						width: 12
						visible: categoryRow.kids && rail.categoryQuery.trim() === ""
						glyph: categoryRow.unfolded
						       ? Phosphor.caretDown : Phosphor.caretRight
						size: 9
						tint: categoryRow.current ? "#d5e6f7" : Format.muted

						MouseArea {
							anchors.fill: parent
							cursorShape: Qt.PointingHandCursor
							onClicked: rail.toggleCategory(categoryRow.modelData.id)
						}
					}
				}
			}

			/* The tree is the slowest thing to fetch, so it says when it is
			 * fetching and it says why it is empty. */
			Column {
				anchors.centerIn: parent
				width: parent.width - 24
				spacing: 4
				visible: Catalog.loading || Catalog.status !== ""
					 || categoryList.count === 0

				BusyIndicator {
					anchors.horizontalCenter: parent.horizontalCenter
					running: Catalog.loading
					visible: Catalog.loading
					implicitWidth: 26
					implicitHeight: 26
				}

				Label {
					width: parent.width
					wrapMode: Text.WordWrap
					horizontalAlignment: Text.AlignHCenter
					font.pixelSize: 11
					color: Catalog.status !== "" ? Format.pending : Format.muted
					text: Catalog.loading ? "loading categories\u2026"
					      : (Catalog.status !== ""
						 ? "categories: " + Catalog.status
						 : (Api.hasCredentials
						    ? "no categories returned"
						    : "paste your Digi-Key credentials in Settings"))
				}
			}
		}

		TextField {
			id: completionField
			Layout.fillWidth: true
			Layout.margins: 6
			placeholderText: Search.filters.length === 0
				? "pick a category \u2014 filter values are found here"
				: "find any filter value"
			enabled: Search.filters.length > 0
			font.pixelSize: 12
			selectByMouse: true
			onTextEdited: rail.refreshCompletion()
			onAccepted: if (rail.hits.length > 0) rail.chooseSuggestion(rail.hits[0])
		}

		Popup {
			id: completionPopup
			parent: completionField
			x: 0
			y: completionField.height + 2
			width: Math.max(completionField.width,
					Math.min(rail.suggestionWidth, rail.width - 16))
			padding: 0
			closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

			background: Rectangle {
				color: Format.surface
				border.width: 1
				border.color: Format.separator
			}

			contentItem: ListView {
				implicitHeight: Math.min(contentHeight, 264)
				clip: true
				model: rail.hits
				ScrollBar.vertical: ScrollBar {}

				delegate: Item {
					id: suggestionRow
					required property var modelData
					width: ListView.view.width
					height: 24

					readonly property bool hovered: suggestionHover.containsMouse

					Rectangle {
						anchors.fill: parent
						color: suggestionRow.modelData.checked === true
						       ? Format.tickBg
						       : (suggestionRow.hovered ? Format.rowHover
										: "transparent")
					}

					Text {
						id: suggestionFilter
						anchors.left: parent.left
						anchors.leftMargin: 8
						anchors.verticalCenter: parent.verticalCenter
						width: Math.min(implicitWidth, parent.width * 0.5)
						text: suggestionRow.modelData.filter + ": "
						font.pixelSize: 12
						color: Format.muted
						elide: Text.ElideRight
					}

					Text {
						anchors.left: suggestionFilter.right
						anchors.right: suggestionCount.left
						anchors.rightMargin: 6
						anchors.verticalCenter: parent.verticalCenter
						text: suggestionRow.modelData.text
						font.pixelSize: 12
						font.bold: suggestionRow.modelData.checked === true
						color: Format.text
						elide: Text.ElideRight
					}

					Text {
						id: suggestionCount
						anchors.right: parent.right
						anchors.rightMargin: 6
						anchors.verticalCenter: parent.verticalCenter
						text: Format.count(suggestionRow.modelData.count)
						font.pixelSize: 10
						color: Format.muted
					}

					MouseArea {
						id: suggestionHover
						anchors.fill: parent
						hoverEnabled: true
						onClicked: rail.chooseSuggestion(suggestionRow.modelData)
					}
				}
			}
		}

		Rectangle {
			Layout.fillWidth: true
			implicitHeight: 30
			color: Format.sectionBg

			Text {
				anchors.left: parent.left
				anchors.leftMargin: 10
				anchors.verticalCenter: parent.verticalCenter
				text: "FILTERS"
				font.pixelSize: 10
				font.bold: true
				font.letterSpacing: 1
				color: Format.sectionText
			}

			Text {
				anchors.left: parent.left
				anchors.leftMargin: 66
				anchors.right: applyFiltersButton.left
				anchors.rightMargin: 6
				anchors.verticalCenter: parent.verticalCenter
				text: Search.filterCount > 0
				      ? Search.filterCount + " ticked"
					      + (Search.pending ? " \u00b7 not applied" : "")
				      : "nothing ticked"
				font.pixelSize: 10
				font.bold: Search.pending
				color: Search.pending ? Format.pending : Format.muted
				elide: Text.ElideRight
			}

			/* The rail only ticks; this is the step that sends the request, so it
			 * sits with the filters. */
			Button {
				id: applyFiltersButton
				anchors.right: clearFiltersButton.left
				anchors.rightMargin: 2
				anchors.verticalCenter: parent.verticalCenter
				text: Search.filterCount > 0 ? "Apply " + Search.filterCount : "Apply"
				icon.source: "qrc:/assets/icons/funnel.svg"
				icon.color: Format.text
				display: AbstractButton.TextBesideIcon
				font.pixelSize: 10
				font.bold: true
				visible: Search.pending
				enabled: !Search.busy
				highlighted: true
				padding: 4
				onClicked: Search.search()
			}

			Button {
				id: clearFiltersButton
				anchors.right: parent.right
				anchors.rightMargin: 4
				anchors.verticalCenter: parent.verticalCenter
				text: "clear"
				icon.source: "qrc:/assets/icons/funnel-x.svg"
				icon.color: Format.text
				display: AbstractButton.TextBesideIcon
				font.pixelSize: 10
				visible: Search.filterCount > 0
				flat: true
				padding: 4
				onClicked: Search.clearFilters()
			}
		}

		ListView {
			id: filterList
			Layout.fillWidth: true
			Layout.fillHeight: true
			clip: true
			model: Search.filters
			ScrollBar.vertical: ScrollBar {}

			delegate: Item {
				id: parameterBlock
				required property var modelData

				readonly property var shown: rail.valuesFor(modelData)
				readonly property string query: rail.queryFor(modelData.parameterId)
				readonly property bool narrowable: modelData.values.length > 8
				readonly property bool hiddenBlock: shown.length === 0
								   && query === ""
			 readonly property int ticked: {
					var n = 0;
					for (var i = 0; i < modelData.values.length; ++i) {
						if (modelData.values[i].checked)
							n++;
					}
					return n;
				}

				width: filterList.width
				height: hiddenBlock ? 0 : body.height
				visible: !hiddenBlock

				Column {
					id: body
					width: parent.width

					Rectangle {
						width: parent.width
						height: 30
						color: Format.blockBg

						Rectangle {
							anchors.bottom: parent.bottom
							width: parent.width
							height: 1
							color: Format.separator
						}

						Text {
							anchors.left: parent.left
							anchors.leftMargin: 10
							anchors.right: narrowField.left
							anchors.verticalCenter: parent.verticalCenter
							text: parameterBlock.modelData.name.toUpperCase()
							      + (parameterBlock.ticked > 0
								 ? "  (" + parameterBlock.ticked + ")" : "")
							font.pixelSize: 10
							font.bold: true
							font.letterSpacing: 0.6
							color: parameterBlock.ticked > 0 ? Format.accent
											 : Format.sectionText
							elide: Text.ElideRight
						}

						TextField {
							id: narrowField
							anchors.right: parent.right
							anchors.rightMargin: 6
							anchors.verticalCenter: parent.verticalCenter
							width: 96
							height: 22
							visible: parameterBlock.narrowable
							placeholderText: "find"
							font.pixelSize: 11
							leftPadding: 6
							rightPadding: 6
							selectByMouse: true
							text: rail.queryFor(parameterBlock.modelData.parameterId)
							      ? rail.queries[parameterBlock.modelData.parameterId] : ""
							onTextEdited: rail.setQuery(parameterBlock.modelData.parameterId,
										    text)
						}
					}

					Repeater {
						model: parameterBlock.shown

						delegate: Item {
							id: valueRow
							width: parameterBlock.width
							height: 22

							readonly property bool checked: modelData.checked === true
							readonly property bool hovered: valueHover.containsMouse

							Rectangle {
								anchors.fill: parent
								color: valueRow.checked ? Format.tickBg
											: (valueRow.hovered ? Format.rowHover
													    : "transparent")
							}

							Rectangle {
								width: 3
								height: parent.height
								color: Format.accent
								visible: valueRow.checked
							}

							Text {
								anchors.left: parent.left
								anchors.leftMargin: 12
								anchors.right: valueCount.left
								anchors.verticalCenter: parent.verticalCenter
								text: modelData.text
								font.pixelSize: 12
								font.bold: valueRow.checked
								color: Format.text
								elide: Text.ElideRight
							}

							Text {
								id: valueCount
								anchors.right: parent.right
								anchors.rightMargin: 10
								anchors.verticalCenter: parent.verticalCenter
								text: Format.count(modelData.count)
								font.pixelSize: 10
								color: Format.muted
							}

							MouseArea {
								id: valueHover
								anchors.fill: parent
								hoverEnabled: true
								onClicked: Search.toggleFilter(parameterBlock.modelData.parameterId,
											  modelData.id)
							}
						}
					}

					Item {
						width: parent.width
						height: parameterBlock.shown.length === 0 ? 22 : 0

						Text {
							anchors.left: parent.left
							anchors.leftMargin: 12
							anchors.verticalCenter: parent.verticalCenter
							text: "no value matches"
							font.pixelSize: 11
							font.italic: true
							color: Format.muted
						}
					}

					Item {
						width: parent.width
						height: parameterBlock.narrowable && parameterBlock.query === ""
							? 20 : 0

						Text {
							anchors.left: parent.left
							anchors.leftMargin: 12
							anchors.verticalCenter: parent.verticalCenter
							text: rail.isExpanded(parameterBlock.modelData.parameterId)
							      ? "less"
							      : "+" + (parameterBlock.modelData.values.length - 8) + " more"
							font.pixelSize: 11
							color: Format.accent
						}

						MouseArea {
							anchors.fill: parent
							onClicked: rail.toggleExpanded(parameterBlock.modelData.parameterId)
						}
					}
				}
			}

			Label {
				anchors.centerIn: parent
				visible: filterList.count === 0
				width: parent.width - 24
				wrapMode: Text.WordWrap
				horizontalAlignment: Text.AlignHCenter
				text: "No filters yet. Pick a category or search, and the filters "
				      + "the catalogue offers for it show up here."
				font.pixelSize: 11
				color: Format.muted
			}
		}
	}
}

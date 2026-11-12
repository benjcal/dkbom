#include "bomcsv.h"
#include "bommodel.h"
#include "prefs.h"
#include "project.h"
#include "schematic.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

namespace {

/* A sheet with one library part and one placed part, in the shape KiCad 10
 * writes: the library part is nested deeper, which is what has to be skipped. */
const QByteArray kSheet = R"(kicad_sch
	(version 20260306)
	(generator "eeschema")
	(lib_symbols
		(symbol "Device:R_Small"
			(property "Reference" "R"
				(at 0 0 0)
			)
			(property "Value" "R_Small"
				(at 0 0 0)
			)
		)
	)
	(symbol
		(lib_id "Device:R_Small")
		(at 82.55 77.47 90)
		(in_bom yes)
		(dnp no)
		(uuid "0493f97b-d466-45c5-a859-23e5326c41f5")
		(property "Reference" "R27"
			(at 85.09 76.1999 0)
		)
		(property "Value" "12k 1%"
			(at 85.09 78.7399 0)
		)
		(pin "1"
			(uuid "36485159-6b4f-44c6-9764-0795706a34cc")
		)
		(pin "2"
			(uuid "010f5b4b-347d-48ab-89f8-0460ba0a350e")
		)
		(instances
			(project "fixture"
				(path "/59d453a4/3dc790c6"
					(reference "R27")
					(unit 1)
				)
			)
		)
	)
)
)";

bool waitUntilIdle(Project &project, int timeout = 30000)
{
	QElapsedTimer timer;
	timer.start();

	do {
		QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
	} while ((project.busy() || project.rows()->totalCount() == 0) && timer.elapsed() < timeout);

	return !project.busy() && project.rows()->totalCount() > 0;
}

QStringList sheetFiles(const QString &folder)
{
	QStringList out;
	for (const QString &name : QDir(folder).entryList({ QStringLiteral("*.kicad_sch") }, QDir::Files))
		out.append(QDir(folder).filePath(name));
	return out;
}

QHash<QString, QByteArray> contents(const QStringList &files)
{
	QHash<QString, QByteArray> out;
	for (const QString &file : files) {
		QFile handle(file);
		if (handle.open(QIODevice::ReadOnly))
			out.insert(file, handle.readAll());
	}
	return out;
}

} // namespace

class TestBom : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();

	void scanSkipsLibrarySymbols();
	void scanReadsReferenceFromInstances();
	void replaceKeepsEveryOtherByte();
	void insertLandsBeforeThePins();
	void insertIsIdempotent();
	void refusesUnsafeValue();
	void parsesKicadCsv();
	void filtersAssignedLines();
	void stagedLineStaysInTheQueue();
	void findsLineByRefs();

	void assignStagesWithoutWriting();
	void saveWritesThroughKicad();
	void saveValidatesTheLineItJustHid();
	void saveRefusesLockedSheet();
	void undoRestoresTheOriginalBytes();
	void closeClearsProject();

private:
	bool copyProject();
	QString m_root;
	QString m_folder;
	std::unique_ptr<QTemporaryDir> m_config;
	std::unique_ptr<QTemporaryDir> m_work;
	std::unique_ptr<Prefs> m_prefs;
};

void TestBom::initTestCase()
{
	/* Prefs writes a config file; the tests must not touch the real one. */
	m_config = std::make_unique<QTemporaryDir>();
	QVERIFY(m_config->isValid());
	qputenv("XDG_CONFIG_HOME", m_config->path().toUtf8());

	m_prefs = std::make_unique<Prefs>();
}

void TestBom::scanSkipsLibrarySymbols()
{
	const QVector<sch::Symbol> symbols = sch::scan(kSheet);

	QCOMPARE(symbols.size(), 1);
	QCOMPARE(symbols.first().uuid, QByteArray("0493f97b-d466-45c5-a859-23e5326c41f5"));
	QCOMPARE(symbols.first().value, QStringLiteral("12k 1%"));

	const QByteArray block = kSheet.mid(symbols.first().start,
					   symbols.first().end - symbols.first().start);
	QVERIFY(block.startsWith("\t(symbol\n"));
	QVERIFY(block.endsWith("\n\t)"));
}

void TestBom::scanReadsReferenceFromInstances()
{
	const QVector<sch::Symbol> symbols = sch::scan(kSheet);

	QCOMPARE(symbols.size(), 1);
	QCOMPARE(symbols.first().ref, QStringLiteral("R27"));
}

void TestBom::replaceKeepsEveryOtherByte()
{
	const sch::Symbol symbol = sch::scan(kSheet).first();
	const QByteArray block = kSheet.mid(symbol.start, symbol.end - symbol.start);

	const sch::Edit edit = sch::setProperty(block, QStringLiteral("Value"),
						QStringLiteral("13k"));
	QVERIFY2(edit.ok, qPrintable(edit.error));

	/* Everything but the value is the same, byte for byte. */
	const int at = block.indexOf("(property \"Value\" \"12k 1%\"");
	QByteArray expected = block;
	expected.replace(at + 19, 6, "13k");
	QCOMPARE(edit.block, expected);
}

void TestBom::insertLandsBeforeThePins()
{
	const sch::Symbol symbol = sch::scan(kSheet).first();
	const QByteArray block = kSheet.mid(symbol.start, symbol.end - symbol.start);

	const sch::Edit edit = sch::setProperty(block, QStringLiteral("DigiKey #"),
						QStringLiteral("RMCF0603FT12K0CT-ND"));
	QVERIFY2(edit.ok, qPrintable(edit.error));

	const int property = edit.block.indexOf("(property \"DigiKey #\" \"RMCF0603FT12K0CT-ND\"");
	const int pin = edit.block.indexOf("(pin \"1\"");
	QVERIFY(property > 0);
	QVERIFY(pin > property);

	/* The inserted field is hidden and sits at the symbol's own position. */
	QVERIFY(edit.block.contains("(at 82.55 77.47 90)"));
	QVERIFY(edit.block.contains("\n\t\t\t(hide yes)"));

	/* Everything that was there is still there: the property was inserted at
	 * the anchor and nothing else moved. */
	const int anchor = block.indexOf("\n\t\t(pin \"1\"");
	QVERIFY(anchor > 0);
	const int inserted = edit.block.size() - block.size();
	QVERIFY(inserted > 0);
	QCOMPARE(edit.block.left(anchor), block.left(anchor));
	QCOMPARE(edit.block.mid(anchor + inserted), block.mid(anchor));

	/* And it still reads back as the same symbol. */
	const QVector<sch::Symbol> after = sch::scan(edit.block);
	QCOMPARE(after.size(), 1);
	QCOMPARE(after.first().ref, QStringLiteral("R27"));
	QCOMPARE(after.first().value, QStringLiteral("12k 1%"));
}

void TestBom::insertIsIdempotent()
{
	const sch::Symbol symbol = sch::scan(kSheet).first();
	const QByteArray block = kSheet.mid(symbol.start, symbol.end - symbol.start);

	const sch::Edit first = sch::setProperty(block, QStringLiteral("DigiKey #"),
						 QStringLiteral("AAA-ND"));
	QVERIFY(first.ok);

	const sch::Edit second = sch::setProperty(first.block, QStringLiteral("DigiKey #"),
						  QStringLiteral("BBB-ND"));
	QVERIFY(second.ok);
	QCOMPARE(second.block.count("DigiKey #"), 1);
	QVERIFY(second.block.contains("\"BBB-ND\""));
}

void TestBom::refusesUnsafeValue()
{
	const sch::Symbol symbol = sch::scan(kSheet).first();
	const QByteArray block = kSheet.mid(symbol.start, symbol.end - symbol.start);

	const sch::Edit edit = sch::setProperty(block, QStringLiteral("DigiKey #"),
						QStringLiteral("a\"b"));
	QVERIFY(!edit.ok);
	QVERIFY(edit.block.isEmpty());
}

void TestBom::parsesKicadCsv()
{
	const QByteArray csv =
		"\"Refs\"\t\"Value\"\t\"Footprint\"\t\"DKPN\"\t\"DNP\"\t\"Qty\"\n"
		"\"C1,C3,C6\"\t\"2.2u\"\t\"\"\t\"1276-1183-1-ND\"\t\"\"\t\"3\"\n"
		"\"R27\"\t\"12k, 1%\"\t\"R_0603\"\t\"\"\t\"DNP\"\t\"1\"\n";

	QString error;
	const QVector<bom::Row> rows = bom::parse(csv, QLatin1Char('\t'), &error);
	QVERIFY2(error.isEmpty(), qPrintable(error));
	QCOMPARE(rows.size(), 2);

	QCOMPARE(rows.at(0).refs, QStringList({ "C1", "C3", "C6" }));
	QCOMPARE(rows.at(0).value, QStringLiteral("2.2u"));
	QCOMPARE(rows.at(0).dkpn, QStringLiteral("1276-1183-1-ND"));
	QCOMPARE(rows.at(0).qty, 3);
	QVERIFY(!rows.at(0).dnp);

	/* A comma inside a quoted field is not a reference separator. */
	QCOMPARE(rows.at(1).value, QStringLiteral("12k, 1%"));
	QCOMPARE(rows.at(1).refs, QStringList({ "R27" }));
	QVERIFY(rows.at(1).dnp);
}

void TestBom::filtersAssignedLines()
{
	BomModel model;

	BomModel::Row done;
	done.refs = { QStringLiteral("R1") };
	done.value = QStringLiteral("10k");
	done.dkpn = QStringLiteral("AAA-ND");
	done.qty = 1;

	BomModel::Row todo;
	todo.refs = { QStringLiteral("R2") };
	todo.value = QStringLiteral("10k");
	todo.qty = 1;

	model.setRows({ done, todo });
	QCOMPARE(model.count(), 1);
	QCOMPARE(model.totalCount(), 2);
	QCOMPARE(model.assignedCount(), 1);
	QCOMPARE(model.rowAt(0).value(QStringLiteral("refs")).toStringList(),
		 QStringList({ QStringLiteral("R2") }));

	model.setShowAssigned(true);
	QCOMPARE(model.count(), 2);
}

void TestBom::stagedLineStaysInTheQueue()
{
	BomModel model;

	BomModel::Row todo;
	todo.refs = { QStringLiteral("R2") };
	todo.value = QStringLiteral("10k");
	todo.qty = 1;

	model.setRows({ todo });
	QCOMPARE(model.count(), 1);

	/* Staged only: the schematic still has no number, so the line is
	 * still work left, and it still has to be on screen. */
	model.setStaged({ { QStringLiteral("R2"), QStringLiteral("AAA-ND") } });
	QCOMPARE(model.count(), 1);
	QCOMPARE(model.assignedCount(), 0);

	const QVariantMap row = model.rowAt(0);
	QCOMPARE(row.value(QStringLiteral("staged")).toBool(), true);
	QCOMPARE(row.value(QStringLiteral("assigned")).toBool(), false);
	QCOMPARE(row.value(QStringLiteral("dkpn")).toString(), QStringLiteral("AAA-ND"));

	/* A merged line with a staged number on one reference stays unstaged. */
	BomModel::Row merged = todo;
	merged.refs = { QStringLiteral("R2"), QStringLiteral("R3") };
	model.setRows({ merged });
	model.setStaged({ { QStringLiteral("R2"), QStringLiteral("AAA-ND") } });
	QVERIFY(!model.rowAt(0).value(QStringLiteral("staged")).toBool());
}

void TestBom::findsLineByRefs()
{
	BomModel model;

	BomModel::Row merged;
	merged.refs = { QStringLiteral("C1"), QStringLiteral("C3"), QStringLiteral("C6") };
	merged.value = QStringLiteral("2.2u");
	merged.qty = 3;

	model.setRows({ merged });

	/* Assigning the same number to a line merges it with another one; the
	 * line the user was standing on still has to be found. */
	QCOMPARE(model.indexOfRefs({ QStringLiteral("C3") }), 0);
	QCOMPARE(model.indexOfRefs({ QStringLiteral("C1"), QStringLiteral("C3"),
				     QStringLiteral("C6") }),
		 0);
	QCOMPARE(model.indexOfRefs({ QStringLiteral("R9") }), -1);
}

bool TestBom::copyProject()
{
	const QDir source(QStringLiteral(DKBOM_FIXTURE_DIR));
	if (!source.exists())
		return false;

	m_work = std::make_unique<QTemporaryDir>();
	if (!m_work->isValid())
		return false;

	m_folder = m_work->path();

	for (const QString &file : sheetFiles(source.absolutePath())) {
		const QString target = QDir(m_folder).filePath(QFileInfo(file).fileName());
		if (!QFile::copy(file, target))
			return false;
	}

	m_root = QDir(m_folder).filePath(QStringLiteral("fixture.kicad_sch"));
	return QFileInfo::exists(m_root);
}

void TestBom::assignStagesWithoutWriting()
{
	if (QStandardPaths::findExecutable(QStringLiteral("kicad-cli")).isEmpty())
		QSKIP("kicad-cli is not installed");
	if (!copyProject())
		QSKIP("the integration test fixture is missing");

	Project project(m_prefs.get());
	QVERIFY(project.openPath(m_root));
	QVERIFY(waitUntilIdle(project));

	project.rows()->setShowAssigned(true);
	project.selectRow(0);
	QVERIFY(project.hasTarget());

	const QVariantMap line = project.rows()->rowAt(0);
	const QStringList refs = line.value(QStringLiteral("refs")).toStringList();
	QVERIFY(!refs.isEmpty());

	const QHash<QString, QByteArray> before = contents(sheetFiles(m_folder));

	/* Assigning only stages a pick, so KiCad can keep the project open. */
	project.assign(QStringLiteral("TEST0000-ND"));
	QCOMPARE(project.pendingCount(), refs.size());

	const QHash<QString, QByteArray> after = contents(sheetFiles(m_folder));
	for (auto it = before.constBegin(); it != before.constEnd(); ++it)
		QCOMPARE(after.value(it.key()), it.value());

	const QVariantMap staged = project.rows()->rowAt(0);
	QVERIFY(staged.value(QStringLiteral("staged")).toBool());
	QCOMPARE(staged.value(QStringLiteral("dkpn")).toString(),
		 QStringLiteral("TEST0000-ND"));

	project.discard();
	QCOMPARE(project.pendingCount(), 0);
	QVERIFY(!project.rows()->rowAt(0).value(QStringLiteral("staged")).toBool());
}

void TestBom::saveWritesThroughKicad()
{
	if (QStandardPaths::findExecutable(QStringLiteral("kicad-cli")).isEmpty())
		QSKIP("kicad-cli is not installed");
	if (!copyProject())
		QSKIP("the integration test fixture is missing");

	Project project(m_prefs.get());
	QVERIFY(project.openPath(m_root));
	QVERIFY(waitUntilIdle(project));

	/* Every line in this project already has a number, and the pane only
	 * shows the unassigned ones by default. */
	project.rows()->setShowAssigned(true);
	QVERIFY(project.rows()->count() > 0);

	const QVariantMap line = project.rows()->rowAt(0);
	const QStringList refs = line.value(QStringLiteral("refs")).toStringList();
	QVERIFY(!refs.isEmpty());

	project.selectRow(0);
	QVERIFY(project.hasTarget());

	const QString number = QStringLiteral("TEST0000-ND");
	project.assign(number);
	project.save();
	QVERIFY(waitUntilIdle(project));

	const int index = project.rows()->indexOfRefs(refs);
	QVERIFY(index >= 0);
	QCOMPARE(project.rows()->rowAt(index).value(QStringLiteral("dkpn")).toString(), number);

	/* And it is in the files, once per symbol. */
	int written = 0;
	for (const QByteArray &text : contents(sheetFiles(m_folder)))
		written += text.count(number.toUtf8());
	QCOMPARE(written, refs.size());
}

void TestBom::saveValidatesTheLineItJustHid()
{
	if (QStandardPaths::findExecutable(QStringLiteral("kicad-cli")).isEmpty())
		QSKIP("kicad-cli is not installed");
	if (!copyProject())
		QSKIP("the integration test fixture is missing");

	Project project(m_prefs.get());
	QVERIFY(project.openPath(m_root));
	QVERIFY(waitUntilIdle(project));

	/* Select a line, then let it leave the unassigned view, which is what
	 * happens to it the moment it is written. The check afterwards must still
	 * find it. */
	project.rows()->setShowAssigned(true);
	project.selectRow(0);
	project.rows()->setShowAssigned(false);
	QCOMPARE(project.rows()->count(), 0);

	QSignalSpy notices(&project, &Project::notice);

	project.assign(QStringLiteral("TEST0000-ND"));
	notices.clear();
	project.save();
	QVERIFY(waitUntilIdle(project));

	QCOMPARE(notices.size(), 1);
	QVERIFY2(notices.first().at(1).toBool(), qPrintable(notices.first().at(0).toString()));
	QVERIFY(notices.first().at(0).toString().contains(QStringLiteral("read back")));
}

void TestBom::saveRefusesLockedSheet()
{
	if (QStandardPaths::findExecutable(QStringLiteral("kicad-cli")).isEmpty())
		QSKIP("kicad-cli is not installed");
	if (!copyProject())
		QSKIP("the integration test fixture is missing");

	Project project(m_prefs.get());
	QVERIFY(project.openPath(m_root));
	QVERIFY(waitUntilIdle(project));
	project.rows()->setShowAssigned(true);
	project.selectRow(0);

	const QHash<QString, QByteArray> before = contents(sheetFiles(m_folder));
	QSignalSpy notices(&project, &Project::notice);

	/* KiCad holding any sheet is reason enough not to touch the project. */
	QStringList locks;
	for (const QString &file : sheetFiles(m_folder)) {
		const QFileInfo info(file);
		const QString lock = info.dir().filePath(QLatin1Char('~') + info.fileName()
							 + QStringLiteral(".lck"));
		if (QFile(lock).open(QIODevice::WriteOnly))
			locks.append(lock);
	}
	QVERIFY(!locks.isEmpty());

	project.assign(QStringLiteral("TEST0000-ND"));
	notices.clear();
	project.save();

	QCOMPARE(notices.size(), 1);
	QVERIFY(!notices.first().at(1).toBool());
	QVERIFY(notices.first().at(0).toString().contains(QStringLiteral("KiCad")));

	const QHash<QString, QByteArray> after = contents(sheetFiles(m_folder));
	for (auto it = before.constBegin(); it != before.constEnd(); ++it)
		QCOMPARE(after.value(it.key()), it.value());

	for (const QString &lock : locks)
		QFile::remove(lock);
}

void TestBom::undoRestoresTheOriginalBytes()
{
	if (QStandardPaths::findExecutable(QStringLiteral("kicad-cli")).isEmpty())
		QSKIP("kicad-cli is not installed");
	if (!copyProject())
		QSKIP("the integration test fixture is missing");

	Project project(m_prefs.get());
	QVERIFY(project.openPath(m_root));
	QVERIFY(waitUntilIdle(project));
	project.rows()->setShowAssigned(true);

	const QHash<QString, QByteArray> before = contents(sheetFiles(m_folder));

	project.selectRow(0);
	QVERIFY(project.canUndo() == false);

	project.assign(QStringLiteral("TEST0000-ND"));
	project.save();
	QVERIFY(waitUntilIdle(project));
	QVERIFY(project.canUndo());

	project.undo();
	QVERIFY(waitUntilIdle(project));
	QVERIFY(!project.canUndo());

	const QHash<QString, QByteArray> after = contents(sheetFiles(m_folder));
	for (auto it = before.constBegin(); it != before.constEnd(); ++it)
		QCOMPARE(after.value(it.key()), it.value());
}

void TestBom::closeClearsProject()
{
	if (QStandardPaths::findExecutable(QStringLiteral("kicad-cli")).isEmpty())
		QSKIP("kicad-cli is not installed");
	if (!copyProject())
		QSKIP("the integration test fixture is missing");

	Project project(m_prefs.get());
	QVERIFY(project.openPath(m_root));
	QVERIFY(waitUntilIdle(project));
	project.rows()->setShowAssigned(true);
	project.selectRow(0);
	QVERIFY(project.hasTarget());

	const QVariantMap line = project.rows()->rowAt(0);
	const QStringList refs = line.value(QStringLiteral("refs")).toStringList();
	QVERIFY(!refs.isEmpty());

	project.assign(QStringLiteral("TEST0000-ND"));
	QCOMPARE(project.pendingCount(), refs.size());
	QCOMPARE(m_prefs->lastProject(), m_root);
	QVERIFY(project.recent().contains(m_root));

	project.close();

	QVERIFY(!project.isOpen());
	QCOMPARE(project.rows()->count(), 0);
	QCOMPARE(project.pendingCount(), 0);
	QVERIFY(!project.hasTarget());
	QVERIFY(!project.canUndo());
	QVERIFY(m_prefs->lastProject().isEmpty());
	QVERIFY(project.recent().contains(m_root));
}

QTEST_MAIN(TestBom)

#include "tst_bom.moc"

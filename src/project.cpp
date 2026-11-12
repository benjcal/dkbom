#include "project.h"

#include "bomcsv.h"
#include "bommodel.h"
#include "prefs.h"
#include "schematic.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

/* One tab between fields: a footprint is "Library:Name" and a value can be
 * "10k 1%", so the delimiter has to be something neither of them contains. */
const QChar kFieldDelimiter = QLatin1Char('\t');

Project::Project(Prefs *prefs, QObject *parent)
	: QObject(parent)
	, m_prefs(prefs)
	, m_rows(new BomModel(this))
{
	m_tool = qEnvironmentVariable("KICAD_CLI");
	if (m_tool.isEmpty())
		m_tool = QStandardPaths::findExecutable(QStringLiteral("kicad-cli"));

	setStatus(m_tool.isEmpty()
			  ? tr("kicad-cli not found; set KICAD_CLI to its path")
			  : tr("Open a KiCad project"));

	connect(m_prefs, &Prefs::recentChanged, this, &Project::recentChanged);
}

QString Project::fileName() const
{
	return m_root.isEmpty() ? QString() : QFileInfo(m_root).fileName();
}

QStringList Project::recent() const
{
	return m_prefs->recentProjects();
}

void Project::setStatus(const QString &status)
{
	if (m_status == status)
		return;

	m_status = status;
	emit statusChanged();
}

void Project::emitNotice(const QString &text, bool ok)
{
	setStatus(text);
	emit notice(text, ok);
}

void Project::open(const QUrl &url)
{
	openPath(url.toLocalFile());
}

bool Project::openPath(const QString &path)
{
	QFileInfo info(path);

	/* A folder means "the project in it", which is what a file dialog hands
	 * back when somebody picks the project directory. */
	if (info.isDir()) {
		const QDir dir(info.absoluteFilePath());
		const QString named = dir.filePath(info.fileName() + QStringLiteral(".kicad_sch"));
		const QStringList sheets = dir.entryList({ QStringLiteral("*.kicad_sch") }, QDir::Files);

		if (QFileInfo::exists(named))
			info = QFileInfo(named);
		else if (sheets.size() == 1)
			info = QFileInfo(dir.filePath(sheets.first()));
		else {
			emitNotice(tr("%1 holds %2 schematic files; pick the root one")
					   .arg(info.fileName())
					   .arg(sheets.size()),
				   false);
			return false;
		}
	}

	if (!info.exists() || info.suffix() != QStringLiteral("kicad_sch")) {
		emitNotice(tr("%1 is not a KiCad schematic").arg(path), false);
		return false;
	}

	m_root = info.absoluteFilePath();
	m_folder = info.absolutePath();
	m_sheets = collectSheets(m_root);
	m_undo.clear();
	m_pending.clear();
	emit pendingChanged();
	clearTarget();
	m_rows->setRows({});

	m_prefs->addRecentProject(m_root);
	emit changed();

	startRead();
	return true;
}

void Project::close()
{
	if (!isOpen())
		return;

	if (m_process) {
		QProcess *stale = m_process;
		m_process = nullptr;
		disconnect(stale, nullptr, this, nullptr);
		stale->kill();
		stale->deleteLater();
	}

	m_root.clear();
	m_folder.clear();
	m_sheets.clear();
	m_sites.clear();
	m_uuidOf.clear();
	m_refOf.clear();
	m_undo.clear();
	m_pending.clear();
	m_expect.clear();
	m_locked = false;
	m_busy = false;

	m_rows->setRows({});
	m_rows->setStaged({});
	clearTarget();

	m_prefs->clearLastProject();

	setStatus(m_tool.isEmpty()
			  ? tr("kicad-cli not found; set KICAD_CLI to its path")
			  : tr("Open a KiCad project"));
	emit changed();
	emit pendingChanged();
	emit undoChanged();
}

/* The root sheet names its children by file; the children can name more. */
QStringList Project::collectSheets(const QString &root) const
{
	QStringList sheets;
	QStringList queue{ root };
	QSet<QString> seen;

	/* KiCad 7 and later call it "Sheetfile"; older files "Sheet file". */
	static const QRegularExpression sheetFile(
		QStringLiteral("\\(property \"Sheet ?file\" \"([^\"]+)\""));

	while (!queue.isEmpty()) {
		const QString file = queue.takeFirst();
		if (seen.contains(file))
			continue;

		seen.insert(file);
		sheets.append(file);

		QFile handle(file);
		if (!handle.open(QIODevice::ReadOnly))
			continue;

		const QString text = QString::fromUtf8(handle.readAll());
		const QString folder = QFileInfo(file).absolutePath();

		auto matches = sheetFile.globalMatch(text);
		while (matches.hasNext()) {
			const QString next = QDir(folder).absoluteFilePath(matches.next().captured(1));
			if (!seen.contains(next))
				queue.append(next);
		}
	}

	return sheets;
}

void Project::index()
{
	m_sites.clear();
	m_uuidOf.clear();
	m_refOf.clear();

	for (const QString &file : m_sheets) {
		QFile handle(file);
		if (!handle.open(QIODevice::ReadOnly))
			continue;

		const QByteArray text = handle.readAll();
		for (const sch::Symbol &symbol : sch::scan(text)) {
			const QString uuid = QString::fromUtf8(symbol.uuid);
			m_sites.insert(uuid, { file, symbol.start, symbol.end });
			m_uuidOf.insert(symbol.ref, uuid);
			m_refOf.insert(uuid, symbol.ref);
		}
	}
}

void Project::refreshLock()
{
	bool locked = false;
	for (const QString &file : m_sheets) {
		const QFileInfo info(file);
		if (QFileInfo::exists(info.dir().filePath(QLatin1Char('~') + info.fileName()
							 + QStringLiteral(".lck")))) {
			locked = true;
			break;
		}
	}

	if (m_locked == locked)
		return;

	m_locked = locked;
	emit changed();
}

void Project::reload()
{
	if (!isOpen())
		return;

	startRead();
}

void Project::startRead()
{
	if (m_tool.isEmpty()) {
		emitNotice(tr("kicad-cli not found; set KICAD_CLI to its path"), false);
		return;
	}

	/* A read already in flight is stale the moment another is asked for. */
	if (m_process) {
		QProcess *stale = m_process;
		m_process = nullptr;
		disconnect(stale, nullptr, this, nullptr);
		stale->kill();
		stale->deleteLater();
	}

	const QString out = m_temp.filePath(QStringLiteral("bom.tsv"));
	QFile::remove(out);

	/* The labels are ours so the parser reads names it chose, and the ref
	 * range delimiter is emptied so the writer gets "R1,R2,R3" and not
	 * "R1-R3". */
	const QStringList args{
		QStringLiteral("sch"),
		QStringLiteral("export"),
		QStringLiteral("bom"),
		QStringLiteral("--fields"),
		QStringLiteral("Reference,Value,Footprint,DigiKey #,DNP,QUANTITY"),
		QStringLiteral("--labels"),
		QStringLiteral("Refs,Value,Footprint,DKPN,DNP,Qty"),
		QStringLiteral("--group-by"),
		QStringLiteral("Value,Footprint,DigiKey #"),
		QStringLiteral("--ref-range-delimiter"),
		QString(),
		QStringLiteral("--field-delimiter"),
		QString(kFieldDelimiter),
		QStringLiteral("--string-delimiter"),
		QStringLiteral("\""),
		QStringLiteral("-o"),
		out,
		m_root,
	};

	m_busy = true;
	emit changed();

	auto *process = new QProcess(this);
	m_process = process;

	connect(process, &QProcess::finished, this,
		[this, process, out](int code, QProcess::ExitStatus status) {
			if (m_process != process) {
				process->deleteLater();
				return;
			}

			const bool failed = status != QProcess::NormalExit || code != 0;
			readFinished(out, failed, QString::fromUtf8(process->readAllStandardError()));
			process->deleteLater();
		});

	connect(process, &QProcess::errorOccurred, this, [this, process, out](QProcess::ProcessError error) {
		if (m_process != process || error != QProcess::FailedToStart)
			return;

		readFinished(out, true, tr("cannot run %1").arg(m_tool));
		process->deleteLater();
	});

	process->start(m_tool, args);
}

void Project::readFinished(const QString &outPath, bool failed, const QString &errors)
{
	const bool validating = !m_expect.isEmpty();
	const QHash<QString, QString> expect = m_expect;
	m_expect.clear();

	/* Every early return below leaves the model as it was; staged numbers have
	 * to agree with what is still waiting to be written. */
	m_rows->setStaged(stagedByRef());

	m_process = nullptr;
	m_busy = false;

	if (failed) {
		emit changed();
		emitNotice(tr("kicad-cli failed: %1").arg(errors.trimmed().isEmpty()
								 ? tr("no output")
								 : errors.trimmed()),
			   false);
		return;
	}

	QFile handle(outPath);
	if (!handle.open(QIODevice::ReadOnly)) {
		emit changed();
		emitNotice(tr("kicad-cli wrote no BOM"), false);
		return;
	}

	const QByteArray data = handle.readAll();
	handle.close();

	QString problem;
	const QVector<bom::Row> rows = bom::parse(data, kFieldDelimiter, &problem);
	if (!problem.isEmpty()) {
		emit changed();
		emitNotice(problem, false);
		return;
	}

	index();
	reconcilePending();

	QVector<BomModel::Row> lines;
	lines.reserve(rows.size());

	for (const bom::Row &row : rows) {
		BomModel::Row line;
		line.refs = row.refs;
		line.value = row.value;
		line.footprint = row.footprint;
		line.dkpn = row.dkpn;
		line.dnp = row.dnp;
		line.qty = row.qty;

		QStringList missing;
		for (const QString &ref : row.refs) {
			const QString uuid = m_uuidOf.value(ref);
			const auto site = m_sites.constFind(uuid);
			if (uuid.isEmpty() || site == m_sites.constEnd()) {
				missing.append(ref);
				continue;
			}

			const QString sheet = QFileInfo(site->file).completeBaseName();
			if (!line.sheets.contains(sheet))
				line.sheets.append(sheet);
		}

		/* A line whose symbols cannot be found is a line that cannot be
		 * written, and saying so beats failing later without a reason. */
		if (!missing.isEmpty())
			line.problem = tr("no symbol for %1").arg(missing.join(QStringLiteral(", ")));

		lines.append(line);
	}

	m_rows->setRows(lines);
	m_rows->setStaged(stagedByRef());
	refreshLock();

	if (hasTarget())
		selectRefs(m_target.value(QStringLiteral("refs")).toStringList());

	if (validating) {
		/* Every reference, including lines the dock hides: the ones just written
		 * have left the unassigned view, which is the whole point of writing
		 * them. */
		QStringList bad;
		for (auto it = expect.constBegin(); it != expect.constEnd(); ++it) {
			const QVariantMap found = m_rows->findRow({ it.key() });
			if (found.value(QStringLiteral("dkpn")).toString() != it.value())
				bad.append(it.key());
		}

		if (bad.isEmpty())
			emitNotice(tr("%1 symbols written, read back by kicad-cli")
					   .arg(expect.size()),
				   true);
		else
			emitNotice(tr("written, but kicad-cli does not show the number on %1")
					   .arg(bad.join(QStringLiteral(", "))),
				   false);
	} else {
		const int unassigned = m_rows->totalCount() - m_rows->assignedCount();
		setStatus(tr("%1 lines, %2 unassigned")
				  .arg(m_rows->totalCount())
				  .arg(unassigned));
	}

	emit changed();
}

void Project::selectRow(int index)
{
	const QVariantMap row = m_rows->rowAt(index);
	if (row.isEmpty()) {
		clearTarget();
		return;
	}

	QVariantMap target = row;
	target.insert(QStringLiteral("index"), index);

	m_target = target;
	emit targetChanged();
}

void Project::selectRefs(const QStringList &refs)
{
	const int index = m_rows->indexOfRefs(refs);
	if (index < 0) {
		clearTarget();
		return;
	}

	selectRow(index);
}

void Project::clearTarget()
{
	if (m_target.isEmpty())
		return;

	m_target.clear();
	emit targetChanged();
}

void Project::assign(const QString &dkpn)
{
	if (!hasTarget()) {
		emitNotice(tr("pick a BOM line first"), false);
		return;
	}

	const QString number = dkpn.trimmed();
	if (number.isEmpty()) {
		emitNotice(tr("that part has no Digi-Key number"), false);
		return;
	}

	const QStringList refs = m_target.value(QStringLiteral("refs")).toStringList();
	const QString value = m_target.value(QStringLiteral("value")).toString();
	if (refs.isEmpty()) {
		emitNotice(tr("that BOM line has no references"), false);
		return;
	}

	/* Resolved now, while the line is in front of the user, so a missing symbol
	 * surfaces here and never at save time. */
	QHash<QString, Pending> staged;
	for (const QString &ref : refs) {
		const QString uuid = m_uuidOf.value(ref);
		if (uuid.isEmpty() || !m_sites.contains(uuid)) {
			emitNotice(tr("no symbol for %1 in these sheets").arg(ref), false);
			return;
		}

		staged.insert(uuid, { ref, value, number });
	}

	for (auto it = staged.constBegin(); it != staged.constEnd(); ++it)
		m_pending.insert(it.key(), it.value());

	m_rows->setStaged(stagedByRef());
	emit pendingChanged();
	emitNotice(tr("%1 → %2 staged; %3 unsaved")
			   .arg(refs.join(QStringLiteral(", ")), number)
			   .arg(m_pending.size()),
		   true);
}

void Project::save()
{
	if (m_pending.isEmpty())
		return;

	if (m_busy) {
		emitNotice(tr("still reading the project; try again"), false);
		return;
	}

	QString error;
	if (!applyAssignments(m_pending, &error)) {
		emitNotice(error, false);
		return;
	}

	/* The read-back check runs per reference: writing the same
	 * number onto two lines merges them in the BOM that comes back. */
	m_expect.clear();
	for (const Pending &pending : m_pending)
		m_expect.insert(pending.ref, pending.dkpn);

	m_pending.clear();
	emit pendingChanged();

	startRead();
}

void Project::discard()
{
	if (m_pending.isEmpty())
		return;

	m_pending.clear();
	m_rows->setStaged({});
	emit pendingChanged();
	emitNotice(tr("unsaved assignments discarded"), true);
}

/* A reload is allowed while something is staged: the sheets are re-read after
 * a write, but also when KiCad has been in the files. Whatever is still there
 * is put back on its line, and a symbol that went away takes its staged number
 * with it. */
void Project::reconcilePending()
{
	if (m_pending.isEmpty())
		return;

	int lost = 0;
	for (auto it = m_pending.begin(); it != m_pending.end();) {
		const QString ref = m_refOf.value(it.key());
		if (ref.isEmpty()) {
			it = m_pending.erase(it);
			++lost;
			continue;
		}

		it->ref = ref;
		++it;
	}

	if (lost == 0)
		return;

	emit pendingChanged();
	emitNotice(tr("%1 unsaved assignment(s) dropped: the symbol is gone").arg(lost), false);
}

QHash<QString, QString> Project::stagedByRef() const
{
	QHash<QString, QString> byRef;
	byRef.reserve(m_pending.size());
	for (const Pending &pending : m_pending)
		byRef.insert(pending.ref, pending.dkpn);
	return byRef;
}

bool Project::applyAssignments(const QHash<QString, Pending> &pending, QString *error)
{
	if (pending.isEmpty()) {
		*error = tr("that BOM line has no references");
		return false;
	}

	QHash<QString, QStringList> byFile;
	for (auto it = pending.constBegin(); it != pending.constEnd(); ++it) {
		const auto site = m_sites.constFind(it.key());
		if (site == m_sites.constEnd()) {
			*error = tr("no symbol for %1 in these sheets").arg(it->ref);
			return false;
		}

		byFile[site->file].append(it.key());
	}

	/* Everything is built before anything is written: a refusal half way
	 * through a project would leave it half assigned. */
	QVector<QPair<QString, QByteArray>> writes;
	QHash<QString, QByteArray> before;

	for (auto it = byFile.constBegin(); it != byFile.constEnd(); ++it) {
		const QString file = it.key();
		const QFileInfo info(file);
		const QString lock = info.dir().filePath(QLatin1Char('~') + info.fileName()
							 + QStringLiteral(".lck"));

		if (QFileInfo::exists(lock)) {
			*error = tr("%1 is open in KiCad; close it first").arg(info.fileName());
			return false;
		}

		QFile handle(file);
		if (!handle.open(QIODevice::ReadOnly)) {
			*error = tr("cannot read %1").arg(info.fileName());
			return false;
		}

		const QByteArray text = handle.readAll();
		handle.close();

		struct Change {
			int start;
			int end;
			QByteArray block;
		};

		QVector<Change> changes;
		for (const QString &uuid : it.value()) {
			const Pending staged = pending.value(uuid);
			const Site site = m_sites.value(uuid);
			const QByteArray block = text.mid(site.start, site.end - site.start);

			/* The reference to symbol mapping has to hold, or the number
			 * would be written onto a part nobody asked about. */
			const QVector<sch::Symbol> found = sch::scan(block);
			if (found.size() != 1 || found.first().ref != staged.ref) {
				*error = tr("%1 does not match its symbol in %2")
						 .arg(staged.ref, info.fileName());
				return false;
			}
			if (found.first().value != staged.value) {
				*error = tr("%1 now reads %2 in %3 (expected %4); discard or reload")
						 .arg(staged.ref, found.first().value, info.fileName(),
						      staged.value);
				return false;
			}

			const sch::Edit edit =
				sch::setProperty(block, QStringLiteral("DigiKey #"), staged.dkpn);
			if (!edit.ok) {
				*error = tr("%1: %2").arg(staged.ref, edit.error);
				return false;
			}

			changes.append({ site.start, site.end, edit.block });
		}

		std::sort(changes.begin(), changes.end(),
			  [](const Change &a, const Change &b) { return a.start > b.start; });

		QByteArray edited = text;
		for (const Change &change : changes)
			edited.replace(change.start, change.end - change.start, change.block);

		before.insert(file, text);
		writes.append({ file, edited });
	}

	/* Kept before the first byte is written, so a failure half way through
	 * still leaves something to undo. */
	m_undo = before;
	emit undoChanged();

	for (const auto &write : writes) {
		const QFileDevice::Permissions permissions = QFile::permissions(write.first);

		QSaveFile out(write.first);
		if (!out.open(QIODevice::WriteOnly)) {
			*error = tr("cannot write %1").arg(QFileInfo(write.first).fileName());
			return false;
		}

		out.write(write.second);
		if (!out.commit()) {
			*error = tr("cannot write %1").arg(QFileInfo(write.first).fileName());
			return false;
		}

		QFile::setPermissions(write.first, permissions);
	}

	return true;
}

void Project::undo()
{
	if (m_undo.isEmpty())
		return;

	for (auto it = m_undo.constBegin(); it != m_undo.constEnd(); ++it) {
		const QFileDevice::Permissions permissions = QFile::permissions(it.key());

		QSaveFile out(it.key());
		if (!out.open(QIODevice::WriteOnly) || out.write(it.value()) < 0 || !out.commit()) {
			emitNotice(tr("cannot restore %1").arg(QFileInfo(it.key()).fileName()), false);
			return;
		}

		QFile::setPermissions(it.key(), permissions);
	}

	m_undo.clear();
	emit undoChanged();
	emitNotice(tr("assignment undone"), true);

	startRead();
}

void Project::forgetRecent()
{
	m_prefs->forgetProjects();
}

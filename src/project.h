#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QUrl>
#include <QVariantMap>

class BomModel;
class Prefs;
class QProcess;

/* One KiCad project, its BOM, and the writing of a Digi-Key number into it.
 *
 * Reading is delegated: `kicad-cli sch export bom` knows the hierarchy, the
 * exclude-from-BOM flag and the grouping, and is the same code KiCad runs, so
 * it cannot disagree with the schematic. Writing happens here, because
 * kicad-cli cannot write: the sheet files are scanned here for the byte range
 * of every placed symbol, keyed by UUID, and one property is spliced in. */
class Project : public QObject {
	Q_OBJECT
	Q_PROPERTY(BomModel *rows READ rows CONSTANT)
	Q_PROPERTY(bool isOpen READ isOpen NOTIFY changed)
	Q_PROPERTY(bool busy READ busy NOTIFY changed)
	Q_PROPERTY(bool locked READ locked NOTIFY changed)
	Q_PROPERTY(QString fileName READ fileName NOTIFY changed)
	Q_PROPERTY(QString folder READ folder NOTIFY changed)
	Q_PROPERTY(QString tool READ tool NOTIFY changed)
	Q_PROPERTY(QString status READ status NOTIFY statusChanged)
	Q_PROPERTY(QVariantMap target READ target NOTIFY targetChanged)
	Q_PROPERTY(bool hasTarget READ hasTarget NOTIFY targetChanged)
	Q_PROPERTY(bool canUndo READ canUndo NOTIFY undoChanged)
	Q_PROPERTY(int pendingCount READ pendingCount NOTIFY pendingChanged)
	Q_PROPERTY(QStringList recent READ recent NOTIFY recentChanged)

public:
	explicit Project(Prefs *prefs, QObject *parent = nullptr);

	BomModel *rows() const { return m_rows; }
	bool isOpen() const { return !m_root.isEmpty(); }
	bool busy() const { return m_busy; }
	bool locked() const { return m_locked; }
	QString fileName() const;
	QString folder() const { return m_folder; }
	QString tool() const { return m_tool; }
	QString status() const { return m_status; }
	QVariantMap target() const { return m_target; }
	bool hasTarget() const { return !m_target.isEmpty(); }
	bool canUndo() const { return !m_undo.isEmpty(); }
	int pendingCount() const { return m_pending.size(); }
	QStringList recent() const;

	/* A file is what is expected; a folder is taken to mean its project file,
	 * which is how somebody who picked the folder in a dialog expects it. */
	Q_INVOKABLE void open(const QUrl &url);
	Q_INVOKABLE bool openPath(const QString &path);
	Q_INVOKABLE void close();
	Q_INVOKABLE void reload();
	Q_INVOKABLE void selectRow(int index);
	Q_INVOKABLE void clearTarget();
	/* A pick is staged here until save(), so KiCad can stay open on the
	 * project while the BOM is filled in from this side. */
	Q_INVOKABLE void assign(const QString &dkpn);
	Q_INVOKABLE void save();
	Q_INVOKABLE void discard();
	Q_INVOKABLE void undo();
	Q_INVOKABLE void forgetRecent();

signals:
	void changed();
	void statusChanged();
	void targetChanged();
	void undoChanged();
	void pendingChanged();
	void recentChanged();
	/* Worth a toast, in both directions: what came back from KiCad, or why it
	 * could not be written. */
	void notice(const QString &text, bool ok);

private:
	struct Site {
		QString file;
		int start = 0;
		int end = 0;
	};

	/* Keyed by UUID, so an assignment survives the annotation changing under it
	 * in KiCad. The reference is kept only to name the thing in messages. */
	struct Pending {
		QString ref;
		QString value;
		QString dkpn;
	};

	void startRead();
	void readFinished(const QString &outPath, bool failed, const QString &errors);
	void index();
	QStringList collectSheets(const QString &root) const;
	void setStatus(const QString &status);
	bool applyAssignments(const QHash<QString, Pending> &pending, QString *error);
	void reconcilePending();
	QHash<QString, QString> stagedByRef() const;
	void selectRefs(const QStringList &refs);
	void refreshLock();
	void emitNotice(const QString &text, bool ok);

	Prefs *m_prefs;
	BomModel *m_rows;
	QTemporaryDir m_temp;
	QProcess *m_process = nullptr;

	QString m_tool;
	QString m_root;
	QString m_folder;
	QStringList m_sheets;
	bool m_busy = false;
	bool m_locked = false;
	QString m_status;

	QHash<QString, Site> m_sites;     // by UUID
	QHash<QString, QString> m_uuidOf; // by reference
	QHash<QString, QString> m_refOf;  // by UUID, the reverse

	QVariantMap m_target;
	QHash<QString, QByteArray> m_undo;
	QHash<QString, Pending> m_pending; // by UUID
	QHash<QString, QString> m_expect;  // reference to the number it read back
};

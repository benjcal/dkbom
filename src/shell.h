#pragma once

#include <QObject>
#include <QString>

/* The one bit of window-system plumbing the QML needs: putting text on the
 * clipboard without a hidden TextEdit in every file that copies something. */
class Shell : public QObject {
	Q_OBJECT

public:
	explicit Shell(QObject *parent = nullptr);

	Q_INVOKABLE void copy(const QString &text) const;
};

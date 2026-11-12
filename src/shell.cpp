#include "shell.h"

#include <QClipboard>
#include <QGuiApplication>

Shell::Shell(QObject *parent)
	: QObject(parent)
{
}

void Shell::copy(const QString &text) const
{
	if (text.isEmpty())
		return;

	QGuiApplication::clipboard()->setText(text);
}

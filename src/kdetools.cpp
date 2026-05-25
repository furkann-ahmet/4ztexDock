// SPDX-License-Identifier: GPL-3.0-or-later
#include "kdetools.h"

#include <QProcess>
#include <QStandardPaths>

namespace kdetools {

QString resolveTool(const QString &qt6Name, const QString &qt5Name)
{
    QString p = QStandardPaths::findExecutable(qt6Name);
    if (!p.isEmpty()) return p;
    return QStandardPaths::findExecutable(qt5Name);
}

bool startDetached(const QString &qt6Name, const QString &qt5Name,
                    const QStringList &args)
{
    const QString tool = resolveTool(qt6Name, qt5Name);
    if (tool.isEmpty()) return false;
    return QProcess::startDetached(tool, args);
}

} // namespace kdetools

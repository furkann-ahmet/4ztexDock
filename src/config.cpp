// SPDX-License-Identifier: GPL-3.0-or-later
#include "config.h"

#include <QSettings>
#include <QStandardPaths>
#include <QDir>
#include <QStringList>
#include <QtGlobal>

namespace dockconfig {

Config Config::defaults()
{
    Config c;
    c.screen = QString();
    c.accent = QColor(167, 139, 250);     // violet-400
    c.frequentLimit = 8;
    return c;
}

QString configFilePath()
{
    const QString base = QStandardPaths::writableLocation(
        QStandardPaths::ConfigLocation);
    return base + "/4ztexDock/config.ini";
}

namespace {

// "R,G,B" stringini QColor'a parse eder. Geçersizse fallback döner.
QColor parseAccent(const QString &raw, const QColor &fallback)
{
    if (raw.isEmpty()) return fallback;
    const QStringList parts = raw.split(',', Qt::SkipEmptyParts);
    if (parts.size() != 3) return fallback;
    bool ok1, ok2, ok3;
    const int r = parts.at(0).trimmed().toInt(&ok1);
    const int g = parts.at(1).trimmed().toInt(&ok2);
    const int b = parts.at(2).trimmed().toInt(&ok3);
    if (!ok1 || !ok2 || !ok3) return fallback;
    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
        return fallback;
    }
    return QColor(r, g, b);
}

} // namespace

Config load()
{
    Config c = Config::defaults();
    const QString path = configFilePath();
    if (!QFile::exists(path)) {
        return c;  // ilk kurulum: defaults kullan
    }

    QSettings ini(path, QSettings::IniFormat);

    // [Display]
    c.screen = ini.value("Display/screen", QString()).toString().trimmed();

    // [Appearance]
    c.accent = parseAccent(
        ini.value("Appearance/accent", QString()).toString(),
        c.accent);

    // [Launcher]
    c.frequentLimit = ini.value("Launcher/frequentLimit",
                                c.frequentLimit).toInt();
    c.frequentLimit = qBound(1, c.frequentLimit, 16);

    return c;
}

} // namespace dockconfig

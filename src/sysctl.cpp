// SPDX-License-Identifier: GPL-3.0-or-later
#include "sysctl.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include "kdetools.h"

#include <QDir>
#include <QProcess>
#include <QStringList>
#include <QtGlobal>

namespace sysctl {

int brightnessMax()
{
    QDBusInterface b("org.kde.Solid.PowerManagement",
                     "/org/kde/Solid/PowerManagement/Actions/BrightnessControl",
                     "org.kde.Solid.PowerManagement.Actions.BrightnessControl",
                     QDBusConnection::sessionBus());
    if (!b.isValid()) return 0;
    QDBusReply<int> r = b.call("brightnessMax");
    return r.isValid() ? r.value() : 0;
}

int brightnessPercent()
{
    QDBusInterface b("org.kde.Solid.PowerManagement",
                     "/org/kde/Solid/PowerManagement/Actions/BrightnessControl",
                     "org.kde.Solid.PowerManagement.Actions.BrightnessControl",
                     QDBusConnection::sessionBus());
    if (!b.isValid()) return -1;
    QDBusReply<int> max = b.call("brightnessMax");
    QDBusReply<int> cur = b.call("brightness");
    if (!max.isValid() || !cur.isValid() || max.value() <= 0) return -1;
    return qBound(0, int(100.0 * cur.value() / max.value()), 100);
}

void setBrightnessPercent(int pct)
{
    QDBusInterface b("org.kde.Solid.PowerManagement",
                     "/org/kde/Solid/PowerManagement/Actions/BrightnessControl",
                     "org.kde.Solid.PowerManagement.Actions.BrightnessControl",
                     QDBusConnection::sessionBus());
    if (!b.isValid()) return;
    QDBusReply<int> max = b.call("brightnessMax");
    if (!max.isValid() || max.value() <= 0) return;
    const int target = qBound(0, pct, 100) * max.value() / 100;
    b.call("setBrightness", target);
}

bool nightLightEnabled()
{
    QDBusInterface nl("org.kde.KWin", "/org/kde/KWin/NightLight",
                      "org.kde.KWin.NightLight",
                      QDBusConnection::sessionBus());
    if (!nl.isValid()) return false;
    return nl.property("enabled").toBool();
}

void openNightLightSettings()
{
    // Plasma 6 NightColor'ın canlı toggle DBus path'i yok — `enabled`
    // property read-only ve `kwin.reconfigure` config dosyasına yapılan
    // değişikliği pick up etmiyor (KDE bug / by-design). Pratik çözüm:
    // KCM'ini açmak ve kullanıcının orada toggle etmesi.
    kdetools::startDetached("kcmshell6", "kcmshell5", {"kcm_nightlight"});
}

bool hasBattery()
{
    QDir psd("/sys/class/power_supply");
    const auto entries = psd.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const auto &e : entries) {
        if (e.startsWith("BAT", Qt::CaseInsensitive)) return true;
    }
    return false;
}

QString activePowerProfile()
{
    QDBusInterface pp("org.freedesktop.UPower.PowerProfiles",
                      "/org/freedesktop/UPower/PowerProfiles",
                      "org.freedesktop.UPower.PowerProfiles",
                      QDBusConnection::systemBus());
    if (!pp.isValid()) return QString();
    return pp.property("ActiveProfile").toString();
}

void setPowerProfile(const QString &profile)
{
    QDBusInterface pp("org.freedesktop.UPower.PowerProfiles",
                      "/org/freedesktop/UPower/PowerProfiles",
                      "org.freedesktop.UPower.PowerProfiles",
                      QDBusConnection::systemBus());
    if (!pp.isValid()) return;
    pp.setProperty("ActiveProfile", profile);
}

void lockSession()
{
    QProcess::startDetached("loginctl", {"lock-session"});
}

void takeScreenshot()
{
    QProcess::startDetached("spectacle", QStringList{"--background"});
}

void openBluetoothSettings()
{
    kdetools::startDetached("kcmshell6", "kcmshell5", {"kcm_bluetooth"});
}

void openNetworkSettings()
{
    QProcess::startDetached("plasma-settings", {"-m", "kcm_networkmanagement"});
    // plasma-settings yoksa kcmshell fallback (Plasma 5/6 fark etmez):
    kdetools::startDetached("kcmshell6", "kcmshell5", {"kcm_networkmanagement"});
}

} // namespace sysctl

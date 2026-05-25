// SPDX-License-Identifier: GPL-3.0-or-later
// 4ztexDock — Sistem ayarları / kontrol yardımcıları.
//
// KDE Solid PowerManagement (brightness), KWin NightLight, UPower
// PowerProfiles ve KDE/Spectacle/loginctl shortcut'ları için DBus + process
// wrapper'ları. Network paneli quick action tile'larından çağrılır.
//
// Tüm fonksiyonlar yan etkili (DBus call, process launch). Pure logic değil —
// unit test yerine integration test'le doğrulanır.

#pragma once

#include <QString>

namespace sysctl {

// KDE Solid PowerManagement — Brightness control
int brightnessMax();
int brightnessPercent();          // Returns -1 if interface unavailable
void setBrightnessPercent(int pct);

// KWin NightLight
bool nightLightEnabled();
void openNightLightSettings();    // Açar KCM (Plasma 6'da canlı toggle DBus path yok)

// Laptop check + UPower PowerProfiles
bool hasBattery();
QString activePowerProfile();     // "performance" / "balanced" / "power-saver"
void setPowerProfile(const QString &profile);

// Session actions
void lockSession();               // loginctl lock-session
void takeScreenshot();            // spectacle --background

// Settings shortcut'ları (KCM açıcılar)
void openBluetoothSettings();
void openNetworkSettings();

} // namespace sysctl

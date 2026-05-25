// SPDX-License-Identifier: GPL-3.0-or-later
// 4ztexDock — User config (INI format).
//
// Path: ~/.config/4ztexDock/config.ini  (XDG_CONFIG_HOME respected)
//
// Schema (current):
//   [Display]
//   screen=DP-3            ; connector name/manufacturer/model/serial
//   [Appearance]
//   accent=167,139,250     ; violet-400. R,G,B (0-255 each)
//   [Launcher]
//   frequentLimit=8        ; frequent apps grid: 1-16
//
// Tüm key'ler opsiyonel. Eksikse defaults() değerleri kullanılır. Runtime
// reload yok — değişiklik sonrası dock'u kapat/aç. (İleride
// QFileSystemWatcher ile canlı reload eklenebilir.)

#pragma once

#include <QString>
#include <QColor>

namespace dockconfig {

struct Config {
    // Display
    QString screen;       // empty → auto

    // Appearance
    QColor accent;        // (167, 139, 250) varsayılan

    // Launcher
    int frequentLimit;    // varsayılan 8 (4×2 grid), clamp [1..16]

    static Config defaults();
};

// Tek seferlik load — main()'in başında bir kez çağır. config.ini yoksa
// defaults dönderir. Schema validation: bilinmeyen key'ler ignore edilir,
// out-of-range değerler clamp edilir.
Config load();

// XDG_CONFIG_HOME/4ztexDock/config.ini tam path. Dosya yoksa da döner —
// kullanıcı kendisi oluşturabilir.
QString configFilePath();

} // namespace dockconfig

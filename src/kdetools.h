// SPDX-License-Identifier: GPL-3.0-or-later
// 4ztexDock — KDE Plasma 5/6 tool wrapper.
//
// KDE 6 ile gelen tool isimleri Qt6 suffix'i taşıyor (kstart6, kcmshell6,
// qdbus6, kbuildsycoca6). Plasma 5'te bunlar suffix'siz veya 5'li
// (kcmshell5). Hangisi sistemde varsa onu çalıştırıyoruz.
//
// Hem PATH'te ararız hem QStandardPaths::findExecutable kullanırız.

#pragma once

#include <QString>
#include <QStringList>

namespace kdetools {

// Plasma 6 (qt6Name) ya da Plasma 5 (qt5Name) executable'ı bul.
// Boş string dönerse her ikisi de mevcut değil.
QString resolveTool(const QString &qt6Name, const QString &qt5Name);

// Sık kullanılanlar için kısayollar
inline QString qdbus()      { return resolveTool("qdbus6",      "qdbus"); }
inline QString kstart()     { return resolveTool("kstart6",     "kstart"); }
inline QString kcmshell()   { return resolveTool("kcmshell6",   "kcmshell5"); }
inline QString kbuildsycoca(){ return resolveTool("kbuildsycoca6","kbuildsycoca5"); }

// Tool'u arg listesiyle detached çalıştır. Hiçbir versiyon yoksa false
// döner; çağrı tarafı log'lar.
bool startDetached(const QString &qt6Name, const QString &qt5Name,
                   const QStringList &args);

} // namespace kdetools

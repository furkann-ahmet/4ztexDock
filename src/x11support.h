// SPDX-License-Identifier: GPL-3.0-or-later
// 4ztexDock — X11 dock window setup.
//
// X11'de layer-shell olmadığı için EWMH yöntemiyle dock penceresi yaratıyoruz:
//   - _NET_WM_WINDOW_TYPE = _NET_WM_WINDOW_TYPE_DOCK
//     (WM bunu "panel" olarak görür, taskbar'da göstermez, normal layout dışı)
//   - _NET_WM_STATE = STICKY (tüm masaüstlerinde) | SKIP_TASKBAR | SKIP_PAGER
//                     | ABOVE (her şeyin üstünde)
//   - _NET_WM_STRUT_PARTIAL: ekranın alt kenarından N piksel rezerve et —
//     fullscreen uygulamalar bu alana kadar büyüsün, dock'un üstüne çıkmasın.
//
// Wayland'da bu fonksiyonların hepsi no-op. Çağrı tarafında platform check yok.

#pragma once

#include <QtGlobal>

class QWidget;

namespace x11dock {

// Dock window olarak işaretle. Çağrılma anında widget'ın native handle'ı
// hazır olmalı (yani winId() çağrılmış / show() öncesi). Wayland'da no-op.
void markAsDock(QWidget *widget);

// Ekranın alt kenarında strutHeight kadar alan rezerve et. fullscreen
// uygulamalar bu zonun üstüne çıkmaz. xStart..xEnd ekran-mutlak X koordinatları
// (panel'in genişliği boyunca). Wayland'da no-op.
void reserveBottomStrut(QWidget *widget, int strutHeight, int xStart, int xEnd);

} // namespace x11dock

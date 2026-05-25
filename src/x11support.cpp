// SPDX-License-Identifier: GPL-3.0-or-later
#include "x11support.h"

#include <QGuiApplication>
#include <QWidget>
#include <QWindow>

// xcb-ewmh — EWMH (Extended Window Manager Hints) yardımcısı. Bu kütüphane
// `_NET_WM_*` atom isimlerini cache'leyip helper fonksiyon sağlıyor.
#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>

namespace x11dock {

namespace {

// Qt6'nın native interface'i üzerinden Qt'nin halihazırda açık olan
// xcb_connection_t'sini al — kendi connection'ımızı yaratırsak race olur.
xcb_connection_t *qtXcbConnection()
{
    auto *guiApp = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (!guiApp) return nullptr;
    auto *native = guiApp->nativeInterface<QNativeInterface::QX11Application>();
    return native ? native->connection() : nullptr;
}

// Ortak EWMH context'i (atom isimleri cache'i). İlk çağrıda init.
struct EwmhContext {
    xcb_ewmh_connection_t ewmh{};
    bool ready = false;

    EwmhContext()
    {
        auto *c = qtXcbConnection();
        if (!c) return;
        xcb_intern_atom_cookie_t *cookies = xcb_ewmh_init_atoms(c, &ewmh);
        if (xcb_ewmh_init_atoms_replies(&ewmh, cookies, nullptr)) {
            ready = true;
        }
    }

    ~EwmhContext()
    {
        if (ready) xcb_ewmh_connection_wipe(&ewmh);
    }
};

EwmhContext &ewmhInstance()
{
    static EwmhContext ctx;
    return ctx;
}

xcb_window_t windowOf(QWidget *widget)
{
    if (!widget) return XCB_WINDOW_NONE;
    auto *win = widget->windowHandle();
    if (!win) return XCB_WINDOW_NONE;
    return static_cast<xcb_window_t>(win->winId());
}

} // namespace


void markAsDock(QWidget *widget)
{
    if (QGuiApplication::platformName() != QLatin1String("xcb")) return;

    auto &ctx = ewmhInstance();
    if (!ctx.ready) return;
    xcb_window_t w = windowOf(widget);
    if (w == XCB_WINDOW_NONE) return;
    auto *c = qtXcbConnection();
    if (!c) return;

    // _NET_WM_WINDOW_TYPE → DOCK
    xcb_atom_t windowType = ctx.ewmh._NET_WM_WINDOW_TYPE_DOCK;
    xcb_ewmh_set_wm_window_type(&ctx.ewmh, w, 1, &windowType);

    // _NET_WM_STATE: birden fazla state set ediliyor.
    xcb_atom_t states[] = {
        ctx.ewmh._NET_WM_STATE_STICKY,        // tüm masaüstlerinde gözük
        ctx.ewmh._NET_WM_STATE_SKIP_TASKBAR,  // taskbar'da gözükmesin (kendisi taskbar zaten)
        ctx.ewmh._NET_WM_STATE_SKIP_PAGER,    // pager'da da gözükmesin
        ctx.ewmh._NET_WM_STATE_ABOVE,         // diğer pencerelerin üstünde
    };
    xcb_ewmh_set_wm_state(&ctx.ewmh, w,
                          sizeof(states) / sizeof(states[0]), states);

    // _NET_WM_DESKTOP = -1 ("all desktops") — STICKY zaten benzer etki ama
    // bazı WM'ler her ikisini de okur.
    xcb_ewmh_set_wm_desktop(&ctx.ewmh, w, 0xFFFFFFFF);

    xcb_flush(c);
}

void reserveBottomStrut(QWidget *widget, int strutHeight, int xStart, int xEnd)
{
    if (QGuiApplication::platformName() != QLatin1String("xcb")) return;

    auto &ctx = ewmhInstance();
    if (!ctx.ready) return;
    xcb_window_t w = windowOf(widget);
    if (w == XCB_WINDOW_NONE) return;
    auto *c = qtXcbConnection();
    if (!c) return;

    // _NET_WM_STRUT_PARTIAL formatı: 12-element CARDINAL array.
    //   [0] left, [1] right, [2] top, [3] bottom,
    //   [4..5] left_start_y, left_end_y,
    //   [6..7] right_start_y, right_end_y,
    //   [8..9] top_start_x, top_end_x,
    //  [10..11] bottom_start_x, bottom_end_x
    // Sadece alt kenarda strut istiyoruz → bottom = strutHeight, geri kalan 0.
    xcb_ewmh_wm_strut_partial_t strut{};
    strut.bottom = strutHeight;
    strut.bottom_start_x = xStart;
    strut.bottom_end_x = xEnd;
    xcb_ewmh_set_wm_strut_partial(&ctx.ewmh, w, strut);

    // Eski WM'ler için fallback: _NET_WM_STRUT (4-element).
    xcb_ewmh_set_wm_strut(&ctx.ewmh, w, 0, 0, 0, strutHeight);

    xcb_flush(c);
}

} // namespace x11dock

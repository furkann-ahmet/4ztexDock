# Changelog

**Diller:** **Türkçe** · [English](CHANGELOG.md)

Tüm önemli değişiklikler bu dosyada listelenir.
Format [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versiyonlama
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Eklenenler

- **KDE Plasma 6 X11 desteği** — Wayland session'a ek olarak `xcb` platform'da da
  çalışır. `_NET_WM_WINDOW_TYPE_DOCK` + `_NET_WM_STRUT_PARTIAL` (sticky/above/
  skip-taskbar atomları dahil) ile dock window. Manual positioning (layer-shell
  anchor/exclusive zone X11'de yok).
- Yeni modül: `src/x11support.{h,cpp}` — xcb-ewmh wrapper. Wayland'da no-op.
- PKGBUILD: `libxcb`, `xcb-util-wm` runtime depends geri eklendi.
- **KDE Plasma 5 desteği** — KWin scripting JS dual-API: `workspace.windowList`
  (Plasma 6) ya da `workspace.clientList` (Plasma 5), `internalId`/`windowId`
  detection runtime'da otomatik. Plasma 5'te ScreenShot2 yokluğu zaten graceful
  fallback'ti (icon+title), şimdi açık olarak doğrulandı.
- Yeni modül: `src/kdetools.{h,cpp}` — Plasma 5/6 tool fallback'i (kstart6→
  kstart, qdbus6→qdbus, kcmshell6→kcmshell5). sysctl + main.cpp tüm tool
  çağrıları bu helper üzerinden gidiyor.
- **İngilizce README**: `README.en.md`. Türkçe README üst tarafa dil seçici.
- `install.sh` modernize edildi: distro-agnostik (Arch dışı için), bağımlılık
  check, otomatik lrelease tool detection (lrelease6 / lrelease-qt6 / lrelease),
  user/system mode, --no-build flag, otomatik dock restart.

### Bilinen sınırlamalar

- **Plasma 6 X11 ve Plasma 5 dalları gerçek sistemde test edilmedi.** Maintainer
  yalnızca KDE Plasma 6 Wayland kullanıyor; kod incelemesi + derleme + Plasma 6
  Wayland regression testi yapıldı, ama X11 dock window setup ve Plasma 5 dual-
  API JS gerçek bir kurulumda doğrulanmadı. Bug raporu cidden yardımcı olur.

## [0.1.0] — 2026-05-25

İlk public release. KDE Plasma 6 Wayland hedefli custom dock.

### Eklenenler

- Wayland layer-shell pencere, KWin caller validation için `app-com.4ztex.dock-*.scope` autostart entegrasyonu.
- **Launcher menü**: frequent app grid (LaunchTracker), arama, klavye nav, folder/session pill shortcut'ları, ayarlar tile'ı. KGlobalAccel ile Meta tuşu binding.
- **Audio paneli**: PipeWire/PulseAudio (`pactl`), Devices + Apps tabı, slider + mute. Anonim PipeWire stream'leri için client metadata + flatpak `app_id` + `/proc/PID/exe` fallback'i ile doğru isim/ikon resolution.
- **Network paneli**: ConnectionCard (Ethernet/WiFi/VPN), brightness slider (Solid PowerManagement DBus), VPN listesi (`nmcli`), action tile grid'i.
- **Notification server**: `org.freedesktop.Notifications` FDO spec implementasyonu, `org.kde.NotificationManager` `InvokeAction` desteği, image-data hint parse, geçmiş listesi (50 cap), politeness check (başka daemon kayıtlıysa name'i almaz).
- **System tray**: `org.kde.StatusNotifierWatcher` host, overflow popup, sağ tık context menüleri.
- **Now Playing**: MPRIS player kaynağı seçici, play/pause/skip.
- **Window task list**: KWin scripting üzerinden açık pencere takibi, workspace filter, drag-to-reorder, minimize pulse feedback.
- **Global icon resolver**: `Icons::resolve()` — theme lookup, sycoca `KApplicationTrader`, token-fuzzy `.desktop` matching (`net.shadps4.qtlauncher` → `net.shadps4.shadps4-qtlauncher.desktop` gibi durumlar için).
- **CLI**: `--help`, `--version`, `--screen` flag'leri.
- **Runtime checks**: Wayland + KWin zorunlu; pactl/nmcli yoksa ilgili panel devre dışı bırakılır.
- **Paketleme**: Arch Linux PKGBUILD + `/etc/xdg/autostart` entry + LICENSE kurulumu.

### Mimari notlar

- KDE Plasma 6 Wayland'in tek seferlik bind kısıtlamaları nedeniyle minimize "shrink-to-icon" animasyonu yapılamadı (KWin Plasma protocol single-bind, `_NET_WM_ICON_GEOMETRY` Wayland'da honor edilmiyor, `zwlr_foreign_toplevel_manager_v1` KWin'de yok). Yerine task button pulse animasyonu.

### Sınırlamalar

- Sadece KDE Plasma 6 Wayland'da tam çalışır. Plasma 5 ve X11 desteklenmiyor.
- UI Türkçe (hardcoded); i18n yol haritasında.
- Yapılandırma dosyası yok — accent rengi, panel pozisyonu vs. derleme zamanı sabit. Yol haritasında.

[Unreleased]: https://github.com/furkann-ahmet/4ztexDock/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/furkann-ahmet/4ztexDock/releases/tag/v0.1.0

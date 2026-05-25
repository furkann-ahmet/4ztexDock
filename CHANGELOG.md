# Changelog

**Languages:** [Türkçe](CHANGELOG.tr.md) · **English**

All notable changes are tracked here. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/); versioning follows
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **KDE Plasma 6 X11 support** — In addition to Wayland sessions, the dock
  now runs on `xcb` platforms. Uses `_NET_WM_WINDOW_TYPE_DOCK` +
  `_NET_WM_STRUT_PARTIAL` (sticky / above / skip-taskbar atoms included) for
  the dock window, with manual positioning since layer-shell's anchor +
  exclusive zone are Wayland-only.
- New module: `src/x11support.{h,cpp}` — xcb-ewmh wrapper, no-op on Wayland.
- PKGBUILD: `libxcb`, `xcb-util-wm` added back to runtime dependencies.
- **KDE Plasma 5 support** — Dual-API JavaScript in the KWin scripting
  bridge: `workspace.windowList` (Plasma 6) or `workspace.clientList`
  (Plasma 5); `internalId` / `windowId` detection at runtime. The lack of
  ScreenShot2 in Plasma 5 was already a graceful fallback (icon + title);
  now explicitly verified.
- New module: `src/kdetools.{h,cpp}` — Plasma 5/6 tool fallback (`kstart6` →
  `kstart`, `qdbus6` → `qdbus`, `kcmshell6` → `kcmshell5`). `sysctl` and
  `main.cpp` route all KDE tool calls through this helper.
- **English README** added as the primary `README.md`; Turkish version moved
  to `README.tr.md` with a language switcher at the top of both files.
- `install.sh` rewritten as distro-agnostic: dependency check, automatic
  `lrelease` tool detection (`lrelease6` / `lrelease-qt6` / `lrelease`),
  user/system mode, `--no-build` flag, automatic dock restart, and an
  `--install-deps` flag that detects the distro from `/etc/os-release` and
  pulls the right packages via apt/dnf/zypper/pacman.

### Known limitations

- **Plasma 6 X11 and Plasma 5 paths are untested on real systems.** The
  maintainer only runs KDE Plasma 6 Wayland; code review, build, and the
  Plasma 6 Wayland regression test were done, but the X11 dock window setup
  and the Plasma 5 dual-API JavaScript haven't been verified on an actual
  installation. Bug reports really do help.

## [0.1.0] — 2026-05-25

First public release. Custom dock targeting KDE Plasma 6 Wayland.

### Added

- Wayland layer-shell window, with autostart integration designed so KWin's
  caller validation places the dock into `app-com.4ztex.dock-*.scope`.
- **Launcher menu**: frequent-apps grid (LaunchTracker), search box,
  keyboard navigation, folder + session shortcut pills, settings tile.
  Meta-key binding via KGlobalAccel.
- **Audio panel**: PipeWire/PulseAudio via `pactl`, Devices + Apps tabs,
  slider + mute. For anonymous PipeWire streams, name/icon resolution falls
  back through client metadata → flatpak `app_id` → `/proc/PID/exe`.
- **Network panel**: ConnectionCard (Ethernet/Wi-Fi/VPN), brightness slider
  (Solid PowerManagement DBus), VPN list (`nmcli`), action tile grid.
- **Notification server**: full implementation of the FDO Notifications
  spec; supports `org.kde.NotificationManager`'s `InvokeAction`, image-data
  hint parsing, a 50-item history cap, and a politeness check that backs
  off if another daemon is already registered.
- **System tray**: `org.kde.StatusNotifierWatcher` host, overflow popup,
  right-click context menus.
- **Now Playing**: MPRIS player source picker with play/pause/skip controls.
- **Window task list**: tracks open windows via KWin scripting, filters by
  workspace, supports drag-to-reorder, and emits a pulse animation on
  minimize.
- **Global icon resolver**: `Icons::resolve()` — theme lookup, sycoca
  `KApplicationTrader`, token-fuzzy `.desktop` matching (e.g.
  `net.shadps4.qtlauncher` → `net.shadps4.shadps4-qtlauncher.desktop`).
- **CLI**: `--help`, `--version`, `--screen` flags.
- **Runtime checks**: Wayland + KWin required; `pactl` / `nmcli` are
  optional (the relevant panel is disabled if missing).
- **Packaging**: Arch Linux PKGBUILD with `/etc/xdg/autostart` entry and
  LICENSE installation.

### Architectural notes

- A true "shrink-to-icon" minimize animation isn't possible under KDE Plasma
  6 Wayland's one-time-bind constraints (the Plasma protocol is held by
  plasmashell, `_NET_WM_ICON_GEOMETRY` isn't honored on Wayland, and
  `zwlr_foreign_toplevel_manager_v1` isn't implemented by KWin). A pulse
  animation on the task button is used instead.

### Limitations

- Initial release only supports KDE Plasma 6 Wayland fully. Plasma 5 and
  X11 are not supported (added in `[Unreleased]`).
- UI strings are hardcoded Turkish; i18n is on the roadmap (now done in
  `[Unreleased]`).
- No configuration file — accent color, panel position, etc. are
  compile-time constants. On the roadmap (now done in `[Unreleased]`).

[Unreleased]: https://github.com/furkann-ahmet/4ztexDock/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/furkann-ahmet/4ztexDock/releases/tag/v0.1.0

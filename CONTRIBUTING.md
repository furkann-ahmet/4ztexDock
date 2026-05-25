# Contributing

**Languages:** [Türkçe](CONTRIBUTING.tr.md) · **English**

Welcome — these notes will speed up review of your PR.

## Build

```sh
qmake6 4ztexDock.pro
make -j$(nproc)
```

Dependencies are listed in `README.md`. The build must come up **with zero
warnings**; if a PR introduces a new warning, please fix it.

## Style

- **C++17**, Qt 6 idioms, KDE-friendly. Any class with `Q_OBJECT` must live in
  its own `.h`/`.cpp` pair — `src/main.cpp` does not contain `Q_OBJECT` because
  it isn't passed through `moc`.
- 4-space indent, `snake_case_for_files`, `CamelCase_for_classes`, `member_`
  suffix.
- Comments are in Turkish (the project language). English is also fine —
  consistency will be normalized in a later i18n pass.
- Do not add inline `setStyleSheet` calls; bind to `style/dock.qss` via
  `objectName` or a class selector.
- New files must carry an SPDX header:

  ```cpp
  // SPDX-License-Identifier: GPL-3.0-or-later
  ```

## Architectural notes

- **Layer-shell + KWin scripting** is the core dependency. Plasma 6 Wayland is
  the primary target; Plasma 5 and X11 paths are implemented as runtime
  alternatives.
- Popup panels inherit from `GlassPopup` (custom-painted feathered shadow plus
  a tongue arrow).
- The KWin bridge (`src/kwinbridge.{h,cpp}`) connects to KWin's JavaScript
  scripting API via `Q_SCRIPTABLE` methods. When adding a new event from KWin,
  you'll need to update the JS bridge, the slot, and the signal — all three.
- The notification server (`src/notificationserver.{h,cpp}`) is a full
  implementation of the FDO Notifications spec — it claims
  `org.freedesktop.Notifications` itself.
- Icon resolution is global: `Icons::resolve(QStringList hints, fallback)` —
  direct theme lookup → sycoca lookup → token-fuzzy `.desktop` match (via
  `DesktopIconResolver`).

## Pull request checklist

- [ ] Builds with zero warnings (`make -j$(nproc) 2>&1 | grep warning` is empty)
- [ ] `--help` and `--version` still work
- [ ] If you added a new class with `Q_OBJECT`, it has its own `.h`/`.cpp` and
      both are added to `HEADERS`/`SOURCES` in `4ztexDock.pro`
- [ ] No new inline `setStyleSheet` (use QSS rules instead)
- [ ] Describe how you manually tested the change in the PR body
- [ ] Added a one- or two-line summary under `## [Unreleased]` in
      `CHANGELOG.md`

## Bug report

When opening a bug report, please include:

- `4ztexDock --version`
- `qmake6 -query QT_VERSION` (your system's Qt version)
- `kwin_wayland --version` (or `kwin_x11 --version`)
- `journalctl --user -b 0 | grep -i 4ztex | tail -50`
- Steps to reproduce the issue

## Developer workflow

If you're iterating on the stylesheet, just save `style/dock.qss` while the
dock is running — it reloads automatically.

If you're working on the C++ side, `./dev-watch.sh` will rebuild and restart
the dock whenever a source file changes (requires `entr`).

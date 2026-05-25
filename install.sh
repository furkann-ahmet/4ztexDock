#!/usr/bin/env bash
# 4ztexDock — build-from-source + install script.
#
# On Arch the preferred path is packaging/PKGBUILD via makepkg. This script
# is for non-Arch distros (Debian, Fedora, openSUSE, etc.) and:
#   - checks dependencies
#   - builds with qmake6 + make (translations included)
#   - lays out binary + qss + .desktop + autostart + icons in the right paths
#
# Usage:
#   ./install.sh --user      → install into ~/.local/  (no sudo)
#   ./install.sh --system    → install into /usr/local/ (sudo required)
#   ./install.sh --help

set -euo pipefail

# --------------------------------------------------------------------------
# CLI parsing — accepts multiple flags, order-independent.
# --------------------------------------------------------------------------
MODE="user"
SKIP_BUILD=0
INSTALL_DEPS=0
while [[ $# -gt 0 ]]; do
    case "$1" in
        --user)           MODE="user" ;;
        --system)         MODE="system" ;;
        --no-build)       SKIP_BUILD=1 ;;
        --install-deps)   INSTALL_DEPS=1 ;;
        -h|--help)
            cat <<EOF
Usage: $0 [--user|--system] [--install-deps] [--no-build]

  --user           (default) install into ~/.local, no sudo required
  --system         install into /usr/local, run with sudo
  --install-deps   detect the distro and install build + runtime
                   dependencies via apt/dnf/zypper/pacman (sudo required).
                   Supported: Debian/Ubuntu, Fedora, openSUSE, Arch.
  --no-build       skip the build step (when the binary is already present)

Typical flow:
  sudo ./install.sh --install-deps --system   # everything in one go
  or
  sudo apt install qt6-base-dev ...           # deps via your package manager
  ./install.sh --user                         # then build + install

Order: (optional) install deps → build (qmake6 + make) → place files.
If the dock is already running it's automatically restarted.
EOF
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            echo "Help: $0 --help" >&2
            exit 2
            ;;
    esac
    shift
done

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SRC_DIR"

# Colored output
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
RED='\033[0;31m'
RESET='\033[0m'
info()  { echo -e "${GREEN}==>${RESET} $*"; }
warn()  { echo -e "${YELLOW}==>${RESET} $*"; }
error() { echo -e "${RED}==>${RESET} $*" >&2; }

# --------------------------------------------------------------------------
# Prefix
# --------------------------------------------------------------------------
if [[ "$MODE" == "system" ]]; then
    PREFIX="/usr/local"
    if [[ $EUID -ne 0 ]]; then
        info "--system requires sudo; re-run with sudo:"
        echo "  sudo $0 --system"
        exit 1
    fi
    SUDO=""
else
    PREFIX="$HOME/.local"
    SUDO=""
fi
info "Install mode: $MODE (prefix=$PREFIX)"

# --------------------------------------------------------------------------
# Distro detection + (optional) auto-install deps
# --------------------------------------------------------------------------

# Reads /etc/os-release ID: arch, debian, ubuntu, fedora, opensuse-*, ...
detect_distro() {
    if [[ -r /etc/os-release ]]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        echo "${ID:-unknown}"
    else
        echo "unknown"
    fi
}

# Returns the package-manager command (apt/dnf/zypper/pacman) + package
# names appropriate for the given distro. Writes "PKGMGR pkg1 pkg2 ...".
deps_command_for() {
    local distro="$1"
    case "$distro" in
        arch|cachyos|endeavouros|manjaro|arcolinux)
            echo "pacman -S --needed --noconfirm" \
                 "qt6-base" "qt6-wayland" "qt6-tools" \
                 "layer-shell-qt" \
                 "libxcb" "xcb-util-wm" \
                 "networkmanager" "pipewire-pulse" \
                 "gcc" "make" "pkgconf"
            ;;
        debian|ubuntu|pop|linuxmint|elementary|kali|raspbian)
            echo "apt-get install -y --no-install-recommends" \
                 "qmake6" "qt6-base-dev" "qt6-wayland-dev" "qt6-l10n-tools" \
                 "layer-shell-qt-dev" \
                 "libxkbcommon-dev" "libwayland-dev" \
                 "libxcb1-dev" "libxcb-ewmh-dev" \
                 "network-manager" "pipewire-pulse" \
                 "build-essential" "pkg-config"
            ;;
        fedora|rhel|centos|rocky|almalinux|nobara)
            echo "dnf install -y" \
                 "qt6-qtbase-devel" "qt6-qtwayland-devel" "qt6-qttools-devel" \
                 "layer-shell-qt-devel" \
                 "libxkbcommon-devel" "wayland-devel" \
                 "libxcb-devel" "xcb-util-wm-devel" \
                 "NetworkManager" "pipewire-pulseaudio" \
                 "gcc-c++" "make" "pkgconf"
            ;;
        opensuse-tumbleweed|opensuse-leap|opensuse|suse)
            echo "zypper install -y" \
                 "qt6-base-devel" "qt6-wayland-devel" "qt6-tools-linguist" \
                 "layer-shell-qt-devel" \
                 "libxkbcommon-devel" "wayland-devel" \
                 "libxcb-devel" "xcb-util-wm-devel" \
                 "NetworkManager" "pipewire-pulseaudio" \
                 "gcc-c++" "make" "pkgconf"
            ;;
        *)
            echo ""  # unknown
            ;;
    esac
}

if [[ $INSTALL_DEPS -eq 1 ]]; then
    if [[ $EUID -ne 0 ]] && [[ "$MODE" != "system" ]]; then
        # --install-deps uses the system package manager; sudo required
        error "--install-deps requires sudo. Use it like:"
        echo "  sudo $0 --install-deps [--user|--system]"
        exit 1
    fi

    DISTRO=$(detect_distro)
    info "Detected distro: $DISTRO"
    DEPS_CMD=$(deps_command_for "$DISTRO")
    if [[ -z "$DEPS_CMD" ]]; then
        error "Unsupported distro: $DISTRO"
        echo "Install the dependencies manually — see the per-distro section in README.md."
        exit 5
    fi
    info "Installing dependencies:"
    echo "  $DEPS_CMD"
    # shellcheck disable=SC2086
    $DEPS_CMD
    info "Dependency installation complete."
fi

# --------------------------------------------------------------------------
# Dependency check
# --------------------------------------------------------------------------
check_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        error "Missing dependency: $1"
        local distro
        distro=$(detect_distro)
        local cmd
        cmd=$(deps_command_for "$distro")
        if [[ -n "$cmd" ]]; then
            echo "To install dependencies on $distro:"
            echo "  sudo $cmd"
            echo ""
            echo "Or: $0 --install-deps --system"
        else
            echo "Install packages manually for your distro (list in README)."
        fi
        exit 3
    fi
}

LRELEASE=""
if [[ $SKIP_BUILD -eq 0 ]]; then
    info "Checking dependencies..."
    check_cmd qmake6
    check_cmd make
    check_cmd g++
    # lrelease tool name varies by distro: lrelease6 / lrelease-qt6 / lrelease
    for tool in lrelease6 lrelease-qt6 lrelease; do
        if command -v "$tool" >/dev/null 2>&1; then
            LRELEASE="$tool"
            break
        fi
    done
    if [[ -z "$LRELEASE" ]]; then
        warn "lrelease (Qt6) not found — i18n .qm files won't be compiled."
        warn "Translations will be missing but the dock will still run in its source language (Turkish)."
    fi
fi

# Runtime tools (warn-only)
for tool in pactl nmcli; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        warn "Runtime tool '$tool' not found — its panel will be disabled."
    fi
done

# --------------------------------------------------------------------------
# Build
# --------------------------------------------------------------------------
if [[ $SKIP_BUILD -eq 0 ]]; then
    info "Compiling translation .qm files..."
    if [[ -n "$LRELEASE" ]]; then
        "$LRELEASE" translations/4ztexDock_tr.ts translations/4ztexDock_en.ts
    fi

    info "qmake6 + make..."
    qmake6 4ztexDock.pro
    make -j"$(nproc)"
fi

if [[ ! -f "$SRC_DIR/4ztexDock" ]]; then
    error "Binary '4ztexDock' not found. If you used --no-build, build manually first."
    exit 4
fi

# --------------------------------------------------------------------------
# Install paths
# --------------------------------------------------------------------------
BIN_DIR="$PREFIX/bin"
SHARE_DIR="$PREFIX/share/4ztexDock"
ICON_DIR="$PREFIX/share/icons/hicolor/scalable/apps"
DESKTOP_DIR="$PREFIX/share/applications"
LICENSE_DIR="$PREFIX/share/licenses/4ztexdock"
DOC_DIR="$PREFIX/share/doc/4ztexdock"

# Autostart location: /etc/xdg/autostart under --system, ~/.config/autostart under --user
if [[ "$MODE" == "system" ]]; then
    AUTOSTART_DIR="/etc/xdg/autostart"
else
    AUTOSTART_DIR="$HOME/.config/autostart"
fi

# --------------------------------------------------------------------------
# Stage everything
# --------------------------------------------------------------------------
install_file() {
    local mode=$1 src=$2 dest=$3
    $SUDO install -Dm"$mode" "$src" "$dest"
}

info "Binary → $BIN_DIR/4ztexDock"
install_file 755 "$SRC_DIR/4ztexDock" "$BIN_DIR/4ztexDock"

info "Stylesheet → $SHARE_DIR/style/dock.qss"
install_file 644 "$SRC_DIR/style/dock.qss" "$SHARE_DIR/style/dock.qss"

info "Icons → $BIN_DIR/icons/"
for f in "$SRC_DIR"/icons/*; do
    [[ -f "$f" ]] || continue
    install_file 644 "$f" "$BIN_DIR/icons/$(basename "$f")"
done

if [[ -f "$SRC_DIR/icons/4ztex-icon.svg" ]]; then
    info "App icon (hicolor) → $ICON_DIR/4ztex-icon.svg"
    install_file 644 "$SRC_DIR/icons/4ztex-icon.svg" \
        "$ICON_DIR/4ztex-icon.svg"
fi

# Build the .desktop from the template, substituting @PREFIX@
TMP_DESKTOP="$(mktemp --suffix=.desktop)"
sed "s|@PREFIX@|$PREFIX|g" "$SRC_DIR/packaging/4ztexDock.desktop.in" \
    > "$TMP_DESKTOP"

info "Desktop entry → $DESKTOP_DIR/com.4ztex.dock.desktop"
install_file 644 "$TMP_DESKTOP" "$DESKTOP_DIR/com.4ztex.dock.desktop"

info "Autostart entry → $AUTOSTART_DIR/com.4ztex.dock.desktop"
install_file 644 "$TMP_DESKTOP" "$AUTOSTART_DIR/com.4ztex.dock.desktop"

rm -f "$TMP_DESKTOP"

info "License → $LICENSE_DIR/LICENSE"
install_file 644 "$SRC_DIR/LICENSE" "$LICENSE_DIR/LICENSE"

if [[ -f "$SRC_DIR/packaging/config.ini.example" ]]; then
    info "Sample config → $DOC_DIR/config.ini.example"
    install_file 644 "$SRC_DIR/packaging/config.ini.example" \
        "$DOC_DIR/config.ini.example"
fi

# --------------------------------------------------------------------------
# Refresh KDE caches
# --------------------------------------------------------------------------
info "Refreshing KDE service cache..."
for cmd in kbuildsycoca6 kbuildsycoca5; do
    if command -v "$cmd" >/dev/null 2>&1; then
        "$cmd" --noincremental 2>/dev/null || true
        break
    fi
done
if command -v update-desktop-database >/dev/null 2>&1; then
    $SUDO update-desktop-database -q "$DESKTOP_DIR" 2>/dev/null || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    $SUDO gtk-update-icon-cache -q -t -f "$PREFIX/share/icons/hicolor" 2>/dev/null || true
fi

# --------------------------------------------------------------------------
# PATH check (user mode)
# --------------------------------------------------------------------------
if [[ "$MODE" == "user" ]]; then
    if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then
        warn "$BIN_DIR is not in PATH. Add this to your shell rc:"
        echo "    export PATH=\"\$HOME/.local/bin:\$PATH\""
    fi
fi

# --------------------------------------------------------------------------
# Restart running instance
# --------------------------------------------------------------------------
if pgrep -x 4ztexDock >/dev/null 2>&1; then
    info "Restarting running 4ztexDock instance..."
    pkill -x 4ztexDock || true
    sleep 0.3
    nohup "$BIN_DIR/4ztexDock" >/dev/null 2>&1 &
    disown
fi

# --------------------------------------------------------------------------
# Done
# --------------------------------------------------------------------------
echo ""
info "Installation complete ✓"
echo ""
cat <<EOF
Next steps:

  1. Hide Plasma's default panel (right-click → Remove Panel).
  2. Log OUT of Plasma → log back IN.
     The XDG autostart entry will be launched by Plasma in the right systemd
     scope → KWin's ScreenShot2 caller cache refreshes → window preview
     thumbnails start working.

  3. Manual start:    $BIN_DIR/4ztexDock &

  4. Config file:     ~/.config/4ztexDock/config.ini
     (sample at:      $DOC_DIR/config.ini.example)

  5. CLI:
     $BIN_DIR/4ztexDock --help
     $BIN_DIR/4ztexDock --version
EOF

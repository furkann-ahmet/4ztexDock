#!/usr/bin/env bash
# 4ztexDock — kaynaktan derleme + kurulum scripti.
#
# Arch Linux için tavsiye edilen yol packaging/PKGBUILD ile makepkg.
# Bu script Arch dışı distrolar için (Debian, Fedora, openSUSE, vs.):
#   - bağımlılıkları kontrol et
#   - qmake6 + make ile derle (translations dahil)
#   - binary + qss + .desktop + autostart + icon'u doğru yere yerleştir
#
# Kullanım:
#   ./install.sh --user      → ~/.local/ altına kurar (sudo yok)
#   ./install.sh --system    → /usr/local/ altına kurar (sudo gerekir)
#   ./install.sh --help

set -euo pipefail

# --------------------------------------------------------------------------
# CLI parsing — birden fazla flag kabul eder, sırasız.
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
Kullanım: $0 [--user|--system] [--install-deps] [--no-build]

  --user           (varsayılan) ~/.local altına kurar, sudo gerektirmez
  --system         /usr/local altına kurar, sudo ile çalıştır
  --install-deps   Distro'yu tespit edip build + runtime bağımlılıklarını
                   apt/dnf/zypper/pacman ile kurar (sudo gerekir).
                   Desteklenen: Debian/Ubuntu, Fedora, openSUSE, Arch.
  --no-build       Derleme adımını atla (binary repo'da hazır olduğunda)

Tipik akış:
  ./install.sh --install-deps --system   # tek seferde her şey
  veya
  sudo apt install qt6-base-dev ...      # kendi dist komutunla deps
  ./install.sh --user                    # sonra build+kurulum

Önce (varsa) deps kurulur → build (qmake6 + make) → dosyalar yerleştirilir.
Çalışan dock varsa otomatik restart edilir.
EOF
            exit 0
            ;;
        *)
            echo "Bilinmeyen argüman: $1" >&2
            echo "Yardım: $0 --help" >&2
            exit 2
            ;;
    esac
    shift
done

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SRC_DIR"

# Renkli output
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
        info "--system için sudo gerekir; sudo ile yeniden çalıştır:"
        echo "  sudo $0 --system"
        exit 1
    fi
    SUDO=""
else
    PREFIX="$HOME/.local"
    SUDO=""
fi
info "Kurulum modu: $MODE (prefix=$PREFIX)"

# --------------------------------------------------------------------------
# Distro detection + (opsiyonel) auto-install deps
# --------------------------------------------------------------------------

# /etc/os-release ID'sine bakar: arch, debian, ubuntu, fedora, opensuse-*, ...
detect_distro() {
    if [[ -r /etc/os-release ]]; then
        # shellcheck disable=SC1091
        . /etc/os-release
        echo "${ID:-unknown}"
    else
        echo "unknown"
    fi
}

# Distro'ya göre apt/dnf/zypper/pacman komutunu döndür + paket isimleri.
# Çıktı: stdout'a "PKGMGR pkg1 pkg2 pkg3 ..." formatında yazar.
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
            echo ""  # bilinmiyor
            ;;
    esac
}

if [[ $INSTALL_DEPS -eq 1 ]]; then
    if [[ $EUID -ne 0 ]] && [[ "$MODE" != "system" ]]; then
        # --install-deps system pkg manager kullanır, sudo şart
        error "--install-deps sudo gerektirir. Şöyle kullan:"
        echo "  sudo $0 --install-deps [--user|--system]"
        exit 1
    fi

    DISTRO=$(detect_distro)
    info "Distro tespit edildi: $DISTRO"
    DEPS_CMD=$(deps_command_for "$DISTRO")
    if [[ -z "$DEPS_CMD" ]]; then
        error "Bilinmeyen distro: $DISTRO"
        echo "Bağımlılıkları elle kur, README.md'deki distro bölümüne bak."
        exit 5
    fi
    info "Bağımlılıklar kuruluyor:"
    echo "  $DEPS_CMD"
    # shellcheck disable=SC2086
    $DEPS_CMD
    info "Bağımlılık kurulumu tamamlandı."
fi

# --------------------------------------------------------------------------
# Dependency check
# --------------------------------------------------------------------------
check_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        error "Eksik bağımlılık: $1"
        local distro
        distro=$(detect_distro)
        local cmd
        cmd=$(deps_command_for "$distro")
        if [[ -n "$cmd" ]]; then
            echo "Bağımlılıkları kurmak için ($distro):"
            echo "  sudo $cmd"
            echo ""
            echo "Veya: $0 --install-deps --system"
        else
            echo "Distro'na göre paketleri elle kur (README'de liste var)."
        fi
        exit 3
    fi
}

LRELEASE=""
if [[ $SKIP_BUILD -eq 0 ]]; then
    info "Bağımlılıklar kontrol ediliyor..."
    check_cmd qmake6
    check_cmd make
    check_cmd g++
    # lrelease tool adı dağıtıma göre değişiyor: lrelease6, lrelease-qt6, lrelease
    for tool in lrelease6 lrelease-qt6 lrelease; do
        if command -v "$tool" >/dev/null 2>&1; then
            LRELEASE="$tool"
            break
        fi
    done
    if [[ -z "$LRELEASE" ]]; then
        warn "lrelease (Qt6) bulunamadı — i18n .qm dosyaları derlenmeyecek."
        warn "Çeviriler eksik kalır ama dock yine de Türkçe (source) ile çalışır."
    fi
fi

# Runtime tool'lar (warn-only)
for tool in pactl nmcli; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        warn "Runtime tool '$tool' bulunamadı — ilgili panel devre dışı kalacak."
    fi
done

# --------------------------------------------------------------------------
# Build
# --------------------------------------------------------------------------
if [[ $SKIP_BUILD -eq 0 ]]; then
    info "Çeviri .qm dosyaları derleniyor..."
    if [[ -n "$LRELEASE" ]]; then
        "$LRELEASE" translations/4ztexDock_tr.ts translations/4ztexDock_en.ts
    fi

    info "qmake6 + make..."
    qmake6 4ztexDock.pro
    make -j"$(nproc)"
fi

if [[ ! -f "$SRC_DIR/4ztexDock" ]]; then
    error "Binary '4ztexDock' bulunamadı. --no-build kullandıysan önce manuel derle."
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

# Autostart konumu: --system'de /etc/xdg/autostart, --user'da ~/.config/autostart
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

# .desktop'ı PREFIX'e göre template'ten oluştur
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
    info "Config örneği → $DOC_DIR/config.ini.example"
    install_file 644 "$SRC_DIR/packaging/config.ini.example" \
        "$DOC_DIR/config.ini.example"
fi

# --------------------------------------------------------------------------
# Refresh KDE caches
# --------------------------------------------------------------------------
info "KDE service cache yenileniyor..."
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
        warn "$BIN_DIR PATH'te değil. Shell rc dosyana ekle:"
        echo "    export PATH=\"\$HOME/.local/bin:\$PATH\""
    fi
fi

# --------------------------------------------------------------------------
# Restart running instance
# --------------------------------------------------------------------------
if pgrep -x 4ztexDock >/dev/null 2>&1; then
    info "Çalışan 4ztexDock instance'ı restart ediliyor..."
    pkill -x 4ztexDock || true
    sleep 0.3
    nohup "$BIN_DIR/4ztexDock" >/dev/null 2>&1 &
    disown
fi

# --------------------------------------------------------------------------
# Done
# --------------------------------------------------------------------------
echo ""
info "Kurulum tamamlandı ✓"
echo ""
cat <<EOF
Sonraki adımlar:

  1. Plasma'nın default panel'ini gizle (sağ tık → Remove Panel).
  2. Plasma'dan ÇIKIŞ → tekrar GİRİŞ yap.
     XDG autostart entry'si Plasma launcher tarafından doğru systemd scope'ta
     başlatılır → KWin ScreenShot2 caller cache'i tazelenir → pencere
     preview thumbnail'leri çalışır.

  3. Manuel başlatma:  $BIN_DIR/4ztexDock &

  4. Config dosyası:   ~/.config/4ztexDock/config.ini
     (örnek: $DOC_DIR/config.ini.example)

  5. CLI:
     $BIN_DIR/4ztexDock --help
     $BIN_DIR/4ztexDock --version
EOF

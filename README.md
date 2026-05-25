# 4ztexDock

> KDE Plasma 5 & 6 için Wayland layer-shell taskbar / launcher / dock.

**Diller:** **Türkçe** · [English](README.en.md)

4ztexDock, KDE Plasma için custom bir dock'tur. Plasma'nın default panel'inin
yerine geçer; pinned launcher'lar, açık pencere task list'i, audio / network /
notifications popup panelleri, system tray, saat ve global launcher menüsünü
tek bir layer-shell window'da toplar.

![dock hero](screenshots/dock.png)

> 🌀 **Vibecoded**: bu proje büyük ölçüde bir LLM (Claude) ile vibecoding
> akışında geliştirildi — mimari kararlar, panel paneline implementasyon ve
> sayısız iterasyon AI eşliğinde yapıldı. Kod incelenmiş ve test edilmiş olsa
> da, "deneysel ürün" gözüyle bak. Bug bulursan ve fix önerisi getirirsen
> seve seve merge ederim.

---

## Özellikler

**Launcher menü** — frequent app grid (LaunchTracker ile usage-weighted), arama
input'u, klavye navigasyonu (↑/↓/Enter/Esc), folder ve session shortcut pill'leri,
ayarlar tile'ı. **Meta** tuşu ile global olarak açılır (KGlobalAccel).

![launcher](screenshots/dock_launcher.png)

**Audio paneli** — iki tab: çıkış/giriş cihazları ve sesli uygulamalar.
PipeWire/PulseAudio `pactl` üzerinden. Anonim sink-input'lar için client level
metadata (flatpak app_id, PID) lookup yapılır → Spotify, Discord, Web Audio gibi
metadata'sız stream'ler de doğru isim + ikonla görünür.

![audio devices](screenshots/dock_audio_devices.png)
![audio apps](screenshots/dock_audio_apps.png)

**Network paneli** — bağlantı türüne göre dinamik kart (Ethernet / WiFi / VPN),
brightness slider (Solid PowerManagement DBus), VPN listesi (nmcli), action tile
grid'i (lock screen, ekran görüntüsü, vs.).

![network](screenshots/dock_network.png)

**Notification server** — `org.freedesktop.Notifications` DBus name'ini doğrudan
kayıt eder; Slack, Discord, libnotify her şey dock'a düşer. Plasma'nın kendi
daemon'u name'i tutuyorsa graceful fallback (politeness check).

![notifications](screenshots/dock_notifications.png)

**Now Playing modülü** — MPRIS player'ları (Spotify, Mozilla, vs.) tek tıkla
kaynak seçimi ve play/pause/skip kontrolleri.

![player source switcher](screenshots/dock_player_source.png)

**System tray** — `org.kde.StatusNotifierWatcher` üzerinden tüm tray icon'ları
host eder; sığmayanlar overflow popup'ında. Sağ tık context menüleri çalışır.

![tray overflow](screenshots/dock_tray.png)

**Diğerleri**: CPU/RAM/network throughput widget, KWin task list (workspace
filter), drag-to-reorder task button'lar, button pulse cue (minimize feedback).

---

## Compositor uyumluluğu

| Ortam                      | Durum       | Not                                                                                    |
| -------------------------- | ----------- | -------------------------------------------------------------------------------------- |
| KDE Plasma 6 (Wayland)     | ✅ Tam      | Birincil hedef. Layer-shell + KWin scripting + ScreenShot2.                            |
| KDE Plasma 6 (X11)         | ✅ Tam †    | `_NET_WM_WINDOW_TYPE_DOCK` + `_NET_WM_STRUT_PARTIAL` ile dock.                         |
| KDE Plasma 5 (X11/Wayland) | ✅ Tam * †  | *Pencere preview thumbnail'ları yok (Plasma 5'te ScreenShot2 DBus API'si yok); ikon+başlık fallback'i. |
| GNOME / Hyprland / sway    | ⚠️ Kısmi   | Audio + network + notification çalışır; task list çalışmaz (KWin yok).                 |

> **† Gerçek sistemde test edilmedi.** Plasma 6 X11 ve Plasma 5 dalları kodda
> implemente edildi ve derlenip kontrol edildi, ancak maintainer'ın elinde
> Plasma 6 Wayland dışında bir kurulum yok. Bu ortamlarda kullanıyorsan bug
> raporu çok hoş karşılanır — `CONTRIBUTING.md`'de "Bug report" bölümü.

Runtime'da otomatik tespit eder: Wayland session'da layer-shell, X11 session'da
EWMH dock window setup. Plasma 5 ya da 6, KWin scripting JS dual-API ile
otomatik. KWin DBus servisi yoksa erken çıkar.

---

## Kurulum

### Gereksinimler

Sistem kurulumlu olmalı:

- **KDE Plasma 6** (önerilen) veya **KDE Plasma 5** (test edilmedi, riskli)
- **Wayland** veya **X11** session
- **Qt 6.6+** (build için private header'lar; daha eski sürümlerde fail edebilir)
- **PipeWire/PulseAudio** (audio panel — eksikse panel devre dışı, dock çalışır)
- **NetworkManager** (network panel — eksikse panel devre dışı, dock çalışır)

---

### Yol 1 — Arch Linux (en kolay, paket yöneticili)

**AUR'dan (kararlı):**

```sh
yay -S 4ztexdock-git
```

Veya manuel:

```sh
git clone https://aur.archlinux.org/4ztexdock-git.git
cd 4ztexdock-git
makepkg -si
```

**Yerel repo'dan (kaynak değişikliği yapıyorsan):**

```sh
git clone https://github.com/furkann-ahmet/4ztexDock.git
cd 4ztexDock/packaging
makepkg -si
```

Paket kuruluyor:

| Konum | Ne |
|---|---|
| `/usr/bin/4ztexDock` | Binary |
| `/usr/share/4ztexDock/style/dock.qss` | Stylesheet |
| `/usr/share/applications/com.4ztex.dock.desktop` | App menüsü entry'si |
| `/etc/xdg/autostart/com.4ztex.dock.desktop` | Plasma login'de auto-launch |
| `/usr/share/icons/hicolor/scalable/apps/4ztex-icon.svg` | Hicolor ikonu |
| `/usr/share/licenses/4ztexdock/LICENSE` | GPL-3.0 |
| `/usr/share/doc/4ztexdock/config.ini.example` | Config örnek |

---

### Yol 2 — Diğer distrolar (`install.sh --install-deps`)

`install.sh` `/etc/os-release` üzerinden distro'yu tespit eder, apt/dnf/zypper/
pacman ile bağımlılıkları kurar, build alır, dosyaları yerleştirir.

```sh
git clone https://github.com/furkann-ahmet/4ztexDock.git
cd 4ztexDock
sudo ./install.sh --install-deps --system
```

Bu tek komut şunları yapar:

1. Distro tespiti (`/etc/os-release` ID alanı)
2. Build + runtime bağımlılıklarını kur (Qt6, layer-shell-qt, xcb-ewmh, NetworkManager, PipeWire, gcc, make)
3. `lrelease6` ile çevirileri derle (`.ts` → `.qm`)
4. `qmake6` + `make -j$(nproc)` ile binary'i derle
5. `/usr/local` altına yerleştir (binary, qss, icons, .desktop, autostart, LICENSE, config örneği)
6. KDE service cache (`kbuildsycoca`) yenile
7. Çalışan dock varsa restart et

**Desteklenen distrolar** (paket adları script içinde):

| Aile | Distrolar |
|---|---|
| Arch | Arch, CachyOS, EndeavourOS, Manjaro, ArcoLinux |
| Debian | Debian, Ubuntu, Pop!_OS, Linux Mint, elementary, Kali, Raspbian |
| Fedora | Fedora, RHEL, CentOS, Rocky, AlmaLinux, Nobara |
| openSUSE | Tumbleweed, Leap |

**Kullanıcı bazlı kurulum** (sudo gerektirmez, deps zaten kurulu olmalı):

```sh
./install.sh --user
# binary  → ~/.local/bin/4ztexDock
# config  → ~/.config/autostart/com.4ztex.dock.desktop
```

Kullanıcı modunda `~/.local/bin` PATH'te değilse script uyarır; shell rc'ne ekle:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

**Yardım:**

```sh
./install.sh --help
```

---

### Yol 3 — Bağımlılıkları elle kur, sonra install.sh

`--install-deps` kullanmak istemiyorsan (örn. spesifik versiyon istiyorsan)
deps'i kendin kur, sonra build/install:

**Debian / Ubuntu:**

```sh
sudo apt update
sudo apt install --no-install-recommends \
    qmake6 qt6-base-dev qt6-wayland-dev qt6-l10n-tools \
    layer-shell-qt-dev libxkbcommon-dev libwayland-dev \
    libxcb1-dev libxcb-ewmh-dev \
    network-manager pipewire-pulse \
    build-essential pkg-config
```

> ⚠️ Ubuntu < 24.04'te `layer-shell-qt-dev` paketi yok.
> `sudo add-apt-repository ppa:kubuntu-ppa/backports` ya da kaynaktan build et.

**Fedora / RHEL / Rocky / AlmaLinux / Nobara:**

```sh
sudo dnf install -y \
    qt6-qtbase-devel qt6-qtwayland-devel qt6-qttools-devel \
    layer-shell-qt-devel libxkbcommon-devel wayland-devel \
    libxcb-devel xcb-util-wm-devel \
    NetworkManager pipewire-pulseaudio \
    gcc-c++ make pkgconf
```

**openSUSE (Tumbleweed/Leap):**

```sh
sudo zypper install -y \
    qt6-base-devel qt6-wayland-devel qt6-tools-linguist \
    layer-shell-qt-devel libxkbcommon-devel wayland-devel \
    libxcb-devel xcb-util-wm-devel \
    NetworkManager pipewire-pulseaudio \
    gcc-c++ make pkgconf
```

Sonra build + install:

```sh
git clone https://github.com/furkann-ahmet/4ztexDock.git
cd 4ztexDock
./install.sh --system        # /usr/local altına (sudo)
# veya
./install.sh --user          # ~/.local altına (no sudo)
```

---

### Yol 4 — Tamamen manuel (script'siz)

`install.sh` çalışmıyor ya da debug için tüm adımları görmek istiyorsan:

```sh
git clone https://github.com/furkann-ahmet/4ztexDock.git
cd 4ztexDock

# 1) Çeviriler
lrelease6 translations/4ztexDock_tr.ts translations/4ztexDock_en.ts
#   (Fedora'da lrelease-qt6, openSUSE'de lrelease6)

# 2) Build
qmake6 4ztexDock.pro
make -j$(nproc)

# 3) Yerleştir (prefix = /usr/local)
PREFIX=/usr/local
sudo install -Dm755 4ztexDock                  "$PREFIX/bin/4ztexDock"
sudo install -Dm644 style/dock.qss             "$PREFIX/share/4ztexDock/style/dock.qss"
sudo install -Dm644 icons/4ztex-icon.svg       "$PREFIX/share/icons/hicolor/scalable/apps/4ztex-icon.svg"
for f in icons/*; do
    [ -f "$f" ] || continue
    sudo install -Dm644 "$f" "$PREFIX/bin/icons/$(basename "$f")"
done

# 4) .desktop + autostart
sed "s|@PREFIX@|$PREFIX|g" packaging/4ztexDock.desktop.in | \
    sudo tee /usr/share/applications/com.4ztex.dock.desktop > /dev/null
sudo cp /usr/share/applications/com.4ztex.dock.desktop \
        /etc/xdg/autostart/com.4ztex.dock.desktop

# 5) License + config örneği
sudo install -Dm644 LICENSE                          "$PREFIX/share/licenses/4ztexdock/LICENSE"
sudo install -Dm644 packaging/config.ini.example     "$PREFIX/share/doc/4ztexdock/config.ini.example"

# 6) KDE cache yenile
sudo kbuildsycoca6 --noincremental    # ya da kbuildsycoca5
sudo update-desktop-database -q /usr/share/applications
```

---

## İlk kurulumdan sonra (KRİTİK ADIMLAR)

Bu adımlar yapılmadan dock tam çalışmaz:

### 1. Plasma'nın default panel'ini gizle

Default panel üzerine sağ tık → **"Remove Panel"** (KDE 6) ya da **"Panel Settings → Remove This Panel"** (KDE 5). Yoksa iki panel üst üste biner görsel çakışma yaratır.

### 2. Logout → Login (zorunlu)

Üç sebep için gerekli:

- **KWin ScreenShot2 caller cache'i**: XDG autostart'tan launch edilen dock, doğru `app-com.4ztex.dock-*.scope` systemd scope'una düşer. Bu olmadan KWin pencere preview thumbnail izni vermez (icon+başlık fallback'i ile çalışır ama deneyim kötü).
- **KGlobalAccel Meta shortcut**: Plasma kicker Meta'yı tutuyorsa rebind gerekir.
- **KApplicationTrader cache**: Yeni `.desktop` entry'sinin pickup edilmesi.

### 3. Meta tuşu Plasma kicker'a bağlıysa düzelt

```sh
kwriteconfig6 --file kglobalshortcutsrc \
    --group plasmashell \
    --key "activate application launcher" \
    "none,none,Activate Application Launcher"
```

Sonra logout/login. (KDE 5 için: `kwriteconfig5`.)

### 4. Kurulum doğrulama

```sh
# Çalışan instance var mı
pgrep -af 4ztexDock

# Manuel başlat (autostart için logout/login bekleme)
/usr/bin/4ztexDock &
# veya kullanıcı modunda
~/.local/bin/4ztexDock &

# CLI çıktıları
4ztexDock --version    # → "4ztexDock 0.1.0"
4ztexDock --help       # → tam help metni

# Notification akışını test
notify-send "selam" "dock notification testi"
# → dock'un bildirim panelinde görünmeli
```

Eğer hâlâ Plasma'nın paneli açıksa, dock'un altında küçük üst üste binme olabilir. Plasma panel'ini kaldır.

### 5. (Opsiyonel) Config dosyası

```sh
mkdir -p ~/.config/4ztexDock
cp /usr/local/share/doc/4ztexdock/config.ini.example ~/.config/4ztexDock/config.ini
$EDITOR ~/.config/4ztexDock/config.ini
# accent rengi, screen, frequent app limit ayarla
pkill -x 4ztexDock; 4ztexDock &; disown
```

---

## Yapılandırma

### CLI

```sh
4ztexDock --help
4ztexDock --version
4ztexDock --screen DP-2     # belirli bir ekran
```

### Config dosyası

Path: `~/.config/4ztexDock/config.ini` (XDG_CONFIG_HOME respected). Şablon
referansı `/usr/share/doc/4ztexdock/config.ini.example` ile kurulur.

```ini
[Display]
screen=DP-3                ; connector name veya manufacturer/model/serial

[Appearance]
accent=167,139,250         ; R,G,B  (violet-400 default)

[Launcher]
frequentLimit=8            ; sık kullanılan grid: 1-16
```

Değişiklik sonrası dock'u restart et: `pkill -x 4ztexDock; /usr/bin/4ztexDock &; disown`.

### Logging kontrolü

`QT_LOGGING_RULES` env var ile kategori-bazlı log on/off:

```sh
# Belirli kategoriyi sustur
QT_LOGGING_RULES="dock.preview=false" 4ztexDock

# Hepsini sustur
QT_LOGGING_RULES="dock.*=false" 4ztexDock

# Debug seviyesini de aç
QT_LOGGING_RULES="dock.*.debug=true" 4ztexDock
```

Kategoriler: `dock.env` (runtime checks), `dock.notif` (notification daemon),
`dock.preview` (KWin ScreenShot2), `dock.security` (input validation).

### Stylesheet

`style/dock.qss` — dev modda runtime'da `QFileSystemWatcher` ile reload edilir.
Kurulu paketin QSS'i `/usr/share/4ztexDock/style/dock.qss` (root, dev iteration
için repo'da düzenle).

---

## Geliştirme

```sh
qmake6 4ztexDock.pro
make -j$(nproc)
./4ztexDock
```

Stylesheet üzerinde çalışıyorsan QSS dosyasını kaydet — otomatik reload edilir
(`QFileSystemWatcher`). C++ değişikliği için yeniden derleme + restart.

`dev-watch.sh` C++ değişikliklerinde otomatik build+restart yapar.

---

## Sorun giderme

**"4ztexDock yalnızca Wayland session altında çalışır"** — KDE login ekranında
session'ı "Plasma (Wayland)" seç.

**"org.kde.KWin DBus servisi bulunamadı"** — KWin Wayland session'da değilsin
ya da KWin henüz hazır değil (autostart'ı `After=plasma-workspace.target` ile
kullan).

**Bildirim panelinde hiç bildirim görünmüyor** — başka bir notification daemon
(Plasma, dunst, mako) `org.freedesktop.Notifications` name'ini tutuyor.
Politeness check'imiz onun önüne geçmez. O daemon'u devre dışı bırak ya da
"dock notifications only" modu istiyorsan o daemon'un kayıt yapmasını engelle.

**Meta tuşu Plasma kicker'ı açıyor** —
`~/.config/kglobalshortcutsrc` dosyasında `[plasmashell][activate application launcher]`
satırını `none,none,Activate Application Launcher` yap, logout/login.

**Pencere preview thumbnail'leri çalışmıyor** — KWin ScreenShot2 caller check'i
fail oldu. `~/.local/share/applications/com.4ztex.dock.desktop` gibi bir kullanıcı
override dosyası varsa sil, sonra `kbuildsycoca6 --noincremental` çalıştır,
logout/login.

---

## Katkı

PR'lar memnuniyetle. [CONTRIBUTING.md](CONTRIBUTING.md) build + style kılavuzu
içerir. Bug report açarken `4ztexDock --version` çıktısını ve session log'unu
(`journalctl --user -b | grep 4ztexDock`) ekle.

---

## Lisans

GPL-3.0-or-later. Detaylar için [LICENSE](LICENSE).

Copyright (C) 2026 4ztex.

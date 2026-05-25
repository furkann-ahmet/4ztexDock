# 4ztexDock

> KDE Plasma 5 & 6 için Wayland layer-shell taskbar / launcher / dock.

[![AUR sürümü](https://img.shields.io/aur/version/4ztexdock-git?label=AUR&color=1793d1)](https://aur.archlinux.org/packages/4ztexdock-git)
[![AUR güncelleme](https://img.shields.io/aur/last-modified/4ztexdock-git?label=g%C3%BCncelleme)](https://aur.archlinux.org/packages/4ztexdock-git)
[![Lisans: GPL-3.0](https://img.shields.io/badge/lisans-GPL--3.0-blue)](LICENSE)
[![CI](https://github.com/furkann-ahmet/4ztexDock/actions/workflows/ci.yml/badge.svg)](https://github.com/furkann-ahmet/4ztexDock/actions/workflows/ci.yml)

**Diller:** **Türkçe** · [English](README.md)

4ztexDock, KDE Plasma'nın varsayılan paneli yerine kullanılmak üzere yazılmış
bir dock. Sabitlediğin uygulamalar, açık pencerelerin listesi, ses / ağ /
bildirim panelleri, sistem tray, saat ve global launcher menüsü — hepsi tek bir
layer-shell penceresinde toplanıyor.

https://github.com/user-attachments/assets/ef9e0e08-ccaf-4f3f-98cd-7d8b5b3b945e

![dock hero](screenshots/dock.png)

> 🌀 **Vibecoded**: bu proje büyük ölçüde bir LLM (Claude) ile vibecoding
> akışında yazıldı. Mimari kararlardan tek tek panel implementasyonlarına
> kadar her şey AI eşliğinde, sayısız iterasyonla şekillendi. Kod elden
> geçirildi ve test edildi ama yine de deneysel bir ürün gözüyle bak. Bug
> bulup düzeltme önerirsen seve seve merge ederim.

---

## Özellikler

**Launcher menü** — En sık başlattığın uygulamaları 4×2 grid'de gösterir
(LaunchTracker kullanım sıklığına göre sıralar). Üstte arama kutusu, ok
tuşları + Enter/Esc ile klavyeden tamamen kontrol edilebilir. Altta klasör
kısayolları ve oturum eylemleri (çıkış, yeniden başlat, kapat). **Meta**
tuşuna basınca her yerden açılır.

![launcher](screenshots/dock_launcher.png)

**Ses paneli** — İki sekmeli: çıkış/giriş cihazları ve sesli uygulamalar.
Arka planda `pactl` kullanır (PipeWire ve PulseAudio için aynı). Anonim
sink-input'lar (`media.name="audio-src"` gibi) için client seviyesinde
metadata çekilir — flatpak app_id ve PID ile birlikte. Bu sayede Spotify,
Discord, tarayıcı Web Audio sekmeleri gibi metadata göndermeyen stream'ler
bile doğru isim ve ikonla görünür.

![audio devices](screenshots/dock_audio_devices.png)
![audio apps](screenshots/dock_audio_apps.png)

**Ağ paneli** — Bağlantı türüne göre kart değişir (Ethernet / Wi-Fi / VPN).
Altında parlaklık slider'ı (KDE'nin Solid PowerManagement DBus arayüzünden),
sistemde tanımlı VPN'lerin listesi (`nmcli` ile) ve eylem tile'ları: ekranı
kilitle, screenshot al, gece modu, ayarlar, Bluetooth.

![network](screenshots/dock_network.png)

**Bildirim sunucusu** — `org.freedesktop.Notifications` DBus adını doğrudan
dock üstüne alır. Slack, Discord, `notify-send`, libnotify — hangi uygulamadan
gelirse gelsin tüm bildirimler doğrudan dock'un bildirim paneline düşer.
Sistemde başka bir notification daemon (Plasma, dunst, mako) zaten adı
tutuyorsa dock kendini geri çeker (politeness check), yani kimseyle çakışmaz.

![notifications](screenshots/dock_notifications.png)

**Now Playing modülü** — Aktif MPRIS player'larından (Spotify, Mozilla,
VLC...) hangisini kontrol edeceğini tek tıkla seçtirir. Play/pause/skip
butonları doğrudan modülde.

![player source switcher](screenshots/dock_player_source.png)

**Sistem tray** — `org.kde.StatusNotifierWatcher` host'u olarak çalışır;
sistemdeki tüm tray ikonları dock'ta gözükür. Sığmayanlar overflow popup'ına
düşer, sağ tık menüleri her ikisinde de çalışır.

![tray overflow](screenshots/dock_tray.png)

**Diğer**: CPU/RAM kullanımı + ağ throughput widget'ı, KWin'den çekilen
açık pencere listesi (workspace filtreli), task button'larını drag-drop ile
yeniden sırala, pencere minimize edilince button'da pulse animasyonu.

---

## Hangi ortamlarda çalışır?

| Ortam                      | Durum       | Not                                                                                    |
| -------------------------- | ----------- | -------------------------------------------------------------------------------------- |
| KDE Plasma 6 (Wayland)     | ✅ Tam      | Birincil hedef. Layer-shell + KWin scripting + ScreenShot2.                            |
| KDE Plasma 6 (X11)         | ✅ Tam †    | `_NET_WM_WINDOW_TYPE_DOCK` + `_NET_WM_STRUT_PARTIAL` ile dock window.                  |
| KDE Plasma 5 (X11/Wayland) | ✅ Tam * †  | *Pencere preview thumbnail'ı yok (Plasma 5'te ScreenShot2 DBus API'si yok); ikon+başlık fallback'i ile çalışır. |
| GNOME / Hyprland / sway    | ⚠️ Kısmi   | Ses + ağ + bildirim panelleri çalışır; task list ve workspace switcher çalışmaz (KWin yok).            |

> **† Gerçek sistemde test edilmedi.** Plasma 6 X11 ve Plasma 5 yolları kod
> tarafında implemente edildi, derleme + kod incelemesi yapıldı; ancak
> maintainer'ın elinde sadece Plasma 6 Wayland kurulumu var. Bu ortamlarda
> dock'u çalıştırıp denersen sorun raporu çok değerli — `CONTRIBUTING.md`'de
> "Bug report" bölümüne bak.

Çalıştığında session tipini ve Plasma sürümünü otomatik algılar: Wayland'da
layer-shell protokolü, X11'de EWMH dock window setup'ı; Plasma 5 ya da 6,
KWin scripting'in JS API farkları runtime'da dual-API ile halledilir. KWin'in
DBus servisi hiç yoksa erken çıkar.

---

## Kurulum

### Gereksinimler

Sisteminde olması gerekenler:

- **KDE Plasma 6** (önerilen) ya da **Plasma 5** (test edilmedi, riskli)
- **Wayland** veya **X11** oturumu
- **Qt 6.6 ve üstü** — build sırasında private header kullanıyoruz, daha eski sürümlerde derleme hata verebilir
- **PipeWire ya da PulseAudio** — ses paneli için; eksikse panel devre dışı kalır, dock yine açılır
- **NetworkManager** — ağ paneli için; eksikse panel devre dışı kalır, dock yine açılır

---

### Yol 1 — Arch Linux (en kolay, paket yöneticisi üzerinden)

**AUR'dan:**

```sh
yay -S 4ztexdock-git
```

Ya da manuel:

```sh
git clone https://aur.archlinux.org/4ztexdock-git.git
cd 4ztexdock-git
makepkg -si
```

**Kaynak repo'dan (geliştirme yapıyorsan):**

```sh
git clone https://github.com/furkann-ahmet/4ztexDock.git
cd 4ztexDock/packaging
makepkg -si
```

Paket şu dosyaları yerleştirir:

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

`install.sh` script'i `/etc/os-release`'ten distro'yu tanıyor, ardından
apt / dnf / zypper / pacman üzerinden gerekli paketleri kuruyor, build
alıyor ve dosyaları yerleştiriyor.

```sh
git clone https://github.com/furkann-ahmet/4ztexDock.git
cd 4ztexDock
sudo ./install.sh --install-deps --system
```

Bu tek komut sırasıyla şunları yapıyor:

1. Distro'yu tespit eder (`/etc/os-release` ID alanından)
2. Build + runtime bağımlılıklarını kurar (Qt6, layer-shell-qt, xcb-ewmh, NetworkManager, PipeWire, gcc, make)
3. Çevirileri `.qm`'e derler (`lrelease6 ... .ts → .qm`)
4. `qmake6` + `make -j$(nproc)` ile derler
5. `/usr/local` altına yerleştirir (binary, qss, icon'lar, .desktop, autostart, LICENSE, örnek config)
6. KDE service cache'ini yeniler (`kbuildsycoca`)
7. Dock zaten çalışıyorsa restart eder

**Desteklenen distrolar** (paket adları script içinde sabit):

| Aile | Distrolar |
|---|---|
| Arch | Arch, CachyOS, EndeavourOS, Manjaro, ArcoLinux |
| Debian | Debian, Ubuntu, Pop!_OS, Linux Mint, elementary, Kali, Raspbian |
| Fedora | Fedora, RHEL, CentOS, Rocky, AlmaLinux, Nobara |
| openSUSE | Tumbleweed, Leap |

**Yalnızca kullanıcı için kurulum** (sudo'suz, ama bağımlılıkların önceden kurulu olması gerek):

```sh
./install.sh --user
# binary  → ~/.local/bin/4ztexDock
# config  → ~/.config/autostart/com.4ztex.dock.desktop
```

Kullanıcı modunda `~/.local/bin` PATH'te yoksa script bunu hatırlatır;
shell rc'ye eklemen gerek:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

**Tüm flag'leri görmek için:**

```sh
./install.sh --help
```

---

### Yol 3 — Bağımlılıkları elle kur, sonra install.sh

`--install-deps`'i kullanmak istemiyorsan (örneğin belirli paket sürümlerini
seçmek istiyorsan), bağımlılıkları kendin kur, sonra script'i çalıştır:

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

> ⚠️ Ubuntu 24.04'ten eski sürümlerde `layer-shell-qt-dev` paketi yok.
> `sudo add-apt-repository ppa:kubuntu-ppa/backports` ile backports'tan ya da
> kaynaktan derlemen gerek.

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

Bağımlılıklar tamamsa build + kurulum:

```sh
git clone https://github.com/furkann-ahmet/4ztexDock.git
cd 4ztexDock
./install.sh --system        # /usr/local altına, sudo ile
# ya da
./install.sh --user          # ~/.local altına, sudo'suz
```

---

### Yol 4 — Tamamen elle (script'siz)

`install.sh` herhangi bir sebepten çalışmıyorsa ya da neyi nereye koyduğunu
adım adım görmek istiyorsan:

```sh
git clone https://github.com/furkann-ahmet/4ztexDock.git
cd 4ztexDock

# 1) Çevirileri derle (.ts → .qm)
lrelease6 translations/4ztexDock_tr.ts translations/4ztexDock_en.ts
#   (Fedora'da bu komut "lrelease-qt6", openSUSE'de "lrelease6" diye geçer)

# 2) Build
qmake6 4ztexDock.pro
make -j$(nproc)

# 3) Dosyaları yerleştir (prefix olarak /usr/local seçtik)
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

# 5) Lisans + örnek config
sudo install -Dm644 LICENSE                          "$PREFIX/share/licenses/4ztexdock/LICENSE"
sudo install -Dm644 packaging/config.ini.example     "$PREFIX/share/doc/4ztexdock/config.ini.example"

# 6) KDE service cache'ini yenile
sudo kbuildsycoca6 --noincremental    # Plasma 5'te: kbuildsycoca5
sudo update-desktop-database -q /usr/share/applications
```

---

## Kurulum sonrası (mutlaka yap)

Aşağıdaki adımları atlarsan dock tam fonksiyonel olmaz.

### 1. Plasma'nın varsayılan paneli'ni kaldır

Plasma panelinin üstüne sağ tık → **"Remove Panel"** (Plasma 6) ya da
**"Panel Settings → Remove This Panel"** (Plasma 5). Aksi halde iki panel üst
üste biner, ekranın altında küçük bir kayma görürsün.

### 2. Çıkış → Tekrar giriş (zorunlu)

Üç ayrı sebebi var:

- **KWin'in ScreenShot2 caller cache'i** — Dock XDG autostart'tan başladığında
  Plasma onu doğru systemd scope'una (`app-com.4ztex.dock-*.scope`) yerleştirir.
  KWin pencere preview thumbnail iznini bu scope'a bakarak veriyor. Bu olmazsa
  dock yine açılır ama önizleme yerine ikon + başlık fallback'i kullanılır
  (görsel olarak çok daha sönük).
- **KGlobalAccel Meta kısayolu** — Plasma kicker zaten Meta tuşunu bağladıysa
  serbest bırakman gerekir, aşağıdaki adıma bak.
- **KApplicationTrader cache** — Yeni `.desktop` dosyasının Plasma tarafından
  tanınması için cache'in yenilenmesi gerek.

### 3. Meta tuşu Plasma kicker'a bağlıysa serbest bırak

```sh
kwriteconfig6 --file kglobalshortcutsrc \
    --group plasmashell \
    --key "activate application launcher" \
    "none,none,Activate Application Launcher"
```

Sonra çıkış/giriş. (Plasma 5'tesin `kwriteconfig6` yerine `kwriteconfig5` kullan.)

### 4. Kurulumu doğrula

```sh
# Dock çalışıyor mu?
pgrep -af 4ztexDock

# Manuel başlatma (logout/login beklemeden)
/usr/bin/4ztexDock &
# ya da kullanıcı modunda
~/.local/bin/4ztexDock &

# CLI testleri
4ztexDock --version    # → "4ztexDock 0.1.0"
4ztexDock --help       # → tam yardım metni

# Bildirim akışı testi
notify-send "selam" "bildirim testi"
# → dock'un bildirim panelinde görünmeli
```

Plasma'nın varsayılan paneli hâlâ açıksa altta küçük bir görsel çakışma
olabilir; o panel'i kaldır.

### 5. (İsteğe bağlı) Config dosyası

Dock'un görünümünü ve davranışını özelleştirmek için:

```sh
mkdir -p ~/.config/4ztexDock
cp /usr/local/share/doc/4ztexdock/config.ini.example ~/.config/4ztexDock/config.ini
$EDITOR ~/.config/4ztexDock/config.ini
# accent rengi, ekran, sık kullanılan grid boyutu gibi şeyler ayarlanabilir
pkill -x 4ztexDock; 4ztexDock &; disown
```

---

## Yapılandırma

### Komut satırı

```sh
4ztexDock --help
4ztexDock --version
4ztexDock --screen DP-2     # belirli bir ekranda aç
```

### Config dosyası

Yol: `~/.config/4ztexDock/config.ini` (XDG_CONFIG_HOME'a saygı gösterir).
Örnek şablon `/usr/share/doc/4ztexdock/config.ini.example` olarak kurulur.

```ini
[Display]
screen=DP-3                ; connector adı ya da manufacturer/model/serial

[Appearance]
accent=167,139,250         ; R,G,B  (varsayılan violet-400)

[Launcher]
frequentLimit=8            ; sık kullanılan grid'de kaç uygulama: 1-16
```

Değişiklik yaptıktan sonra dock'u yeniden başlat:
`pkill -x 4ztexDock; /usr/bin/4ztexDock &; disown`.

### Log kontrolü

`QT_LOGGING_RULES` env var'ı ile kategori bazlı log açıp kapatabilirsin:

```sh
# Bir kategoriyi sustur
QT_LOGGING_RULES="dock.preview=false" 4ztexDock

# Tüm dock log'larını sustur
QT_LOGGING_RULES="dock.*=false" 4ztexDock

# Debug seviyesini de aç
QT_LOGGING_RULES="dock.*.debug=true" 4ztexDock
```

Kategoriler: `dock.env` (runtime check'leri), `dock.notif` (bildirim daemon'u),
`dock.preview` (KWin ScreenShot2), `dock.security` (input validation).

### Stylesheet

`style/dock.qss` — geliştirme sırasında `QFileSystemWatcher` ile canlı
yeniden yükleniyor. Kurulu paketteki QSS `/usr/share/4ztexDock/style/dock.qss`
(root'a ait); kendi tarzına göre değiştirmek istiyorsan repo'daki kopyayı
düzenleyip yeniden derle.

---

## Geliştirme

```sh
qmake6 4ztexDock.pro
make -j$(nproc)
./4ztexDock
```

Stylesheet üzerinde çalışıyorsan QSS dosyasını kaydetmek yeterli — dock
otomatik olarak reload eder (`QFileSystemWatcher` üzerinden). C++ tarafında
değişiklik yaptıysan tekrar derleyip restart etmen gerek.

`dev-watch.sh` C++ değişikliklerinde otomatik build + restart yapar (entr
gerektirir).

---

## Sorun giderme

**"4ztexDock yalnızca Wayland veya X11 session altında çalışır"** — Login
ekranında "Plasma" oturumlarından birini seç (Wayland ya da X11).

**"org.kde.KWin DBus servisi bulunamadı"** — Ya KWin henüz başlamamış (Plasma
session'ı tam yüklenmesini bekle) ya da bambaşka bir compositor üzerindesin
(GNOME / Hyprland / sway). KDE Plasma şart.

**Bildirim panelinde hiçbir şey gözükmüyor** — Başka bir bildirim daemon'u
(Plasma'nın kendisi, dunst, mako, vs.) `org.freedesktop.Notifications` adını
zaten almış. Politeness check sayesinde dock o adı çalmaz; eğer bildirimlerin
dock'a düşmesini istiyorsan o daemon'u devre dışı bırakman gerek.

**Meta tuşu Plasma kicker'ını açıyor** — `~/.config/kglobalshortcutsrc`
dosyasında `[plasmashell][activate application launcher]` satırını
`none,none,Activate Application Launcher` olarak güncelle, sonra çıkış/giriş yap.

**Pencere önizleme thumbnail'leri çalışmıyor** — KWin'in ScreenShot2 caller
check'i geçmemiş demektir. `~/.local/share/applications/com.4ztex.dock.desktop`
gibi user-side bir override dosyası varsa onu sil, ardından
`kbuildsycoca6 --noincremental` çalıştır, sonra çıkış/giriş.

---

## Katkı

PR'lara açığım. Build ve kod stili kılavuzu için [CONTRIBUTING.md](CONTRIBUTING.md).
Bug raporu açarken şu üçünü eklersen yardımcı olur: `4ztexDock --version`
çıktısı, `qmake6 -query QT_VERSION`, session log'u
(`journalctl --user -b | grep 4ztexDock`).

---

## Lisans

GPL-3.0-or-later. Detaylar için [LICENSE](LICENSE).

Copyright (C) 2026 4ztex.

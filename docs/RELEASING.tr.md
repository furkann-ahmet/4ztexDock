# Release Workflow

**Diller:** **Türkçe** · [English](RELEASING.md)

4ztexDock release süreci. Yeni versiyon yayınlamak isteyen maintainer için.

## Versiyonlama

[Semantic Versioning](https://semver.org/spec/v2.0.0.html) — `MAJOR.MINOR.PATCH`:

- **MAJOR**: breaking change (DBus API, config format, command-line flags).
- **MINOR**: yeni feature (geriye uyumlu).
- **PATCH**: bug fix, doc/i18n iyileştirme.

0.x.y zaten "henüz stable değil" anlamında — breaking değişiklikleri minor
bump ile yapabilirsin.

---

## Release adımları

### 1. Hazırlık

```sh
# Çalışma branch'i ana branch'le sync
git checkout main
git pull --rebase

# Build temiz + tüm testler pass
make clean
qmake6 4ztexDock.pro && make -j$(nproc)
cd tests && qmake6 tests.pro && make -j$(nproc) && ./run-all.sh
```

Hepsi yeşilse devam.

### 2. CHANGELOG.md güncelle

```md
## [0.2.0] — 2026-MM-DD

### Eklenenler
- yeni feature 1
- yeni feature 2

### Değişiklikler
- ...

### Düzeltilenler
- ...

### Sınırlamalar / Bilinen sorunlar
- ...
```

`[Unreleased]` başlığı altındaki notları yeni release başlığına taşı, yeni
boş `[Unreleased]` ekle. Compare URL'lerini güncelle:

```md
[Unreleased]: https://github.com/furkann-ahmet/4ztexDock/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/furkann-ahmet/4ztexDock/releases/tag/v0.2.0
```

### 3. Versiyon bump

Dört yerde aynı versiyonu güncelle:

- `packaging/PKGBUILD` → `pkgver=0.2.0`
- `src/main.cpp` → `QApplication::setApplicationVersion(QStringLiteral("0.2.0"));`
- `src/notificationserver.cpp` → `version = "0.2.0";`
- `CHANGELOG.md` → yukarıda zaten

> ⚠️ `packaging/aur/PKGBUILD` -git paketi olduğu için pkgver'i `pkgver()`
> fonksiyonu otomatik üretiyor; orada değiştirmeye gerek yok.

### 4. Commit + tag

```sh
git add -A
git commit -m "release: v0.2.0"
git tag -a v0.2.0 -m "v0.2.0"
git push origin main
git push origin v0.2.0
```

### 5. GitHub Release

```sh
gh release create v0.2.0 \
    --title "v0.2.0" \
    --notes-file <(awk '/^## \[0.2.0\]/,/^## \[/{print}' CHANGELOG.md | sed '$d')
```

### 6. AUR push

`-git` paketi otomatik commit'ten build alacaktır; sabit `4ztexdock` AUR
paketi için (henüz yayınlamadıysak ileride):

```sh
# AUR repo'yu clone et (ilk seferlik):
git clone ssh://aur@aur.archlinux.org/4ztexdock-git.git aur-4ztexdock-git
cd aur-4ztexdock-git

# Yeni PKGBUILD'i kopyala
cp ../4ztexDock/packaging/aur/PKGBUILD .
cp ../4ztexDock/packaging/4ztexdock.install .

# .SRCINFO regenerate
makepkg --printsrcinfo > .SRCINFO

# Test build (sandbox'ta clean ortam)
makepkg -si

# Commit + push
git add PKGBUILD .SRCINFO 4ztexdock.install
git commit -m "Update to v0.2.0"
git push
```

### 7. Doğrulama

- GitHub Releases sayfasında v0.2.0 görünüyor mu
- AUR sayfasında pkgver güncel mi
- `yay -Syu 4ztexdock-git` test makinasında çalışıyor mu
- `4ztexDock --version` doğru versiyonu yazıyor mu

---

## Hotfix akışı

Critical bug için minor/patch release:

```sh
git checkout -b hotfix/v0.2.1 v0.2.0
# fix commit'leri...
git checkout main
git merge --no-ff hotfix/v0.2.1
git tag -a v0.2.1 -m "v0.2.1"
git push origin main v0.2.1
```

Sonra normal adımları izle (sadece patch bump'ı yap).

---

## Rollback

Yayınlanmış bir release'i geri almak:

```sh
# GitHub tarafından
gh release delete v0.2.0 --yes
git push origin :refs/tags/v0.2.0

# AUR tarafından
cd aur-4ztexdock-git
git revert HEAD
git push
```

Kullanıcı eski versiyona geri dönmek için: `sudo pacman -U
4ztexdock-git-0.1.0-1-x86_64.pkg.tar.zst` (cache'ten).

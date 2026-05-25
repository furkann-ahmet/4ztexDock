# Release Workflow

**Languages:** [Türkçe](RELEASING.tr.md) · **English**

How to publish a new release of 4ztexDock. Intended for maintainers.

## Versioning

We use [Semantic Versioning](https://semver.org/spec/v2.0.0.html) —
`MAJOR.MINOR.PATCH`:

- **MAJOR**: breaking change (DBus API, config format, CLI flags).
- **MINOR**: new feature (backward-compatible).
- **PATCH**: bug fix, doc / i18n improvement.

`0.x.y` already signals "not stable yet" — you can land breaking changes as
minor bumps during this phase.

---

## Release steps

### 1. Prep

```sh
# Sync the working branch with main
git checkout main
git pull --rebase

# Make sure the build is clean and all tests pass
make clean
qmake6 4ztexDock.pro && make -j$(nproc)
cd tests && qmake6 tests.pro && make -j$(nproc) && ./run-all.sh
```

If everything is green, continue.

### 2. Update CHANGELOG.md (and CHANGELOG.tr.md)

```md
## [0.2.0] — 2026-MM-DD

### Added
- new feature 1
- new feature 2

### Changed
- ...

### Fixed
- ...

### Known limitations
- ...
```

Move the notes under `[Unreleased]` into the new release heading, then
re-add an empty `[Unreleased]` block. Update the compare URLs at the bottom:

```md
[Unreleased]: https://github.com/furkann-ahmet/4ztexDock/compare/v0.2.0...HEAD
[0.2.0]: https://github.com/furkann-ahmet/4ztexDock/releases/tag/v0.2.0
```

Repeat the same change in `CHANGELOG.tr.md`.

### 3. Bump version

Update the version in four places, all to the same value:

- `packaging/PKGBUILD` → `pkgver=0.2.0`
- `src/main.cpp` → `QApplication::setApplicationVersion(QStringLiteral("0.2.0"));`
- `src/notificationserver.cpp` → `version = "0.2.0";`
- `CHANGELOG.md` / `CHANGELOG.tr.md` → already done above

> ⚠️ `packaging/aur/PKGBUILD` is the `-git` package and computes its version
> via `pkgver()`; you don't need to change it.

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

The `-git` package picks up new commits automatically. For the stable
`4ztexdock` AUR package (if/when we publish one):

```sh
# Clone the AUR repo once
git clone ssh://aur@aur.archlinux.org/4ztexdock-git.git aur-4ztexdock-git
cd aur-4ztexdock-git

# Copy the new PKGBUILD
cp ../4ztexDock/packaging/aur/PKGBUILD .
cp ../4ztexDock/packaging/4ztexdock.install .

# Regenerate .SRCINFO
makepkg --printsrcinfo > .SRCINFO

# Test build in a clean sandbox
makepkg -si

# Commit + push
git add PKGBUILD .SRCINFO 4ztexdock.install
git commit -m "Update to v0.2.0"
git push
```

### 7. Verify

- GitHub Releases page shows v0.2.0.
- The AUR page reflects the new `pkgver`.
- `yay -Syu 4ztexdock-git` installs cleanly on a test machine.
- `4ztexDock --version` prints the expected version.

---

## Hotfix flow

For a critical bug that needs a patch release:

```sh
git checkout -b hotfix/v0.2.1 v0.2.0
# … fix commits …
git checkout main
git merge --no-ff hotfix/v0.2.1
git tag -a v0.2.1 -m "v0.2.1"
git push origin main v0.2.1
```

Then follow the normal release steps (just bump the patch component).

---

## Rollback

To withdraw a published release:

```sh
# On GitHub
gh release delete v0.2.0 --yes
git push origin :refs/tags/v0.2.0

# On AUR
cd aur-4ztexdock-git
git revert HEAD
git push
```

Users can roll back manually with `sudo pacman -U
4ztexdock-git-0.1.0-1-x86_64.pkg.tar.zst` from their pacman cache.

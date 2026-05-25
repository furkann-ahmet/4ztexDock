# Katkı Rehberi

4ztexDock'a hoş geldin. Aşağıdaki noktalar PR'ların kabul süresini hızlandırır.

## Build

```sh
qmake6 4ztexDock.pro
make -j$(nproc)
```

Bağımlılıklar `README.md`'de listeli. Derleme **sıfır warning** ile geçmeli;
yeni warning eklenirse PR'da düzeltilmesi beklenir.

## Stil

- **C++17**, Qt 6 idiomları, KDE-friendly. Q_OBJECT içeren her sınıf **ayrı
  `.h`/`.cpp` dosyasında olmalı** — `src/main.cpp` `Q_OBJECT` içermiyor (moc'lanmıyor).
- 4-space indent, snake_case_for_files, CamelCase_for_classes, `member_` suffix.
- Yorumlar Türkçe (proje dili). İstersen İngilizce yazabilirsin, ileride i18n
  pass'inde standardize edilir.
- Inline `setStyleSheet` ekleme; `style/dock.qss`'e selector/objectName ile bağla.
- Yeni dosyalara SPDX header ekle:

  ```cpp
  // SPDX-License-Identifier: GPL-3.0-or-later
  ```

## Mimari notlar

- **Layer-shell + KWin scripting** core bağımlılık. Plasma 6 Wayland hedef.
- Panel popup'ları `GlassPopup` base sınıfından miras alır (custom-painted feathered shadow + tongue).
- KWin bridge (`src/kwinbridge.{h,cpp}`) Q_SCRIPTABLE method'lar ile KWin'in JavaScript script API'sine bağlanır. KWin tarafına yeni event eklerken JS bridge + slot + signal üçlüsünü güncelle.
- Notification server (`src/notificationserver.{h,cpp}`) FDO Notifications spec implementasyonu — `org.freedesktop.Notifications` adını alır.
- İkon resolution global: `Icons::resolve(QStringList hints, fallback)` — direct theme, sycoca lookup, token-fuzzy match (DesktopIconResolver).

## Pull Request kontrol listesi

- [ ] Sıfır warning ile derleniyor (`make -j$(nproc) 2>&1 | grep warning` boş)
- [ ] `--help`, `--version` hâlâ çalışıyor
- [ ] Yeni Q_OBJECT içeren bir sınıf eklediysen ayrı `.h`/`.cpp`'de + `.pro`'ya HEADERS/SOURCES eklendi
- [ ] Yeni inline `setStyleSheet` yok (yerine QSS rule)
- [ ] Test edebileceğin bir komut sat: nasıl manuel test ettiğini PR açıklamasına yaz
- [ ] CHANGELOG.md'ye "Unreleased" başlığı altına 1-2 satır özet ekle

## Bug report

Açıklamada şunları paylaş:

- `4ztexDock --version`
- `qmake6 -query QT_VERSION` (sistem Qt'si)
- `kwin_wayland --version` (compositor)
- `journalctl --user -b 0 | grep -i 4ztex | tail -50`
- Reprodüksiyon adımları

## Geliştirici workflow

Stylesheet üzerinde çalışıyorsan: dock çalışırken `style/dock.qss`'i kaydet
→ otomatik reload.

C++ üzerinde çalışıyorsan: `./dev-watch.sh` (entr ile build+restart watch).

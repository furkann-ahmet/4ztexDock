// SPDX-License-Identifier: GPL-3.0-or-later
// 4ztexDock — Global icon resolver and .desktop entry indexer.
//
// Icons::resolve(hints) — KDE/Plasma'nın icon theme'inden ya da kullanıcının
// disk'indeki .desktop dosyalarındaki Icon= satırlarından çağrı yapan kodun
// verdiği "hint"leri sırayla deneyerek bir QIcon döner. Direct theme lookup
// yetmediğinde DesktopIconResolver token-fuzzy match'i devreye girer (örn.
// KWin app_id="net.shadps4.qtlauncher" için
// "net.shadps4.shadps4-qtlauncher.desktop" eşleşir).

#pragma once

#include <QString>
#include <QStringList>
#include <QIcon>
#include <QList>

namespace Icons {

// KDE'nin kdeglobals dosyasından icon theme adını okur.
// Boş dönerse breeze fallback'i kullanılır.
QString readKdeIconTheme();

// Tek bir hint için lookup chain: file path → fromTheme → lowercase → reverse-DNS
// tail → symbolic-strip → /usr/share/pixmaps/<name>.{svg,png,xpm} → hicolor
// dosya araması. Null QIcon döner bulunmazsa (resolve() gibi generic fallback
// VERMEZ — caller "bulundu mu?" check'i yapabilsin diye).
QIcon trySingle(const QString &name);

// QIcon::setThemeName / setThemeSearchPaths / setFallbackSearchPaths set'leyerek
// hicolor + breeze + flatpak/snap export'larını tarayan global tema konfigi
// yapar. Uygulama startup'ında bir kez çağırın.
void configureGlobalTheme();

// Sıralı hint listesinden ilk uygun QIcon'u döner. Aşamalar:
//   1. Her hint için direct theme lookup (file path da olabilir)
//   2. Aynı hint'ten DesktopIconResolver::findIconName(lower) ile Icon= çek
//   3. Token-fuzzy match: hint'i `.-_ ` ile böl, .desktop key'leriyle intersect
//   4. fallback parametresi
//   5. Hard generic chain: application-x-executable → applications-other → ...
QIcon resolve(const QStringList &hints,
              const QString &fallback = QStringLiteral("application-x-executable"));

inline QIcon resolve(const QString &hint,
                     const QString &fallback = QStringLiteral("application-x-executable"))
{
    return resolve(QStringList{hint}, fallback);
}

} // namespace Icons


// Sistemde kurulu .desktop dosyalarını parse edip key → Entry cache'i tutar.
// Key'ler: StartupWMClass, Name, .desktop dosya adı, Exec'in ilk argümanının
// basename'i — hepsi lowercase.
//
// Lazy + cache'li: ilk çağrıda diski tarar, sonraki çağrılar O(1).
class DesktopIconResolver
{
public:
    // .desktop'taki [Desktop Action xxx] section'ları — Plasma taskbar context
    // menüsündeki "Yeni Pencere" / "Yeni Özel Pencere" gibi jumplist öğeleri.
    struct Action {
        QString name;
        QString exec;
        QString icon;
    };

    struct Entry {
        QString icon;
        QString exec;
        QString name;
        QList<Action> actions;
    };

    // Doğrudan key lookup (lowercase). Boş Entry döner key yoksa.
    static Entry entry(const QString &key);

    // Convenience accessor'lar.
    static QString findIconName(const QString &key) { return entry(key).icon; }
    static QString findExec(const QString &key)     { return entry(key).exec; }
    static QString findName(const QString &key)     { return entry(key).name; }
    static QList<Action> findActions(const QString &key) { return entry(key).actions; }

    // Token-based fuzzy lookup — KWin app_id ile .desktop filename uyuşmadığı
    // durumlar için. App ID'yi `.-_ ` ile böler, cache'teki tüm key'leri aynı
    // şekilde böler, en çok ortak token'a sahip entry'yi döner (en az 2 ortak
    // token şartı; 1-token rastgele match'leri engeller).
    static Entry findByTokens(const QString &appId);

    // Launcher menüsü için: sistemde kurulu, NoDisplay/Hidden olmayan ve
    // Type=Application olan tüm .desktop dosyalarını alfabetik sıralı dön.
    // Lazy + cache'li, ilk çağrıda diski tarar.
    static const QList<Entry> &installedApps();
};

// SPDX-License-Identifier: GPL-3.0-or-later
#include "icons.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>

// ============================================================================
// Icons:: namespace implementation
// ============================================================================

QString Icons::readKdeIconTheme()
{
    QFile f(QDir::homePath() + "/.config/kdeglobals");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return QString();
    bool inIcons = false;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith('[')) {
            inIcons = (line == QLatin1String("[Icons]"));
            continue;
        }
        if (inIcons && line.startsWith(QLatin1String("Theme="))) {
            return line.mid(6).trimmed();
        }
    }
    return QString();
}

void Icons::configureGlobalTheme()
{
    // KDE'nin ayarladığı tema adını manuel olarak Qt'ye söyle (platform
    // plugin'i pickleyememiş olabilir). Boşsa breeze'e düş.
    const QString kdeTheme = readKdeIconTheme();
    if (!kdeTheme.isEmpty()) {
        QIcon::setThemeName(kdeTheme);
    } else if (QIcon::themeName().isEmpty()) {
        QIcon::setThemeName(QStringLiteral("breeze"));
    }
    QIcon::setFallbackThemeName(QStringLiteral("breeze"));

    // Theme search paths: TEMA klasörlerinin kökleri — Qt buradan hicolor,
    // breeze, vs. tema dizinlerini gezer. Flatpak/snap/AppImage gibi dış
    // kaynaklı app'ler icon'larını /var/lib/flatpak/exports/share/icons
    // gibi yerlere export ediyor; Qt default olarak buralara bakmıyor.
    QStringList themePaths = QIcon::themeSearchPaths();
    const QStringList themeExtras{
        QDir::homePath() + "/.local/share/icons",
        QDir::homePath() + "/.local/share/flatpak/exports/share/icons",
        "/var/lib/flatpak/exports/share/icons",
        "/usr/share/icons",
        "/usr/share/pixmaps",
    };
    for (const QString &p : themeExtras) {
        if (!themePaths.contains(p) && QDir(p).exists()) themePaths << p;
    }
    QIcon::setThemeSearchPaths(themePaths);

    // Fallback search paths: tema lookup fail ederse buralarda doğrudan
    // <name>.<ext> dosya araması (subdir gezmez). Yine de pixmaps gibi
    // flat yapıları için faydalı.
    QStringList paths = QIcon::fallbackSearchPaths();
    const QStringList extras{
        QCoreApplication::applicationDirPath() + "/icons",
        QDir::homePath() + "/.local/share/icons",
        "/usr/share/pixmaps",
    };
    for (const QString &p : extras) {
        if (!paths.contains(p) && QDir(p).exists()) paths << p;
    }
    QIcon::setFallbackSearchPaths(paths);
}

QIcon Icons::trySingle(const QString &name)
{
    if (name.isEmpty()) return QIcon();
    // Slash içeren bir name "file path" demek — ya gerçek dosya olmalı, ya da
    // tema lookup'ı denemek anlamsız (Qt 6.4 ile 6.11 burada farklı davranıyor,
    // 6.4 path-like string'i theme name kabul ediyor). Mevcut değilse erken çık.
    if (name.contains('/')) {
        return QFile::exists(name) ? QIcon(name) : QIcon();
    }
    QIcon ic = QIcon::fromTheme(name);
    if (!ic.isNull()) return ic;
    const QString lower = name.toLower();
    if (lower != name) {
        ic = QIcon::fromTheme(lower);
        if (!ic.isNull()) return ic;
    }
    if (name.contains('.')) {
        const QString tail = name.section('.', -1).toLower();
        if (!tail.isEmpty() && tail != lower) {
            ic = QIcon::fromTheme(tail);
            if (!ic.isNull()) return ic;
        }
    }
    if (name.endsWith(QLatin1String("-symbolic"))) {
        ic = QIcon::fromTheme(name.chopped(9));
        if (!ic.isNull()) return ic;
    }
    if (name.contains(' ')) {
        ic = QIcon::fromTheme(QString(lower).replace(' ', '-'));
        if (!ic.isNull()) return ic;
    }
    for (const char *ext : {".svg", ".png", ".xpm"}) {
        const QString p = "/usr/share/pixmaps/" + lower + ext;
        if (QFile::exists(p)) return QIcon(p);
    }
    // hicolor doğrudan dosya araması — fromTheme bazen stale icon-theme.cache
    // veya inheritance pickup hatalarından dolayı bunları kaçırıyor. Flatpak,
    // system ve user-local kök dizinlerinin hicolor'larına da bakılır.
    static const QStringList hicolorRoots{
        QStringLiteral("/usr/share/icons/hicolor"),
        QStringLiteral("/var/lib/flatpak/exports/share/icons/hicolor"),
        QDir::homePath() + "/.local/share/flatpak/exports/share/icons/hicolor",
        QDir::homePath() + "/.local/share/icons/hicolor",
        QStringLiteral("/usr/local/share/icons/hicolor"),
    };
    for (const QString &root : hicolorRoots) {
        for (const char *size : {"scalable", "512x512", "256x256",
                                  "128x128", "96x96", "64x64",
                                  "48x48", "32x32", "22x22"}) {
            for (const char *ext : {".svg", ".png"}) {
                const QString p = root + "/" + size + "/apps/" + lower + ext;
                if (QFile::exists(p)) return QIcon(p);
            }
        }
    }
    return QIcon();
}

QIcon Icons::resolve(const QStringList &hints, const QString &fallback)
{
    for (const QString &raw : hints) {
        if (raw.isEmpty()) continue;
        QIcon hit = trySingle(raw);
        if (!hit.isNull()) return hit;
        // .desktop dosyalarındaki StartupWMClass / Name / binary üzerinden
        // gerçek icon adını çek ve dene.
        const QString resolved =
            DesktopIconResolver::findIconName(raw.toLower());
        if (!resolved.isEmpty()) {
            hit = trySingle(resolved);
            if (!hit.isNull()) return hit;
        }
    }
    // Token-fuzzy match: hint'lerden hiçbiri direct/keyed cache'te yakalamadıysa
    // .desktop key'leriyle token intersection bazlı en yakın eşleşmeyi dene.
    for (const QString &raw : hints) {
        if (raw.isEmpty()) continue;
        const auto e = DesktopIconResolver::findByTokens(raw);
        if (!e.icon.isEmpty()) {
            QIcon hit = trySingle(e.icon);
            if (!hit.isNull()) return hit;
        }
    }
    if (!fallback.isEmpty()) {
        QIcon ic = trySingle(fallback);
        if (!ic.isNull()) return ic;
    }
    // Hard generic fallback chain.
    static const QStringList genericChain{
        QStringLiteral("application-x-executable"),
        QStringLiteral("applications-other"),
        QStringLiteral("system-run"),
        QStringLiteral("package-x-generic"),
        QStringLiteral("unknown"),
    };
    for (const QString &g : genericChain) {
        QIcon ic = QIcon::fromTheme(g);
        if (!ic.isNull() && !ic.availableSizes().isEmpty()) return ic;
    }
    return QIcon();
}


// ============================================================================
// DesktopIconResolver
// ============================================================================

namespace {

using Action = DesktopIconResolver::Action;
using Entry  = DesktopIconResolver::Entry;

// Tek .desktop dosyasını parse edip launcher için uygun Entry döner.
// Application tipinde değilse, NoDisplay/Hidden ise boş Entry.
Entry parseSingleApp(const QString &path)
{
    Entry e;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return e;
    bool inEntry = false, hidden = false, noDisplay = false;
    QString type;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.startsWith('[')) {
            inEntry = (line == QLatin1String("[Desktop Entry]"));
            continue;
        }
        if (!inEntry || line.isEmpty() || line.startsWith('#')) continue;
        const int eq = line.indexOf('=');
        if (eq < 0) continue;
        const QString k = line.left(eq).trimmed();
        const QString v = line.mid(eq + 1).trimmed();
        if (k == QLatin1String("Name")) e.name = v;
        else if (k == QLatin1String("Icon")) e.icon = v;
        else if (k == QLatin1String("Exec")) e.exec = v;
        else if (k == QLatin1String("Type")) type = v;
        else if (k == QLatin1String("Hidden") && v.toLower() == "true") hidden = true;
        else if (k == QLatin1String("NoDisplay") && v.toLower() == "true") noDisplay = true;
    }
    if (hidden || noDisplay || type != QLatin1String("Application")) return Entry();
    static const QRegularExpression placeholders("\\s*%[fFuUickdDnNvm]");
    e.exec.remove(placeholders);
    e.exec = e.exec.trimmed();
    return e;
}

QList<Entry> buildInstalledAppsList()
{
    QList<Entry> apps;
    QSet<QString> seen;
    const QStringList dirs{
        "/usr/share/applications",
        "/usr/local/share/applications",
        QDir::homePath() + "/.local/share/applications",
        "/var/lib/flatpak/exports/share/applications",
        QDir::homePath() + "/.local/share/flatpak/exports/share/applications",
    };
    for (const QString &d : dirs) {
        QDir dir(d);
        if (!dir.exists()) continue;
        const auto files =
            dir.entryList(QStringList{"*.desktop"}, QDir::Files);
        for (const QString &file : files) {
            if (seen.contains(file)) continue;
            seen.insert(file);
            Entry e = parseSingleApp(dir.absoluteFilePath(file));
            if (e.exec.isEmpty() || e.name.isEmpty()) continue;
            apps.append(e);
        }
    }
    std::sort(apps.begin(), apps.end(), [](const Entry &a, const Entry &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return apps;
}

void parseFile(const QString &path, QMap<QString, Entry> &map)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return;
    }

    QString iconName;
    QString wmClass;
    QString appName;
    QString execLine;
    QStringList declaredActions;
    bool isHidden = false;
    bool noDisplay = false;

    // Action section'ları: [Desktop Action xxx] header'ından sonra gelen
    // Name/Exec/Icon satırları. Section adına göre bir map'te biriktirip
    // sonunda Actions= deklarasyonuyla sırasına göre listeye dönüştürürüz.
    QMap<QString, Action> actionMap;

    enum Section { None, MainEntry, ActionEntry };
    Section section = None;
    QString currentActionId;

    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith('#')) {
            continue;
        }
        if (line.startsWith('[')) {
            if (line == QLatin1String("[Desktop Entry]")) {
                section = MainEntry;
                currentActionId.clear();
            } else if (line.startsWith(QLatin1String("[Desktop Action "))
                       && line.endsWith(QLatin1Char(']'))) {
                section = ActionEntry;
                currentActionId = line.mid(16, line.length() - 17).trimmed();
                if (!actionMap.contains(currentActionId)) {
                    actionMap.insert(currentActionId, Action{});
                }
            } else {
                section = None;
                currentActionId.clear();
            }
            continue;
        }

        const int eq = line.indexOf('=');
        if (eq < 0) continue;
        const QString key = line.left(eq).trimmed();
        const QString val = line.mid(eq + 1).trimmed();

        if (section == MainEntry) {
            if (key == QLatin1String("Icon")) {
                iconName = val;
            } else if (key == QLatin1String("StartupWMClass")) {
                wmClass = val;
            } else if (key == QLatin1String("Name")) {
                appName = val;
            } else if (key == QLatin1String("Exec")) {
                execLine = val;
            } else if (key == QLatin1String("Hidden")
                       && val.toLower() == QLatin1String("true")) {
                isHidden = true;
            } else if (key == QLatin1String("NoDisplay")
                       && val.toLower() == QLatin1String("true")) {
                noDisplay = true;
            } else if (key == QLatin1String("Actions")) {
                declaredActions = val.split(';', Qt::SkipEmptyParts);
            }
        } else if (section == ActionEntry && !currentActionId.isEmpty()) {
            Action &a = actionMap[currentActionId];
            if (key == QLatin1String("Name")) {
                a.name = val;
            } else if (key == QLatin1String("Exec")) {
                a.exec = val;
            } else if (key == QLatin1String("Icon")) {
                a.icon = val;
            }
        }
    }

    if (iconName.isEmpty() || isHidden) {
        return;
    }
    // NoDisplay=true olan .desktop dosyaları URL handler, daemon, vs.
    // gerçek uygulama değil — bunlar key cache'i kapatıp asıl uygulama
    // .desktop dosyasının action'larını ekarte ediyordu (örn. VS Code
    // için code-url-handler.desktop alfabetik olarak code.desktop'tan önce
    // geliyor ve "code" key'ini kapıyordu).
    if (noDisplay) {
        return;
    }

    auto cleanExec = [](QString s) {
        static const QRegularExpression placeholders("\\s*%[fFuUickdDnNvm]");
        s.remove(placeholders);
        return s.trimmed();
    };

    // Declared actions sırasına göre, exec ve isim sahip olanları seç.
    QList<Action> actions;
    for (const QString &id : declaredActions) {
        const QString trimmed = id.trimmed();
        if (trimmed.isEmpty()) continue;
        auto it = actionMap.constFind(trimmed);
        if (it == actionMap.constEnd()) continue;
        if (it->name.isEmpty() || it->exec.isEmpty()) continue;
        Action a = *it;
        a.exec = cleanExec(a.exec);
        actions.append(a);
    }

    Entry entry{iconName, cleanExec(execLine), appName, actions};

    auto insertKey = [&](const QString &k) {
        const QString lower = k.toLower();
        if (lower.isEmpty()) {
            return;
        }
        if (!map.contains(lower)) {
            map.insert(lower, entry);
        }
    };

    insertKey(wmClass);
    insertKey(appName);
    insertKey(QFileInfo(path).completeBaseName());

    if (!execLine.isEmpty()) {
        QString firstArg = execLine.section(' ', 0, 0);
        firstArg.remove('"');
        insertKey(QFileInfo(firstArg).baseName());
    }
}

QMap<QString, Entry> buildCache()
{
    QMap<QString, Entry> map;
    const QStringList dirs = {
        "/usr/share/applications",
        "/usr/local/share/applications",
        QDir::homePath() + "/.local/share/applications",
        "/var/lib/flatpak/exports/share/applications",
        QDir::homePath() + "/.local/share/flatpak/exports/share/applications",
        "/var/lib/snapd/desktop/applications",
        QDir::homePath() + "/Applications",  // AppImageLauncher
    };
    for (const QString &d : dirs) {
        QDir dir(d);
        if (!dir.exists()) {
            continue;
        }
        const auto files = dir.entryList(QStringList() << "*.desktop", QDir::Files);
        for (const QString &file : files) {
            parseFile(dir.absoluteFilePath(file), map);
        }
    }
    return map;
}

const QMap<QString, Entry> &mapInstance()
{
    static const QMap<QString, Entry> cache = buildCache();
    return cache;
}

} // namespace

DesktopIconResolver::Entry DesktopIconResolver::entry(const QString &key)
{
    if (key.isEmpty()) {
        return {};
    }
    const auto &cache = mapInstance();
    return cache.value(key.toLower());
}

DesktopIconResolver::Entry DesktopIconResolver::findByTokens(const QString &appId)
{
    if (appId.isEmpty()) return {};
    static const QRegularExpression splitter("[.\\-_ ]");
    QSet<QString> appTokens;
    for (const QString &p :
         appId.toLower().split(splitter, Qt::SkipEmptyParts)) {
        if (p.length() >= 3) appTokens.insert(p);
    }
    if (appTokens.isEmpty()) return {};

    const auto &cache = mapInstance();
    QString bestKey;
    int bestScore = 0;
    for (auto it = cache.constBegin(); it != cache.constEnd(); ++it) {
        QSet<QString> keyTokens;
        for (const QString &p :
             it.key().split(splitter, Qt::SkipEmptyParts)) {
            if (p.length() >= 3) keyTokens.insert(p);
        }
        QSet<QString> common = appTokens;
        common.intersect(keyTokens);
        if (int(common.size()) > bestScore) {
            bestScore = common.size();
            bestKey = it.key();
        }
    }
    if (bestScore >= 2) return cache.value(bestKey);
    return {};
}

const QList<DesktopIconResolver::Entry> &DesktopIconResolver::installedApps()
{
    static const QList<Entry> apps = buildInstalledAppsList();
    return apps;
}

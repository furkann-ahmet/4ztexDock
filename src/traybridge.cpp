#include "traybridge.h"

#include <QAction>
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QMenu>
#include <QPixmap>
#include <QRegularExpression>
#include <QTimer>
#include <QVariant>

#include <algorithm>

// ----- DBusMenu (com.canonical.dbusmenu) layout type -----
// GetLayout returns (u, (ia{sv}av)) — the second tuple element is the menu
// tree. Each node has id, properties, children (each child is a variant
// containing a nested node). Registered with the Qt meta-type system so
// Qt's automatic demarshalling can pick our operator>>.

struct DBusMenuLayoutItem {
    int id = 0;
    QVariantMap properties;
    QList<QVariant> children;  // each: QDBusArgument with same struct
};
Q_DECLARE_METATYPE(DBusMenuLayoutItem)

inline QDBusArgument &operator<<(QDBusArgument &arg, const DBusMenuLayoutItem &item)
{
    arg.beginStructure();
    arg << item.id << item.properties;
    arg.beginArray(qMetaTypeId<QDBusVariant>());
    for (const QVariant &v : item.children) {
        arg << QDBusVariant(v);
    }
    arg.endArray();
    arg.endStructure();
    return arg;
}

inline const QDBusArgument &operator>>(const QDBusArgument &arg,
                                       DBusMenuLayoutItem &item)
{
    arg.beginStructure();
    arg >> item.id >> item.properties;
    arg.beginArray();
    item.children.clear();
    while (!arg.atEnd()) {
        QDBusVariant dv;
        arg >> dv;
        item.children.append(dv.variant());
    }
    arg.endArray();
    arg.endStructure();
    return arg;
}

namespace {

constexpr const char *kWatcherService = "org.kde.StatusNotifierWatcher";
constexpr const char *kWatcherPath = "/StatusNotifierWatcher";
constexpr const char *kWatcherIface = "org.kde.StatusNotifierWatcher";
constexpr const char *kItemIface = "org.kde.StatusNotifierItem";

} // namespace

// ----- TrayBridge -----

TrayBridge::TrayBridge(QObject *parent) : QObject(parent)
{
    // One-shot Qt meta-type registration for DBus types we demarshal.
    [[maybe_unused]] static const int registered = []() {
        qDBusRegisterMetaType<DBusMenuLayoutItem>();
        return 0;
    }();

    hostService_ = QString("org.kde.StatusNotifierHost-%1-toolbar")
                       .arg(QCoreApplication::applicationPid());

    registerHost();
    // Defer the initial item enumeration to the next event-loop tick so the
    // caller has a chance to wire up itemAdded/itemRemoved before we start
    // emitting signals — otherwise pre-existing tray apps come in during the
    // constructor and slip past any listener that hasn't connected yet.
    QTimer::singleShot(0, this, [this]() { fetchInitialItems(); });

    // Subscribe to watcher signals for live updates.
    QDBusConnection::sessionBus().connect(
        kWatcherService, kWatcherPath, kWatcherIface,
        "StatusNotifierItemRegistered", this,
        SLOT(onWatcherItemRegistered(QString)));
    QDBusConnection::sessionBus().connect(
        kWatcherService, kWatcherPath, kWatcherIface,
        "StatusNotifierItemUnregistered", this,
        SLOT(onWatcherItemUnregistered(QString)));
}

QList<TrayItem *> TrayBridge::items() const
{
    return itemsByKey_.values();
}

void TrayBridge::registerHost()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qWarning() << "[TrayBridge] session bus not connected";
        return;
    }

    // Register our own service name first so the watcher accepts us as a host.
    if (!bus.registerService(hostService_)) {
        qWarning() << "[TrayBridge] could not register host service" << hostService_
                   << ":" << bus.lastError().message();
    }

    QDBusInterface watcher(kWatcherService, kWatcherPath, kWatcherIface, bus);
    if (!watcher.isValid()) {
        qWarning() << "[TrayBridge] StatusNotifierWatcher not reachable";
        return;
    }
    QDBusReply<void> reply = watcher.call("RegisterStatusNotifierHost", hostService_);
    if (!reply.isValid()) {
        qWarning() << "[TrayBridge] RegisterStatusNotifierHost failed:"
                   << reply.error().message();
    }
}

void TrayBridge::fetchInitialItems()
{
    QDBusInterface watcher(kWatcherService, kWatcherPath, kWatcherIface,
                           QDBusConnection::sessionBus());
    if (!watcher.isValid()) {
        qWarning() << "[TrayBridge] watcher interface invalid:"
                   << watcher.lastError().message();
        return;
    }

    const QVariant prop = watcher.property("RegisteredStatusNotifierItems");
    const QStringList items = prop.toStringList();
    for (const QString &entry : items) {
        addItem(entry);
    }
}

void TrayBridge::onWatcherItemRegistered(const QString &serviceAndPath)
{
    addItem(serviceAndPath);
}

void TrayBridge::onWatcherItemUnregistered(const QString &serviceAndPath)
{
    removeItem(serviceAndPath);
}

void TrayBridge::onItemChanged()
{
    auto *item = qobject_cast<TrayItem *>(sender());
    if (!item) return;
    emit itemChanged(item);
}

void TrayBridge::addItem(const QString &serviceAndPath)
{
    if (itemsByKey_.contains(serviceAndPath)) {
        return;
    }
    const auto [service, path] = splitServicePath(serviceAndPath);
    if (service.isEmpty()) {
        qWarning() << "[TrayBridge] could not parse item key" << serviceAndPath;
        return;
    }

    auto *item = new TrayItem(service, path, this);
    itemsByKey_.insert(serviceAndPath, item);
    QObject::connect(item, &TrayItem::changed, this, &TrayBridge::onItemChanged);
    emit itemAdded(item);
}

void TrayBridge::removeItem(const QString &serviceAndPath)
{
    auto it = itemsByKey_.find(serviceAndPath);
    if (it == itemsByKey_.end()) return;
    TrayItem *item = it.value();
    itemsByKey_.erase(it);
    emit itemRemoved(item);
    item->deleteLater();
}

QPair<QString, QString> TrayBridge::splitServicePath(const QString &serviceAndPath)
{
    // The watcher uses two encodings:
    //   "service/full/object/path"  — newer (KDE Plasma 5.20+)
    //   "service"                   — legacy; path defaults to /StatusNotifierItem
    const int slash = serviceAndPath.indexOf('/');
    if (slash < 0) {
        return {serviceAndPath, QStringLiteral("/StatusNotifierItem")};
    }
    return {serviceAndPath.left(slash), serviceAndPath.mid(slash)};
}

// ----- TrayItem -----

TrayItem::TrayItem(const QString &service, const QString &path, QObject *parent)
    : QObject(parent), service_(service), path_(path)
{
    refresh();

    QDBusConnection bus = QDBusConnection::sessionBus();
    // Any of these signals → property cache is stale, re-read.
    static const QStringList kSignals = {
        "NewIcon", "NewTitle", "NewStatus", "NewToolTip", "NewAttentionIcon"
    };
    for (const QString &sig : kSignals) {
        bus.connect(service_, path_, kItemIface, sig, this, SLOT(refresh()));
    }
}

void TrayItem::refresh()
{
    QDBusInterface iface(service_, path_, kItemIface,
                         QDBusConnection::sessionBus());
    if (!iface.isValid()) return;

    id_ = iface.property("Id").toString();
    title_ = iface.property("Title").toString();
    iconName_ = iface.property("IconName").toString();
    status_ = iface.property("Status").toString();
    // ItemIsMenu (StatusNotifier spec): true ise app yalnızca context menu
    // destekliyor demek — left-click `Activate` çağrılmamalı, doğrudan menu
    // gösterilmeli. Spec'te default false. Property yoksa toString() boş
    // string döner → toLower() boş kalır → comparison false → güvenli default.
    itemIsMenu_ = (iface.property("ItemIsMenu").toString().toLower()
                       == QLatin1String("true"))
                  || iface.property("ItemIsMenu").toBool();
    ownerName_ = computeOwnerName();

    // ToolTip property uses signature (s a(iiay) s s) — the inner pixmap
    // array trips Qt's automatic demarshaller (some implementations emit it
    // empty, some emit non-basic inner types that Qt's QDBusArgument refuses
    // to read, which dbus then SIGABRTs on). We don't render the pixmap part
    // anyway, so we just reuse the Title for the QPushButton::setToolTip()
    // and skip ToolTip entirely.
    tooltipTitle_ = title_;
    tooltipText_.clear();

    icon_ = resolveIcon();

    emit changed();
}

QString TrayItem::title() const
{
    // Many Chromium-Electron apps (Discord, Slack, ZapZap...) report a
    // generic auto-generated Title like "chrome_status_icon_1" that doesn't
    // reflect the actual app. In those cases prefer the bus-owner's binary
    // name (read from /proc/<pid>/comm) which gives the real app name.
    auto isGeneric = [](const QString &t) {
        if (t.isEmpty()) return true;
        const QString lower = t.toLower();
        return lower.startsWith("chrome_status_icon")
               || lower.startsWith("chromium_status_icon")
               || lower.startsWith("status_icon")
               || lower == "chrome" || lower == "chromium";
    };

    QString clean = title_;
    if (isGeneric(clean) && !ownerName_.isEmpty()) {
        clean = ownerName_;
    } else {
        // Strip a "_status_icon_N" suffix from titles that *do* lead with
        // the app name (e.g. "Slack_status_icon_1" → "Slack").
        const int idx = clean.indexOf(QLatin1String("_status_icon_"));
        if (idx > 0) {
            clean = clean.left(idx);
        }
    }

    if (clean.isEmpty()) clean = id_;
    if (clean.isEmpty()) clean = ownerName_;
    if (!clean.isEmpty() && clean[0].isLower()) {
        clean[0] = clean[0].toUpper();
    }
    return clean;
}

QString TrayItem::computeOwnerName() const
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.freedesktop.DBus", "/org/freedesktop/DBus",
        "org.freedesktop.DBus", "GetConnectionUnixProcessID");
    msg << service_;
    QDBusReply<quint32> reply = QDBusConnection::sessionBus().call(msg);
    if (!reply.isValid()) return {};

    QFile comm(QStringLiteral("/proc/%1/comm").arg(reply.value()));
    if (!comm.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(comm.readAll()).trimmed();
}

// Fetch one of the SNI properties via the Properties.Get DBus method. We
// avoid QDBusInterface::property() because it relies on introspection that
// some apps return incomplete data for; the explicit Get call works even
// when introspection skips the property.
static QVariant getProperty(const QString &service, const QString &path,
                            const QString &propName)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        service, path, "org.freedesktop.DBus.Properties", "Get");
    msg << QString("org.kde.StatusNotifierItem") << propName;
    QDBusMessage reply = QDBusConnection::sessionBus().call(msg);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }
    QVariant outer = reply.arguments().first();
    if (outer.canConvert<QDBusVariant>()) {
        return outer.value<QDBusVariant>().variant();
    }
    return outer;
}

// Decode org.kde.StatusNotifierItem.IconPixmap (signature a(iiay)) into a
// QIcon. The data inside each pixmap is ARGB32 in network (big-endian) byte
// order per the freedesktop spec; we byte-swap each pixel so the resulting
// QImage (Format_ARGB32) reads correctly on little-endian hosts.
static QIcon fetchPixmapIcon(const QString &service, const QString &path)
{
    QVariant inner = getProperty(service, path, QStringLiteral("IconPixmap"));
    if (!inner.isValid()) return {};
    if (!inner.canConvert<QDBusArgument>()) return {};

    // CRITICAL: const-qualify the argument so beginArray/beginStructure
    // hit the *reading* (const) overloads. Without const, Qt picks the
    // non-const overloads — those are for marshalling (writing), which
    // triggers "write from a read-only object" and aborts dbus.
    const QDBusArgument arg = inner.value<QDBusArgument>();
    if (arg.currentType() != QDBusArgument::ArrayType) return {};

    QImage best;
    arg.beginArray();
    while (!arg.atEnd()) {
        arg.beginStructure();
        int w = 0, h = 0;
        QByteArray data;
        arg >> w >> h >> data;
        arg.endStructure();
        if (w <= 0 || h <= 0 || data.size() != w * h * 4) continue;
        if (w <= best.width()) continue;

        // Each pixel: A R G B (big-endian). On a little-endian host,
        // Format_ARGB32 expects bytes in memory order B G R A. Swap.
        QByteArray bytes = data;
        uchar *p = reinterpret_cast<uchar *>(bytes.data());
        const int pixels = w * h;
        for (int i = 0; i < pixels; ++i) {
            std::swap(p[0], p[3]);
            std::swap(p[1], p[2]);
            p += 4;
        }
        QImage img(reinterpret_cast<const uchar *>(bytes.constData()),
                   w, h, QImage::Format_ARGB32);
        best = img.copy();
    }
    arg.endArray();

    if (best.isNull()) return {};
    return QIcon(QPixmap::fromImage(best));
}

// SNI apps sometimes expose a DesktopFileName property pointing at their
// .desktop launcher. Parse its Icon= field and resolve through the theme so
// we don't need per-app mapping.
static QIcon fetchDesktopFileIcon(const QString &service, const QString &path)
{
    const QVariant val = getProperty(service, path,
                                     QStringLiteral("DesktopFileName"));
    QString filename = val.toString();
    if (filename.isEmpty()) return {};

    QStringList candidates;
    if (filename.contains('/')) {
        candidates << filename;
    } else {
        if (!filename.endsWith(QLatin1String(".desktop"))) {
            filename += QLatin1String(".desktop");
        }
        const QStringList dirs = {
            QDir::homePath() + "/.local/share/applications",
            "/usr/local/share/applications",
            "/usr/share/applications",
            "/var/lib/flatpak/exports/share/applications",
            QDir::homePath() + "/.local/share/flatpak/exports/share/applications",
        };
        for (const QString &d : dirs) {
            candidates << d + "/" + filename;
        }
    }

    auto tryAllPaths = [](const QString &iconName) -> QIcon {
        if (iconName.isEmpty()) return {};
        // Absolute path
        if (iconName.contains('/') && QFile::exists(iconName)) {
            return QIcon(iconName);
        }
        // Theme
        QIcon themeHit = QIcon::fromTheme(iconName);
        if (!themeHit.isNull() && !themeHit.availableSizes().isEmpty()) {
            return themeHit;
        }
        // Lowercase variant
        const QString lower = iconName.toLower();
        if (lower != iconName) {
            QIcon hit = QIcon::fromTheme(lower);
            if (!hit.isNull() && !hit.availableSizes().isEmpty()) return hit;
        }
        // Reverse-DNS tail (com.foo.Bar → bar)
        const int dot = iconName.lastIndexOf('.');
        if (dot > 0 && dot + 1 < iconName.size()) {
            QString tail = iconName.mid(dot + 1).toLower();
            if (tail.endsWith(QLatin1String("-symbolic"))) tail.chop(9);
            QIcon hit = QIcon::fromTheme(tail);
            if (!hit.isNull() && !hit.availableSizes().isEmpty()) return hit;
        }
        // /usr/share/pixmaps direct file lookup — pek çok 3rd-party app
        // icon'unu hicolor/breeze yerine pixmaps'e koyuyor.
        for (const char *ext : {".png", ".svg", ".xpm"}) {
            const QString p = QStringLiteral("/usr/share/pixmaps/") + lower + ext;
            if (QFile::exists(p)) return QIcon(p);
        }
        // hicolor/<size>/apps doğrudan arama — flatpak, system, user roots.
        const QStringList roots{
            QStringLiteral("/usr/share/icons/hicolor"),
            QStringLiteral("/var/lib/flatpak/exports/share/icons/hicolor"),
            QDir::homePath()
                + "/.local/share/flatpak/exports/share/icons/hicolor",
            QDir::homePath() + "/.local/share/icons/hicolor",
        };
        for (const QString &root : roots) {
            for (const char *size : {"scalable", "512x512", "256x256",
                                       "128x128", "64x64", "48x48", "32x32"}) {
                for (const char *ext : {".svg", ".png"}) {
                    const QString p = root + "/" + size + "/apps/" + lower + ext;
                    if (QFile::exists(p)) return QIcon(p);
                }
            }
        }
        return {};
    };

    for (const QString &fp : candidates) {
        QFile f(fp);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        bool inEntry = false;
        while (!f.atEnd()) {
            const QString line = QString::fromUtf8(f.readLine()).trimmed();
            if (line.startsWith('[')) {
                inEntry = (line == QLatin1String("[Desktop Entry]"));
                continue;
            }
            if (!inEntry) continue;
            if (line.startsWith(QLatin1String("Icon="))) {
                const QString icon = line.mid(5).trimmed();
                return tryAllPaths(icon);
            }
        }
    }
    return {};
}

QIcon TrayItem::resolveIcon() const
{
    auto isReal = [](const QIcon &i) {
        return !i.isNull() && !i.availableSizes().isEmpty();
    };

    if (!iconName_.isEmpty()) {
        // 1) Direct theme lookup.
        QIcon icon = QIcon::fromTheme(iconName_);
        if (isReal(icon)) return icon;

        // 2) Strip "-symbolic" suffix — many themes only provide the
        //    non-symbolic variant for app icons.
        if (iconName_.endsWith(QLatin1String("-symbolic"))) {
            QIcon stripped = QIcon::fromTheme(iconName_.chopped(9));
            if (isReal(stripped)) return stripped;
        }

        // 3) Reverse-DNS basename: "com.spotify.Client" → "client".
        const int lastDot = iconName_.lastIndexOf('.');
        if (lastDot > 0 && lastDot + 1 < iconName_.length()) {
            QString tail = iconName_.mid(lastDot + 1);
            if (tail.endsWith(QLatin1String("-symbolic"))) {
                tail.chop(9);
            }
            QIcon byTail = QIcon::fromTheme(tail.toLower());
            if (isReal(byTail)) return byTail;
        }

        // 4) Plain lowercase.
        QIcon lower = QIcon::fromTheme(iconName_.toLower());
        if (isReal(lower)) return lower;

        // 5) Direct file lookup. fallbackSearchPaths + icon theme cache bazen
        //    bunları kaçırıyor (cache stale / inheritance issue). Manuel olarak
        //    pixmaps + tüm hicolor kökleri (flatpak system+user dahil) taranır.
        const QString lname = iconName_.toLower();
        for (const char *ext : {".png", ".svg", ".xpm"}) {
            const QString p = QStringLiteral("/usr/share/pixmaps/") + lname + ext;
            if (QFile::exists(p)) return QIcon(p);
        }
        const QStringList iconRoots{
            QStringLiteral("/usr/share/icons/hicolor"),
            QStringLiteral("/var/lib/flatpak/exports/share/icons/hicolor"),
            QDir::homePath()
                + "/.local/share/flatpak/exports/share/icons/hicolor",
            QDir::homePath() + "/.local/share/icons/hicolor",
        };
        for (const QString &root : iconRoots) {
            for (const char *size : {"scalable", "512x512", "256x256",
                                       "128x128", "64x64", "48x48", "32x32",
                                       "22x22"}) {
                for (const char *ext : {".svg", ".png"}) {
                    const QString p = root + "/" + size + "/apps/" + lname + ext;
                    if (QFile::exists(p)) return QIcon(p);
                }
            }
        }
    }

    // No usable theme glyph — try DesktopFileName which points at the app's
    // .desktop launcher. Parsing its Icon= field gives a generic way to
    // resolve any modern app without per-app mapping.
    QIcon desktopHit = fetchDesktopFileIcon(service_, path_);
    if (!desktopHit.isNull()) return desktopHit;

    // Fall through to the raw pixmap bytes the app shipped via IconPixmap
    // (Chromium / Electron apps like Slack, Discord, ZapZap typically only
    // provide pixmap data, no theme name). For pixmap-built icons we just
    // check isNull — availableSizes() can be empty depending on the Qt
    // build and we end up rejecting a perfectly good QPixmap-based icon.
    QIcon pixmap = fetchPixmapIcon(service_, path_);
    if (!pixmap.isNull()) return pixmap;

    // Still nothing — try the binary name from /proc/<pid>/comm. Önce theme,
    // sonra /usr/share/pixmaps + tüm hicolor kökleri (flatpak dahil).
    if (!ownerName_.isEmpty()) {
        const QString owner = ownerName_.toLower();
        QIcon byOwner = QIcon::fromTheme(owner);
        if (isReal(byOwner)) return byOwner;
        for (const char *ext : {".png", ".svg", ".xpm"}) {
            const QString p = QStringLiteral("/usr/share/pixmaps/") + owner + ext;
            if (QFile::exists(p)) return QIcon(p);
        }
        const QStringList iconRoots{
            QStringLiteral("/usr/share/icons/hicolor"),
            QStringLiteral("/var/lib/flatpak/exports/share/icons/hicolor"),
            QDir::homePath()
                + "/.local/share/flatpak/exports/share/icons/hicolor",
            QDir::homePath() + "/.local/share/icons/hicolor",
        };
        for (const QString &root : iconRoots) {
            for (const char *size : {"scalable", "512x512", "256x256",
                                       "128x128", "64x64", "48x48", "32x32"}) {
                for (const char *ext : {".svg", ".png"}) {
                    const QString p = root + "/" + size + "/apps/" + owner + ext;
                    if (QFile::exists(p)) return QIcon(p);
                }
            }
        }
    }

    // Last-resort generic chain — application-x-executable bazı temalarda
    // sadece mimetypes/ altında olduğu için fromTheme pickleyemeyebilir;
    // birden fazla alternatif sırayla deneyelim.
    for (const auto &name : {"application-x-executable", "applications-other",
                             "system-run", "package-x-generic", "unknown"}) {
        QIcon generic = QIcon::fromTheme(QString::fromLatin1(name));
        if (isReal(generic)) return generic;
    }
    return {};
}

void TrayItem::activate(int x, int y)
{
    QDBusInterface iface(service_, path_, kItemIface,
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        iface.asyncCall("Activate", x, y);
    }
}

void TrayItem::secondaryActivate(int x, int y)
{
    QDBusInterface iface(service_, path_, kItemIface,
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        iface.asyncCall("SecondaryActivate", x, y);
    }
}

void TrayItem::contextMenu(int x, int y)
{
    QDBusInterface iface(service_, path_, kItemIface,
                         QDBusConnection::sessionBus());
    if (iface.isValid()) {
        iface.asyncCall("ContextMenu", x, y);
    }
}

namespace {

// Read a node value out of a possibly QDBusVariant-wrapped QVariant.
QVariant unwrapVariant(const QVariant &v)
{
    if (v.canConvert<QDBusVariant>()) {
        return v.value<QDBusVariant>().variant();
    }
    return v;
}

// Translate DBusMenu's '_'-prefixed mnemonics to Qt's '&'-prefixed ones,
// escaping any literal '&' the label already contains.
QString translateMnemonic(QString label)
{
    label.replace(QLatin1String("&"), QLatin1String("&&"));
    label.replace(QLatin1String("__"), QLatin1String("\x1F"));  // tmp marker
    label.replace(QLatin1Char('_'), QLatin1Char('&'));
    label.replace(QLatin1Char('\x1F'), QLatin1Char('_'));
    return label;
}

void collectEntries(const DBusMenuLayoutItem &node, QList<TrayMenuEntry> &out)
{
    for (const QVariant &childVar : node.children) {
        if (!childVar.canConvert<QDBusArgument>()) continue;
        const QDBusArgument arg = childVar.value<QDBusArgument>();
        DBusMenuLayoutItem child;
        arg >> child;

        const QString type = unwrapVariant(child.properties.value("type")).toString();
        const QVariant visibleVar = unwrapVariant(child.properties.value("visible"));
        const bool visible = visibleVar.isValid() ? visibleVar.toBool() : true;
        if (!visible) continue;

        TrayMenuEntry e;
        e.id = child.id;
        e.label = translateMnemonic(
            unwrapVariant(child.properties.value("label")).toString());
        e.iconName = unwrapVariant(child.properties.value("icon-name")).toString();
        const QVariant enabledVar = unwrapVariant(child.properties.value("enabled"));
        e.enabled = enabledVar.isValid() ? enabledVar.toBool() : true;
        e.separator = (type == QLatin1String("separator"));
        e.toggleType = unwrapVariant(child.properties.value("toggle-type")).toString();
        e.checked = (unwrapVariant(child.properties.value("toggle-state")).toInt() == 1);
        e.hasSubmenu = (unwrapVariant(child.properties.value("children-display"))
                           .toString() == QLatin1String("submenu"));

        if (e.hasSubmenu) {
            collectEntries(child, e.children);
        }

        out.append(e);
    }
}

} // namespace

QList<TrayMenuEntry> TrayItem::fetchMenuEntries()
{
    const QVariant menuProp = getProperty(service_, path_,
                                          QStringLiteral("Menu"));
    const QDBusObjectPath menuPath = menuProp.value<QDBusObjectPath>();
    if (menuPath.path().isEmpty() || menuPath.path() == QLatin1String("/")) {
        return {};
    }

    const QString mp = menuPath.path();

    // Politely tell the app the menu is about to show — gives it a chance
    // to refresh dynamic labels/check-states. Fire-and-forget.
    {
        QDBusMessage about = QDBusMessage::createMethodCall(
            service_, mp, "com.canonical.dbusmenu", "AboutToShow");
        about << 0;
        QDBusConnection::sessionBus().asyncCall(about);
    }

    // GetLayout(parentId=0, recursionDepth=-1, propertyNames=[])
    QDBusMessage msg = QDBusMessage::createMethodCall(
        service_, mp, "com.canonical.dbusmenu", "GetLayout");
    msg << 0 << -1 << QStringList();
    QDBusMessage reply = QDBusConnection::sessionBus().call(msg);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
        return {};
    }

    const QVariant layoutVar = reply.arguments().at(1);
    if (!layoutVar.canConvert<QDBusArgument>()) {
        return {};
    }

    DBusMenuLayoutItem root;
    const QDBusArgument arg = layoutVar.value<QDBusArgument>();
    arg >> root;

    QList<TrayMenuEntry> result;
    collectEntries(root, result);
    return result;
}

void TrayItem::invokeMenuClick(int entryId)
{
    const QVariant menuProp = getProperty(service_, path_,
                                          QStringLiteral("Menu"));
    const QDBusObjectPath menuPath = menuProp.value<QDBusObjectPath>();
    if (menuPath.path().isEmpty()) return;

    QDBusMessage msg = QDBusMessage::createMethodCall(
        service_, menuPath.path(), "com.canonical.dbusmenu", "Event");
    msg << entryId << QString("clicked")
        << QVariant::fromValue(QDBusVariant(QString()))
        << quint32(QDateTime::currentSecsSinceEpoch());
    QDBusConnection::sessionBus().asyncCall(msg);
}

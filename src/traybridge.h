#pragma once

#include <QHash>
#include <QIcon>
#include <QObject>
#include <QPointer>
#include <QString>

class QWidget;

class TrayItem;

// Flat representation of a single DBusMenu node — the UI side renders these
// via its own popup widget (Qt's QMenu does not position correctly when its
// parent is a Wayland layer-shell surface, so we don't use it).
struct TrayMenuEntry {
    int id = 0;
    QString label;
    QString iconName;
    bool enabled = true;
    bool separator = false;
    bool checked = false;
    bool hasSubmenu = false;
    QString toggleType;          // "checkmark" / "radio" / ""
    QList<TrayMenuEntry> children;
};

// Bridges org.kde.StatusNotifierWatcher (the freedesktop "system tray" spec
// used by Discord/Steam/Telegram/etc on KDE Wayland). Registers ourselves as
// a StatusNotifierHost, mirrors the current set of registered items, and
// emits per-item add/remove/change signals so the UI layer can manage a
// dynamic row of tray icons.
class TrayBridge : public QObject
{
    Q_OBJECT

public:
    explicit TrayBridge(QObject *parent = nullptr);

    QList<TrayItem *> items() const;

signals:
    void itemAdded(TrayItem *item);
    void itemRemoved(TrayItem *item);
    // Re-emitted when *any* tracked item's title/icon/status changes; the
    // UI can match against `item` to refresh just that icon.
    void itemChanged(TrayItem *item);

private slots:
    void onWatcherItemRegistered(const QString &serviceAndPath);
    void onWatcherItemUnregistered(const QString &serviceAndPath);
    void onItemChanged();

private:
    void registerHost();
    void fetchInitialItems();
    void addItem(const QString &serviceAndPath);
    void removeItem(const QString &serviceAndPath);
    static QPair<QString, QString> splitServicePath(const QString &serviceAndPath);

    QString hostService_;
    QHash<QString, TrayItem *> itemsByKey_;  // key = "service" + "path"
};

// Mirrors a single org.kde.StatusNotifierItem. Properties are cached on
// refresh(); signals from the item trigger automatic refresh + emit a
// changed() so listeners can re-render.
class TrayItem : public QObject
{
    Q_OBJECT

public:
    TrayItem(const QString &service, const QString &path, QObject *parent = nullptr);

    QString service() const { return service_; }
    QString path() const { return path_; }
    QString id() const { return id_; }
    QString title() const;
    QString iconName() const { return iconName_; }
    QString status() const { return status_; }
    QString toolTipTitle() const { return tooltipTitle_; }
    QString toolTipText() const { return tooltipText_; }
    // App declared `ItemIsMenu=true` — left-click should show its context
    // menu instead of calling Activate (which the app considers a no-op).
    bool itemIsMenu() const { return itemIsMenu_; }
    // Resolved QIcon: prefers IconName via theme lookup with progressive
    // fallbacks (stripped suffixes, last-segment basename), and finally a
    // generic placeholder so the button is never empty.
    QIcon icon() const { return icon_; }

    // Trigger the app's primary action (left-click). x/y are the global
    // pointer position so the app can anchor a popup if it wants.
    void activate(int x, int y);
    void secondaryActivate(int x, int y);
    void contextMenu(int x, int y);

    // Fetch the top-level entries of the app's DBusMenu. Empty list means
    // the app exposes no menu and the caller should fall back to the
    // simple ContextMenu() DBus method.
    QList<TrayMenuEntry> fetchMenuEntries();

    // Invoke an entry's "clicked" event back through DBusMenu.
    void invokeMenuClick(int entryId);

signals:
    void changed();

private slots:
    void refresh();

private:
    QIcon resolveIcon() const;
    QString computeOwnerName() const;

    QString service_;
    QString path_;
    QString id_;
    QString title_;
    QString iconName_;
    QString status_;
    QString tooltipTitle_;
    QString tooltipText_;
    QString ownerName_;     // /proc/<pid>/comm of the bus-name owner — used
                            // to disambiguate Electron apps whose Title is
                            // an auto-generated "chrome_status_icon_N"
    bool itemIsMenu_ = false;
    QIcon icon_;
};

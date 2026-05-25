#include "kwinbridge.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDebug>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>

namespace {

constexpr const char *kServiceName = "org.fourztex.dock";
constexpr const char *kObjectPath = "/Bridge";
constexpr const char *kPluginName = "fourztex-toolbar-bridge";

const char *kKWinScript = R"JS(
    const SERVICE = "org.fourztex.dock";
    const PATH = "/Bridge";
    const IFACE = "org.fourztex.dock.Bridge";

    // KWin scripting API farkları:
    //   Plasma 6: workspace.windowList(), workspace.windowAdded/Removed/Activated,
    //             win.internalId (UUID), win.windowId (X11 ID veya undefined)
    //   Plasma 5: workspace.clientList(), workspace.clientAdded/Removed/Activated,
    //             win.windowId (X11 ID, internal id görevi de görür), no internalId
    // Runtime'da hangi API mevcutsa onu kullan. windowKey() tek tip ID döner.
    var isPlasma6 = (typeof workspace.windowList === "function");
    function listWindows() {
        return isPlasma6 ? workspace.windowList() : workspace.clientList();
    }
    function onAdded(cb)    { (isPlasma6 ? workspace.windowAdded    : workspace.clientAdded).connect(cb); }
    function onRemoved(cb)  { (isPlasma6 ? workspace.windowRemoved  : workspace.clientRemoved).connect(cb); }
    function onActivated(cb){ (isPlasma6 ? workspace.windowActivated : workspace.clientActivated).connect(cb); }
    function activeWindow() { return isPlasma6 ? workspace.activeWindow : workspace.activeClient; }
    function windowKey(w) {
        if (!w) return "";
        // P6 internalId varsa onu kullan, P5'te windowId fallback'i.
        if (w.internalId !== undefined && w.internalId !== null) {
            try { return w.internalId.toString(); } catch (_) {}
        }
        if (typeof w.windowId === "number") return String(w.windowId);
        return "";
    }
    function windowX11Id(w) {
        return (w && typeof w.windowId === "number") ? w.windowId : 0;
    }

    function safeStr(x) {
        if (x === null || x === undefined) return "";
        try { return x.toString(); } catch (e) { return ""; }
    }

    function emitWin(event, win) {
        if (!win) return;
        try {
            // desktopFileName ve pid hem P5 hem P6'da var. windowId X11 native
            // ID — Wayland-native pencerelerde 0 olur. internalId P5'te yoksa
            // windowKey() windowId'yi string olarak kullanır.
            callDBus(SERVICE, PATH, IFACE, "notifyWindow",
                event,
                windowKey(win),
                safeStr(win.resourceClass),
                win.caption ? win.caption : "",
                !!win.normalWindow,
                safeStr(win.desktopFileName),
                (typeof win.pid === "number") ? win.pid : 0,
                windowX11Id(win));
        } catch (e) {
            try {
                callDBus(SERVICE, PATH, IFACE, "notifyError",
                    "emitWin " + event + ": " + e.toString());
            } catch (_) {}
        }
    }

    function emitActive() {
        try {
            var aw = activeWindow();
            callDBus(SERVICE, PATH, IFACE, "notifyActive",
                aw ? windowKey(aw) : "");
        } catch (e) {
            try {
                callDBus(SERVICE, PATH, IFACE, "notifyError",
                    "emitActive: " + e.toString());
            } catch (_) {}
        }
    }

    function attach(win) {
        if (!win) return;
        try {
            win.captionChanged.connect(function() { emitWin("update", win); });
        } catch (e) {}
        try {
            // Minimize state değişimi — buton pulse cue için bildir.
            win.minimizedChanged.connect(function() {
                try {
                    callDBus(SERVICE, PATH, IFACE, "notifyWindowMinimized",
                        windowKey(win), !!win.minimized);
                } catch (_) {}
            });
        } catch (e) {}
    }

    try {
        onAdded(function(win) {
            attach(win);
            emitWin("added", win);
        });
        onRemoved(function(win) {
            emitWin("removed", win);
        });
        onActivated(function(_win) {
            emitActive();
        });

        var initial = listWindows();
        for (var i = 0; i < initial.length; i++) {
            attach(initial[i]);
            emitWin("added", initial[i]);
        }
        emitActive();

        // Global shortcut — Meta tuşu ile launcher'ı aç. Tek başına Meta
        // KDE'de modifier-tap olarak yakalanır; KWin script registerShortcut
        // bunu kabul ederse default'ta çalışır, etmezse System Settings →
        // Shortcuts'tan "4ztex Dock — Launcher" satırını elle rebind et.
        registerShortcut("4ztexDockLauncher",
            "%SHORTCUT_LABEL%",
            "Meta",
            function() {
                callDBus(SERVICE, PATH, IFACE, "notifyLauncherShortcut");
            });

        callDBus(SERVICE, PATH, IFACE, "notifyReady");
    } catch (e) {
        callDBus(SERVICE, PATH, IFACE, "notifyError",
            "init: " + e.toString());
    }
)JS";

QString writeScriptToTemp()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString path = base + "/4ztex-kwin-bridge.js";
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "[KWinBridge] cannot write script to" << path << ":" << file.errorString();
        return {};
    }
    // i18n: %SHORTCUT_LABEL% placeholder'ını runtime'da çevirili metinle değiştir
    // (KWin Settings → Shortcuts'ta gözüken etiket bu).
    QString script = QString::fromLatin1(kKWinScript);
    const QString shortcutLabel = QCoreApplication::translate(
        "dock", "4ztex Dock — Launcher menüsünü aç");
    script.replace("%SHORTCUT_LABEL%", shortcutLabel);

    QTextStream out(&file);
    out << script;
    file.close();
    return path;
}

KWinBridge *s_instance = nullptr;

} // namespace

KWinBridge::KWinBridge(QObject *parent) : QObject(parent)
{
    s_instance = this;

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qWarning() << "[KWinBridge] session bus not connected";
        return;
    }
    if (!bus.registerService(kServiceName)) {
        qWarning() << "[KWinBridge] could not register service" << kServiceName
                   << ":" << bus.lastError().message();
    }
    if (!bus.registerObject(kObjectPath, this,
                            QDBusConnection::ExportScriptableSlots
                                | QDBusConnection::ExportScriptableSignals)) {
        qWarning() << "[KWinBridge] could not register object" << kObjectPath
                   << ":" << bus.lastError().message();
    }

    installScript();
}

KWinBridge *KWinBridge::instance()
{
    return s_instance;
}

void KWinBridge::installScript()
{
    const QString scriptPath = writeScriptToTemp();
    if (scriptPath.isEmpty()) {
        return;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusInterface iface("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", bus);
    if (!iface.isValid()) {
        qWarning() << "[KWinBridge] org.kde.KWin/Scripting not reachable:"
                   << iface.lastError().message();
        return;
    }

    iface.call("unloadScript", kPluginName);

    QDBusReply<int> reply = iface.call("loadScript", scriptPath, QString(kPluginName));
    if (!reply.isValid()) {
        qWarning() << "[KWinBridge] loadScript failed:" << reply.error().message();
        return;
    }
    const int scriptId = reply.value();
    qDebug() << "[KWinBridge] loadScript ok, id=" << scriptId;

    QDBusReply<void> startReply = iface.call("start");
    if (!startReply.isValid()) {
        qWarning() << "[KWinBridge] start() failed:" << startReply.error().message();
    } else {
        qDebug() << "[KWinBridge] Scripting.start() ok";
    }
}

void KWinBridge::runLoadedScript(int /*scriptId*/)
{
}

void KWinBridge::notifyWindow(const QString &event, const QString &id,
                              const QString &cls, const QString &title, bool normal,
                              const QString &desktopFileName, int pid, int x11WindowId)
{
    if (event == QLatin1String("added")) {
        emit windowAdded(id, cls, title, normal, desktopFileName, pid, x11WindowId);
    } else if (event == QLatin1String("removed")) {
        emit windowRemoved(id);
    } else if (event == QLatin1String("update")) {
        emit windowUpdated(id, title);
    }
}

void KWinBridge::notifyActive(const QString &id)
{
    emit activeWindowChanged(id);
}

void KWinBridge::notifyReady()
{
    ready_ = true;
    emit bridgeReady();
}

void KWinBridge::notifyError(const QString &error)
{
    qWarning() << "[KWinBridge] script error:" << error;
}

void KWinBridge::notifyLauncherShortcut()
{
    emit launcherShortcutTriggered();
}

void KWinBridge::notifyWindowMinimized(const QString &id, bool minimized)
{
    emit windowMinimizedChanged(id, minimized);
}

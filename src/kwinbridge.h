#pragma once

#include <QObject>

class KWinBridge : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.fourztex.dock.Bridge")
public:
    explicit KWinBridge(QObject *parent = nullptr);

    static KWinBridge *instance();

    bool isReady() const { return ready_; }

signals:
    void windowAdded(const QString &id, const QString &cls, const QString &title,
                     bool normal, const QString &desktopFileName, int pid,
                     int x11WindowId);
    void windowRemoved(const QString &id);
    void windowUpdated(const QString &id, const QString &title);
    void activeWindowChanged(const QString &id);
    void bridgeReady();
    void launcherShortcutTriggered();
    void windowMinimizedChanged(const QString &id, bool minimized);

public slots:
    // NOT: Q_SCRIPTABLE slot için default arg KULLANMA — DBus introspection
    // ile imza karmaşıklaşır. JS callDBus tüm argümanları geçer; her arg
    // explicit imzada olmalı. `int` tipinde tutuyoruz çünkü KWin JS number'ı
    // DBus 'i' tipinde gönderir; `uint` ('u') ile uyumsuzluk silent drop'a
    // yol açıyor.
    Q_SCRIPTABLE void notifyWindow(const QString &event, const QString &id,
                                   const QString &cls, const QString &title, bool normal,
                                   const QString &desktopFileName, int pid,
                                   int x11WindowId);
    Q_SCRIPTABLE void notifyLauncherShortcut();
    Q_SCRIPTABLE void notifyWindowMinimized(const QString &id, bool minimized);
    Q_SCRIPTABLE void notifyActive(const QString &id);
    Q_SCRIPTABLE void notifyReady();
    Q_SCRIPTABLE void notifyError(const QString &error);

private:
    void installScript();
    void runLoadedScript(int scriptId);

    bool ready_ = false;
};

#pragma once

// FDO Notifications spec'i (org.freedesktop.Notifications) implement eden DBus
// server'ı. Plasma'nın notification daemon'u kayıt yapamadığında ya da hiç
// kurulu olmadığında dock kendi tarafında bildirimleri alır. Slack, Discord,
// notify-send, browser'lar, libnotify — hepsi standart interface'i kullanıyor,
// adres farklı olmadığı için transparently bizim toolbar'a düşer.
//
// İki QObject sınıfı:
//   - NotificationsAdaptor : QDBusAbstractAdaptor    DBus method binding
//   - NotificationServer   : QObject                 history + UI bridge
//
// Adaptor pattern: Qt'nin DBus stack'i adaptor'ün public slot'larını otomatik
// olarak DBus method handler'larına çevirir; signals automatic emit edilir.

#include <QObject>
#include <QDBusAbstractAdaptor>
#include <QDBusContext>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QImage>
#include <QDateTime>
#include <QList>
#include <functional>

class NotificationServer;

class NotificationsAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
    Q_CLASSINFO("D-Bus Introspection",
        "<interface name=\"org.freedesktop.Notifications\">"
        "  <method name=\"GetCapabilities\">"
        "    <arg direction=\"out\" type=\"as\" name=\"caps\"/>"
        "  </method>"
        "  <method name=\"Notify\">"
        "    <arg direction=\"in\" type=\"s\" name=\"app_name\"/>"
        "    <arg direction=\"in\" type=\"u\" name=\"replaces_id\"/>"
        "    <arg direction=\"in\" type=\"s\" name=\"app_icon\"/>"
        "    <arg direction=\"in\" type=\"s\" name=\"summary\"/>"
        "    <arg direction=\"in\" type=\"s\" name=\"body\"/>"
        "    <arg direction=\"in\" type=\"as\" name=\"actions\"/>"
        "    <arg direction=\"in\" type=\"a{sv}\" name=\"hints\"/>"
        "    <arg direction=\"in\" type=\"i\" name=\"expire_timeout\"/>"
        "    <arg direction=\"out\" type=\"u\" name=\"id\"/>"
        "  </method>"
        "  <method name=\"CloseNotification\">"
        "    <arg direction=\"in\" type=\"u\" name=\"id\"/>"
        "  </method>"
        "  <method name=\"GetServerInformation\">"
        "    <arg direction=\"out\" type=\"s\" name=\"name\"/>"
        "    <arg direction=\"out\" type=\"s\" name=\"vendor\"/>"
        "    <arg direction=\"out\" type=\"s\" name=\"version\"/>"
        "    <arg direction=\"out\" type=\"s\" name=\"spec_version\"/>"
        "  </method>"
        "  <signal name=\"NotificationClosed\">"
        "    <arg type=\"u\" name=\"id\"/>"
        "    <arg type=\"u\" name=\"reason\"/>"
        "  </signal>"
        "  <signal name=\"ActionInvoked\">"
        "    <arg type=\"u\" name=\"id\"/>"
        "    <arg type=\"s\" name=\"action_key\"/>"
        "  </signal>"
        "</interface>")
public:
    explicit NotificationsAdaptor(NotificationServer *server);

public Q_SLOTS:
    QStringList GetCapabilities();
    uint Notify(const QString &appName, uint replacesId, const QString &appIcon,
                const QString &summary, const QString &body,
                const QStringList &actions, const QVariantMap &hints,
                int expireTimeout);
    void CloseNotification(uint id);
    void GetServerInformation(QString &name, QString &vendor,
                              QString &version, QString &specVersion);

Q_SIGNALS:
    void NotificationClosed(uint id, uint reason);
    void ActionInvoked(uint id, const QString &actionKey);

private:
    NotificationServer *server_;
};


class NotificationServer : public QObject
{
    Q_OBJECT
public:
    struct ActionEntry {
        QString key;
        QString label;
    };

    struct Notification {
        uint id = 0;
        QString appName;
        QString appIcon;          // theme icon name veya dosya yolu
        QString summary;
        QString body;             // HTML markup içerebilir (Notifications spec)
        QString imagePath;        // hints[image-path] veya app_icon
        QImage  imageRaw;         // hints[image-data] (varsa)
        QList<ActionEntry> actions;
        QString desktopEntry;     // hints[desktop-entry]
        int urgency = 1;          // 0=low, 1=normal, 2=critical
        QDateTime time;
    };

    explicit NotificationServer(QObject *parent = nullptr);
    ~NotificationServer() override;

    // org.freedesktop.Notifications bus name'ini almaya çalışır. Plasma'nın
    // daemon'u name'i tutuyorsa false döner (conflict). Plasma config bozuk
    // veya non-KDE session'da name boştadır → true.
    bool start();
    bool isRegistered() const { return registered_; }

    QList<Notification> notifications() const { return notifications_; }
    int count() const { return notifications_.size(); }

    void clear();
    void dismissAt(int index);

    // UI'dan çağrılır: kullanıcı action butonuna tıkladı. ActionInvoked
    // signal'ini emit eder; gönderim app artık callback'i alır.
    void invokeAction(uint id, const QString &actionKey);

    void setOnChanged(std::function<void()> fn) { onChanged_ = std::move(fn); }

private:
    friend class NotificationsAdaptor;

    // Adaptor → buraya
    uint addNotification(Notification n, uint replacesId);
    void closeNotification(uint id, uint reason);

    NotificationsAdaptor *adaptor_ = nullptr;
    QList<Notification> notifications_;
    std::function<void()> onChanged_;
    bool registered_ = false;
    uint nextId_ = 1;
};

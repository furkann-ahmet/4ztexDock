#include "notificationserver.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusReply>
#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDebug>
#include <QLoggingCategory>

// Logging kategorisi — QT_LOGGING_RULES="dock.notif=false" ile sustur.
Q_LOGGING_CATEGORY(dockNotif, "dock.notif")

// Notify hints içindeki image-data (iiibiiay) — width, height, rowstride,
// has_alpha, bits_per_sample, channels, data. Qt'nin custom struct
// demarshalling'i için bu yardımcı tip.
struct ImageData {
    int width = 0;
    int height = 0;
    int rowstride = 0;
    bool hasAlpha = false;
    int bitsPerSample = 0;
    int channels = 0;
    QByteArray data;
};
Q_DECLARE_METATYPE(ImageData)

const QDBusArgument &operator>>(const QDBusArgument &arg, ImageData &img)
{
    arg.beginStructure();
    arg >> img.width >> img.height >> img.rowstride >> img.hasAlpha
        >> img.bitsPerSample >> img.channels >> img.data;
    arg.endStructure();
    return arg;
}

QDBusArgument &operator<<(QDBusArgument &arg, const ImageData &img)
{
    arg.beginStructure();
    arg << img.width << img.height << img.rowstride << img.hasAlpha
        << img.bitsPerSample << img.channels << img.data;
    arg.endStructure();
    return arg;
}

namespace {

// Notification spec image hint key'leri eski/yeni isim varyasyonlarıyla
// gelir. Önce yeni adı dene, sonra eski freedesktop spec'i, en son Plasma'nın
// dahili alternatif key'i.
QImage extractImageFromHints(const QVariantMap &hints)
{
    static const char *kKeys[] = {
        "image-data",       // FDO spec 1.2+
        "image_data",       // FDO spec eski
        "icon_data",        // çok eski
    };
    for (const char *k : kKeys) {
        auto it = hints.constFind(QString::fromLatin1(k));
        if (it == hints.constEnd()) continue;
        const QVariant v = it.value();
        if (!v.canConvert<QDBusArgument>()) continue;
        const QDBusArgument arg = v.value<QDBusArgument>();
        ImageData img;
        arg >> img;
        if (img.width <= 0 || img.height <= 0 || img.data.isEmpty()) continue;
        const QImage::Format fmt = img.hasAlpha ? QImage::Format_RGBA8888
                                                : QImage::Format_RGB888;
        QImage q(reinterpret_cast<const uchar *>(img.data.constData()),
                 img.width, img.height, img.rowstride, fmt);
        // copy() bağımsız buffer'a klonlar (ImageData scope'tan çıkacak)
        return q.copy();
    }
    return {};
}

} // namespace


// ============================================================================
// NotificationsAdaptor
// ============================================================================

NotificationsAdaptor::NotificationsAdaptor(NotificationServer *server)
    : QDBusAbstractAdaptor(server), server_(server)
{
    qDBusRegisterMetaType<ImageData>();
}

QStringList NotificationsAdaptor::GetCapabilities()
{
    // Spec'teki standart capability listesi. body-markup HTML alt-küme'sini
    // QLabel zaten renderlıyor; persistence: history listemiz var.
    return {
        "actions",
        "body",
        "body-hyperlinks",
        "body-markup",
        "icon-static",
        "persistence",
    };
}

uint NotificationsAdaptor::Notify(const QString &appName, uint replacesId,
                                   const QString &appIcon,
                                   const QString &summary, const QString &body,
                                   const QStringList &actions,
                                   const QVariantMap &hints,
                                   int /*expireTimeout*/)
{
    NotificationServer::Notification n;
    n.appName = appName;
    n.appIcon = appIcon;
    n.summary = summary;
    n.body = body;
    n.time = QDateTime::currentDateTime();

    // Actions: spec'te [key, label, key, label, ...] flat liste
    for (int i = 0; i + 1 < actions.size(); i += 2) {
        NotificationServer::ActionEntry a;
        a.key = actions.at(i);
        a.label = actions.at(i + 1);
        n.actions.push_back(a);
    }

    // Hint'lerden tipik alanlar
    if (auto it = hints.constFind("desktop-entry"); it != hints.constEnd())
        n.desktopEntry = it->toString();
    if (auto it = hints.constFind("urgency"); it != hints.constEnd())
        n.urgency = it->toInt();
    if (auto it = hints.constFind("image-path"); it != hints.constEnd())
        n.imagePath = it->toString();
    else if (auto it = hints.constFind("image_path"); it != hints.constEnd())
        n.imagePath = it->toString();
    n.imageRaw = extractImageFromHints(hints);
    if (n.imagePath.isEmpty() && n.imageRaw.isNull() && !appIcon.isEmpty()
        && (appIcon.startsWith('/') || appIcon.startsWith("file://"))) {
        n.imagePath = appIcon;
    }

    return server_->addNotification(std::move(n), replacesId);
}

void NotificationsAdaptor::CloseNotification(uint id)
{
    server_->closeNotification(id, /*reason=ClosedByCall*/ 3);
}

void NotificationsAdaptor::GetServerInformation(QString &name, QString &vendor,
                                                 QString &version,
                                                 QString &specVersion)
{
    name = "4ztexDock";
    vendor = "4ztex";
    version = "0.1.0";
    specVersion = "1.2";
}


// ============================================================================
// NotificationServer
// ============================================================================

NotificationServer::NotificationServer(QObject *parent) : QObject(parent)
{
    adaptor_ = new NotificationsAdaptor(this);
}

NotificationServer::~NotificationServer()
{
    if (registered_) {
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.unregisterObject("/org/freedesktop/Notifications");
        bus.unregisterService("org.freedesktop.Notifications");
    }
}

bool NotificationServer::start()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qCWarning(dockNotif) << "session bus not connected";
        return false;
    }

    // Politeness check: önce başka bir daemon'un (Plasma, dunst, mako, vs.)
    // org.freedesktop.Notifications adını çoktan kayıt edip etmediğine bak.
    // Etmişse SAHİBİ ALMA — başka uygulamaların notification akışını kırarız.
    // Bu, Plasma config bozuk olduğunda devreye girer; Plasma sonradan
    // düzelirse bizim önceden almış olmamız Plasma'nın daemon'unu engellerdi.
    if (bus.interface()->isServiceRegistered(
            "org.freedesktop.Notifications").value()) {
        qCInfo(dockNotif) << "org.freedesktop.Notifications zaten başka "
                   "bir daemon tarafından kayıtlı — sistemin daemon'una "
                   "müdahale etmiyoruz. Dock panelinde yalnızca dock-içi "
                   "notification'lar gösterilecek.";
        return false;
    }

    if (!bus.registerService("org.freedesktop.Notifications")) {
        // Race: az önce check yaptık ama biri arada aldı.
        qCWarning(dockNotif) << "org.freedesktop.Notifications "
                      "kaydedilemedi (race):"
                   << bus.lastError().message();
        return false;
    }

    if (!bus.registerObject("/org/freedesktop/Notifications", this)) {
        qCWarning(dockNotif) << "object kaydedilemedi:"
                   << bus.lastError().message();
        bus.unregisterService("org.freedesktop.Notifications");
        return false;
    }

    registered_ = true;
    qCInfo(dockNotif) << "org.freedesktop.Notifications kayıtlı — dock "
               "artık tüm sistem bildirimlerini alıyor.";
    return true;
}

uint NotificationServer::addNotification(Notification n, uint replacesId)
{
    if (replacesId != 0) {
        // Replace: aynı id'li notification'ı güncelle
        for (auto &existing : notifications_) {
            if (existing.id == replacesId) {
                n.id = replacesId;
                existing = std::move(n);
                if (onChanged_) onChanged_();
                return replacesId;
            }
        }
    }
    if (n.id == 0) n.id = nextId_++;
    notifications_.prepend(std::move(n));
    // Geçmiş listesinin tavanı — son 50 sonuçla sınırla
    while (notifications_.size() > 50) notifications_.removeLast();
    if (onChanged_) onChanged_();
    return notifications_.first().id;
}

void NotificationServer::closeNotification(uint id, uint reason)
{
    bool removed = false;
    for (int i = 0; i < notifications_.size(); ++i) {
        if (notifications_.at(i).id == id) {
            notifications_.removeAt(i);
            removed = true;
            break;
        }
    }
    if (removed) {
        Q_EMIT adaptor_->NotificationClosed(id, reason);
        if (onChanged_) onChanged_();
    }
}

void NotificationServer::clear()
{
    if (notifications_.isEmpty()) return;
    // Spec: her dismiss için NotificationClosed emit et (reason=2 = DismissedByUser)
    for (const auto &n : std::as_const(notifications_)) {
        Q_EMIT adaptor_->NotificationClosed(n.id, 2);
    }
    notifications_.clear();
    if (onChanged_) onChanged_();
}

void NotificationServer::dismissAt(int index)
{
    if (index < 0 || index >= notifications_.size()) return;
    const uint id = notifications_.at(index).id;
    notifications_.removeAt(index);
    Q_EMIT adaptor_->NotificationClosed(id, 2);
    if (onChanged_) onChanged_();
}

void NotificationServer::invokeAction(uint id, const QString &actionKey)
{
    if (id == 0) return;
    Q_EMIT adaptor_->ActionInvoked(id, actionKey);
    // Spec: action invoke sonrası notification kapanır (reason 2)
    closeNotification(id, 2);
}

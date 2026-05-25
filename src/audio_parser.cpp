// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio_parser.h"

#include <QRegularExpression>

namespace audio {

QString unquote(const QString &s)
{
    QString r = s.trimmed();
    if (r.size() >= 2 && r.startsWith('"') && r.endsWith('"')) {
        return r.mid(1, r.size() - 2);
    }
    return r;
}

int parseVolumePct(const QString &line)
{
    static const QRegularExpression re(R"((\d+)\s*%)");
    auto m = re.match(line);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}

QList<Device> parseDevices(const QString &out, const QString &header,
                            const QString &defaultName)
{
    QList<Device> list;
    Device cur;
    bool inProps = false;
    const QStringList lines = out.split('\n');
    const QString headerPrefix = header + " #";
    for (const QString &raw : lines) {
        if (raw.startsWith(headerPrefix)) {
            if (cur.id >= 0) list.push_back(cur);
            cur = Device();
            cur.id = raw.mid(headerPrefix.size()).toInt();
            inProps = false;
            continue;
        }
        if (cur.id < 0) continue;
        const QString stripped = raw.trimmed();
        if (stripped == QLatin1String("Properties:")) {
            inProps = true;
            continue;
        }
        if (inProps) {
            const int eq = stripped.indexOf('=');
            if (eq > 0) {
                const QString key = stripped.left(eq).trimmed();
                const QString val = unquote(stripped.mid(eq + 1));
                if (key == QLatin1String("device.icon_name")) cur.iconName = val;
            }
        } else if (stripped.startsWith(QLatin1String("Name:"))) {
            cur.name = stripped.mid(5).trimmed();
        } else if (stripped.startsWith(QLatin1String("Description:"))) {
            cur.description = stripped.mid(12).trimmed();
        } else if (stripped.startsWith(QLatin1String("Mute:"))) {
            cur.muted = stripped.contains(QLatin1String("yes"));
        } else if (stripped.startsWith(QLatin1String("Volume:"))) {
            cur.volume = parseVolumePct(stripped);
        }
    }
    if (cur.id >= 0) list.push_back(cur);
    for (Device &d : list) d.isDefault = (d.name == defaultName);
    return list;
}

QList<Stream> parseStreams(const QString &out)
{
    QList<Stream> list;
    Stream cur;
    bool inProps = false;
    const QStringList lines = out.split('\n');
    for (const QString &raw : lines) {
        if (raw.startsWith(QLatin1String("Sink Input #"))) {
            if (cur.id >= 0) list.push_back(cur);
            cur = Stream();
            cur.id = raw.mid(12).toInt();
            inProps = false;
            continue;
        }
        if (cur.id < 0) continue;
        const QString stripped = raw.trimmed();
        if (stripped == QLatin1String("Properties:")) {
            inProps = true;
            continue;
        }
        if (inProps) {
            const int eq = stripped.indexOf('=');
            if (eq > 0) {
                const QString key = stripped.left(eq).trimmed();
                const QString val = unquote(stripped.mid(eq + 1));
                if (key == QLatin1String("application.name")) cur.appName = val;
                else if (key == QLatin1String("application.process.binary")) cur.appBinary = val;
                else if (key == QLatin1String("application.process.id")) cur.pid = val.toInt();
                else if (key == QLatin1String("pipewire.sec.pid")) cur.hostPid = val.toInt();
                else if (key == QLatin1String("application.icon_name")) cur.iconName = val;
                else if (key == QLatin1String("media.name")) cur.mediaName = val;
                else if (key == QLatin1String("pipewire.access.portal.app_id"))
                    cur.flatpakAppId = val;
            }
        } else if (stripped.startsWith(QLatin1String("Sink:"))) {
            cur.sinkId = stripped.mid(5).trimmed().toInt();
        } else if (stripped.startsWith(QLatin1String("Client:"))) {
            cur.clientId = stripped.mid(7).trimmed().toInt();
        } else if (stripped.startsWith(QLatin1String("Mute:"))) {
            cur.muted = stripped.contains(QLatin1String("yes"));
        } else if (stripped.startsWith(QLatin1String("Volume:"))) {
            cur.volume = parseVolumePct(stripped);
        }
    }
    if (cur.id >= 0) list.push_back(cur);
    return list;
}

QHash<int, Client> parseClients(const QString &out)
{
    QHash<int, Client> map;
    Client cur;
    bool inProps = false;
    const QStringList lines = out.split('\n');
    for (const QString &raw : lines) {
        if (raw.startsWith(QLatin1String("Client #"))) {
            if (cur.id >= 0) map.insert(cur.id, cur);
            cur = Client();
            cur.id = raw.mid(8).toInt();
            inProps = false;
            continue;
        }
        if (cur.id < 0) continue;
        const QString stripped = raw.trimmed();
        if (stripped == QLatin1String("Properties:")) {
            inProps = true;
            continue;
        }
        if (inProps) {
            const int eq = stripped.indexOf('=');
            if (eq > 0) {
                const QString key = stripped.left(eq).trimmed();
                const QString val = unquote(stripped.mid(eq + 1));
                if (key == QLatin1String("application.name")) cur.appName = val;
                else if (key == QLatin1String("application.process.binary"))
                    cur.appBinary = val;
                else if (key == QLatin1String("application.process.id"))
                    cur.pid = val.toInt();
                else if (key == QLatin1String("pipewire.sec.pid"))
                    cur.hostPid = val.toInt();
                else if (key == QLatin1String("application.icon_name"))
                    cur.iconName = val;
                else if (key == QLatin1String("pipewire.access.portal.app_id"))
                    cur.flatpakAppId = val;
            }
        }
    }
    if (cur.id >= 0) map.insert(cur.id, cur);
    return map;
}

void mergeStreamWithClient(Stream &s, const QHash<int, Client> &clients)
{
    if (s.clientId < 0) return;
    auto it = clients.constFind(s.clientId);
    if (it == clients.constEnd()) return;
    const Client &c = it.value();
    if (s.appName.isEmpty())      s.appName = c.appName;
    if (s.appBinary.isEmpty())    s.appBinary = c.appBinary;
    if (s.iconName.isEmpty())     s.iconName = c.iconName;
    if (s.pid <= 0)               s.pid = c.pid;
    if (s.hostPid <= 0)           s.hostPid = c.hostPid;
    if (s.flatpakAppId.isEmpty()) s.flatpakAppId = c.flatpakAppId;
}

bool isMonitor(const Device &d)
{
    return d.name.endsWith(QLatin1String(".monitor"));
}

} // namespace audio

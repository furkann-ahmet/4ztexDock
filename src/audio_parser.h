// SPDX-License-Identifier: GPL-3.0-or-later
// 4ztexDock — PulseAudio / PipeWire (pactl) çıktısı parser'ları.
//
// `pactl list` text output'u line-by-line parse ederek Device, Stream ve Client
// objelerine çevirir. Saf veri katmanı — Qt widget / DBus / process bağımlılığı
// yok. Test edilebilir.
//
// Parse zinciri:
//   1. `pactl list sinks`        → parseDevices(out, "Sink", defaultSink)
//   2. `pactl list sources`      → parseDevices(out, "Source", defaultSource)
//   3. `pactl list sink-inputs`  → parseStreams(out)
//   4. `pactl list clients`      → parseClients(out)
//   5. mergeStreamWithClient()   → anonim stream'lere client metadata'sını gömer.

#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QHash>

namespace audio {

struct Device {
    int id = -1;
    QString name;          // alsa_output.xxx  (pactl internal id)
    QString description;   // human-readable
    int volume = 0;        // 0-150 (Pulse normalize range)
    bool muted = false;
    bool isDefault = false;
    QString iconName;      // device.icon_name property
};

struct Stream {
    int id = -1;
    int sinkId = -1;
    int volume = 0;
    bool muted = false;
    int pid = -1;          // application.process.id  (flatpak app'lerde container içi PID)
    int hostPid = -1;      // pipewire.sec.pid (kernel socket peer credential — host gerçek PID)
    int clientId = -1;     // "Client:" satırı (PipeWire client objesi)
    QString appName;       // application.name
    QString appBinary;     // application.process.binary
    QString iconName;      // application.icon_name
    QString mediaName;     // media.name
    QString flatpakAppId;  // pipewire.access.portal.app_id (.desktop ile birebir eşleşir)
};

// PipeWire/Pulse "client" objesi. Anonim sink-input'ların (Web Audio, oyun
// child process, vs.) application.* property'leri client level'da tutuluyor;
// sink-input boş gelebiliyor. Bu struct merge için kullanılır.
struct Client {
    int id = -1;
    int pid = -1;
    int hostPid = -1;
    QString appName;
    QString appBinary;
    QString iconName;
    QString flatpakAppId;
};

// Quoted string trim'i. pactl property değerleri tipik olarak çift tırnaklı:
//   application.name = "Spotify"
QString unquote(const QString &s);

// "Volume: front-left: 24904 / 38% / -25,21 dB, front-right: ..."  → 38.
int parseVolumePct(const QString &line);

// Sink veya source listesi. `header` = "Sink" veya "Source"; `defaultName`
// `pactl get-default-{sink,source}` çıktısı, isDefault flag'ı için.
QList<Device> parseDevices(const QString &out, const QString &header,
                           const QString &defaultName);

// `pactl list sink-inputs` çıktısı → uygulamaların aktif ses stream'leri.
QList<Stream> parseStreams(const QString &out);

// `pactl list clients` çıktısı → PipeWire client objeleri (id → Client).
QHash<int, Client> parseClients(const QString &out);

// Stream'in eksik (boş string / negatif int) app metadata'sını client objesinden
// tamamlar. Stream'de değer varsa korunur — sadece BOŞ field'lar doldurulur.
void mergeStreamWithClient(Stream &s, const QHash<int, Client> &clients);

// `.monitor` suffix'li source'lar her sink için bir loopback üretir (Pulse
// internal). UI'da göstermek istemediğimiz için filter helper'ı.
bool isMonitor(const Device &d);

} // namespace audio

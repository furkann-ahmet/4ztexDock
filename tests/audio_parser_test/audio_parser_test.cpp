// SPDX-License-Identifier: GPL-3.0-or-later
// audio_parser.{h,cpp} için unit test'ler.
//
// Gerçek `pactl` çıktısı fixture'ları string literal olarak gömülü; testler
// hiçbir process spawn etmez, tamamen pure parse. Edge case'ler:
//   - Flatpak Spotify stream (Client lookup + flatpakAppId)
//   - Anonim media.name="audio-src" (uygulama metadata'sı yok, client'tan tamamla)
//   - .monitor source filter
//   - Multi-device parse + default flagging

#include <QtTest>
#include "audio_parser.h"

using namespace audio;

class AudioParserTest : public QObject
{
    Q_OBJECT

private slots:
    // ------------------------------------------------------------------
    // unquote: pactl property değerlerinden tırnakları temizler
    // ------------------------------------------------------------------
    void unquote_strips_double_quotes()
    {
        QCOMPARE(unquote(QStringLiteral("\"Spotify\"")), QStringLiteral("Spotify"));
        QCOMPARE(unquote(QStringLiteral("  \"hello world\"  ")),
                 QStringLiteral("hello world"));
        // No quotes → return as-is (trimmed)
        QCOMPARE(unquote(QStringLiteral("   bare value   ")),
                 QStringLiteral("bare value"));
        // Sadece tek tırnak — değiştirilmemeli
        QCOMPARE(unquote(QStringLiteral("\"unbalanced")),
                 QStringLiteral("\"unbalanced"));
        // Boş string
        QCOMPARE(unquote(QString()), QString());
    }

    // ------------------------------------------------------------------
    // parseVolumePct: Volume satırından ilk % değerini çıkartır
    // ------------------------------------------------------------------
    void parseVolumePct_extracts_first_percent()
    {
        QCOMPARE(parseVolumePct(QStringLiteral(
            "Volume: front-left: 24904 /  38% / -25,21 dB, front-right: 24904 / 38%")),
            38);
        QCOMPARE(parseVolumePct(QStringLiteral(
            "Volume: mono: 65536 / 100% / 0,00 dB")), 100);
        QCOMPARE(parseVolumePct(QStringLiteral("Volume: yok")), 0);
        QCOMPARE(parseVolumePct(QString()), 0);
    }

    // ------------------------------------------------------------------
    // isMonitor: .monitor suffix'li device'ları flag'ler
    // ------------------------------------------------------------------
    void isMonitor_detects_pulse_monitor_loopback()
    {
        Device d;
        d.name = "alsa_output.pci-0000_00_1f.3.analog-stereo.monitor";
        QVERIFY(isMonitor(d));

        d.name = "alsa_input.pci-0000_00_1f.3.analog-stereo";
        QVERIFY(!isMonitor(d));

        d.name = QString();
        QVERIFY(!isMonitor(d));
    }

    // ------------------------------------------------------------------
    // parseDevices: temel sink listesi parse + isDefault flagging
    // ------------------------------------------------------------------
    void parseDevices_parses_sinks_with_default_flag()
    {
        const QString output = QStringLiteral(
            "Sink #55\n"
            "\tName: alsa_output.pci-0000_00_1f.3.analog-stereo\n"
            "\tDescription: Built-in Audio Analog Stereo\n"
            "\tMute: no\n"
            "\tVolume: front-left: 24904 / 38% / -25,21 dB\n"
            "\tProperties:\n"
            "\t\tdevice.icon_name = \"audio-card-analog-stereo\"\n"
            "Sink #56\n"
            "\tName: bluez_output.A4_C3_F0_8C_D4_5C.1\n"
            "\tDescription: Arctis 7+ Analog Stereo\n"
            "\tMute: yes\n"
            "\tVolume: front-left: 36864 / 56% / -16,01 dB\n"
            "\tProperties:\n"
            "\t\tdevice.icon_name = \"audio-headset\"\n");

        const auto devs = parseDevices(output, "Sink",
            "alsa_output.pci-0000_00_1f.3.analog-stereo");

        QCOMPARE(devs.size(), 2);

        QCOMPARE(devs.at(0).id, 55);
        QCOMPARE(devs.at(0).name,
                 QStringLiteral("alsa_output.pci-0000_00_1f.3.analog-stereo"));
        QCOMPARE(devs.at(0).description,
                 QStringLiteral("Built-in Audio Analog Stereo"));
        QCOMPARE(devs.at(0).volume, 38);
        QCOMPARE(devs.at(0).muted, false);
        QCOMPARE(devs.at(0).iconName,
                 QStringLiteral("audio-card-analog-stereo"));
        QCOMPARE(devs.at(0).isDefault, true);

        QCOMPARE(devs.at(1).id, 56);
        QCOMPARE(devs.at(1).muted, true);
        QCOMPARE(devs.at(1).volume, 56);
        QCOMPARE(devs.at(1).isDefault, false);
    }

    // ------------------------------------------------------------------
    // parseStreams: Spotify benzeri stream — full app metadata + clientId
    // ------------------------------------------------------------------
    void parseStreams_extracts_full_metadata()
    {
        const QString output = QStringLiteral(
            "Sink Input #26953\n"
            "\tDriver: PipeWire\n"
            "\tOwner Module: n/a\n"
            "\tClient: 26952\n"
            "\tSink: 55\n"
            "\tMute: no\n"
            "\tVolume: aux0: 65536 / 100% / 0,00 dB\n"
            "\tProperties:\n"
            "\t\tmedia.name = \"audio-src\"\n"
            "\t\tapplication.name = \"spotify\"\n"
            "\t\tapplication.process.binary = \"spotify\"\n"
            "\t\tapplication.process.id = \"4\"\n"
            "\t\tpipewire.sec.pid = \"32220\"\n"
            "\t\tpipewire.access.portal.app_id = \"com.spotify.Client\"\n");

        const auto streams = parseStreams(output);
        QCOMPARE(streams.size(), 1);
        const Stream &s = streams.first();
        QCOMPARE(s.id, 26953);
        QCOMPARE(s.sinkId, 55);
        QCOMPARE(s.clientId, 26952);
        QCOMPARE(s.volume, 100);
        QCOMPARE(s.muted, false);
        QCOMPARE(s.appName, QStringLiteral("spotify"));
        QCOMPARE(s.appBinary, QStringLiteral("spotify"));
        QCOMPARE(s.pid, 4);
        QCOMPARE(s.hostPid, 32220);
        QCOMPARE(s.flatpakAppId, QStringLiteral("com.spotify.Client"));
        QCOMPARE(s.mediaName, QStringLiteral("audio-src"));
    }

    // ------------------------------------------------------------------
    // parseStreams: anonim PipeWire stream — sadece media.name var, app metadata
    // tamamen boş (client lookup ile sonradan doldurulacak)
    // ------------------------------------------------------------------
    void parseStreams_handles_anonymous_stream()
    {
        const QString output = QStringLiteral(
            "Sink Input #999\n"
            "\tClient: 75\n"
            "\tSink: 55\n"
            "\tMute: no\n"
            "\tVolume: mono: 65536 / 100%\n"
            "\tProperties:\n"
            "\t\tmedia.name = \"audio-src\"\n"
            "\t\tnode.name = \"audio-src\"\n");

        const auto streams = parseStreams(output);
        QCOMPARE(streams.size(), 1);
        const Stream &s = streams.first();
        QCOMPARE(s.id, 999);
        QCOMPARE(s.clientId, 75);
        QVERIFY(s.appName.isEmpty());
        QVERIFY(s.appBinary.isEmpty());
        QCOMPARE(s.pid, -1);
        QCOMPARE(s.hostPid, -1);
        QVERIFY(s.flatpakAppId.isEmpty());
        QCOMPARE(s.mediaName, QStringLiteral("audio-src"));
    }

    // ------------------------------------------------------------------
    // parseClients: pipewire.sec.pid + flatpakAppId yakalama
    // ------------------------------------------------------------------
    void parseClients_extracts_app_metadata()
    {
        const QString output = QStringLiteral(
            "Client #26952\n"
            "\tDriver: PipeWire\n"
            "\tProperties:\n"
            "\t\tapplication.name = \"spotify\"\n"
            "\t\tapplication.process.binary = \"spotify\"\n"
            "\t\tapplication.process.id = \"4\"\n"
            "\t\tpipewire.sec.pid = \"32220\"\n"
            "\t\tpipewire.access.portal.app_id = \"com.spotify.Client\"\n"
            "Client #75\n"
            "\tProperties:\n"
            "\t\tapplication.name = \"libcanberra\"\n"
            "\t\tpipewire.sec.pid = \"2150\"\n");

        const auto clients = parseClients(output);
        QCOMPARE(clients.size(), 2);

        const Client spotify = clients.value(26952);
        QCOMPARE(spotify.id, 26952);
        QCOMPARE(spotify.appName, QStringLiteral("spotify"));
        QCOMPARE(spotify.appBinary, QStringLiteral("spotify"));
        QCOMPARE(spotify.pid, 4);
        QCOMPARE(spotify.hostPid, 32220);
        QCOMPARE(spotify.flatpakAppId, QStringLiteral("com.spotify.Client"));

        const Client canberra = clients.value(75);
        QCOMPARE(canberra.appName, QStringLiteral("libcanberra"));
        QCOMPARE(canberra.hostPid, 2150);
        QVERIFY(canberra.flatpakAppId.isEmpty());
    }

    // ------------------------------------------------------------------
    // mergeStreamWithClient: anonim stream'in eksik metadata'sını client'tan
    // tamamlar; dolu olan field'lar KORUNUR
    // ------------------------------------------------------------------
    void mergeStreamWithClient_fills_only_empty_fields()
    {
        Stream s;
        s.id = 999;
        s.clientId = 75;
        s.appName = QStringLiteral("existing-name");  // dolu — korunmalı
        // appBinary, flatpakAppId, pid: BOŞ → client'tan gelmeli

        QHash<int, Client> clients;
        Client c;
        c.id = 75;
        c.appName = QStringLiteral("client-name");       // ignore (stream'de dolu)
        c.appBinary = QStringLiteral("spotify");
        c.pid = 4;
        c.hostPid = 32220;
        c.flatpakAppId = QStringLiteral("com.spotify.Client");
        clients.insert(75, c);

        mergeStreamWithClient(s, clients);

        // Dolu olan KORUNDU
        QCOMPARE(s.appName, QStringLiteral("existing-name"));
        // Boş olanlar TAMAMLANDI
        QCOMPARE(s.appBinary, QStringLiteral("spotify"));
        QCOMPARE(s.pid, 4);
        QCOMPARE(s.hostPid, 32220);
        QCOMPARE(s.flatpakAppId, QStringLiteral("com.spotify.Client"));
    }

    // ------------------------------------------------------------------
    // mergeStreamWithClient: clientId yoksa veya hash'te bulunmazsa no-op
    // ------------------------------------------------------------------
    void mergeStreamWithClient_no_op_when_client_missing()
    {
        Stream s;
        s.clientId = -1;  // bilinmiyor
        QHash<int, Client> clients;
        Client c;
        c.id = 75;
        c.appBinary = QStringLiteral("spotify");
        clients.insert(75, c);

        mergeStreamWithClient(s, clients);
        QVERIFY(s.appBinary.isEmpty());

        s.clientId = 999;  // hash'te yok
        mergeStreamWithClient(s, clients);
        QVERIFY(s.appBinary.isEmpty());
    }

    // ------------------------------------------------------------------
    // parseStreams: birden fazla Sink Input parse'ı
    // ------------------------------------------------------------------
    void parseStreams_parses_multiple_inputs()
    {
        const QString output = QStringLiteral(
            "Sink Input #1\n"
            "\tSink: 55\n"
            "\tClient: 10\n"
            "\tMute: no\n"
            "\tVolume: mono: 65536 / 50%\n"
            "Sink Input #2\n"
            "\tSink: 56\n"
            "\tClient: 20\n"
            "\tMute: yes\n"
            "\tVolume: mono: 65536 / 75%\n");

        const auto streams = parseStreams(output);
        QCOMPARE(streams.size(), 2);
        QCOMPARE(streams.at(0).id, 1);
        QCOMPARE(streams.at(0).sinkId, 55);
        QCOMPARE(streams.at(0).volume, 50);
        QCOMPARE(streams.at(0).muted, false);
        QCOMPARE(streams.at(1).id, 2);
        QCOMPARE(streams.at(1).sinkId, 56);
        QCOMPARE(streams.at(1).muted, true);
    }

    // ------------------------------------------------------------------
    // Empty input → boş liste, crash yok
    // ------------------------------------------------------------------
    void parsers_handle_empty_input()
    {
        QVERIFY(parseStreams(QString()).isEmpty());
        QVERIFY(parseClients(QString()).isEmpty());
        QVERIFY(parseDevices(QString(), "Sink", "x").isEmpty());
    }
};

QTEST_GUILESS_MAIN(AudioParserTest)
#include "audio_parser_test.moc"

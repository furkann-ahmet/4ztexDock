// SPDX-License-Identifier: GPL-3.0-or-later
// icons.{h,cpp} için temel test'ler.
//
// DesktopIconResolver tüm sistem .desktop'larını disk'ten okuyan global
// singleton'a sahip — purely unit test edemiyoruz. Bu yüzden:
//   1. Mantık ünitesi: findByTokens'ın token splitting / intersection / minimum
//      score logic'i sistemi referans alarak smoke test'le doğrulanır
//   2. Cache erişimi: entry() boş key için boş dönmeli, kurulu app için bir şey
//      dönmeli (varsa).
//
// Sıfır .desktop dosyası olan minimal sistemde bu test'lerin çoğu boş set
// senaryosunu kapsar ve yine PASS'tir.

#include <QtTest>
#include <QGuiApplication>
#include <QDir>
#include "icons.h"

class IconsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // installedApps lazy cache'i ilk çağrıda diski tarar; warm-up yapalım.
        (void)DesktopIconResolver::installedApps();
    }

    // ------------------------------------------------------------------
    // entry: boş key → boş Entry
    // ------------------------------------------------------------------
    void entry_empty_key_returns_empty()
    {
        const auto e = DesktopIconResolver::entry(QString());
        QVERIFY(e.icon.isEmpty());
        QVERIFY(e.name.isEmpty());
        QVERIFY(e.exec.isEmpty());
    }

    // ------------------------------------------------------------------
    // entry: olmayan bir key → boş Entry (crash yok)
    // ------------------------------------------------------------------
    void entry_unknown_key_returns_empty()
    {
        const auto e = DesktopIconResolver::entry(
            QStringLiteral("zzz-definitely-not-installed-99999"));
        QVERIFY(e.icon.isEmpty());
    }

    // ------------------------------------------------------------------
    // findByTokens: boş app_id → boş Entry
    // ------------------------------------------------------------------
    void findByTokens_empty_input_returns_empty()
    {
        const auto e = DesktopIconResolver::findByTokens(QString());
        QVERIFY(e.icon.isEmpty());
    }

    // ------------------------------------------------------------------
    // findByTokens: 1-tokenlık girdi → minimum score 2 şartı nedeniyle boş
    // ------------------------------------------------------------------
    void findByTokens_single_token_below_threshold()
    {
        // Tek karakterli/kısa token'lar zaten filtreleniyor (length >= 3 şartı).
        const auto e1 = DesktopIconResolver::findByTokens(QStringLiteral("xy"));
        QVERIFY(e1.icon.isEmpty());

        // Tek bir 3+ karakter token, herhangi bir .desktop ile en fazla 1 ortak
        // token üretir — minimum 2 olmadığından boş dönmeli.
        const auto e2 = DesktopIconResolver::findByTokens(
            QStringLiteral("zzzunique"));
        QVERIFY(e2.icon.isEmpty());
    }

    // ------------------------------------------------------------------
    // installedApps: liste boş olabilir (CI sandbox) ama crash yok ve sıralı
    // ------------------------------------------------------------------
    void installedApps_returns_sorted_list()
    {
        const auto &apps = DesktopIconResolver::installedApps();
        // Sıralı mı?
        for (int i = 1; i < apps.size(); ++i) {
            QVERIFY2(apps.at(i - 1).name.localeAwareCompare(apps.at(i).name) <= 0,
                     "installedApps must be alphabetically sorted by name");
        }
        // Hepsinde exec + name dolu olmalı
        for (const auto &e : apps) {
            QVERIFY(!e.name.isEmpty());
            QVERIFY(!e.exec.isEmpty());
        }
    }

    // ------------------------------------------------------------------
    // Icons::trySingle: file path varsa direkt yükle, yoksa fromTheme dener
    // ------------------------------------------------------------------
    void trySingle_empty_name_returns_null()
    {
        QVERIFY(Icons::trySingle(QString()).isNull());
    }

    void trySingle_invalid_path_returns_null_silent()
    {
        // Slash içeren ama mevcut olmayan path → null QIcon, crash yok
        const QIcon ic = Icons::trySingle(
            QStringLiteral("/definitely/not/a/real/path.png"));
        QVERIFY(ic.isNull());
    }
};

// Icons API'si QIcon kullanır → QGuiApplication şart (Q_PLATFORM_TEST gerek
// duyuyor — QTEST_MAIN'in QApplication versiyonu). Headless CI için
// QT_QPA_PLATFORM=offscreen env var ile çalıştır.
QTEST_MAIN(IconsTest)
#include "icons_test.moc"

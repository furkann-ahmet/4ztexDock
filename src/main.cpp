// SPDX-License-Identifier: GPL-3.0-or-later
// 4ztexDock — Wayland layer-shell taskbar/launcher for KDE Plasma 6.
// Copyright (C) 2026 4ztex <furkan.ahmet@kuika.com>
// See LICENSE in the repository root for terms.

#include "kwinbridge.h"
#include "icons.h"
#include "audio_parser.h"
#include "config.h"
#include "kdetools.h"
#include "notificationserver.h"
#include "sysctl.h"
#include "traybridge.h"
#include "x11support.h"

#include <QLoggingCategory>

// Logging kategorileri — QT_LOGGING_RULES="dock.preview=false" gibi env var
// ile runtime'da on/off. Default'ta hepsi açık (info+warning).
Q_LOGGING_CATEGORY(dockPreview,  "dock.preview")   // KWin ScreenShot2 pencere preview yakalama
Q_LOGGING_CATEGORY(dockSecurity, "dock.security")  // Saldırgan-kontrolünde input validation
Q_LOGGING_CATEGORY(dockEnv,      "dock.env")       // Runtime environment / dependency checks

#include <LayerShellQt/Window>

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusUnixFileDescriptor>
#include <QDBusVariant>
#include <QDrag>
#include <QGraphicsOpacityEffect>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainterPath>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QPointer>
#include <QRegularExpression>
#include <QUrl>
#include <QSettings>
#include <QStandardPaths>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QTranslator>
#include <QLinearGradient>
#include <QLineEdit>
#include <QMargins>
#include <QPainter>
#include <QPaintEvent>
#include <QProcess>
#include <QPushButton>
#include <QRadialGradient>
#include <QScreen>
#include <QScrollArea>
#include <QSizePolicy>
#include <QSlider>
#include <QSet>
#include <QStringList>
#include <QStyle>
#include <QTime>
#include <QTimer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

#include <sys/mman.h>
#include <unistd.h>

namespace {

constexpr int BarHeight = 58;
constexpr int BarTopProtrusion = 16;
constexpr int WindowHeight = BarHeight + BarTopProtrusion;
constexpr int BarMargin = 16;
constexpr int MainButtonSize = 72;

// Inline preview yüksekliği — toolbar window'unun BarTopProtrusion'a ek
// olarak bunun kadar üst tarafa expand ettiği yer. WindowPreviewCard
// boyutuna göre yeterli.
constexpr int PreviewRowHeight = 200;
constexpr int PreviewRowTopMargin = 12;

// Preview row'un kullandığı tek pencere kaydı. DockWindow ve
// WindowPreviewRow ortak bu tipi kullanıyor.
struct PreviewEntry {
    QString windowId;
    QString title;
    QIcon icon;
};

class WindowPreviewRow;  // forward; tanımı aşağıda

// Design tokens — bkz. style/dock.qss header.
// Notr zinc taban. Accent kit kullanilir.
namespace Color
{
constexpr int ToolbarBgA = 225;
constexpr int PopupBgA = 238;
const QColor ToolbarBg(13, 14, 18, ToolbarBgA);
const QColor ToolbarBorder(255, 255, 255, 22);
const QColor ToolbarShadow(0, 0, 0, 80);
const QColor PopupBg(13, 14, 18, PopupBgA);
const QColor PopupBorder(255, 255, 255, 22);
const QColor Text(244, 244, 245, 235);
const QColor SecondaryText(161, 161, 170, 200);
const QColor Accent(167, 139, 250);            // violet-400
const QColor AccentText(237, 233, 254, 245);   // violet-100
const QColor Danger(244, 63, 94, 230);
}

// Çoklu pass + exponential alpha decay ile feathered drop shadow. Her pass
// bir rounded rect ama pass sayısı yüksek (16) ve her birinin alpha'sı çok
// düşük olduğundan toplam etki Gaussian blur'a yakın bir gradient — kenar
// keskinliği oluşmaz, gölge çevreye yayılır.
inline void drawFeatheredShadow(QPainter &p, const QRectF &rect, qreal radius,
                                qreal maxExpand, qreal yOffMax, int maxAlpha,
                                int passes = 16)
{
    p.setPen(Qt::NoPen);
    for (int i = 0; i < passes; ++i) {
        const qreal t = qreal(i) / qreal(std::max(passes - 1, 1));   // 0..1
        // ease-in expand: ilk pass'ler darken, son pass'ler geniş yayılır.
        const qreal expand = maxExpand * std::pow(t, 1.6);
        const qreal yOff = yOffMax * std::pow(t, 1.2);
        // Alpha eksponansiyel azalır — her pass'in kenarı bir öncekinin
        // alpha'sıyla blend olur, görsel olarak smooth gradient.
        const qreal a = maxAlpha * std::pow(1.0 - t, 1.5);
        const int alpha = qRound(a);
        if (alpha <= 0) continue;
        p.setBrush(QColor(0, 0, 0, alpha));
        // radius'u expand ile birlikte agresif büyüt: rounded rect kenarları
        // dış pass'lerde çok yumuşak olur, sharp edge oluşmaz.
        const qreal r = radius + expand * 0.6;
        p.drawRoundedRect(rect.adjusted(-expand, yOff,
                                          expand, yOff + expand * 0.4),
                          r, r);
    }
}

// A floating "island" container — paints a pill-shaped bg with feathered
// shadow and a hairline border, and exposes its inner QHBoxLayout so callers
// can drop widgets in. Used as the visual chrome for the music, tray, and
// clock sections (the task icons stay un-containered between the islands).
class NodeContainer : public QWidget
{
public:
    explicit NodeContainer(QWidget *parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        // Fixed height = bar interior minus 2px breathing room. Keeps every
        // node the same height regardless of its content, so all the pill
        // radii match and the islands read as a row.
        setFixedHeight(56);

        auto *l = new QHBoxLayout(this);
        l->setContentsMargins(14, 0, 14, 0);
        l->setSpacing(6);
        layout_ = l;
    }

    QHBoxLayout *contentLayout() const { return layout_; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const QRectF rect = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = rect.height() / 2.0;

        // Minimal feathered shadow — dar bant, düşük alpha, sade. Çevreye
        // taşma az, kenar keskinliği da yok (kademeli pass).
        drawFeatheredShadow(p, rect, radius, /*maxExpand*/ 6.0,
                            /*yOffMax*/ 3.0, /*maxAlpha*/ 30, /*passes*/ 8);
        p.setPen(Qt::NoPen);

        p.setBrush(Color::ToolbarBg);
        p.drawRoundedRect(rect, radius, radius);

        p.setPen(QPen(Color::ToolbarBorder, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rect, radius, radius);
    }

private:
    QHBoxLayout *layout_;
};

class DockWindow : public QWidget
{
public:
    using QWidget::QWidget;

    void setFloatingButton(QWidget *button, QWidget *anchor = nullptr)
    {
        floatingButton_ = button;
        floatingAnchor_ = anchor;
        positionFloatingButton();
    }

    // Wire up the layer-shell window so the bar can update its anchor
    // margins each animation tick — that's how we keep the bar centered
    // horizontally while it grows/shrinks instead of left-anchored growth.
    void setLayerShell(LayerShellQt::Window *layerWindow, int outputWidth)
    {
        layerWindow_ = layerWindow;
        outputWidth_ = outputWidth;
        if (paintedBarWidth_ > 0) {
            applyBarWidth(paintedBarWidth_);
        }
    }

    void kickResize()
    {
        // Force the entire widget hierarchy to settle BEFORE we measure the
        // content width. Adding/removing task buttons invalidates child layout
        // sizeHints, but Qt defers LayoutRequest delivery to the next event-
        // loop tick. If we measure now we get the *previous* width and the
        // bar animates one step behind every change.
        if (auto *l = layout()) {
            l->invalidate();
            l->activate();
        }
        adjustWidthToContent();
    }

    void enableFrameHeartbeat()
    {
        if (heartbeat_) {
            return;
        }
        heartbeat_ = new QTimer(this);
        heartbeat_->setTimerType(Qt::PreciseTimer);
        heartbeat_->setInterval(16);
        QObject::connect(heartbeat_, &QTimer::timeout, this, [this]() {
            // Always synchronously repaint so the Wayland surface has fresh
            // damage every frame; otherwise the compositor treats it as idle
            // and our paints queue up until another surface presents.
            repaint();
        });
        heartbeat_->start();
    }


    // Inline pencere önizlemesi: WindowPreviewRow toolbar window'un üst
    // bölgesinde child widget olarak çizilir. Surface yüksekliği bu süre
    // boyunca PreviewRowHeight kadar artırılır (anchor bottom olduğu için
    // üst kenar yukarı doğru genişler). Top-level popup, xdg-popup positioner,
    // pointer grab YOK — tamamen standart Qt event akışı.
    void showPreview(const QString &appKey,
                     const QList<PreviewEntry> &entries,
                     int anchorCxInBar,
                     std::function<void(const QString &)> onActivate,
                     std::function<void(const QString &)> onClose,
                     std::function<void()> onRowEnter,
                     std::function<void()> onRowLeave);

    void hidePreview(bool instant = false);

    QString currentPreviewKey() const { return currentPreviewKey_; }
    bool previewVisible() const { return previewVisible_; }

    // Mouse'un toolbar surface'ine girip çıkışı — Wayland'da QCursor::pos()
    // surface dışına güvenilir bilgi vermediği için bu widget leave/enter
    // event'lerini güveniyoruz. Cursor surface'i terk edince compositor
    // leave eventi yollar, bu bize geliyor.
    void setOnSurfaceLeave(std::function<void()> fn) { onSurfaceLeave_ = std::move(fn); }
    void setOnSurfaceEnter(std::function<void()> fn) { onSurfaceEnter_ = std::move(fn); }

protected:
    bool event(QEvent *event) override
    {
        if (event->type() == QEvent::LayoutRequest) {
            if (QApplication::activePopupWidget()) {
                return QWidget::event(event);
            }
            adjustWidthToContent();
        }
        return QWidget::event(event);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        positionFloatingButton();
        // Yeni surface alanı (preview expand sonrası) Qt'nin default paint
        // event'inden geçmediği için eski buffer siyah görünüyordu. Resize
        // event'inde tüm widget'ı paint için işaretle.
        update();
        // Note: do NOT call layer->setDesiredSize here. The animation path is
        // the only place that should drive the size; otherwise compositor
        // configure events feed back into a duplicate resize loop.
    }

    void enterEvent(QEnterEvent *event) override
    {
        QWidget::enterEvent(event);
        if (onSurfaceEnter_) onSurfaceEnter_();
    }
    void leaveEvent(QEvent *event) override
    {
        QWidget::leaveEvent(event);
        if (onSurfaceLeave_) onSurfaceLeave_();
    }

    void paintEvent(QPaintEvent *event) override
    {
        QPainter p(this);
        p.setCompositionMode(QPainter::CompositionMode_Source);
        p.fillRect(rect(), Qt::transparent);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        QWidget::paintEvent(event);
    }

private:
    int currentContentWidth() const
    {
        if (auto *l = layout()) {
            const int margins = l->contentsMargins().left() + l->contentsMargins().right();
            return std::max(0, l->sizeHint().width() - margins);
        }
        return 0;
    }

    void adjustWidthToContent()
    {
        const int target = currentContentWidth();
        if (target <= 0 || target == animTarget_) {
            return;
        }
        animTarget_ = target;
        paintedBarWidth_ = target;
        applyBarWidth(target);
    }

    // Resize the bar widget to its content width and position it on the
    // output so the launcher dome lands at screen center, even when content
    // on either side is asymmetric (e.g. the music pill is hidden).
    void applyBarWidth(int contentWidth)
    {
        const int marginsW = layout()
                                 ? layout()->contentsMargins().left()
                                       + layout()->contentsMargins().right()
                                 : 36;
        const int outer = std::max(0, contentWidth) + marginsW;
        if (outer <= 0) return;

        // Resize the widget first so child layouts settle and the floating
        // launcher's anchor reports its post-resize coordinate.
        if (width() != outer) {
            setFixedWidth(outer);
            if (auto *l = layout()) {
                l->activate();
            }
            positionFloatingButton();
        }

        if (layerWindow_ && outputWidth_ > 0) {
            // Where is the launcher in bar-local coords? Anchor center is
            // the source of truth; fall back to bar midpoint if it hasn't
            // been wired up yet.
            int launcherX = outer / 2;
            if (floatingAnchor_ && floatingAnchor_->width() > 0) {
                const QPoint p = floatingAnchor_->mapTo(this, QPoint(0, 0));
                launcherX = p.x() + floatingAnchor_->width() / 2;
            }
            int leftMargin = outputWidth_ / 2 - launcherX;
            if (leftMargin < 0) leftMargin = 0;
            if (leftMargin + outer > outputWidth_) {
                leftMargin = std::max(0, outputWidth_ - outer);
            }
            const int rightMargin = std::max(0, outputWidth_ - leftMargin - outer);
            layerWindow_->setMargins(QMargins(leftMargin, 0, rightMargin, 0));
        }
        repaint();
    }

    void positionFloatingButton(int barWidth = -1)
    {
        if (!floatingButton_) {
            return;
        }
        const int effectiveWidth = barWidth >= 0 ? barWidth : width();
        int x;
        if (floatingAnchor_) {
            const QPoint anchorPos = floatingAnchor_->mapTo(this, QPoint(0, 0));
            x = anchorPos.x() + (floatingAnchor_->width() - floatingButton_->width()) / 2;
        } else {
            x = (effectiveWidth - floatingButton_->width()) / 2;
        }
        // Launcher dome'un Y'si window üst kenarına değil, BAR'ın üst kenarına
        // göre olmalı. Bar items layout'un topMargin'inden başlıyor; launcher
        // BarTopProtrusion kadar yukarı taşıyor. Window expand ettiğinde
        // topMargin artıyor — launcher onu da takip etsin.
        int y = 0;
        if (auto *l = layout()) {
            y = std::max(0, l->contentsMargins().top() - BarTopProtrusion);
        }
        floatingButton_->move(x, y);
        floatingButton_->raise();
    }

    void positionPreviewRow();
    void updatePreviewSurfaceHeight();

    QWidget *floatingButton_ = nullptr;
    QWidget *floatingAnchor_ = nullptr;
    QTimer *heartbeat_ = nullptr;
    QTimer *animTimer_ = nullptr;
    int animTarget_ = 0;
    int animStartWidth_ = 0;
    int animEndWidth_ = 0;
    qint64 animStartMs_ = 0;
    int animDurationMs_ = 220;
    int paintedBarWidth_ = 0;
    LayerShellQt::Window *layerWindow_ = nullptr;
    int outputWidth_ = 0;

    WindowPreviewRow *previewRow_ = nullptr;
    QString currentPreviewKey_;
    int lastAnchorCx_ = 0;
    bool previewVisible_ = false;
    QVariantAnimation *previewHeightAnim_ = nullptr;
    qreal previewProgress_ = 0.0;
    std::function<void()> onSurfaceEnter_;
    std::function<void()> onSurfaceLeave_;

};

class CircleIconButton : public QPushButton
{
public:
    explicit CircleIconButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setFlat(true);
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
        unsetCursor();

        hoverAnimation_.setDuration(160);
        hoverAnimation_.setStartValue(0.0);
        hoverAnimation_.setEndValue(1.0);
        hoverAnimation_.setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(&hoverAnimation_, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
            hoverProgress_ = value.toReal();
            update();
        });
    }

protected:
    void enterEvent(QEnterEvent *event) override
    {
        QPushButton::enterEvent(event);
        updateHoverState(event->position().toPoint());
    }

    void leaveEvent(QEvent *event) override
    {
        QPushButton::leaveEvent(event);
        setCircularHover(false);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        QPushButton::mouseMoveEvent(event);
        updateHoverState(event->position().toPoint());
    }

    bool hitButton(const QPoint &position) const override
    {
        return isInsideVisualCircle(position);
    }

    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event)

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const qreal hover = hoverProgress_;
        const bool pressed = isDown();
        const qreal borderWidth = 1.0;
        const qreal baseVisualSize = qMin<qreal>(width(), height()) - 6.0;
        const qreal visualSize = pressed ? baseVisualSize * 0.96 : baseVisualSize;
        const qreal radius = visualSize / 2.0;
        const qreal hoverShift = pressed ? 0.0 : -hover;
        const QRectF buttonRect((width() - visualSize) / 2.0,
                                (height() - visualSize) / 2.0 + hoverShift,
                                visualSize,
                                visualSize);

        // Minimal feathered shadow — launcher dome ihtiyacına göre biraz
        // daha güçlü ama yine compact. Hover'da hafif yoğunlaşır.
        drawFeatheredShadow(painter, buttonRect, radius,
                            /*maxExpand*/ 8.0 + 2.0 * hover,
                            /*yOffMax*/ 4.0,
                            /*maxAlpha*/ qRound(40 + 12 * hover),
                            /*passes*/ 8);
        painter.setPen(Qt::NoPen);

        // Single neutral dark gradient body + thin white rim. No accent, no glow.
        // The icon carries the brand — chrome stays out of its way.
        QLinearGradient baseGradient(buttonRect.topLeft(), buttonRect.bottomLeft());
        baseGradient.setColorAt(0.0, QColor(28, 30, 36, 248));
        baseGradient.setColorAt(1.0, QColor(13, 14, 18, 248));

        const QColor border = mixColor(QColor(255, 255, 255, 38),
                                       QColor(255, 255, 255, 64), hover);
        painter.setPen(QPen(border, borderWidth));
        painter.setBrush(baseGradient);
        painter.drawRoundedRect(buttonRect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);

        painter.setPen(Qt::NoPen);

        // On hover, very subtle white inner glow — affordance, not decoration.
        if (hover > 0.0) {
            QRadialGradient halo(buttonRect.center(), buttonRect.width() * 0.55);
            halo.setColorAt(0.0, QColor(255, 255, 255, qRound(18 * hover)));
            halo.setColorAt(1.0, QColor(255, 255, 255, 0));
            painter.setBrush(halo);
            painter.drawRoundedRect(buttonRect.adjusted(1.0, 1.0, -1.0, -1.0),
                                    radius - 1.0, radius - 1.0);
        }

        // Top inset highlight — subtle dome feel.
        QLinearGradient insetHighlight(buttonRect.topLeft(), buttonRect.bottomLeft());
        insetHighlight.setColorAt(0.0, QColor(255, 255, 255, 18));
        insetHighlight.setColorAt(0.40, QColor(255, 255, 255, 0));
        insetHighlight.setColorAt(1.0, QColor(0, 0, 0, 30));
        painter.setBrush(insetHighlight);
        painter.drawRoundedRect(buttonRect.adjusted(1.5, 1.5, -1.5, -1.5),
                                radius - 1.5, radius - 1.5);

        if (!icon().isNull()) {
            const QSize size = iconSize();
            const QRect iconRect(buttonRect.center().x() - size.width() / 2,
                                 buttonRect.center().y() - size.height() / 2,
                                 size.width(),
                                 size.height());
            icon().paint(&painter, iconRect, Qt::AlignCenter, isEnabled() ? QIcon::Normal : QIcon::Disabled);
        }
    }

private:
    static QColor mixColor(const QColor &from, const QColor &to, qreal progress)
    {
        const qreal t = qBound(0.0, progress, 1.0);
        return QColor(qRound(from.red() + (to.red() - from.red()) * t),
                      qRound(from.green() + (to.green() - from.green()) * t),
                      qRound(from.blue() + (to.blue() - from.blue()) * t),
                      qRound(from.alpha() + (to.alpha() - from.alpha()) * t));
    }

    void animateHover(qreal target)
    {
        hoverAnimation_.stop();
        hoverAnimation_.setStartValue(hoverProgress_);
        hoverAnimation_.setEndValue(target);
        hoverAnimation_.start();
    }

    bool isInsideVisualCircle(const QPoint &position) const
    {
        const qreal baseVisualSize = qMin<qreal>(width(), height()) - 6.0;
        const QPointF center(width() / 2.0, height() / 2.0);
        const qreal radius = baseVisualSize / 2.0;
        const QPointF delta = QPointF(position) - center;
        return delta.x() * delta.x() + delta.y() * delta.y() <= radius * radius;
    }

    void updateHoverState(const QPoint &position)
    {
        setCircularHover(isInsideVisualCircle(position));
    }

    void setCircularHover(bool hovered)
    {
        if (circularHover_ == hovered) {
            return;
        }

        circularHover_ = hovered;
        if (hovered) {
            setCursor(Qt::PointingHandCursor);
        } else {
            unsetCursor();
        }
        animateHover(hovered ? 1.0 : 0.0);
    }

    qreal hoverProgress_ = 0.0;
    bool circularHover_ = false;
    QVariantAnimation hoverAnimation_;
};

// Base class for icon buttons that need animated hover crossfade + press
// scale-down. Drops QSS-driven hover in favor of a single animated bg paint,
// scaling the entire visual by ~6% when held. Used by TrayButton,
// TaskItemButton, and the media controls in NowPlayingWidget.
// Layout container'ı tıklanabilir yapmak için minimal QWidget alt sınıfı.
// Notification row'unun default action invoke'u ve audio panel cihaz satırının
// "tıklayınca default'a al" davranışı bu sınıfı kullanır. Çocuk QPushButton'lar
// kendi click event'lerini consume ettiği için iç kontrol etkileşimleri
// bubble up etmez.
class ClickableWidget : public QWidget
{
public:
    explicit ClickableWidget(QWidget *parent = nullptr) : QWidget(parent) {}
    std::function<void()> onClick;

protected:
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->pos()) && onClick) {
            onClick();
        }
        QWidget::mouseReleaseEvent(e);
    }
};

class HoverPressIconButton : public QPushButton
{
public:
    explicit HoverPressIconButton(QWidget *parent = nullptr) : QPushButton(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setCursor(Qt::PointingHandCursor);

        hoverAnim_.setDuration(140);
        hoverAnim_.setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(&hoverAnim_, &QVariantAnimation::valueChanged, this,
                         [this](const QVariant &v) {
                             hover_ = v.toReal();
                             update();
                         });

        pressAnim_.setDuration(110);
        pressAnim_.setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(&pressAnim_, &QVariantAnimation::valueChanged, this,
                         [this](const QVariant &v) {
                             press_ = v.toReal();
                             update();
                         });
    }

    void setRadius(qreal r) { radius_ = r; update(); }
    qreal radius() const { return radius_; }

    // Hover'ı manuel kilitler. Inline preview animasyonu boyunca window
    // resize child geometry'lerini yeniden hesaplıyor ve Qt mouse-under
    // tracking'i bu süreçte transient olarak kayıp gidiyor — button'a
    // yapay leaveEvent geliyor, hover anim 0'a düşüyor. Lock'la bunu
    // ignore edip animasyon süresince hover'ı 1.0'da tutuyoruz.
    void setHoverLocked(bool locked)
    {
        if (hoverLocked_ == locked) return;
        hoverLocked_ = locked;
        if (locked) {
            animateTo(hoverAnim_, hover_, 1.0);
        } else if (!underMouse()) {
            animateTo(hoverAnim_, hover_, 0.0);
        }
    }

protected:
    qreal hover_ = 0.0;
    qreal press_ = 0.0;
    qreal radius_ = 9.0;
    bool hoverLocked_ = false;

    // Subclasses can return >1.0 to grow the entire visual (e.g. active task
    // icons grow ~10% with animation). Combined multiplicatively with the
    // press scale-down.
    virtual qreal extraScale() const { return 1.0; }

    void enterEvent(QEnterEvent *e) override
    {
        QPushButton::enterEvent(e);
        animateTo(hoverAnim_, hover_, 1.0);
    }

    void leaveEvent(QEvent *e) override
    {
        QPushButton::leaveEvent(e);
        if (!hoverLocked_) {
            animateTo(hoverAnim_, hover_, 0.0);
        }
    }

    void mousePressEvent(QMouseEvent *e) override
    {
        animateTo(pressAnim_, press_, 1.0);
        QPushButton::mousePressEvent(e);
    }

    void mouseReleaseEvent(QMouseEvent *e) override
    {
        animateTo(pressAnim_, press_, 0.0);
        QPushButton::mouseReleaseEvent(e);
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        // Press scale-down combined with any subclass scale-up (active state).
        const qreal scale = (1.0 - 0.06 * press_) * extraScale();
        const qreal cx = width() / 2.0;
        const qreal cy = height() / 2.0;
        p.translate(cx, cy);
        p.scale(scale, scale);
        p.translate(-cx, -cy);

        const QRectF rect = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        paintBackground(p, rect);
        paintForeground(p, rect);
    }

    virtual void paintBackground(QPainter &p, const QRectF &rect)
    {
        const int bgAlpha = qRound(16 * hover_);
        const int borderAlpha = qRound(22 * hover_);
        if (bgAlpha > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, bgAlpha));
            p.drawRoundedRect(rect, radius_, radius_);
        }
        if (borderAlpha > 0) {
            p.setPen(QPen(QColor(255, 255, 255, borderAlpha), 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(rect, radius_, radius_);
        }
    }

    virtual void paintForeground(QPainter &p, const QRectF &rect)
    {
        if (!icon().isNull()) {
            const QSize is = iconSize();
            const QPointF c = rect.center();
            const QRect ir(qRound(c.x() - is.width() / 2.0),
                           qRound(c.y() - is.height() / 2.0),
                           is.width(), is.height());
            icon().paint(&p, ir, Qt::AlignCenter,
                         isEnabled() ? QIcon::Normal : QIcon::Disabled);
        } else if (!text().isEmpty()) {
            p.setPen(QColor(244, 244, 245, 235));
            p.setFont(font());
            p.drawText(rect, Qt::AlignCenter, text());
        }
    }

private:
    void animateTo(QVariantAnimation &a, qreal from, qreal to)
    {
        a.stop();
        a.setStartValue(from);
        a.setEndValue(to);
        a.start();
    }

    QVariantAnimation hoverAnim_;
    QVariantAnimation pressAnim_;
};

class TrayButton : public HoverPressIconButton
{
public:
    using HoverPressIconButton::HoverPressIconButton;

    void setBadge(int count)
    {
        if (badge_ == count) {
            return;
        }
        badge_ = count;
        update();
    }

protected:
    void paintForeground(QPainter &p, const QRectF &rect) override
    {
        HoverPressIconButton::paintForeground(p, rect);
        if (badge_ <= 0) {
            return;
        }

        const qreal diameter = 11.5;
        const QRectF dot(width() - diameter - 4.0, 4.0, diameter, diameter);

        p.setPen(QPen(QColor(13, 14, 18, 238), 1.2));
        p.setBrush(Color::Danger);
        p.drawEllipse(dot);

        QFont badgeFont = p.font();
        badgeFont.setPointSizeF(6.5);
        badgeFont.setBold(true);
        p.setFont(badgeFont);
        p.setPen(QColor(255, 255, 255, 240));
        p.drawText(dot.translated(0, -0.5), Qt::AlignCenter, QString::number(badge_));
    }

private:
    int badge_ = 0;
};

class ActiveWindowLabel : public QWidget
{
public:
    explicit ActiveWindowLabel(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName("activeWindowLabel");
        setFixedSize(220, 42);

        auto *column = new QVBoxLayout(this);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(1);

        title_ = new QLabel;
        title_->setObjectName("activeWindowTitle");
        title_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        column->addWidget(title_);

        subtitle_ = new QLabel;
        subtitle_->setObjectName("activeWindowSubtitle");
        subtitle_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        column->addWidget(subtitle_);

        connectToBridge();
        updateText(QString());
    }

private:
    void connectToBridge()
    {
        auto *bridge = KWinBridge::instance();
        if (!bridge) {
            return;
        }
        QObject::connect(bridge, &KWinBridge::activeWindowChanged,
                         this, [this](const QString &id) {
                             activeId_ = id;
                             updateText(titles_.value(id));
                         });
        QObject::connect(bridge, &KWinBridge::windowAdded,
                         this, [this](const QString &id, const QString &cls,
                                      const QString &title, bool normal,
                                      const QString &, int, int) {
                             Q_UNUSED(cls);
                             if (!normal) return;
                             titles_[id] = title;
                             if (id == activeId_) {
                                 updateText(title);
                             }
                         });
        QObject::connect(bridge, &KWinBridge::windowUpdated,
                         this, [this](const QString &id, const QString &title) {
                             titles_[id] = title;
                             if (id == activeId_) {
                                 updateText(title);
                             }
                         });
        QObject::connect(bridge, &KWinBridge::windowRemoved,
                         this, [this](const QString &id) {
                             titles_.remove(id);
                             if (id == activeId_) {
                                 activeId_.clear();
                                 updateText(QString());
                             }
                         });
    }

    void updateText(const QString &title)
    {
        const QDate today = QDate::currentDate();
        const QLocale loc;
        const QString fullDate = loc.toString(today, "dd MMMM yyyy");
        const QString shortDate = loc.toString(today, "dd MMMM");
        const QString day = loc.toString(today, "dddd");

        if (title.isEmpty() || title.startsWith("4ztexDock")) {
            title_->setText(fullDate);
            subtitle_->setText(day);
            setToolTip(fullDate + " " + day);
            return;
        }

        QFontMetrics metrics(title_->font());
        const QString elided = metrics.elidedText(title, Qt::ElideRight, title_->width() > 0 ? title_->width() - 2 : width() - 2);
        title_->setText(elided);
        subtitle_->setText(shortDate + " · " + day);
        setToolTip(title + "\n" + fullDate + " " + day);
    }

    QLabel *title_ = nullptr;
    QLabel *subtitle_ = nullptr;
    QHash<QString, QString> titles_;
    QString activeId_;
};

class ClockBlock : public QWidget
{
public:
    explicit ClockBlock(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName("clockBlock");
        auto *col = new QVBoxLayout(this);
        // Internal horizontal padding so the time/date text doesn't sit flush
        // against the surrounding NodeContainer chrome — gives the clock pill
        // a more luxurious, proportional feel.
        col->setContentsMargins(6, 0, 6, 0);
        col->setSpacing(1);

        // Tabular numerals (OpenType "tnum") so each digit occupies the same
        // glyph width — eliminates the half-pixel jitter every minute when
        // a narrower "1" or "0" cycles into HH:mm.
        auto monoNum = [](const QFont &base) {
            QFont f = base;
            f.setFeature(QFont::Tag("tnum"), 1);
            return f;
        };

        // Center alignment instead of right — the time text is shorter than
        // the date below it, and right-aligning leaves a ragged empty area on
        // the time's left. Centering makes both lines symmetric within the
        // pill and reads as "aligned" at a glance.
        time_ = new QLabel;
        time_->setObjectName("clockLabel");
        time_->setAlignment(Qt::AlignCenter);
        time_->setFont(monoNum(time_->font()));

        date_ = new QLabel;
        date_->setObjectName("clockDateLabel");
        date_->setFont(monoNum(date_->font()));
        date_->setAlignment(Qt::AlignCenter);

        col->addWidget(time_);
        col->addWidget(date_);

        // Width adapts to its labels (sizeHint); height is intentionally
        // tight against the font metrics so the QHBoxLayout above can center
        // the whole block in the pill — extra height here would let the
        // labels' descender slack push content asymmetrically up.
        setFixedHeight(38);

        QObject::connect(&timer_, &QTimer::timeout, this, [this]() { refresh(); });
        timer_.start(1000);
        refresh();
    }

private:
    void refresh()
    {
        const QDate today = QDate::currentDate();
        const QLocale loc;
        time_->setText(QTime::currentTime().toString("HH:mm"));
        // Numeric date "dd.MM.yyyy" reads as a compact block under the time;
        // the matching dot/colon punctuation lines up visually with HH:mm.
        date_->setText(today.toString("dd.MM.yyyy"));
        setToolTip(loc.toString(today, "dd MMMM yyyy dddd"));
    }

    QLabel *time_ = nullptr;
    QLabel *date_ = nullptr;
    QTimer timer_;
};

// Bir uygulamanın MPRIS D-Bus arayüzünü keşfedip medya kontrol action'larını
// üreten yardımcı. .desktop dosyasındaki Actions= satırı bazı uygulamalarda
// yok ama medya çalanların hepsi MPRIS expose ediyor (Spotify, Firefox/Chrome
// video, VLC, mpv, Plasma'nın kendi araçları, vs). Plasma taskbar da bu
// kaynakı taskbar context menüsüne ekliyor.
class MprisAppHelper
{
public:
    struct PlayerActions {
        QString busName;
        QString identity;
        QString playbackStatus;  // Playing / Paused / Stopped
        bool canControl = false;
        bool canPause = false;
        bool canPlay = false;
        bool canGoNext = false;
        bool canGoPrevious = false;
    };

    static QList<PlayerActions> playersForAppKey(const QString &appKey)
    {
        QList<PlayerActions> result;
        const QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected() || !bus.interface()) return result;
        const QStringList all = bus.interface()->registeredServiceNames().value();

        const QString lowerKey = appKey.toLower();
        for (const QString &s : all) {
            if (!s.startsWith(QLatin1String("org.mpris.MediaPlayer2."))) continue;

            QDBusInterface root(s, "/org/mpris/MediaPlayer2",
                                 "org.mpris.MediaPlayer2", bus);
            if (!root.isValid()) continue;

            const QString desktopEntry = root.property("DesktopEntry").toString().toLower();
            const QString identity = root.property("Identity").toString();
            const QString svcTail = s.mid(QString("org.mpris.MediaPlayer2.").size())
                                     .section('.', 0, 0).toLower();

            auto fuzzyMatch = [&lowerKey](const QString &candidate) {
                if (candidate.isEmpty()) return false;
                return lowerKey.contains(candidate) || candidate.contains(lowerKey);
            };

            const bool match =
                fuzzyMatch(desktopEntry) || fuzzyMatch(svcTail)
                || fuzzyMatch(identity.toLower());
            if (!match) continue;

            QDBusInterface player(s, "/org/mpris/MediaPlayer2",
                                   "org.mpris.MediaPlayer2.Player", bus);
            if (!player.isValid()) continue;

            PlayerActions a;
            a.busName = s;
            a.identity = identity;
            a.playbackStatus = player.property("PlaybackStatus").toString();
            a.canControl = player.property("CanControl").toBool();
            a.canPause = player.property("CanPause").toBool();
            a.canPlay = player.property("CanPlay").toBool();
            a.canGoNext = player.property("CanGoNext").toBool();
            a.canGoPrevious = player.property("CanGoPrevious").toBool();
            result.append(a);
        }
        return result;
    }

    static void call(const QString &busName, const QString &method)
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(
            busName, "/org/mpris/MediaPlayer2",
            "org.mpris.MediaPlayer2.Player", method);
        QDBusConnection::sessionBus().asyncCall(msg);
    }
};

// ============================================================================
// Icons — merkezi icon resolver. Layer-shell Wayland app'lerde Qt'nin
// platform-theme integration'ı eksik kalabiliyor ve `fromTheme` çağrıları
// kullanıcının asıl icon teması yerine `hicolor` fallback'ine düşüyor. Burada
// app başlangıcında doğru theme'i set ediyoruz + ek fallback search path'leri
// ekliyoruz + çoklu hint'ten en iyi QIcon'u resolve eden helper sunuyoruz.
// ============================================================================

class MarqueeLabel : public QWidget
{
public:
    explicit MarqueeLabel(QWidget *parent = nullptr) : QWidget(parent)
    {
        QObject::connect(&timer_, &QTimer::timeout, this, [this]() {
            offset_ += 1;
            if (offset_ > textWidth_ + kGap) {
                offset_ = 0;
            }
            update();
        });
    }

    void setText(const QString &text)
    {
        if (text == text_) {
            return;
        }
        text_ = text;
        offset_ = 0;
        recomputeMetrics();
        update();
    }

    QString text() const { return text_; }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        if (text_.isEmpty()) {
            return;
        }
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.setFont(font());
        p.setPen(Color::Text);

        if (textWidth_ <= width()) {
            p.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, text_);
            return;
        }

        const int x = -offset_;
        p.setClipRect(rect());
        p.drawText(QRect(x, 0, textWidth_, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, text_);
        p.drawText(QRect(x + textWidth_ + kGap, 0, textWidth_, height()),
                   Qt::AlignLeft | Qt::AlignVCenter, text_);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        recomputeMetrics();
    }

    void hideEvent(QHideEvent *event) override
    {
        QWidget::hideEvent(event);
        timer_.stop();
    }

    void showEvent(QShowEvent *event) override
    {
        QWidget::showEvent(event);
        recomputeMetrics();
    }

private:
    void recomputeMetrics()
    {
        QFontMetrics fm(font());
        textWidth_ = fm.horizontalAdvance(text_);
        if (textWidth_ > width()) {
            if (!timer_.isActive()) {
                timer_.start(40);
            }
        } else {
            timer_.stop();
            offset_ = 0;
        }
    }

    static constexpr int kGap = 48;
    QString text_;
    int textWidth_ = 0;
    int offset_ = 0;
    QTimer timer_;
};

class NowPlayingWidget : public QWidget
{
public:
    explicit NowPlayingWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName("nowPlaying");
        // Single horizontal row: source icon | scrolling title | prev | play |
        // next. No stacked column, no trailing stretch — every pixel of width
        // is used so the music pill hugs its content with no dead space.
        setFixedHeight(32);
        setMinimumWidth(260);

        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(4);

        auto *sourceBtn = new HoverPressIconButton;
        sourceBtn->setRadius(14.0);
        sourceBtn->setFixedSize(28, 28);
        sourceBtn->setIconSize({18, 18});
        sourceBtn->setToolTip(QCoreApplication::translate("dock", "Kaynak seç"));
        sourceButton_ = sourceBtn;
        row->addWidget(sourceButton_);

        marquee_ = new MarqueeLabel;
        marquee_->setObjectName("nowPlayingMarquee");
        marquee_->setFixedHeight(28);
        marquee_->setMinimumWidth(120);
        row->addWidget(marquee_, 1);

        prev_ = makeControl("media-skip-backward",
            QCoreApplication::translate("dock", "Önceki"));
        playPause_ = makeControl("media-playback-start",
            QCoreApplication::translate("dock", "Oynat / Duraklat"));
        next_ = makeControl("media-skip-forward",
            QCoreApplication::translate("dock", "Sonraki"));

        row->addWidget(prev_);
        row->addWidget(playPause_);
        row->addWidget(next_);

        QObject::connect(sourceButton_, &QPushButton::clicked, this, [this]() {
            showSourcePicker(sourceButton_->mapToGlobal(QPoint(0, sourceButton_->height() + 4)));
        });
        QObject::connect(prev_, &QPushButton::clicked, this,
                         [this]() { mediaCall("Previous"); });
        QObject::connect(playPause_, &QPushButton::clicked, this,
                         [this]() { mediaCall("PlayPause"); });
        QObject::connect(next_, &QPushButton::clicked, this,
                         [this]() { mediaCall("Next"); });

        nam_ = new QNetworkAccessManager(this);

        QObject::connect(&timer_, &QTimer::timeout, this, [this]() { refresh(); });
        timer_.start(1500);
        QTimer::singleShot(0, this, [this]() { refresh(); });

        hide();
    }

private:
    QPushButton *makeControl(const QString &iconName, const QString &tooltip)
    {
        auto *btn = new HoverPressIconButton;
        btn->setRadius(8.0);
        btn->setFixedSize(28, 28);
        btn->setIcon(QIcon::fromTheme(iconName));
        btn->setIconSize({16, 16});
        btn->setToolTip(tooltip);
        return btn;
    }

    static QIcon resolveSourceIcon(const QString &service, const QString &identity)
    {
        auto tryName = [](const QString &name) -> QIcon {
            if (name.isEmpty()) {
                return {};
            }
            QIcon icon = QIcon::fromTheme(name);
            if (!icon.isNull()) {
                return icon;
            }
            const QString resolved = DesktopIconResolver::findIconName(name);
            if (!resolved.isEmpty()) {
                icon = QIcon::fromTheme(resolved);
                if (!icon.isNull()) {
                    return icon;
                }
                if (QFile::exists(resolved)) {
                    return QIcon(resolved);
                }
            }
            return {};
        };

        QIcon icon = tryName(identity.toLower());
        if (!icon.isNull()) {
            return icon;
        }

        const QString suffix = service.section('.', 3, 3).toLower();
        icon = tryName(suffix);
        if (!icon.isNull()) {
            return icon;
        }

        return QIcon::fromTheme("audio-headphones");
    }

    void mediaCall(const QString &method)
    {
        if (currentService_.isEmpty()) {
            return;
        }
        QDBusInterface player(currentService_, "/org/mpris/MediaPlayer2",
                              "org.mpris.MediaPlayer2.Player");
        player.call(method);
        QTimer::singleShot(180, this, [this]() { refresh(); });
    }

    static QVariantMap fetchMetadata(const QString &service)
    {
        QVariantMap result;
        QDBusMessage msg = QDBusMessage::createMethodCall(
            service, "/org/mpris/MediaPlayer2",
            "org.freedesktop.DBus.Properties", "Get");
        msg << QString("org.mpris.MediaPlayer2.Player") << QString("Metadata");
        QDBusMessage reply = QDBusConnection::sessionBus().call(msg);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
            return result;
        }

        QVariant outer = reply.arguments().first();
        QVariant inner = outer;
        if (outer.canConvert<QDBusVariant>()) {
            inner = outer.value<QDBusVariant>().variant();
        }
        if (inner.canConvert<QDBusArgument>()) {
            QDBusArgument arg = inner.value<QDBusArgument>();
            arg >> result;
        } else if (inner.canConvert<QVariantMap>()) {
            result = inner.toMap();
        }
        return result;
    }

    static QString unwrapString(const QVariant &v)
    {
        if (v.canConvert<QDBusVariant>()) {
            return v.value<QDBusVariant>().variant().toString();
        }
        return v.toString();
    }

    static QStringList unwrapStringList(const QVariant &v)
    {
        QVariant inner = v;
        if (v.canConvert<QDBusVariant>()) {
            inner = v.value<QDBusVariant>().variant();
        }
        if (inner.canConvert<QStringList>()) {
            return inner.toStringList();
        }
        if (inner.canConvert<QDBusArgument>()) {
            QStringList list;
            QDBusArgument arg = inner.value<QDBusArgument>();
            arg >> list;
            return list;
        }
        return {};
    }

    struct SourceInfo {
        QString service;
        QString identity;
        QString status;
    };

    QList<SourceInfo> listSources() const
    {
        const QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected() || !bus.interface()) {
            return {};
        }
        const QStringList all = bus.interface()->registeredServiceNames().value();
        QList<SourceInfo> result;
        for (const QString &s : all) {
            if (!s.startsWith("org.mpris.MediaPlayer2.")) {
                continue;
            }
            QDBusInterface player(s, "/org/mpris/MediaPlayer2",
                                  "org.mpris.MediaPlayer2.Player", bus);
            if (!player.isValid()) {
                continue;
            }
            const QString status = player.property("PlaybackStatus").toString();
            if (status.isEmpty() || status == "Stopped") {
                continue;
            }

            QDBusInterface app(s, "/org/mpris/MediaPlayer2",
                               "org.mpris.MediaPlayer2", bus);
            QString identity = app.property("Identity").toString();
            if (identity.isEmpty()) {
                identity = s.section('.', 3).section('.', 0, 0);
            }
            result.append({s, identity, status});
        }
        return result;
    }

    static QString statusLabel(const QString &status)
    {
        if (status == "Playing") {
            return QCoreApplication::translate("dock", "Çalıyor");
        }
        if (status == "Paused") {
            return QCoreApplication::translate("dock", "Duraklatıldı");
        }
        return status;
    }

    void showSourcePicker(const QPoint &pos)
    {
        const auto sources = listSources();
        if (sources.isEmpty()) {
            return;
        }
        QMenu menu;
        for (const SourceInfo &src : sources) {
            const QString label = QString("%1  ·  %2").arg(src.identity, statusLabel(src.status));
            auto *action = menu.addAction(label);
            action->setCheckable(true);
            action->setChecked(src.service == currentService_);
            action->setIcon(resolveSourceIcon(src.service, src.identity));
            const QString svc = src.service;
            QObject::connect(action, &QAction::triggered, this, [this, svc]() {
                preferredService_ = svc;
                refresh();
            });
        }
        menu.exec(pos);

        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(sourceButton_, &leave);
        sourceButton_->setAttribute(Qt::WA_UnderMouse, false);
        sourceButton_->update();
    }

    void refresh()
    {
        const auto sources = listSources();
        if (sources.isEmpty()) {
            hideWidget();
            return;
        }

        SourceInfo chosen;
        if (!preferredService_.isEmpty()) {
            for (const SourceInfo &s : sources) {
                if (s.service == preferredService_) {
                    chosen = s;
                    break;
                }
            }
        }
        if (chosen.service.isEmpty()) {
            for (const SourceInfo &s : sources) {
                if (s.status == "Playing") {
                    chosen = s;
                    break;
                }
            }
            if (chosen.service.isEmpty()) {
                chosen = sources.first();
            }
        }

        currentService_ = chosen.service;

        QVariantMap meta = fetchMetadata(chosen.service);

        const QString title = unwrapString(meta.value("xesam:title"));
        const QStringList artists = unwrapStringList(meta.value("xesam:artist"));
        QString artist = artists.join(", ");
        if (artist.isEmpty()) {
            artist = unwrapString(meta.value("xesam:albumArtist"));
        }
        if (artist.isEmpty()) {
            artist = unwrapString(meta.value("xesam:album"));
        }

        QString line;
        if (!title.isEmpty()) {
            line = artist.isEmpty() ? title : (artist + " — " + title);
        } else {
            line = chosen.identity;
        }

        marquee_->setText(line);
        setToolTip(QString("%1\n%2 · %3").arg(line, chosen.identity, statusLabel(chosen.status)));

        const QString artUrl = unwrapString(meta.value("mpris:artUrl"));
        if (!loadAlbumArt(artUrl, chosen.service, chosen.identity)) {
            sourceButton_->setIcon(resolveSourceIcon(chosen.service, chosen.identity));
            currentArtUrl_.clear();
        }
        sourceButton_->setToolTip(
            QCoreApplication::translate("dock", "%1\nKaynak seç").arg(chosen.identity));

        const QString iconName = (chosen.status == "Playing")
                                     ? "media-playback-pause"
                                     : "media-playback-start";
        playPause_->setIcon(QIcon::fromTheme(iconName));
        playPause_->setToolTip(chosen.status == "Playing" ? "Duraklat" : "Oynat");

        const bool wasActive = active_;
        active_ = true;
        if (isHidden()) {
            show();
        }
        // Parent NodeContainer (music pill) görünür kalır artık — boşken
        // SystemStatsWidget alanı dolduruyor, simetri bozulmuyor. Aktiflik
        // değişimi callback ile bildirilir; main() iki widget'ı senkronize
        // tutar.
        if (auto *p = parentWidget()) {
            if (p->isHidden()) p->show();
        }
        if (!wasActive && onActiveChanged_) onActiveChanged_(true);
    }

    void hideWidget()
    {
        currentService_.clear();
        currentArtUrl_.clear();
        const bool wasActive = active_;
        active_ = false;
        if (!isHidden()) {
            hide();
        }
        if (wasActive && onActiveChanged_) onActiveChanged_(false);
    }

public:
    bool isActive() const { return active_; }
    void setOnActiveChanged(std::function<void(bool)> fn) { onActiveChanged_ = std::move(fn); }

private:
    bool active_ = false;
    std::function<void(bool)> onActiveChanged_;

    bool loadAlbumArt(const QString &url, const QString &service, const QString &identity)
    {
        if (url.isEmpty()) {
            return false;
        }
        if (url == currentArtUrl_) {
            return true;
        }

        if (url.startsWith("file://")) {
            const QString path = QUrl(url).toLocalFile();
            QPixmap pm(path);
            if (!pm.isNull()) {
                currentArtUrl_ = url;
                applyAlbumPixmap(pm);
                return true;
            }
            return false;
        }

        if (url.startsWith("http://") || url.startsWith("https://")) {
            currentArtUrl_ = url;
            if (artCache_.contains(url)) {
                applyAlbumPixmap(artCache_.value(url));
                return true;
            }
            // Show source icon as placeholder while downloading
            sourceButton_->setIcon(resolveSourceIcon(service, identity));

            QNetworkRequest req{QUrl(url)};
            req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
            QNetworkReply *reply = nam_->get(req);
            QObject::connect(reply, &QNetworkReply::finished, this,
                             [this, reply, url]() {
                                 if (reply->error() == QNetworkReply::NoError) {
                                     QPixmap pm;
                                     if (pm.loadFromData(reply->readAll()) && !pm.isNull()) {
                                         artCache_.insert(url, pm);
                                         if (currentArtUrl_ == url) {
                                             applyAlbumPixmap(pm);
                                         }
                                     }
                                 }
                                 reply->deleteLater();
                             });
            return true;
        }

        return false;
    }

    void applyAlbumPixmap(const QPixmap &source)
    {
        const int dim = 24;
        QPixmap scaled = source.scaled(QSize(dim, dim) * 2,
                                       Qt::KeepAspectRatioByExpanding,
                                       Qt::SmoothTransformation);

        QPixmap result(QSize(dim, dim) * 2);
        result.fill(Qt::transparent);
        QPainter p(&result);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        QPainterPath clip;
        clip.addRoundedRect(0, 0, result.width(), result.height(), 6, 6);
        p.setClipPath(clip);
        const int x = (result.width() - scaled.width()) / 2;
        const int y = (result.height() - scaled.height()) / 2;
        p.drawPixmap(x, y, scaled);
        p.end();

        result.setDevicePixelRatio(2.0);
        sourceButton_->setIcon(QIcon(result));
    }

    MarqueeLabel *marquee_ = nullptr;
    QPushButton *sourceButton_ = nullptr;
    QPushButton *prev_ = nullptr;
    QPushButton *playPause_ = nullptr;
    QPushButton *next_ = nullptr;
    QTimer timer_;
    QString currentService_;
    QString preferredService_;
    QString currentArtUrl_;
    QNetworkAccessManager *nam_ = nullptr;
    QHash<QString, QPixmap> artCache_;
};

// MPRIS player aktif değilken music pill'in yerini tutar: CPU·sıcaklık, RAM ve
// ağ throughput. NowPlayingWidget gibi 32px height; minWidth 320 (3 blok
// için CPU/RAM 2-block tasarımdan biraz daha geniş — music pill aktif
// olduğunda ada ~60px daralır, animasyonlu reflow rahatsız etmiyor).
class SystemStatsWidget : public QWidget
{
public:
    explicit SystemStatsWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName("systemStats");
        setFixedHeight(32);
        setMinimumWidth(320);

        auto *row = new QHBoxLayout(this);
        row->setContentsMargins(10, 0, 10, 0);
        row->setSpacing(10);

        auto buildBlock = [&](const QString &label, QLabel **out) {
            auto *box = new QWidget(this);
            auto *bl = new QHBoxLayout(box);
            bl->setContentsMargins(0, 0, 0, 0);
            bl->setSpacing(6);
            auto *lbl = new QLabel(label, box);
            lbl->setObjectName("systemStatLabel");
            auto *val = new QLabel("--", box);
            val->setObjectName("systemStatPercent");
            val->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
            bl->addWidget(lbl, 0);
            bl->addWidget(val, 1);
            *out = val;
            return box;
        };

        cpuBlock_ = buildBlock("CPU", &cpuVal_);
        ramBlock_ = buildBlock("RAM", &ramVal_);
        netBlock_ = buildBlock("NET", &netVal_);
        // CPU bloğu sıcaklık suffix'i ile en geniş; NET ↑/↓ formatı yüzünden
        // RAM'den bir tık geniş. Stretch oranları içerik genişliğine göre.
        row->addWidget(cpuBlock_, 3);
        row->addWidget(ramBlock_, 2);
        row->addWidget(netBlock_, 4);

        cpuTempPath_ = findCpuTempPath();

        timer_ = new QTimer(this);
        timer_->setInterval(1500);
        QObject::connect(timer_, &QTimer::timeout, this, [this]() { refresh(); });
        timer_->start();
        QTimer::singleShot(0, this, [this]() { refresh(); });
    }

private:
    void refresh()
    {
        cpuValue_ = readCpuPercent();
        ramValue_ = readRamPercent();
        const int cpuTemp = readCpuTemp();
        sampleNet();

        if (cpuVal_) {
            QString s = QString("%1%").arg(cpuValue_);
            if (cpuTemp > 0) s += QString("  %1°").arg(cpuTemp);
            cpuVal_->setText(s);
        }
        if (ramVal_) ramVal_->setText(QString("%1%").arg(ramValue_));
        if (netVal_) {
            netVal_->setText(QString::fromUtf8("↑%1 ↓%2")
                                 .arg(formatRate(txBytesPerSec_),
                                      formatRate(rxBytesPerSec_)));
        }
        update();
    }

    // /proc/stat'ın ilk satırı kümülatif CPU tick'lerini verir. İki ölçüm
    // arasındaki idle/total fark oranından kullanım yüzdesi çıkar.
    int readCpuPercent()
    {
        QFile f("/proc/stat");
        if (!f.open(QIODevice::ReadOnly)) return 0;
        QString line = QString::fromUtf8(f.readLine());
        f.close();
        const auto parts = line.split(' ', Qt::SkipEmptyParts);
        if (parts.size() < 5 || parts.first() != QLatin1String("cpu")) return 0;
        const qint64 user = parts.value(1).toLongLong();
        const qint64 nice = parts.value(2).toLongLong();
        const qint64 system = parts.value(3).toLongLong();
        const qint64 idle = parts.value(4).toLongLong();
        const qint64 iowait = parts.size() > 5 ? parts.value(5).toLongLong() : 0;
        const qint64 total = user + nice + system + idle + iowait;
        const qint64 active = total - idle - iowait;
        int pct = 0;
        if (lastCpuTotal_ > 0 && total > lastCpuTotal_) {
            const qint64 dt = total - lastCpuTotal_;
            const qint64 da = active - lastCpuActive_;
            pct = qBound(0, int(100.0 * da / dt), 100);
        }
        lastCpuTotal_ = total;
        lastCpuActive_ = active;
        return pct;
    }

    int readRamPercent()
    {
        QFile f("/proc/meminfo");
        if (!f.open(QIODevice::ReadOnly)) return 0;
        // /proc dosyalarında Qt QFile::atEnd() baştan true döner (size()==0
        // bilgisinden) — readLine loop hiç fire etmez. Tüm içeriği readAll
        // ile alıp manuel split et.
        const QString content = QString::fromUtf8(f.readAll());
        qint64 total = 0;
        qint64 available = 0;
        static const QRegularExpression wsRe("\\s+");
        const auto lines = content.split('\n', Qt::SkipEmptyParts);
        for (const QString &raw : lines) {
            const QString line = raw.trimmed();
            if (line.startsWith(QLatin1String("MemTotal:"))) {
                const QStringList parts = line.split(wsRe, Qt::SkipEmptyParts);
                if (parts.size() >= 2) total = parts[1].toLongLong();
            } else if (line.startsWith(QLatin1String("MemAvailable:"))) {
                const QStringList parts = line.split(wsRe, Qt::SkipEmptyParts);
                if (parts.size() >= 2) available = parts[1].toLongLong();
            }
            if (total > 0 && available > 0) break;
        }
        if (total <= 0) return 0;
        return qBound(0, int(100.0 * (total - available) / total), 100);
    }

    // hwmon altında k10temp (AMD) / coretemp (Intel) / zenpower bulup
    // Tctl|Tdie|Package label'ına sahip temp girişini seçer. Bulamazsa
    // thermal_zone0'a düşer. Yol tek seferlik probe edilip cache'lenir.
    QString findCpuTempPath() const
    {
        const QStringList preferred{"k10temp", "zenpower", "coretemp"};
        QDir hwmon("/sys/class/hwmon");
        const auto entries = hwmon.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const auto &e : entries) {
            const QString base = "/sys/class/hwmon/" + e;
            QFile nameFile(base + "/name");
            if (!nameFile.open(QIODevice::ReadOnly)) continue;
            const QString name = QString::fromUtf8(nameFile.readAll()).trimmed();
            if (!preferred.contains(name)) continue;
            for (int i = 1; i <= 10; ++i) {
                QFile labelFile(base + QString("/temp%1_label").arg(i));
                if (!labelFile.open(QIODevice::ReadOnly)) continue;
                const QString label = QString::fromUtf8(labelFile.readAll()).trimmed();
                if (label.contains("Tctl") || label.contains("Tdie")
                    || label.startsWith("Package")) {
                    return base + QString("/temp%1_input").arg(i);
                }
            }
            // label match'i yoksa hwmon node'un ilk temp1_input'unu kullan
            const QString fallback = base + "/temp1_input";
            if (QFile::exists(fallback)) return fallback;
        }
        return "/sys/class/thermal/thermal_zone0/temp";
    }

    int readCpuTemp()
    {
        if (cpuTempPath_.isEmpty()) return -1;
        QFile f(cpuTempPath_);
        if (!f.open(QIODevice::ReadOnly)) return -1;
        const qint64 milli = QString::fromUtf8(f.readAll()).trimmed().toLongLong();
        if (milli <= 0) return -1;
        return int(milli / 1000);
    }

    // /proc/net/dev'i okuyup loopback / docker / veth / bridge dışı tüm
    // arabirimlerin rx ve tx byte toplamını alır, önceki örnekle farkı
    // /saniye normalize ederek throughput'a çevirir.
    void sampleNet()
    {
        qint64 rxTotal = 0, txTotal = 0;
        QFile f("/proc/net/dev");
        if (!f.open(QIODevice::ReadOnly)) return;
        // /proc files have size==0; Qt's atEnd() returns true before any
        // readLine. readAll + split is the only reliable approach.
        const QStringList lines =
            QString::fromUtf8(f.readAll()).split('\n', Qt::SkipEmptyParts);
        for (int i = 2; i < lines.size(); ++i) {  // ilk 2 satır başlık
            const QString &line = lines[i];
            const int colon = line.indexOf(':');
            if (colon <= 0) continue;
            const QString name = line.left(colon).trimmed();
            if (name == "lo" || name.startsWith("docker") || name.startsWith("veth")
                || name.startsWith("br-") || name.startsWith("virbr")) continue;
            const auto parts = line.mid(colon + 1).split(' ', Qt::SkipEmptyParts);
            if (parts.size() < 9) continue;
            rxTotal += parts.value(0).toLongLong();
            txTotal += parts.value(8).toLongLong();
        }
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (lastNetMs_ > 0 && now > lastNetMs_) {
            const double dtSec = (now - lastNetMs_) / 1000.0;
            const qint64 drx = std::max<qint64>(0, rxTotal - lastRx_);
            const qint64 dtx = std::max<qint64>(0, txTotal - lastTx_);
            rxBytesPerSec_ = qint64(drx / dtSec);
            txBytesPerSec_ = qint64(dtx / dtSec);
            // Rolling peak — instant max, yavaş decay. Min taban koruması
            // tiny aktivitede bar'ın %100'e fırlamasını engeller.
            const qint64 instant = std::max(rxBytesPerSec_, txBytesPerSec_);
            netPeakBps_ = std::max(netPeakBps_, instant);
            netPeakBps_ = std::max<qint64>(64 * 1024, qint64(netPeakBps_ * 0.97));
        }
        lastRx_ = rxTotal;
        lastTx_ = txTotal;
        lastNetMs_ = now;
    }

    static QString formatRate(qint64 bps)
    {
        if (bps >= 1024LL * 1024 * 1024)
            return QString::number(bps / (1024.0 * 1024 * 1024), 'f', 1) + "G";
        if (bps >= 1024 * 1024)
            return QString::number(bps / (1024.0 * 1024), 'f', 1) + "M";
        if (bps >= 1024) return QString::number(bps / 1024) + "K";
        return QString::number(bps) + "B";
    }

    static QColor barColor(int pct)
    {
        if (pct >= 85) return Color::Danger;
        if (pct >= 65) return QColor(251, 191, 36, 230); // amber-400
        return Color::Accent;
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const int barH = 3;
        const int barY = height() - barH - 4;
        auto drawBar = [&](const QRect &geom, qreal frac, const QColor &col) {
            if (geom.isNull() || geom.width() <= 0) return;
            const QRectF bgRect(geom.x(), barY, geom.width(), barH);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 28));
            p.drawRoundedRect(bgRect, barH / 2.0, barH / 2.0);
            const qreal w = std::max<qreal>(0.0, geom.width() * std::clamp(frac, 0.0, 1.0));
            if (w > 0) {
                const QRectF fillRect(geom.x(), barY, w, barH);
                p.setBrush(col);
                p.drawRoundedRect(fillRect, barH / 2.0, barH / 2.0);
            }
        };
        if (cpuBlock_) drawBar(cpuBlock_->geometry(), cpuValue_ / 100.0, barColor(cpuValue_));
        if (ramBlock_) drawBar(ramBlock_->geometry(), ramValue_ / 100.0, barColor(ramValue_));
        if (netBlock_ && netPeakBps_ > 0) {
            const qint64 instant = std::max(rxBytesPerSec_, txBytesPerSec_);
            drawBar(netBlock_->geometry(), double(instant) / double(netPeakBps_),
                    Color::Accent);
        }
    }

private:
    QWidget *cpuBlock_ = nullptr;
    QWidget *ramBlock_ = nullptr;
    QWidget *netBlock_ = nullptr;
    QLabel *cpuVal_ = nullptr;
    QLabel *ramVal_ = nullptr;
    QLabel *netVal_ = nullptr;
    int cpuValue_ = 0;
    int ramValue_ = 0;
    QTimer *timer_ = nullptr;
    qint64 lastCpuTotal_ = 0;
    qint64 lastCpuActive_ = 0;
    QString cpuTempPath_;
    qint64 lastRx_ = 0;
    qint64 lastTx_ = 0;
    qint64 lastNetMs_ = 0;
    qint64 rxBytesPerSec_ = 0;
    qint64 txBytesPerSec_ = 0;
    qint64 netPeakBps_ = 64 * 1024;
};

class GlassPopup : public QWidget
{
public:
    // Asimetrik shadow padding. Popup anchor'ın üstüne açılıyor: body alt
    // edge anchor'a yapışık → alt taraf shadow alanı gerekmez (anchor zaten
    // blokluyor). Üst/yan kenarlarda pad var ki shadow yumuşak yayılsın.
    // Bu, popup'ın anchor'a daha yakın görünmesini sağlar.
    enum PanelShape {
        ShapeTongue,    // Speech bubble — alt kenarda anchor'a uzanan üçgen
        ShapePill,      // Geniş yumuşak köşeler, tongue yok
        ShapeNotched,   // Alt kenarda launcher dome çapına oturan çentik
        ShapeHybrid,    // Pill köşeler + tongue
    };

    // Şu an aktif şekil — diğer seçenekleri test ederken bu satırı değiştir.
    static constexpr PanelShape kPanelShape = ShapeHybrid;

    static constexpr int kShadowPadSide = 22;
    static constexpr int kShadowPadTop = 22;
    // Tongue içeren modlar için altta extra alan; aksi halde minimum.
    static constexpr int kShadowPadBottom =
        (kPanelShape == ShapeTongue || kPanelShape == ShapeHybrid) ? 14 : 0;
    static constexpr int kTongueHeight = 8;
    static constexpr int kTongueWidth = 18;
    static constexpr int kNotchHeight = 10;     // Notched mode için derinlik
    static constexpr int kNotchWidth = 80;      // Launcher dome civarı
    static constexpr qreal kPanelRadius =
        (kPanelShape == ShapePill || kPanelShape == ShapeHybrid) ? 26.0 : 18.0;

    explicit GlassPopup(const QSize &size, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
        setAttribute(Qt::WA_TranslucentBackground);
        contentSize_ = size;
        setFixedSize(size.width() + 2 * kShadowPadSide,
                      size.height() + kShadowPadTop + kShadowPadBottom);
        setContentsMargins(kShadowPadSide, kShadowPadTop,
                            kShadowPadSide, kShadowPadBottom);
    }

    // Widget'ın görsel content area boyutu — surface kenarındaki shadow
    // pad bunun dışında. Subclass kullanıcıları pozisyonlama için bunu
    // kullanmalı (widget->width()/height() değil).
    QSize contentSize() const { return contentSize_; }

    enum HorizontalAlign { AlignLeft, AlignHCenter, AlignRight };

    // Anchor widget'ın ÜSTÜNE konumlandırıp gösterir. Tongue (alt kenardaki
    // üçgen çıkıntı) tepesi anchor center'ına hizalanır; gap = tongue tepesi
    // ile anchor üst kenarı arasındaki boşluk.
    void showAbove(QWidget *anchor, int gap = 2,
                    HorizontalAlign hAlign = AlignHCenter)
    {
        if (!anchor) { show(); return; }
        const int visualW = contentSize_.width();
        const int visualH = contentSize_.height();
        const QPoint anchorTL = anchor->mapToGlobal(QPoint(0, 0));
        const int anchorCenterX = anchorTL.x() + anchor->width() / 2;
        int visualX;
        switch (hAlign) {
        case AlignLeft:    visualX = anchorTL.x(); break;
        case AlignRight:   visualX = anchorTL.x() + anchor->width() - visualW; break;
        default:           visualX = anchorCenterX - visualW / 2;
        }
        // Body bottom = anchor.y - gap - tongueH (tongue tip body bottom +
        // tongueH'da, anchor.y - gap'te olmasını istiyoruz).
        const int visualY = anchorTL.y() - gap - kTongueHeight - visualH;
        move(visualX - kShadowPadSide, visualY - kShadowPadTop);
        // Tongue X (content rect koord'unda): anchor center'a göre.
        tongueOffsetX_ = anchorCenterX - visualX;
        // Content rect içinde tongue tepesinin x'i; clamp ile body kenarı
        // içinde kalsın (radius'a uzak).
        const int margin = int(kPanelRadius) + kTongueWidth / 2 + 4;
        tongueOffsetX_ = std::clamp(tongueOffsetX_, margin, visualW - margin);
        show();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QRectF rect = QRectF(contentsRect()).adjusted(0.5, 0.5, -0.5, -0.5);

        // Body rect üzerinde rect-based feathered shadow. Tongue alanı çok
        // küçük olduğundan onun çevresine shadow paint etmiyoruz — fark
        // edilmez ve performant.
        drawFeatheredShadow(painter, rect, kPanelRadius, /*maxExpand*/ 18.0,
                            /*yOffMax*/ 10.0, /*maxAlpha*/ 60, /*passes*/ 10);

        // Body + tongue tek path. Fill ve border bu path üzerinden.
        const QPainterPath path = buildBodyPath(rect);
        painter.setBrush(Color::PopupBg);
        painter.setPen(Qt::NoPen);
        painter.drawPath(path);

        painter.setPen(QPen(Color::PopupBorder, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
    }

    // Body path — shape moduna göre alt kenarda varyasyon. Üst kenar ve yan
    // kenarlar her zaman aynı (rounded rect). Alt kenarda:
    //  - Tongue/Hybrid : ortada üçgen tongue
    //  - Notched       : ortada anchor center'da kavisli çentik
    //  - Pill          : düz alt (sadece radius'lu köşeler)
    QPainterPath buildBodyPath(const QRectF &rect) const
    {
        QPainterPath path;
        const qreal r = kPanelRadius;
        const qreal anchorCx = rect.left() + tongueOffsetX_;

        path.moveTo(rect.left() + r, rect.top());
        path.lineTo(rect.right() - r, rect.top());
        path.arcTo(rect.right() - 2*r, rect.top(), 2*r, 2*r, 90, -90);
        path.lineTo(rect.right(), rect.bottom() - r);
        path.arcTo(rect.right() - 2*r, rect.bottom() - 2*r, 2*r, 2*r, 0, -90);

        if constexpr (kPanelShape == ShapeTongue || kPanelShape == ShapeHybrid) {
            const qreal tw = kTongueWidth;
            const qreal th = kTongueHeight;
            path.lineTo(anchorCx + tw / 2.0, rect.bottom());
            path.lineTo(anchorCx, rect.bottom() + th);
            path.lineTo(anchorCx - tw / 2.0, rect.bottom());
        } else if constexpr (kPanelShape == ShapeNotched) {
            // Anchor'a kavisli içeri çentik — launcher dome üst kavisine
            // estetik olarak oturur.
            const qreal nw = kNotchWidth;
            const qreal nh = kNotchHeight;
            const QRectF notchRect(anchorCx - nw / 2.0, rect.bottom() - nh,
                                    nw, nh * 2.0);
            path.lineTo(anchorCx + nw / 2.0, rect.bottom());
            // Yarım elips ile içeri kavis
            path.arcTo(notchRect, 0, 180);
            path.lineTo(anchorCx - nw / 2.0, rect.bottom());
        }
        // ShapePill: alt kenar düz (extra çizgi yok), aşağıdaki lineTo direkt
        // alt-sol köşeye gider.

        path.lineTo(rect.left() + r, rect.bottom());
        path.arcTo(rect.left(), rect.bottom() - 2*r, 2*r, 2*r, 270, -90);
        path.lineTo(rect.left(), rect.top() + r);
        path.arcTo(rect.left(), rect.top(), 2*r, 2*r, 180, -90);
        path.closeSubpath();
        return path;
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        // Body dışına (shadow pad area) tıklamayı outside-click olarak yorum-
        // la; Qt::Popup default davranışını manuel olarak tutuyoruz.
        if (!contentsRect().contains(event->pos())) {
            hide();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

private:
    QSize contentSize_;
    int tongueOffsetX_ = 0;  // anchor center'a göre tongue tepesinin x (content rect içi)
};

class TaskItemButton : public HoverPressIconButton
{
public:
    explicit TaskItemButton(QWidget *parent = nullptr) : HoverPressIconButton(parent)
    {
        setRadius(9.0);

        // Active state animates between inactive (scale 1.0) and active
        // (scale 1.10) — gives the focused app a subtle "lift".
        activeAnim_.setDuration(220);
        activeAnim_.setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(&activeAnim_, &QVariantAnimation::valueChanged, this,
                         [this](const QVariant &v) {
                             activeScale_ = v.toReal();
                             update();
                         });

        // Pulse animation — minimize event'inde "uygulamam buraya kapatıldı"
        // görsel ipucu. Buton kenarında accent halka 0→1→0 yumuşak fade.
        pulseAnim_.setDuration(520);
        pulseAnim_.setEasingCurve(QEasingCurve::OutQuad);
        QObject::connect(&pulseAnim_, &QVariantAnimation::valueChanged, this,
                         [this](const QVariant &v) {
                             pulseValue_ = v.toReal();
                             update();
                         });
    }

    // Buton'da pulse animasyonu tetikle (minimize event'inden çağrılır).
    void triggerPulse()
    {
        pulseAnim_.stop();
        pulseAnim_.setKeyValueAt(0.0, 0.0);
        pulseAnim_.setKeyValueAt(0.35, 1.0);
        pulseAnim_.setKeyValueAt(1.0, 0.0);
        pulseAnim_.setStartValue(0.0);
        pulseAnim_.setEndValue(0.0);
        pulseAnim_.start();
    }

    ~TaskItemButton() override
    {
        // If we get destroyed while user is mid-drag (e.g. window closes during
        // drag exec), make sure the static drag-source pointer doesn't dangle.
        if (s_dragSource_ == this) {
            s_dragSource_ = nullptr;
        }
    }

    static TaskItemButton *currentDragSource() { return s_dragSource_; }
    static void clearCurrentDragSource()
    {
        if (s_dragSource_) {
            s_dragSource_->setGraphicsEffect(nullptr);
            s_dragSource_ = nullptr;
        }
    }

    void setWindowCount(int count)
    {
        if (windowCount_ == count) {
            return;
        }
        windowCount_ = count;
        update();
    }

    void setDragKey(const QString &key) { dragKey_ = key; }
    QString dragKey() const { return dragKey_; }

    void setContextMenuHandler(std::function<void(const QPoint &)> handler)
    {
        contextMenuHandler_ = std::move(handler);
    }

    void setActive(bool active)
    {
        if (active_ == active) return;
        active_ = active;
        activeAnim_.stop();
        activeAnim_.setStartValue(activeScale_);
        activeAnim_.setEndValue(active ? 1.0 : 0.0);
        activeAnim_.start();
    }
    bool isActive() const { return active_; }

protected:
    qreal extraScale() const override
    {
        // 8% grow when active — combined with press scale by base class.
        // 8% on a 44px inactive pod fits inside the 48px widget without
        // clipping the hairline border.
        return 1.0 + 0.08 * activeScale_;
    }

    void paintBackground(QPainter &p, const QRectF &rect) override
    {
        // Pod is intentionally smaller than the widget so the active scale-up
        // has room to expand without clipping at the widget bounds. The 2px
        // gap on each side also reads as inner padding around the icon —
        // gives each task icon visible breathing room (the "yuvarlaklar
        // olmalı" feel).
        const qreal pad = 2.0;
        const QRectF pod = rect.adjusted(pad, pad, -pad, -pad);
        const qreal r = std::min(pod.width(), pod.height()) / 2.0;

        p.setPen(Qt::NoPen);
        p.setBrush(QBrush(Color::ToolbarBg));
        p.drawRoundedRect(pod, r, r);

        const int hoverAlpha = qRound(22 * hover_);
        if (hoverAlpha > 0) {
            p.setBrush(QColor(255, 255, 255, hoverAlpha));
            p.drawRoundedRect(pod, r, r);
        }

        p.setPen(QPen(Color::ToolbarBorder, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(pod, r, r);
    }

    void paintForeground(QPainter &p, const QRectF &rect) override
    {
        // Icon centered (delegated to base class).
        HoverPressIconButton::paintForeground(p, rect);

        const qreal cx = width() / 2.0;
        // Dot sits inside the pod's bottom edge (pod pad = 2, dot 3 above
        // the pod bottom).
        const qreal dotY = height() - 2.0 - 3.0;

        if (active_) {
            const qreal d = 4.5;
            p.setPen(Qt::NoPen);
            p.setBrush(QBrush(Color::Accent));
            p.drawEllipse(QPointF(cx, dotY), d / 2.0, d / 2.0);
        } else if (windowCount_ > 0) {
            const int dots = std::min(windowCount_, 3);
            const qreal dotSize = 2.5;
            const qreal gap = 2.5;
            const qreal totalWidth = dots * dotSize + (dots - 1) * gap;
            const qreal startX = cx - totalWidth / 2.0;
            p.setPen(Qt::NoPen);
            p.setBrush(QBrush(QColor(161, 161, 170, 180)));
            for (int i = 0; i < dots; ++i) {
                p.drawEllipse(QRectF(startX + i * (dotSize + gap),
                                     dotY - dotSize / 2.0,
                                     dotSize, dotSize));
            }
        }
    }

    void contextMenuEvent(QContextMenuEvent *event) override
    {
        if (contextMenuHandler_) {
            // QMenu::exec inside the handler is a nested event loop and may
            // process DeferredDelete events. If an action ends up deleting
            // this button (e.g. unpinning a no-windows app), `this` would be
            // dangling here — guard with QPointer and bail out.
            QPointer<TaskItemButton> guard(this);
            contextMenuHandler_(event->globalPos());
            if (!guard) {
                event->accept();
                return;
            }
            // After the popup menu closes, Qt does not deliver a Leave event
            // to the underlying button on Wayland; the hover state stays stuck.
            QEvent leaveEvent(QEvent::Leave);
            QApplication::sendEvent(this, &leaveEvent);
            setAttribute(Qt::WA_UnderMouse, false);
            update();
            event->accept();
            return;
        }
        HoverPressIconButton::contextMenuEvent(event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            dragStart_ = event->position().toPoint();
        }
        HoverPressIconButton::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        HoverPressIconButton::mouseMoveEvent(event);
        if (dragKey_.isEmpty() || !(event->buttons() & Qt::LeftButton)) {
            return;
        }
        if ((event->position().toPoint() - dragStart_).manhattanLength()
            < QApplication::startDragDistance()) {
            return;
        }

        auto *drag = new QDrag(this);
        auto *mime = new QMimeData;
        mime->setData("application/x-4ztex-appkey", dragKey_.toUtf8());
        drag->setMimeData(mime);
        // No setPixmap — see snap-to-slot comment in OpenWindowsBar.
        s_dragSource_ = this;
        // Guard `this` access after drag exec: nested event loop can process
        // DeferredDelete if the app's window closes mid-drag.
        QPointer<TaskItemButton> guard(this);
        drag->exec(Qt::MoveAction);
        if (!guard) {
            return;
        }
        if (s_dragSource_ == this) {
            s_dragSource_ = nullptr;
        }
    }

private:
    QPoint dragStart_;
    QString dragKey_;
    std::function<void(const QPoint &)> contextMenuHandler_;
    int windowCount_ = 1;
    bool active_ = false;
    qreal activeScale_ = 0.0;
    QVariantAnimation activeAnim_;
    qreal pulseValue_ = 0.0;
    QVariantAnimation pulseAnim_;
    inline static TaskItemButton *s_dragSource_ = nullptr;

protected:
    void paintEvent(QPaintEvent *e) override
    {
        HoverPressIconButton::paintEvent(e);
        if (pulseValue_ <= 0.0) return;
        // Pulse efekti — accent menekşe halka button rect'inin çevresinde,
        // yumuşak fade (alpha pulseValue_'ye orantılı, expand 1-4 px).
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const qreal expand = 1.0 + pulseValue_ * 3.0;
        const QRectF r = QRectF(rect()).adjusted(-expand, -expand, expand, expand);
        QColor c = Color::Accent;
        c.setAlphaF(0.55 * pulseValue_);
        p.setPen(QPen(c, 2.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r, radius() + expand, radius() + expand);
    }
};

// Async pencere thumbnail yakalama. KWin'in ScreenShot2 DBus API'sine
// CaptureWindow(handle, options, fd) çağrısı atıp gelen ham QImage verisini
// memfd üzerinden okur, QPixmap'a çevirip callback ile döner. Başarısızlık
// (KWin yok, izin reddi, format desteklenmiyor, vs.) durumunda boş pixmap
// döner — UI tarafı bunu sessizce icon+başlık'a düşürür.
//
// Önemli not: KWin 5.20+/Plasma 6 ScreenShot2, executable path'ine bakarak
// sadece sabit bir whitelist'teki process'lere (spectacle, krfb, vs.)
// izin veriyor. Bizim gibi 3rd-party uygulamalar `NoAuthorized` alır. İlk
// red sonrası bu helper kendini devre dışı bırakır — tekrar tekrar DBus
// gürültüsü çıkarmak yerine sessizce ikon+başlık fallback'ine düşer.
inline bool &thumbnailsAuthorizedFlag()
{
    static bool flag = true;
    return flag;
}

// memfd okuma context'i. Heap'te tutulur (self-deleting) — DBus reply
// callback'i ve sonraki QTimer retry'ları arasında yaşar.
struct ThumbnailReadCtx {
    QDBusUnixFileDescriptor qfd;
    std::function<void(const QPixmap &)> cb;
    QByteArray buf;
    qint64 total = 0;
    qint64 offset = 0;
    int width = 0;
    int height = 0;
    int stride = 0;
    int format = 0;
    int retries = 0;
    QString uuid;

    void tryRead()
    {
        const int rfd = qfd.fileDescriptor();
        while (offset < total) {
            errno = 0;
            const ssize_t got = ::pread(rfd, buf.data() + offset,
                                         size_t(total - offset),
                                         off_t(offset));
            if (got > 0) {
                offset += got;
                continue;
            }
            // got==0: EOF (memfd henüz yazılmamış veya yarıda kalmış)
            // got<0 + EAGAIN/EINTR: KWin O_NONBLOCK fd ile yazıyor, henüz hazır değil
            // Her iki durum da "yazıcı henüz tamam değil" demek — retry.
            const int err = errno;
            const bool retryable = (got == 0)
                                   || (got < 0 && (err == EAGAIN || err == EINTR));
            if (retryable) {
                // ~5ms × 200 = ~1s toplam timeout.
                if (++retries > 200) {
                    qCWarning(dockPreview) << "pread timeout for" << uuid
                               << "retries=" << retries
                               << "offset=" << offset << "/" << total
                               << "lastGot=" << got << "lastErrno=" << err;
                    cb(QPixmap());
                    delete this;
                    return;
                }
                QTimer::singleShot(5, [this]() { tryRead(); });
                return;
            }
            qCWarning(dockPreview) << "pread failed at offset" << offset
                       << "/" << total << "for" << uuid
                       << "got=" << got << "errno=" << err;
            cb(QPixmap());
            delete this;
            return;
        }
        QImage img(reinterpret_cast<const uchar *>(buf.constData()),
                   width, height, stride,
                   static_cast<QImage::Format>(format));
        if (img.isNull()) {
            qCWarning(dockPreview) << "QImage ctor null for" << uuid
                       << "format=" << format;
            cb(QPixmap());
        } else {
            // copy() bağımsız tampona kopyalar; ctx silindiğinde buf gider.
            cb(QPixmap::fromImage(img.copy()));
        }
        delete this;
    }
};

inline void captureWindowThumbnail(const QString &uuid,
                                   std::function<void(const QPixmap &)> cb)
{
    if (uuid.isEmpty() || !thumbnailsAuthorizedFlag()) {
        cb(QPixmap());
        return;
    }
    // KWin'in internalId formatı genelde {uuid} parantezli geliyor; ScreenShot2
    // hem parantezli hem parantezsiz handle kabul ediyor, doğrudan veriyoruz.
    int fd = ::memfd_create("4ztex-thumb", MFD_CLOEXEC);
    if (fd < 0) {
        cb(QPixmap());
        return;
    }
    QDBusUnixFileDescriptor qfd(fd);
    ::close(fd);  // QDBusUnixFileDescriptor kendi dup'unu tuttu.

    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusMessage msg = QDBusMessage::createMethodCall(
        "org.kde.KWin",
        "/org/kde/KWin/ScreenShot2",
        "org.kde.KWin.ScreenShot2",
        "CaptureWindow");
    QVariantMap opts;
    opts.insert("include-cursor", false);
    opts.insert("native-resolution", false);
    msg.setArguments({ uuid, opts, QVariant::fromValue(qfd) });

    QDBusPendingCall pending = bus.asyncCall(msg, 4000);
    auto *watcher = new QDBusPendingCallWatcher(pending);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished,
                     [qfd, cb = std::move(cb), watcher, uuid]() mutable {
        watcher->deleteLater();
        QDBusPendingReply<QVariantMap> reply = *watcher;
        if (reply.isError()) {
            const QString errName = reply.error().name();
            // Plasma 6'da yetkisiz uygulamalar her zaman bunu alır; ilk
            // seferinde flag'i kapatıp gelecek çağrıları sessizleştiriyoruz.
            if (errName == QLatin1String("org.kde.KWin.ScreenShot2.Error.NoAuthorized")) {
                if (thumbnailsAuthorizedFlag()) {
                    thumbnailsAuthorizedFlag() = false;
                    qCWarning(dockPreview) << "KWin screenshot izni reddedildi; "
                                  "thumbnail kapatıldı, ikon+başlık fallback'i kullanılacak.";
                }
            } else {
                qCWarning(dockPreview) << "CaptureWindow DBus error for" << uuid
                           << ":" << errName << reply.error().message();
            }
            cb(QPixmap());
            return;
        }
        const QVariantMap r = reply.value();
        const int width  = r.value(QStringLiteral("width")).toInt();
        const int height = r.value(QStringLiteral("height")).toInt();
        const int stride = r.value(QStringLiteral("stride")).toInt();
        const int format = r.value(QStringLiteral("format")).toInt();
        if (width <= 0 || height <= 0 || stride <= 0) {
            qCWarning(dockPreview) << "CaptureWindow returned invalid metadata for"
                       << uuid << "w=" << width << "h=" << height
                       << "stride=" << stride << "format=" << format
                       << "keys=" << r.keys();
            cb(QPixmap());
            return;
        }
        // KWin'in ScreenShotWriter2 worker thread'i DBus reply'dan SONRA fd'ye
        // yazıyor — yani reply geldiğinde memfd henüz boş, ham senkron pread
        // EAGAIN döner (KWin fd'yi O_NONBLOCK ile set ediyor). UI'yı
        // dondurmadan veri yazılmasını beklemek için kısa aralıklı async
        // retry yapıyoruz. Toplam timeout ~1s.
        auto *ctx = new ThumbnailReadCtx;
        ctx->qfd = qfd;
        ctx->cb = std::move(cb);
        ctx->total = qint64(height) * qint64(stride);
        ctx->buf = QByteArray(int(ctx->total), Qt::Uninitialized);
        ctx->width = width;
        ctx->height = height;
        ctx->stride = stride;
        ctx->format = format;
        ctx->uuid = uuid;
        ctx->tryRead();
    });
}

// Tek bir pencerenin önizleme kartı. Üstte thumbnail (yoksa stilize boş
// alan), altta küçük ikon + başlık, hover'da sağ üstte kapatma butonu.
// Karta tıklamak pencereyi aktive eder; X butonu pencereyi kapatır.
class WindowPreviewCard : public QWidget
{
public:
    static constexpr int kCardW = 220;
    static constexpr int kCardH = 160;
    static constexpr int kThumbH = 112;

    WindowPreviewCard(const QString &windowId, const QString &title,
                       const QIcon &icon, QWidget *parent = nullptr)
        : QWidget(parent), windowId_(windowId), title_(title), icon_(icon)
    {
        setFixedSize(kCardW, kCardH);
        setAttribute(Qt::WA_Hover, true);
        setMouseTracking(true);
        setCursor(Qt::PointingHandCursor);

        closeBtn_ = new QPushButton(this);
        closeBtn_->setFixedSize(20, 20);
        closeBtn_->setCursor(Qt::PointingHandCursor);
        closeBtn_->setFocusPolicy(Qt::NoFocus);
        closeBtn_->setIcon(QIcon::fromTheme("window-close"));
        closeBtn_->setIconSize({12, 12});
        closeBtn_->setStyleSheet(
            "QPushButton { background: rgba(0,0,0,140); border: 1px solid rgba(255,255,255,30);"
            " border-radius: 10px; }"
            "QPushButton:hover { background: rgba(244,63,94,220); border-color: rgba(255,255,255,80); }");
        closeBtn_->move(kCardW - closeBtn_->width() - 8, 8);
        closeBtn_->setVisible(false);
        QObject::connect(closeBtn_, &QPushButton::clicked, this, [this]() {
            if (onClose_) onClose_();
        });

        hoverAnim_.setDuration(140);
        hoverAnim_.setEasingCurve(QEasingCurve::OutCubic);
        QObject::connect(&hoverAnim_, &QVariantAnimation::valueChanged, this,
                         [this](const QVariant &v) {
                             hover_ = v.toReal();
                             update();
                         });
    }

    QString windowId() const { return windowId_; }
    void setThumbnail(const QPixmap &px)
    {
        thumbnail_ = px;
        update();
    }
    bool hasThumbnail() const { return !thumbnail_.isNull(); }

    void setOnActivate(std::function<void()> fn) { onActivate_ = std::move(fn); }
    void setOnClose(std::function<void()> fn) { onClose_ = std::move(fn); }

protected:
    void enterEvent(QEnterEvent *e) override
    {
        QWidget::enterEvent(e);
        closeBtn_->setVisible(true);
        animateHover(1.0);
    }
    void leaveEvent(QEvent *e) override
    {
        QWidget::leaveEvent(e);
        closeBtn_->setVisible(false);
        animateHover(0.0);
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        QWidget::mouseReleaseEvent(e);
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())
            && !closeBtn_->geometry().contains(e->position().toPoint())) {
            if (onActivate_) onActivate_();
        }
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QRectF full = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = 12.0;

        // Hover halosu: hafif beyaz iç-doluluk.
        const int hoverFill = qRound(28 * hover_);
        if (hoverFill > 0) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, hoverFill));
            p.drawRoundedRect(full, radius, radius);
        }

        // Thumbnail alanı.
        const QRectF thumbRect(8, 8, kCardW - 16, kThumbH);
        QPainterPath thumbPath;
        thumbPath.addRoundedRect(thumbRect, 8, 8);
        p.save();
        p.setClipPath(thumbPath);
        if (!thumbnail_.isNull()) {
            // KeepAspectRatioByExpanding ile kırparak doldur; cover-fit gibi.
            QSize target = thumbRect.size().toSize();
            QPixmap scaled = thumbnail_.scaled(target,
                                                Qt::KeepAspectRatioByExpanding,
                                                Qt::SmoothTransformation);
            const QPointF off(thumbRect.center().x() - scaled.width() / 2.0,
                              thumbRect.center().y() - scaled.height() / 2.0);
            p.drawPixmap(off, scaled);
        } else {
            // Placeholder: hafif gradient + büyük soluk ikon.
            QLinearGradient g(thumbRect.topLeft(), thumbRect.bottomRight());
            g.setColorAt(0.0, QColor(40, 42, 54, 220));
            g.setColorAt(1.0, QColor(24, 25, 33, 220));
            p.setPen(Qt::NoPen);
            p.setBrush(g);
            p.drawRect(thumbRect);
            if (!icon_.isNull()) {
                const int s = 48;
                const QRect ir(int(thumbRect.center().x() - s / 2),
                               int(thumbRect.center().y() - s / 2), s, s);
                icon_.paint(&p, ir, Qt::AlignCenter, QIcon::Normal);
            }
        }
        p.restore();
        p.setPen(QPen(QColor(255, 255, 255, 18), 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawPath(thumbPath);

        // İkon + başlık satırı (kartın altı).
        const int iconSize = 16;
        const QRect iconRect(12, kThumbH + 8 + 12, iconSize, iconSize);
        if (!icon_.isNull()) {
            icon_.paint(&p, iconRect, Qt::AlignCenter, QIcon::Normal);
        }
        const int textLeft = iconRect.right() + 8;
        const int textRight = kCardW - 12;
        const QRect textRect(textLeft, iconRect.top() - 1,
                             textRight - textLeft, iconSize + 2);
        p.setPen(QPen(Color::Text));
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        const QString elided = QFontMetrics(f).elidedText(title_, Qt::ElideRight,
                                                          textRect.width());
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elided);
    }

private:
    void animateHover(qreal target)
    {
        hoverAnim_.stop();
        hoverAnim_.setStartValue(hover_);
        hoverAnim_.setEndValue(target);
        hoverAnim_.start();
    }

    QString windowId_;
    QString title_;
    QIcon icon_;
    QPixmap thumbnail_;
    QPushButton *closeBtn_ = nullptr;
    qreal hover_ = 0.0;
    QVariantAnimation hoverAnim_;
    std::function<void()> onActivate_;
    std::function<void()> onClose_;
};

// Toolbar penceresinin üstünde inline expand ile gösterilen önizleme satırı.
// Top-level popup yerine bar window'unun bir child widget'ı olduğu için
// Qt::Popup grab'i, xdg-popup positioner kilidi, hover-grab spurious leave
// gibi tüm Wayland-popup sorunları geçerli değil — standart Qt enter/leave
// akışıyla çalışır.
class WindowPreviewRow : public QWidget
{
public:
    static constexpr int kPadH = 12;
    static constexpr int kPadV = 12;
    static constexpr int kCardGap = 10;

    explicit WindowPreviewRow(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setMouseTracking(true);
        // Üst-level QHBoxLayout'a takmıyoruz — pozisyonu OpenWindowsBar
        // manuel hesaplıyor (anchor butonun X'ine göre).
        row_ = new QHBoxLayout(this);
        row_->setContentsMargins(kPadH, kPadV, kPadH, kPadV);
        row_->setSpacing(kCardGap);
        hide();
    }

    using Entry = PreviewEntry;

    void setOnActivate(std::function<void(const QString &)> fn) { onActivate_ = std::move(fn); }
    void setOnClose(std::function<void(const QString &)> fn) { onClose_ = std::move(fn); }
    void setOnEnter(std::function<void()> fn) { onEnter_ = std::move(fn); }
    void setOnLeave(std::function<void()> fn) { onLeave_ = std::move(fn); }

    QString currentAppKey() const { return currentKey_; }

    void setEntries(const QString &appKey, const QList<PreviewEntry> &entries)
    {
        currentKey_ = appKey;
        if (entries.isEmpty()) {
            hide();
            return;
        }

        QList<QString> newIds;
        newIds.reserve(entries.size());
        for (const Entry &e : entries) newIds.append(e.windowId);

        // Boyutu sizeHint async hesaplamasına bırakmıyoruz — kart sayısına
        // göre explicit kilitle. Eski cardlar silinip yenisi eklenirken
        // sizeHint bir frame için (24x24) gibi minik bir değere düşüp
        // popup'ı "yuvarlak siyah küçük zımbırtı" olarak gösteriyordu.
        const int n = entries.size();
        const int fixedW = 2 * kPadH + n * WindowPreviewCard::kCardW
                           + (n > 1 ? (n - 1) * kCardGap : 0);
        const int fixedH = 2 * kPadV + WindowPreviewCard::kCardH;
        setFixedSize(fixedW, fixedH);

        if (newIds != currentIds_) {
            QLayoutItem *child;
            while ((child = row_->takeAt(0)) != nullptr) {
                delete child->widget();
                delete child;
            }
            cards_.clear();
            currentIds_ = newIds;
            for (const PreviewEntry &e : entries) {
                auto *card = new WindowPreviewCard(e.windowId, e.title, e.icon, this);
                const QString wid = e.windowId;
                card->setOnActivate([this, wid]() {
                    if (onActivate_) onActivate_(wid);
                });
                card->setOnClose([this, wid]() {
                    if (onClose_) onClose_(wid);
                });
                row_->addWidget(card);
                cards_.append(card);
            }
        }

        // Thumbnail isteklerini her gösterimde tazele.
        for (int i = 0; i < entries.size() && i < cards_.size(); ++i) {
            const QString wid = entries.at(i).windowId;
            QPointer<WindowPreviewCard> guard(cards_.at(i));
            captureWindowThumbnail(wid, [guard](const QPixmap &px) {
                if (guard && !px.isNull()) {
                    guard->setThumbnail(px);
                }
            });
        }
    }

protected:
    void enterEvent(QEnterEvent *e) override
    {
        QWidget::enterEvent(e);
        if (onEnter_) onEnter_();
    }
    void leaveEvent(QEvent *e) override
    {
        QWidget::leaveEvent(e);
        if (onLeave_) onLeave_();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF rect = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const qreal radius = 16.0;

        // GlassPopup ile aynı sade feathered shadow.
        drawFeatheredShadow(p, rect, radius, /*maxExpand*/ 18.0,
                            /*yOffMax*/ 10.0, /*maxAlpha*/ 60, /*passes*/ 10);

        p.setBrush(Color::PopupBg);
        p.drawRoundedRect(rect, radius, radius);

        p.setPen(QPen(Color::PopupBorder, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rect, radius, radius);
    }

private:
    QHBoxLayout *row_ = nullptr;
    QList<WindowPreviewCard *> cards_;
    QList<QString> currentIds_;
    QString currentKey_;
    std::function<void(const QString &)> onActivate_;
    std::function<void(const QString &)> onClose_;
    std::function<void()> onEnter_;
    std::function<void()> onLeave_;
};

// DockWindow inline preview API'sinin tanımı — WindowPreviewRow yukarıda
// tam olarak tanımlandıktan sonra burada implement ediliyor.
inline void DockWindow::showPreview(
    const QString &appKey,
    const QList<PreviewEntry> &entries,
    int anchorCxInBar,
    std::function<void(const QString &)> onActivate,
    std::function<void(const QString &)> onClose,
    std::function<void()> onRowEnter,
    std::function<void()> onRowLeave)
{
    if (entries.isEmpty()) {
        hidePreview();
        return;
    }
    if (!previewRow_) {
        previewRow_ = new WindowPreviewRow(this);
        previewRow_->raise();
    }
    previewRow_->setOnActivate(onActivate);
    previewRow_->setOnClose(onClose);
    previewRow_->setOnEnter(onRowEnter);
    previewRow_->setOnLeave(onRowLeave);
    previewRow_->setEntries(appKey, entries);
    currentPreviewKey_ = appKey;
    lastAnchorCx_ = anchorCxInBar;

    if (!previewVisible_) {
        previewVisible_ = true;
        // Animasyonsuz instant açılış. Animasyon bar window height'ını ramp
        // edince layout topMargin değişiyor, launcher Y'si bu süreçte
        // recompute oluyor → görsel "launcher aşağı kayma" artefakt'ı
        // yaratıyordu. Snappy geçiş = izolasyon.
        if (previewHeightAnim_) previewHeightAnim_->stop();
        previewProgress_ = 1.0;
        if (previewRow_) {
            previewRow_->setAttribute(Qt::WA_TransparentForMouseEvents, false);
        }
    }
    previewRow_->show();
    previewRow_->raise();
    positionPreviewRow();
    updatePreviewSurfaceHeight();
    // Swap'te (zaten visible) animation tetiklenmediği için yeni paint
    // event scheduling olmuyor — sync repaint ile hemen yansıt.
    previewRow_->repaint();
    repaint();
}

inline void DockWindow::hidePreview(bool /*instant*/)
{
    if (!previewVisible_) return;
    previewVisible_ = false;
    currentPreviewKey_.clear();
    if (previewRow_) previewRow_->hide();
    // Kapanma her zaman ANINDA. Animasyonlu kapanış sırasında bar window
    // shrink eder ve layout topMargin değişir → launcher Y'si bu süreçte
    // güncellenir, başka interaction'lar (launcher click vs.) görsel
    // artifact yaratır. Snappy kapanma her şeyi izole tutar; sadece açılma
    // animasyonu kalır.
    if (previewHeightAnim_) previewHeightAnim_->stop();
    previewProgress_ = 0.0;
    updatePreviewSurfaceHeight();
}

inline void DockWindow::positionPreviewRow()
{
    if (!previewRow_) return;
    // setFixedSize ile boyutu setEntries'te kilitledik; size() okumak güvenli.
    const QSize s = previewRow_->size();
    int x = lastAnchorCx_ - s.width() / 2;
    if (x < 8) x = 8;
    if (x + s.width() > width() - 8) x = std::max(8, width() - 8 - s.width());
    const int y = PreviewRowTopMargin;
    previewRow_->setGeometry(x, y, s.width(), s.height());
}

inline void DockWindow::updatePreviewSurfaceHeight()
{
    const int extra = int(PreviewRowHeight * previewProgress_);
    const int newH = WindowHeight + extra;

    // Bar içeriği (HBoxLayout child'ları) AlignVCenter ile dikey ortalanıyor.
    // Window büyüdükçe bar yukarı kaymasın diye root layout'un top margin'ini
    // büyüttüğümüz kadar artırıyoruz — bar items alt 74px'te sabit kalır,
    // üstte açılan extra alan WindowPreviewRow'a kalır.
    if (auto *l = layout()) {
        QMargins m = l->contentsMargins();
        m.setTop(BarTopProtrusion + extra);
        l->setContentsMargins(m);
    }

    // Wayland layer-shell surface'inin gerçek boyutunu compositor'a
    // bildirmek için setDesiredSize çağrısı şart — sadece setFixedHeight
    // yapılırsa Qt widget büyür ama Wayland surface eski boyutta kalır
    // ve preview alanı clip'lenir.
    if (layerWindow_) {
        layerWindow_->setDesiredSize(QSize(0, newH));
    }
    if (height() != newH) {
        setFixedHeight(newH);
    }
    positionFloatingButton();
    update();  // yeni alanı transparent ile clear et
}

class OpenWindowsBar : public QWidget
{
public:
    explicit OpenWindowsBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName("openWindowsBar");

        auto *root = new QHBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        // leftView_ / rightView_ are bare child containers — no QHBoxLayout,
        // we position task buttons manually. This decouples animations and
        // y-positioning from Qt's deferred layout pass, which was the root
        // cause of overlap-on-cross-side and the "icons hug the top" bug.
        leftView_ = new QWidget(this);
        leftView_->setObjectName("openWindowsSide");
        leftView_->setAcceptDrops(true);
        leftView_->installEventFilter(this);
        leftView_->setFixedHeight(BarHeight);

        centerSlot_ = new QWidget(this);
        centerSlot_->setFixedSize(MainButtonSize + 24, 1);
        centerSlot_->setAttribute(Qt::WA_TransparentForMouseEvents);

        rightView_ = new QWidget(this);
        rightView_->setObjectName("openWindowsSide");
        rightView_->setAcceptDrops(true);
        rightView_->installEventFilter(this);
        rightView_->setFixedHeight(BarHeight);

        root->addWidget(leftView_);
        root->addWidget(centerSlot_);
        root->addWidget(rightView_);

        loadPinnedDefaults();

        connectToBridge();

        setupPreview();

        // The first layout pass runs before the toolbar window is shown, so
        // children temporarily settle at stale y coordinates (visible as
        // icons hugging the top of the bar until any interaction triggers
        // another relayout). Re-run on the next event-loop tick — by then
        // the window has its real geometry and items center properly.
        QTimer::singleShot(0, this, [this]() { relayoutGroups(); });
    }

    QWidget *centerAnchor() const { return centerSlot_; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        // Task butonu hover olayları: önizleme popup'ını aç/kapat. TaskItemButton'da
        // Q_OBJECT yok, qobject_cast kullanılamıyor; bu yüzden hash'te varlığa bakarak
        // güvenli static_cast yapıyoruz.
        if (event->type() == QEvent::Enter || event->type() == QEvent::Leave) {
            auto it = buttonToKey_.constFind(watched);
            if (it != buttonToKey_.constEnd()) {
                if (event->type() == QEvent::Enter) {
                    onButtonHoverEntered(it.value(), static_cast<TaskItemButton *>(watched));
                } else {
                    onButtonHoverLeft();
                }
                return QWidget::eventFilter(watched, event);
            }
        }

        if (watched != leftView_ && watched != rightView_) {
            return QWidget::eventFilter(watched, event);
        }
        const Side side = (watched == leftView_) ? Side::Left : Side::Right;

        if (event->type() == QEvent::DragEnter) {
            auto *de = static_cast<QDragEnterEvent *>(event);
            if (de->mimeData()->hasFormat("application/x-4ztex-appkey")) {
                de->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::DragMove) {
            auto *de = static_cast<QDragMoveEvent *>(event);
            if (de->mimeData()->hasFormat("application/x-4ztex-appkey")) {
                de->acceptProposedAction();
                applyDragReorder(
                    QString::fromUtf8(de->mimeData()->data("application/x-4ztex-appkey")),
                    side, de->position().toPoint().x());
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto *de = static_cast<QDropEvent *>(event);
            if (de->mimeData()->hasFormat("application/x-4ztex-appkey")) {
                // Reveal the source widget before relayoutGroups runs so its
                // settle animation is visible.
                TaskItemButton::clearCurrentDragSource();
                applyDragReorder(
                    QString::fromUtf8(de->mimeData()->data("application/x-4ztex-appkey")),
                    side, de->position().toPoint().x());
                de->acceptProposedAction();
                return true;
            }
        }
        return false;
    }

private:
    enum class Side { Left, Right };

    struct AppGroup {
        TaskItemButton *button = nullptr;
        QString cls;
        QString primaryTitle;
        QStringList windowIds;
        QString execCommand;
        bool pinned = false;
    };

    struct WindowMeta {
        QString cls;
        QString title;
        bool normal = false;
        QString desktopFileName;   // KWin'in matchlediği .desktop adı (uzantısız)
        int pid = 0;               // /proc/<pid> üzerinden ek hint için
        int x11WindowId = 0;       // X11 native windowId (XWayland dahil); 0 ise Wayland-native
    };

    static constexpr int kMaxApps = 16;
    static constexpr int kSoftCapacity = 9;
    static constexpr int kSoftSize = 48;
    static constexpr int kCompactSize = 42;

    Side sideOf(const QString &key) const
    {
        return leftKeys_.contains(key) ? Side::Left : Side::Right;
    }

    void assignToSide(const QString &key)
    {
        if (leftKeys_.contains(key) || rightKeys_.contains(key)) {
            return;
        }
        // Place new icons at the FAR end of the side so the icon adjacent
        // to the launcher stays put. Left side: index 0 is the leftmost
        // (farthest from launcher) — prepend. Right side: last index is
        // the rightmost (farthest from launcher) — append.
        if (leftKeys_.size() <= rightKeys_.size()) {
            leftKeys_.prepend(key);
        } else {
            rightKeys_.append(key);
        }
    }

    void removeFromSides(const QString &key)
    {
        leftKeys_.removeAll(key);
        rightKeys_.removeAll(key);
    }

    void rebalance()
    {
        // When moving icons between sides to even out the counts, pull from
        // the FAR end (farthest from launcher) and drop on the other side's
        // FAR end too — the launcher-adjacent icons stay in place.
        while (rightKeys_.size() > leftKeys_.size()) {
            leftKeys_.prepend(rightKeys_.takeLast());
        }
        while (leftKeys_.size() > rightKeys_.size() + 1) {
            rightKeys_.append(leftKeys_.takeFirst());
        }
    }

    void loadPinnedDefaults()
    {
        QSettings settings;
        const QStringList saved = settings.value("dock/pinnedApps").toStringList();
        for (const QString &rawKey : saved) {
            const QString appKey = rawKey.toLower();
            if (apps_.contains(appKey)) {
                continue;
            }

            const DesktopIconResolver::Entry meta = DesktopIconResolver::entry(appKey);
            const QString title = meta.name.isEmpty() ? appKey : meta.name;
            QString exec = meta.exec;
            if (exec.isEmpty()) {
                exec = rawKey;
            }

            AppGroup group;
            group.cls = rawKey;
            group.primaryTitle = title;
            group.execCommand = exec;
            group.pinned = true;
            group.button = createButton(appKey, false);
            group.button->setIcon(lookupAppIcon(appKey));
            group.button->setWindowCount(0);
            group.button->setToolTip(title);

            apps_.insert(appKey, group);
            assignToSide(appKey);
        }
        relayoutGroups();
    }

    void applyDragReorder(const QString &srcKey, Side targetSide, int dropX)
    {
        if (srcKey.isEmpty() || !apps_.contains(srcKey)) {
            return;
        }
        if (!leftKeys_.contains(srcKey) && !rightKeys_.contains(srcKey)) {
            return;
        }

        QStringList &target = (targetSide == Side::Left) ? leftKeys_ : rightKeys_;

        int targetIdx = target.size();
        for (int i = 0; i < target.size(); ++i) {
            auto it = apps_.find(target.at(i));
            if (it == apps_.end() || !it.value().button || !it.value().button->isVisible()) {
                continue;
            }
            const QRect r = it.value().button->geometry();
            if (dropX < r.center().x()) {
                targetIdx = i;
                break;
            }
        }

        const Side srcSide = sideOf(srcKey);
        if (srcSide == targetSide) {
            QStringList &src = (srcSide == Side::Left) ? leftKeys_ : rightKeys_;
            const int srcIdx = src.indexOf(srcKey);
            const int finalIdx = (srcIdx < targetIdx) ? targetIdx - 1 : targetIdx;
            if (srcIdx == finalIdx) {
                return;
            }
            src.move(srcIdx, finalIdx);
        } else {
            QStringList &src = (srcSide == Side::Left) ? leftKeys_ : rightKeys_;
            const int srcIdx = src.indexOf(srcKey);
            if (target.isEmpty()) {
                src.removeAt(srcIdx);
                target.append(srcKey);
                rebalance();
            } else {
                const int victimIdx = std::min<int>(targetIdx, target.size() - 1);
                const QString victim = target.at(victimIdx);
                target[victimIdx] = srcKey;
                src[srcIdx] = victim;
            }
        }
        relayoutGroups();
    }

    void connectToBridge()
    {
        auto *bridge = KWinBridge::instance();
        if (!bridge) {
            return;
        }
        QObject::connect(bridge, &KWinBridge::windowAdded,
                         this, &OpenWindowsBar::onBridgeWindowAdded);
        QObject::connect(bridge, &KWinBridge::windowRemoved,
                         this, &OpenWindowsBar::onBridgeWindowRemoved);
        QObject::connect(bridge, &KWinBridge::windowUpdated,
                         this, &OpenWindowsBar::onBridgeWindowUpdated);
        QObject::connect(bridge, &KWinBridge::activeWindowChanged,
                         this, &OpenWindowsBar::onBridgeActiveChanged);
        QObject::connect(bridge, &KWinBridge::windowMinimizedChanged,
                         this, &OpenWindowsBar::onBridgeWindowMinimized);
    }

    // Minimize event'inde, o pencerenin app'inin task butonunu pulse animasyonu
    // ile flash — gerçek shrink animasyonu yapamadığımız için (KWin Wayland
    // mimari sınırı) "uygulamam buraya gitti" görsel ipucu.
    void onBridgeWindowMinimized(const QString &id, bool minimized)
    {
        if (!minimized) return;
        const QString appKey = idToKey_.value(id);
        if (appKey.isEmpty()) return;
        auto it = apps_.find(appKey);
        if (it == apps_.end() || !it.value().button) return;
        it.value().button->triggerPulse();
    }

    static bool isFilteredClass(const QString &clsLower)
    {
        static const QSet<QString> filteredClasses = {
            "plasmashell",
            "org.kde.plasmashell",
            "krunner",
            "org.kde.krunner",
            "ksmserver",
            "ksmserver-logout-greeter",
            "kded",
            "kded5",
            "kded6",
            "kglobalaccel",
            "kglobalaccel5",
            "polkit-kde-authentication-agent-1",
            "org.kde.polkit-kde-authentication-agent-1",
            "xdg-desktop-portal",
            "xdg-desktop-portal-kde",
            "xdg-desktop-portal-kwallet",
            "kscreenlocker_greet",
            "kwin_wayland",
            "kwin",
            "systemsettings",
            "ksmserver-shutdown",
        };
        if (filteredClasses.contains(clsLower)) {
            return true;
        }
        return clsLower.startsWith("org.kde.plasma")
               || clsLower.startsWith("org.kde.krunner")
               || clsLower.startsWith("kded")
               || clsLower.startsWith("kglobalaccel");
    }

    void onBridgeWindowAdded(const QString &id, const QString &cls,
                             const QString &title, bool normal,
                             const QString &desktopFileName, int pid,
                             int x11WindowId)
    {
        WindowMeta meta;
        meta.cls = cls;
        meta.title = title;
        meta.normal = normal;
        meta.desktopFileName = desktopFileName;
        meta.pid = pid;
        meta.x11WindowId = x11WindowId;
        windowMeta_[id] = meta;
        reconcileWindow(id);
    }

    void onBridgeWindowRemoved(const QString &id)
    {
        windowMeta_.remove(id);
        const QString appKey = idToKey_.value(id);
        idToKey_.remove(id);
        // Açık önizleme bu uygulamayı listeliyorsa içeriği tazele.
        if (!appKey.isEmpty() && previewAnchor_) {
            if (DockWindow *tw = dockWindow();
                tw && tw->currentPreviewKey() == appKey) {
                QTimer::singleShot(0, this, [this, appKey]() {
                    auto it = apps_.constFind(appKey);
                    if (it == apps_.constEnd() || it.value().windowIds.isEmpty()) {
                        hidePreview();
                    } else if (previewAnchor_) {
                        showPreview(appKey, previewAnchor_);
                    }
                });
            }
        }
        if (appKey.isEmpty()) {
            return;
        }
        auto it = apps_.find(appKey);
        if (it == apps_.end()) {
            return;
        }
        it.value().windowIds.removeAll(id);
        if (it.value().windowIds.isEmpty()) {
            if (it.value().pinned) {
                if (it.value().button) {
                    it.value().button->setWindowCount(0);
                    setButtonActive(it.value().button, false);
                    it.value().button->setToolTip(it.value().primaryTitle);
                }
            } else {
                if (it.value().button) {
                    it.value().button->deleteLater();
                }
                apps_.erase(it);
                removeFromSides(appKey);
                rebalance();
            }
        } else {
            refreshTooltip(it.value());
            if (it.value().button) {
                it.value().button->setWindowCount(it.value().windowIds.size());
            }
        }
        if (activeId_ == id) {
            activeId_.clear();
        }
        if (activeAppKey_ == appKey
            && (apps_.find(appKey) == apps_.end() || apps_.value(appKey).windowIds.isEmpty())) {
            activeAppKey_.clear();
            for (auto ait = apps_.begin(); ait != apps_.end(); ++ait) {
                setButtonActive(ait.value().button, false);
            }
        }
        relayoutGroups();
    }

    void onBridgeWindowUpdated(const QString &id, const QString &title)
    {
        auto mit = windowMeta_.find(id);
        if (mit != windowMeta_.end()) {
            mit.value().title = title;
        }

        const QString appKey = idToKey_.value(id);
        if (appKey.isEmpty()) {
            // We never added this window (likely opened with an empty title).
            // Now that it has one, re-evaluate whether it should appear.
            reconcileWindow(id);
            return;
        }
        auto it = apps_.find(appKey);
        if (it != apps_.end()) {
            if (!it.value().windowIds.isEmpty() && it.value().windowIds.first() == id) {
                it.value().primaryTitle = title;
            }
            refreshTooltip(it.value());
        }
        if (previewAnchor_) {
            if (DockWindow *tw = dockWindow();
                tw && tw->currentPreviewKey() == appKey) {
                showPreview(appKey, previewAnchor_);
            }
        }
    }

    void reconcileWindow(const QString &id)
    {
        auto mit = windowMeta_.constFind(id);
        if (mit == windowMeta_.constEnd()) {
            return;
        }
        const WindowMeta &meta = mit.value();
        const QString clsLower = meta.cls.toLower();
        const bool filtered = !meta.normal
                              || meta.title.isEmpty()
                              || clsLower.isEmpty()
                              || meta.title.startsWith("4ztexDock")
                              || isFilteredClass(clsLower);
        if (filtered) {
            return;
        }
        addOrUpdateWindow(id, meta.cls, meta.title, meta.desktopFileName,
                          meta.pid);
    }

    void onBridgeActiveChanged(const QString &id)
    {
        activeId_ = id;
        const QString key = idToKey_.value(id);
        setActiveAppKey(key, id);
    }

    void addOrUpdateWindow(const QString &id, const QString &cls, const QString &title,
                            const QString &desktopFileName = QString(),
                            int pid = 0)
    {
        const QString appKey = cls.toLower();
        const QString existingKey = idToKey_.value(id);

        if (!existingKey.isEmpty() && existingKey != appKey) {
            auto oldIt = apps_.find(existingKey);
            if (oldIt != apps_.end()) {
                oldIt.value().windowIds.removeAll(id);
                if (oldIt.value().windowIds.isEmpty() && !oldIt.value().pinned) {
                    if (oldIt.value().button) {
                        oldIt.value().button->deleteLater();
                    }
                    apps_.erase(oldIt);
                    removeFromSides(existingKey);
                    rebalance();
                } else {
                    if (oldIt.value().button) {
                        oldIt.value().button->setWindowCount(oldIt.value().windowIds.size());
                    }
                    refreshTooltip(oldIt.value());
                }
            }
            idToKey_.remove(id);
        }

        idToKey_[id] = appKey;

        auto it = apps_.find(appKey);
        if (it == apps_.end()) {
            if (apps_.size() >= kMaxApps) {
                return;
            }
            AppGroup group;
            group.cls = cls;
            group.windowIds.append(id);
            group.primaryTitle = title;
            group.execCommand = DesktopIconResolver::findExec(appKey);
            group.button = createButton(appKey, id == activeId_);
            group.button->setIcon(lookupAppIconRich(cls, desktopFileName, pid));
            group.button->setWindowCount(1);
            refreshTooltip(group);
            apps_.insert(appKey, group);
            assignToSide(appKey);
        } else {
            if (!it.value().windowIds.contains(id)) {
                it.value().windowIds.append(id);
            }
            if (it.value().windowIds.first() == id) {
                it.value().primaryTitle = title;
            }
            if (it.value().execCommand.isEmpty()) {
                it.value().execCommand = DesktopIconResolver::findExec(appKey);
            }
            refreshTooltip(it.value());
            if (it.value().button) {
                it.value().button->setWindowCount(it.value().windowIds.size());
            }
            setButtonActive(it.value().button, isAppActive(it.value()));
        }

        relayoutGroups();

        if (id == activeId_) {
            setActiveAppKey(appKey, id);
        }
    }

    void setActiveAppKey(const QString &newKey, const QString &activeId)
    {
        if (newKey == activeAppKey_) {
            if (!newKey.isEmpty() && !activeId.isEmpty()) {
                auto it = apps_.find(newKey);
                if (it != apps_.end()
                    && it.value().windowIds.contains(activeId)
                    && it.value().windowIds.first() != activeId) {
                    it.value().windowIds.removeAll(activeId);
                    it.value().windowIds.prepend(activeId);
                    refreshTooltip(it.value());
                }
            }
            return;
        }
        activeAppKey_ = newKey;
        for (auto it = apps_.begin(); it != apps_.end(); ++it) {
            const bool isActive = (it.key() == newKey);
            setButtonActive(it.value().button, isActive);
            if (isActive && !activeId.isEmpty()
                && it.value().windowIds.contains(activeId)) {
                it.value().windowIds.removeAll(activeId);
                it.value().windowIds.prepend(activeId);
                refreshTooltip(it.value());
            }
        }
    }

    bool isAppActive(const AppGroup &group) const
    {
        if (activeAppKey_.isEmpty()) {
            return false;
        }
        auto it = apps_.constFind(activeAppKey_);
        if (it == apps_.constEnd()) {
            return false;
        }
        return &it.value() == &group;
    }

    void refreshTooltip(const AppGroup &group)
    {
        if (!group.button) {
            return;
        }
        const int extras = group.windowIds.size() - 1;
        if (extras <= 0) {
            group.button->setToolTip(group.primaryTitle);
        } else {
            group.button->setToolTip(QString("%1  (+%2 daha)").arg(group.primaryTitle).arg(extras));
        }
    }

    TaskItemButton *createButton(const QString &appKey, bool active)
    {
        const int initialIcon = (currentItemSize_ >= kSoftSize) ? 26 : 22;
        auto *btn = new TaskItemButton(this);
        btn->setFixedSize(currentItemSize_, currentItemSize_);
        btn->setActive(active);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIcon(Icons::resolve(QStringList{}));
        btn->setIconSize({initialIcon, initialIcon});
        btn->setDragKey(appKey);

        const QString key = appKey;
        QObject::connect(btn, &QPushButton::clicked, this, [this, key]() {
            activateApp(key);
        });
        btn->setContextMenuHandler([this, key](const QPoint &pos) {
            // Wayland'da xdg-popup (QMenu) parent surface'in geometry'sine
            // göre relative konumlanır. Hide animation çalışırken sağ tık
            // olursa parent shrink ettikçe menu de aşağı kayıyor, ekran
            // dışına çıkıyor. Önce hidePreview(true) ile resize'ı sync olarak
            // bitir (animation stop + height immediate 74), sonra bir event
            // loop tick'i bekle ki Wayland configure event'i işlensin, EN
            // SON menüyü doğru pozisyonda aç.
            hidePreview(true);
            QTimer::singleShot(0, this, [this, key, pos]() {
                showContextMenu(key, pos);
            });
        });
        // Hover-önizleme: butona event filter takılır; buttonToKey_ ile
        // çift yönlü eşleme tutarız (Leave anında butondan key'e ulaşmak için).
        btn->installEventFilter(this);
        buttonToKey_.insert(btn, key);
        QObject::connect(btn, &QObject::destroyed, this, [this, btn]() {
            buttonToKey_.remove(btn);
            if (previewAnchor_ == btn) {
                previewAnchor_ = nullptr;
                hidePreview();
            }
        });
        return btn;
    }

    void showContextMenu(const QString &appKey, const QPoint &globalPos)
    {
        auto it = apps_.find(appKey);
        if (it == apps_.end()) {
            return;
        }
        const bool isPinned = it.value().pinned;
        const bool hasWindows = !it.value().windowIds.isEmpty();

        // All action handlers are deferred via singleShot(0). QMenu::exec is a
        // nested event loop — running unpinApp directly from a triggered slot
        // would deleteLater() the button whose contextMenuEvent is on the call
        // stack, and the deferred deletion can fire inside the menu loop. That
        // leaves the originating TaskItemButton dangling when its epilogue
        // (synthetic Leave + setAttribute + update) tries to run after exec.
        // Posting via singleShot ensures the work runs after the outer event
        // dispatch has fully unwound out of contextMenuEvent.
        QMenu menu;

        // Uygulamanın kendi .desktop dosyasında tanımlı jumplist öğeleri
        // (Plasma taskbar'ın yaptığı gibi). Örn. Firefox -> "Yeni Özel
        // Pencere", VS Code -> "Yeni Boş Pencere". Bunlar dock'un kendi
        // ayarlarından önce, üstte gösterilir.
        const QList<DesktopIconResolver::Action> appActions =
            DesktopIconResolver::findActions(appKey);
        for (const auto &a : appActions) {
            QIcon icon;
            if (!a.icon.isEmpty()) icon = QIcon::fromTheme(a.icon);
            auto *act = menu.addAction(icon, a.name);
            const QString exec = a.exec;
            QObject::connect(act, &QAction::triggered, this, [this, exec]() {
                QTimer::singleShot(0, this, [exec]() {
                    QProcess::startDetached("/bin/sh", {"-c", exec});
                });
            });
        }

        // MPRIS medya kontrolleri — uygulamanın aktif bir MPRIS player'ı
        // varsa otomatik eklenir. Spotify, VLC, mpv, Firefox/Chrome video,
        // vs. hepsi standardı destekliyor. .desktop Actions yetmediğinde
        // bu ikinci global kaynak devreye giriyor.
        const auto mprisPlayers = MprisAppHelper::playersForAppKey(appKey);
        bool mprisAdded = false;
        for (const auto &p : mprisPlayers) {
            if (!p.canControl) continue;
            if (!mprisAdded && !appActions.isEmpty()) {
                menu.addSeparator();
            }
            mprisAdded = true;
            const bool playing = p.playbackStatus == QLatin1String("Playing");
            const QString playLabel = playing
                ? QCoreApplication::translate("dock", "Duraklat")
                : QCoreApplication::translate("dock", "Çal");
            const QString playIcon = playing ? "media-playback-pause"
                                              : "media-playback-start";
            const QString bus = p.busName;
            if (playing ? p.canPause : p.canPlay) {
                auto *act = menu.addAction(QIcon::fromTheme(playIcon), playLabel);
                QObject::connect(act, &QAction::triggered, this, [bus]() {
                    MprisAppHelper::call(bus, "PlayPause");
                });
            }
            if (p.canGoPrevious) {
                auto *prev = menu.addAction(QIcon::fromTheme("media-skip-backward"),
                    QCoreApplication::translate("dock", "Önceki parça"));
                QObject::connect(prev, &QAction::triggered, this, [bus]() {
                    MprisAppHelper::call(bus, "Previous");
                });
            }
            if (p.canGoNext) {
                auto *next = menu.addAction(QIcon::fromTheme("media-skip-forward"),
                    QCoreApplication::translate("dock", "Sonraki parça"));
                QObject::connect(next, &QAction::triggered, this, [bus]() {
                    MprisAppHelper::call(bus, "Next");
                });
            }
        }

        if (!appActions.isEmpty() || mprisAdded) {
            menu.addSeparator();
        }

        if (isPinned) {
            auto *unpin = menu.addAction(QIcon::fromTheme("edit-delete"),
                QCoreApplication::translate("dock", "Dock'tan kaldır"));
            QObject::connect(unpin, &QAction::triggered, this, [this, appKey]() {
                QTimer::singleShot(0, this, [this, appKey]() { unpinApp(appKey); });
            });
        } else {
            auto *pin = menu.addAction(QIcon::fromTheme("bookmark-new"),
                QCoreApplication::translate("dock", "Dock'a sabitle"));
            QObject::connect(pin, &QAction::triggered, this, [this, appKey]() {
                QTimer::singleShot(0, this, [this, appKey]() { pinApp(appKey); });
            });
        }

        if (hasWindows) {
            menu.addSeparator();
            auto *launchNew = menu.addAction(QIcon::fromTheme("window-new"),
                QCoreApplication::translate("dock", "Yeni pencere aç"));
            QObject::connect(launchNew, &QAction::triggered, this, [this, appKey]() {
                QTimer::singleShot(0, this, [this, appKey]() { launchInstance(appKey); });
            });
            auto *closeAll = menu.addAction(QIcon::fromTheme("window-close"),
                it.value().windowIds.size() > 1
                    ? QCoreApplication::translate("dock", "Tüm pencereleri kapat")
                    : QCoreApplication::translate("dock", "Pencereyi kapat"));
            QObject::connect(closeAll, &QAction::triggered, this, [this, appKey]() {
                QTimer::singleShot(0, this, [this, appKey]() { closeAllWindows(appKey); });
            });
        }

        menu.exec(globalPos);
    }

    void pinApp(const QString &appKey)
    {
        auto it = apps_.find(appKey);
        if (it == apps_.end() || it.value().pinned) {
            return;
        }
        it.value().pinned = true;
        if (it.value().execCommand.isEmpty()) {
            const QString exec = DesktopIconResolver::findExec(appKey);
            if (!exec.isEmpty()) {
                it.value().execCommand = exec;
            } else if (!it.value().cls.isEmpty()) {
                it.value().execCommand = it.value().cls;
            } else {
                it.value().execCommand = appKey;
            }
        }
        if (it.value().button) {
            it.value().button->setToolTip(it.value().primaryTitle);
        }
        savePinnedSettings();
    }

    void unpinApp(const QString &appKey)
    {
        auto it = apps_.find(appKey);
        if (it == apps_.end() || !it.value().pinned) {
            return;
        }
        it.value().pinned = false;
        savePinnedSettings();

        if (it.value().windowIds.isEmpty()) {
            if (it.value().button) {
                it.value().button->deleteLater();
            }
            apps_.erase(it);
            removeFromSides(appKey);
            rebalance();
            relayoutGroups();
        }
    }

    void launchInstance(const QString &appKey)
    {
        auto it = apps_.find(appKey);
        if (it == apps_.end()) {
            return;
        }
        QString cmd = it.value().execCommand;
        if (cmd.isEmpty()) {
            cmd = DesktopIconResolver::findExec(appKey);
        }
        if (cmd.isEmpty()) {
            cmd = appKey;
        }
        QProcess::startDetached("/bin/sh", {"-c", cmd});
    }

    void closeAllWindows(const QString &appKey)
    {
        auto it = apps_.find(appKey);
        if (it == apps_.end()) {
            return;
        }
        for (const QString &wid : std::as_const(it.value().windowIds)) {
            runWindowAction(wid, "close");
        }
    }

    static void runWindowAction(const QString &kwinId, const QString &action)
    {
        if (kwinId.isEmpty() || action.isEmpty()) {
            return;
        }
        QString safeId = kwinId;
        safeId.replace('"', QString());
        safeId.replace('\\', QString());
        QString actionLine;
        if (action == QLatin1String("activate")) {
            actionLine = "workspace.activeWindow = w; "
                         "if (typeof workspace.raiseWindow === 'function') workspace.raiseWindow(w);";
        } else if (action == QLatin1String("close")) {
            actionLine = "w.closeWindow();";
        } else {
            return;
        }
        const QString js =
            QStringLiteral("var t='%1';var wins=workspace.windowList();"
                           "for (var i=0;i<wins.length;i++){"
                           "  var w=wins[i];"
                           "  if (w.internalId.toString()===t){ %2 break; }"
                           "}")
                .arg(safeId, actionLine);

        const QString path = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                             + QStringLiteral("/4ztex-action-%1.js")
                                   .arg(QString::number(quint64(QDateTime::currentMSecsSinceEpoch())));
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return;
        }
        f.write(js.toUtf8());
        f.close();

        QDBusConnection bus = QDBusConnection::sessionBus();
        QDBusInterface iface("org.kde.KWin", "/Scripting", "org.kde.kwin.Scripting", bus);
        QDBusReply<int> reply = iface.call("loadScript", path,
                                           QStringLiteral("4ztex-action-%1")
                                               .arg(QString::number(QDateTime::currentMSecsSinceEpoch())));
        if (!reply.isValid()) {
            QFile::remove(path);
            return;
        }
        const int sid = reply.value();
        QDBusInterface sc("org.kde.KWin",
                          QStringLiteral("/Scripting/Script%1").arg(sid),
                          "org.kde.kwin.Script", bus);
        sc.call("run");
        QTimer::singleShot(1000, [path]() { QFile::remove(path); });
    }

    void savePinnedSettings()
    {
        QStringList keys;
        for (auto it = apps_.begin(); it != apps_.end(); ++it) {
            if (it.value().pinned) {
                keys.append(it.key());
            }
        }
        QSettings settings;
        settings.setValue("dock/pinnedApps", keys);
    }

    void relayoutGroups()
    {
        const int count = apps_.size();
        const int itemSize = (count <= kSoftCapacity) ? kSoftSize : kCompactSize;
        currentItemSize_ = itemSize;
        // Icon sizes kept stable across modes (user said: don't shrink icons).
        // The bigger pod gives extra padding around the same-size glyph.
        const int iconSize = (count <= kSoftCapacity) ? 26 : 22;
        const int spacing = 6;

        auto sideWidth = [&](int n) {
            return n <= 0 ? 0 : n * itemSize + (n - 1) * spacing;
        };
        const int leftW = sideWidth(leftKeys_.size());
        const int rightW = sideWidth(rightKeys_.size());
        // Per-side targets — each side sizes to its own content. The previous
        // "max(leftW, rightW) + padding" forced symmetry and left a visible
        // gap on the lighter side; now an asymmetric task count just shifts
        // the bar (via applyBarWidth's launcher-anchored margin compensation)
        // instead of padding empty space on the right.
        const int newLeftTarget = leftW;
        const int newRightTarget = rightW;
        const int viewH = leftView_->height() > 0 ? leftView_->height() : BarHeight;
        const int y = std::max(0, (viewH - itemSize) / 2);
        const int slot = itemSize + spacing;

        // Capture each button's CURRENT pos in OpenWindowsBar coords. We
        // animate everything from this snapshot to the new target so the
        // closest-to-launcher icons move synchronously with leftView_ and
        // rightView_ growing/shrinking — no overflow during the transition,
        // and the relative gap between icons and the launcher stays put.
        // QPointer guards each entry so an animation tick after a button has
        // been deleted (e.g. its app closed mid-transition) is a no-op
        // instead of a use-after-free.
        struct Anim {
            QPointer<TaskItemButton> btn;
            QPoint startInDest;   // start position in destView local coords
            QPoint endInDest;     // end position in destView local coords
            QWidget *destView;    // target parent
        };
        QList<Anim> plan;

        auto endXLeft = [&](int i) {
            return newLeftTarget - leftW + i * slot;
        };
        auto endXRight = [&](int i) {
            return i * slot;
        };

        auto snapshot = [&](TaskItemButton *btn, QWidget *destView, QPoint endPos) {
            btn->setFixedSize(itemSize, itemSize);
            btn->setIconSize({iconSize, iconSize});
            if (!btn->isVisible()) {
                btn->setVisible(true);
            }
            QPoint start;
            if (btn->parentWidget() != destView) {
                // New button (created with parent=OpenWindowsBar) or
                // cross-side move: slide in from off-side so we never
                // start on top of another button.
                start = (destView == leftView_)
                            ? QPoint(endPos.x() + slot, endPos.y())
                            : QPoint(endPos.x() - slot, endPos.y());
                btn->setParent(destView);
                btn->show();
                btn->move(start);
            } else {
                start = btn->pos();
            }
            plan.append(Anim{btn, start, endPos, destView});
        };

        for (int i = 0; i < leftKeys_.size(); ++i) {
            auto it = apps_.find(leftKeys_.at(i));
            if (it == apps_.end() || !it.value().button) continue;
            snapshot(it.value().button, leftView_, QPoint(endXLeft(i), y));
        }
        for (int i = 0; i < rightKeys_.size(); ++i) {
            auto it = apps_.find(rightKeys_.at(i));
            if (it == apps_.end() || !it.value().button) continue;
            snapshot(it.value().button, rightView_, QPoint(endXRight(i), y));
        }

        const int oldLeftTarget = leftView_->width() > 0 ? leftView_->width()
                                                          : newLeftTarget;
        const int oldRightTarget = rightView_->width() > 0 ? rightView_->width()
                                                           : newRightTarget;

        // Stop any in-flight master animation.
        if (masterAnim_) {
            masterAnim_->stop();
            masterAnim_->deleteLater();
            masterAnim_ = nullptr;
        }
        masterAnim_ = new QVariantAnimation(this);
        masterAnim_->setDuration(220);
        masterAnim_->setEasingCurve(QEasingCurve::OutCubic);
        masterAnim_->setStartValue(0.0);
        masterAnim_->setEndValue(1.0);

        QObject::connect(masterAnim_, &QVariantAnimation::valueChanged, this,
                         [this, oldLeftTarget, newLeftTarget,
                          oldRightTarget, newRightTarget, plan](const QVariant &v) {
            const double t = v.toDouble();
            const int lvt = oldLeftTarget + int((newLeftTarget - oldLeftTarget) * t);
            const int rvt = oldRightTarget + int((newRightTarget - oldRightTarget) * t);
            if (leftView_->width() != lvt) leftView_->setFixedWidth(lvt);
            if (rightView_->width() != rvt) rightView_->setFixedWidth(rvt);
            for (const Anim &a : plan) {
                if (!a.btn) continue;
                const QPoint p(int(a.startInDest.x() + (a.endInDest.x() - a.startInDest.x()) * t),
                               int(a.startInDest.y() + (a.endInDest.y() - a.startInDest.y()) * t));
                a.btn->move(p);
            }
            if (auto *w = window()) {
                w->update();
                if (auto *bar = dynamic_cast<DockWindow *>(w)) {
                    bar->kickResize();
                }
            }
        });
        masterAnim_->start(QAbstractAnimation::DeleteWhenStopped);
        // Cleanup pointer on completion.
        QObject::connect(masterAnim_, &QObject::destroyed, this, [this]() {
            masterAnim_ = nullptr;
        });
    }

    // Smoothly move a task button from one point to another. Same easing/
    // duration as CircleIconButton hover (OutCubic 180ms). The drag-source
    // animates here too, so it visually "snaps" between slots as the user
    // drags it across the dock.
    void animateButton(TaskItemButton *btn, QPoint from, QPoint to)
    {
        if (auto *prev = btn->findChild<QVariantAnimation *>(
                "taskMoveAnim", Qt::FindDirectChildrenOnly)) {
            prev->stop();
            prev->deleteLater();
        }
        auto *anim = new QVariantAnimation(btn);
        anim->setObjectName("taskMoveAnim");
        anim->setDuration(180);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        anim->setStartValue(from);
        anim->setEndValue(to);
        QObject::connect(anim, &QVariantAnimation::valueChanged, btn,
                         [btn](const QVariant &v) { btn->move(v.toPoint()); });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    void activateApp(const QString &appKey)
    {
        auto it = apps_.find(appKey);
        if (it == apps_.end()) {
            return;
        }
        if (it.value().windowIds.isEmpty()) {
            const QString cmd = it.value().execCommand;
            if (!cmd.isEmpty()) {
                QProcess::startDetached("/bin/sh", {"-c", cmd});
            }
            return;
        }
        const QStringList &wids = it.value().windowIds;
        QString target;
        if (activeAppKey_ == appKey && wids.size() > 1) {
            const int idx = wids.indexOf(activeId_);
            target = wids.at((idx + 1) % wids.size());
        } else {
            target = wids.first();
        }
        runWindowAction(target, "activate");
    }

    void setButtonActive(TaskItemButton *btn, bool active)
    {
        if (!btn || btn->isActive() == active) {
            return;
        }
        btn->setActive(active);
    }

    // KWin'in window'a iliştirdiği desktopFileName + pid ipuçlarını öncelikle
    // kullanan zengin lookup. KWin .desktop'ı tanıyorsa Icon= değerini direkt
    // okuruz. .desktop yoksa PID'den binary basename'i çekip onu da hint olarak
    // ekleriz. Hepsi başarısızsa eski WM_CLASS-bazlı lookupAppIcon'a düşer.
    static QIcon lookupAppIconRich(const QString &cls,
                                    const QString &desktopFileName,
                                    int pid)
    {
        // 1) KWin'in match'lediği .desktop varsa direkt onu parse et.
        if (!desktopFileName.isEmpty()) {
            QString filename = desktopFileName;
            if (!filename.endsWith(QLatin1String(".desktop"))) {
                filename += QLatin1String(".desktop");
            }
            const QStringList dirs{
                QDir::homePath() + "/.local/share/applications",
                "/usr/local/share/applications",
                "/usr/share/applications",
                "/var/lib/flatpak/exports/share/applications",
                QDir::homePath() + "/.local/share/flatpak/exports/share/applications",
                "/var/lib/snapd/desktop/applications",
                QDir::homePath() + "/Applications",            // AppImageLauncher
                QDir::homePath() + "/.local/share/icons/hicolor",
            };
            bool foundDesktop = false;
            for (const QString &d : dirs) {
                QFile f(d + "/" + filename);
                if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
                foundDesktop = true;
                bool inEntry = false;
                while (!f.atEnd()) {
                    const QString line = QString::fromUtf8(f.readLine()).trimmed();
                    if (line.startsWith('[')) {
                        inEntry = (line == QLatin1String("[Desktop Entry]"));
                        continue;
                    }
                    if (!inEntry) continue;
                    if (line.startsWith(QLatin1String("Icon="))) {
                        const QString iconValue = line.mid(5).trimmed();
                        if (iconValue.isEmpty()) break;
                        if (iconValue.contains('/') && QFile::exists(iconValue)) {
                            return QIcon(iconValue);
                        }
                        QIcon hit = Icons::trySingle(iconValue);
                        if (!hit.isNull()) return hit;
                        break;
                    }
                }
            }
            // 1b) Direct file path bulunmadıysa: önce DesktopIconResolver'ın
            // direct key cache'ine (StartupWMClass/Name/file basename), sonra
            // token-fuzzy match'e bak. Token match KWin'in app_id'sini .desktop
            // filename'iyle uyuşmadığı (paketçi/upstream isim farkı) durumda
            // kurtarır — örn. `net.foo.bar` app_id vs `net.foo.foo-bar.desktop`.
            if (!foundDesktop) {
                QString viaResolver =
                    DesktopIconResolver::findIconName(desktopFileName.toLower());
                if (viaResolver.isEmpty()) {
                    const auto e = DesktopIconResolver::findByTokens(desktopFileName);
                    viaResolver = e.icon;
                }
                if (!viaResolver.isEmpty()) {
                    QIcon hit = Icons::trySingle(viaResolver);
                    if (!hit.isNull()) return hit;
                }
            }
        }
        // 2) PID üzerinden binary'yi bul — /proc/<pid>/exe → /path/to/binary.
        //    Hem binary basename'ini hem de binary'nin sibling klasöründeki
        //    icon.png/svg dosyalarını dene (örn. /opt/<vendor>/<app>/icon.png).
        QStringList extraHints;
        if (pid > 0) {
            const QString exePath = QFile::symLinkTarget(
                QStringLiteral("/proc/%1/exe").arg(pid));
            if (!exePath.isEmpty()) {
                const QFileInfo info(exePath);
                const QString base = info.completeBaseName();
                if (!base.isEmpty()) extraHints << base;
                // Binary'nin yanında ve bir üst klasördeki icon dosyalarını dene.
                const QStringList nearbyDirs{info.dir().absolutePath(),
                                              info.dir().absolutePath() + "/.."};
                for (const QString &d : nearbyDirs) {
                    for (const char *name : {"icon", "logo", "app"}) {
                        for (const char *ext : {".svg", ".png"}) {
                            const QString p = d + "/" + name + ext;
                            if (QFile::exists(p)) return QIcon(p);
                        }
                    }
                    // Binary adıyla aynı isimli icon
                    if (!base.isEmpty()) {
                        for (const char *ext : {".svg", ".png", ".xpm"}) {
                            const QString p = d + "/" + base + ext;
                            if (QFile::exists(p)) return QIcon(p);
                        }
                    }
                }
            }
        }
        // 3) cls + binary hint'iyle merkezi resolver chain.
        QStringList hints{cls};
        for (const QString &h : extraHints) {
            if (!hints.contains(h)) hints << h;
        }
        QIcon viaCentral = Icons::resolve(hints, QString());
        if (!viaCentral.isNull()) return viaCentral;

        // 4) Eski WM_CLASS-only chain'e fallback (override map, version strip, vs.).
        return lookupAppIcon(cls);
    }

    static QIcon resolveIconByName(const QString &name)
    {
        if (name.isEmpty()) {
            return {};
        }
        QIcon icon = QIcon::fromTheme(name);
        if (!icon.isNull()) {
            return icon;
        }
        if (QFile::exists(name)) {
            return QIcon(name);
        }
        return {};
    }

    static QIcon lookupAppIcon(const QString &cls)
    {
        if (cls.isEmpty()) {
            return QIcon::fromTheme("application-x-executable");
        }
        const QString lower = cls.toLower();

        QIcon icon = QIcon::fromTheme(lower);
        if (!icon.isNull()) {
            return icon;
        }

        const QString desktopIcon = DesktopIconResolver::findIconName(lower);
        if (!desktopIcon.isEmpty()) {
            icon = resolveIconByName(desktopIcon);
            if (!icon.isNull()) {
                return icon;
            }
        }

        static const QMap<QString, QString> overrides = {
            {"code", "visual-studio-code"},
            {"code-oss", "visual-studio-code"},
            {"code - insiders", "visual-studio-code"},
            {"vscodium", "visual-studio-code"},
            {"alacritty", "utilities-terminal"},
            {"konsole", "utilities-terminal"},
            {"yakuake", "utilities-terminal"},
            {"kitty", "utilities-terminal"},
            {"wezterm", "utilities-terminal"},
            {"foot", "utilities-terminal"},
            {"gnome-terminal", "utilities-terminal"},
            {"xterm", "utilities-terminal"},
            {"org.kde.dolphin", "system-file-manager"},
            {"org.kde.konsole", "utilities-terminal"},
            {"org.kde.kate", "kate"},
            {"org.kde.spectacle", "spectacle"},
            {"firefox-developer-edition", "firefox"},
            {"firefoxdeveloperedition", "firefox"},
            {"zen-browser", "zen-browser"},
            {"app.zen_browser.zen", "zen-browser"},
            {"chromium-browser", "chromium"},
            {"google-chrome", "google-chrome"},
            {"chrome", "google-chrome"},
            {"brave-browser", "brave-browser"},
            {"telegram-desktop", "telegram"},
            {"telegramdesktop", "telegram"},
            {"org.telegram.desktop", "telegram"},
            {"discord", "discord"},
            {"webcord", "discord"},
            {"slack", "slack"},
            {"thunderbird", "thunderbird"},
            {"obs", "com.obsproject.Studio"},
            {"obs-studio", "com.obsproject.Studio"},
            {"com.obsproject.studio", "com.obsproject.Studio"},
            {"blender", "blender"},
            {"inkscape", "inkscape"},
            {"krita", "krita"},
            {"gimp-2.10", "gimp"},
            {"gimp", "gimp"},
            {"spotify", "spotify"},
            {"steam", "steam"},
            {"signal", "signal-desktop"},
            {"signal desktop", "signal-desktop"},
        };
        const QString mapped = overrides.value(lower);
        if (!mapped.isEmpty()) {
            icon = QIcon::fromTheme(mapped);
            if (!icon.isNull()) {
                return icon;
            }
        }

        static const QRegularExpression versionRe("[-_][0-9]+([._-][0-9]+)*$");
        auto tryName = [](const QString &candidate) -> QIcon {
            if (candidate.isEmpty()) {
                return {};
            }
            QIcon i = QIcon::fromTheme(candidate);
            if (!i.isNull()) {
                return i;
            }
            const QString fromDesktop = DesktopIconResolver::findIconName(candidate);
            if (!fromDesktop.isEmpty()) {
                return resolveIconByName(fromDesktop);
            }
            return {};
        };

        QString stripped = QString(lower).remove(versionRe);
        if (stripped != lower) {
            icon = tryName(stripped);
            if (!icon.isNull()) {
                return icon;
            }
        }

        const int dot = lower.lastIndexOf('.');
        if (dot >= 0 && dot + 1 < lower.size()) {
            const QString tail = lower.mid(dot + 1);
            icon = tryName(tail);
            if (!icon.isNull()) {
                return icon;
            }
            const QString tailStripped = QString(tail).remove(versionRe);
            if (tailStripped != tail) {
                icon = tryName(tailStripped);
                if (!icon.isNull()) {
                    return icon;
                }
            }
        }

        const int dash = lower.indexOf('-');
        if (dash > 0) {
            icon = tryName(lower.left(dash));
            if (!icon.isNull()) {
                return icon;
            }
        }

        const int space = lower.indexOf(' ');
        if (space > 0) {
            icon = tryName(lower.left(space));
            if (!icon.isNull()) {
                return icon;
            }
        }

        // Son şans: merkezi resolver chain'i (reverse-DNS tail, /usr/share/pixmaps,
        // -symbolic strip, generic fallback chain). Burada hint listesi cls
        // varyasyonlarını içeriyor.
        QStringList centralHints{lower};
        if (cls != lower) centralHints << cls;
        if (!stripped.isEmpty() && stripped != lower) centralHints << stripped;
        return Icons::resolve(centralHints);
    }

    void setupPreview()
    {
        previewShowTimer_.setSingleShot(true);
        previewShowTimer_.setInterval(420);
        previewHideTimer_.setSingleShot(true);
        // Buton leave → buton enter (swap) için kullanılıyor. Mouse surface'i
        // terk ettiğinde anında kapama yapan ayrı bir yol var (DockWindow
        // leave event callback'i), bu yüzden timer biraz uzun olabilir —
        // butonlar arası gezinmede kullanıcıya rahat süre ver.
        previewHideTimer_.setInterval(400);

        QObject::connect(&previewShowTimer_, &QTimer::timeout, this, [this]() {
            if (!pendingPreviewAnchor_ || pendingPreviewKey_.isEmpty()) return;
            showPreview(pendingPreviewKey_, pendingPreviewAnchor_);
        });
        QObject::connect(&previewHideTimer_, &QTimer::timeout, this, [this]() {
            hidePreview();
        });

        // Toolbar surface enter/leave event'leri Wayland'da güvenilir kanal:
        // mouse surface'ten ayrıldığında compositor leave event yollar ve
        // Qt bunu top-level widget'a iletir.
        QTimer::singleShot(0, this, [this]() {
            if (DockWindow *tw = dockWindow()) {
                tw->setOnSurfaceEnter([this]() {
                    previewHideTimer_.stop();
                });
                tw->setOnSurfaceLeave([this]() {
                    hidePreview();
                });
            }
        });
    }

    DockWindow *dockWindow() const
    {
        return dynamic_cast<DockWindow *>(window());
    }

    void onButtonHoverEntered(const QString &appKey, TaskItemButton *btn)
    {
        previewHideTimer_.stop();
        if (previewAnchor_ == btn) return;
        auto it = apps_.constFind(appKey);
        if (it == apps_.constEnd() || it.value().windowIds.isEmpty()) {
            if (previewAnchor_) previewHideTimer_.start();
            return;
        }
        DockWindow *tw = dockWindow();
        if (previewAnchor_ || (tw && tw->previewVisible())) {
            showPreview(appKey, btn);
            return;
        }
        pendingPreviewKey_ = appKey;
        pendingPreviewAnchor_ = btn;
        previewShowTimer_.start();
    }

    void onButtonHoverLeft()
    {
        previewShowTimer_.stop();
        pendingPreviewAnchor_ = nullptr;
        pendingPreviewKey_.clear();
        if (previewAnchor_) {
            previewHideTimer_.start();
        }
    }

    void showPreview(const QString &appKey, TaskItemButton *anchor)
    {
        if (!anchor) return;
        auto it = apps_.constFind(appKey);
        if (it == apps_.constEnd() || it.value().windowIds.isEmpty()) {
            hidePreview();
            return;
        }
        DockWindow *tw = dockWindow();
        if (!tw) return;

        QList<WindowPreviewRow::Entry> entries;
        const AppGroup &g = it.value();
        const QIcon icon = g.button ? g.button->icon() : lookupAppIcon(g.cls);
        for (const QString &wid : g.windowIds) {
            WindowPreviewRow::Entry e;
            e.windowId = wid;
            e.title = windowMeta_.value(wid).title;
            if (e.title.isEmpty()) e.title = g.primaryTitle;
            e.icon = icon;
            entries.append(e);
        }

        // Önceki anchor'ın hover-lock'unu serbest bırak; yeni anchor'u
        // kilitle. Bu, popup açılma/kapanma animasyonu sırasında window
        // resize kaynaklı yapay leave eventlerinin button'un hover styling'ini
        // kaybetmesini önler.
        if (previewAnchor_ && previewAnchor_ != anchor) {
            previewAnchor_->setHoverLocked(false);
        }
        previewAnchor_ = anchor;
        anchor->setHoverLocked(true);
        const int anchorCx = anchor->mapTo(tw, QPoint(anchor->width() / 2, 0)).x();
        tw->showPreview(
            appKey, entries, anchorCx,
            // onActivate
            [this](const QString &wid) {
                runWindowAction(wid, "activate");
                hidePreview();
            },
            // onClose
            [this](const QString &wid) {
                runWindowAction(wid, "close");
                QTimer::singleShot(50, this, [this]() {
                    if (!previewAnchor_) return;
                    DockWindow *tw = dockWindow();
                    if (!tw) return;
                    const QString key = tw->currentPreviewKey();
                    auto it = apps_.constFind(key);
                    if (it == apps_.constEnd() || it.value().windowIds.isEmpty()) {
                        hidePreview();
                    } else {
                        showPreview(key, previewAnchor_);
                    }
                });
            },
            // onRowEnter
            [this]() { previewHideTimer_.stop(); },
            // onRowLeave
            [this]() { previewHideTimer_.start(); });
    }

public:
    void hidePreview(bool instant = false)
    {
        previewShowTimer_.stop();
        previewHideTimer_.stop();
        pendingPreviewAnchor_ = nullptr;
        pendingPreviewKey_.clear();
        if (previewAnchor_) {
            previewAnchor_->setHoverLocked(false);
            previewAnchor_ = nullptr;
        }
        if (DockWindow *tw = dockWindow()) {
            tw->hidePreview(instant);
        }
    }

private:

    QWidget *leftView_ = nullptr;
    QWidget *rightView_ = nullptr;
    QWidget *centerSlot_ = nullptr;
    QStringList leftKeys_;
    QStringList rightKeys_;
    QMap<QString, AppGroup> apps_;
    QHash<QString, QString> idToKey_;
    QHash<QString, WindowMeta> windowMeta_;
    QString activeAppKey_;
    QString activeId_;
    int currentItemSize_ = kSoftSize;
    QVariantAnimation *masterAnim_ = nullptr;

    QTimer previewShowTimer_;
    QTimer previewHideTimer_;
    QPointer<TaskItemButton> previewAnchor_;
    QPointer<TaskItemButton> pendingPreviewAnchor_;
    QString pendingPreviewKey_;
    QHash<QObject *, QString> buttonToKey_;
};

// Uygulama başlatma sayacı — frequent apps grid için QSettings tabanlı
// persistent counter. exec string'i QSettings key'ine sanitize edilir
// (`/`, `=` gibi karakterler dönüştürülür).
class LaunchTracker
{
public:
    static QString sanitize(const QString &exec)
    {
        QString k = exec;
        k.replace('/', '_');
        k.replace('=', '_');
        k.replace('[', '_');
        k.replace(']', '_');
        return k;
    }
    static void record(const QString &exec)
    {
        if (exec.isEmpty()) return;
        QSettings s;
        s.beginGroup("launcher/usage");
        const QString k = sanitize(exec);
        const int n = s.value(k, 0).toInt();
        s.setValue(k, n + 1);
        s.endGroup();
    }
    static QMap<QString, int> usage()
    {
        QSettings s;
        s.beginGroup("launcher/usage");
        QMap<QString, int> map;
        const auto keys = s.childKeys();
        for (const QString &k : keys) {
            map[k] = s.value(k).toInt();
        }
        s.endGroup();
        return map;
    }
};

// Modern launcher menüsü:
//   • Üstte büyük search bar (accent focus ring)
//   • Search boş ise "Sık Kullanılanlar" 4×2 grid (en çok başlatılan 8 app)
//   • "Tüm Uygulamalar" — flat list (border yok, sadece hover/selected hi)
//   • Klavye nav: ↑/↓ ile seçim hareket eder, Enter seçili'yi çalıştırır,
//     Esc kapatır
//   • Empty state: arama eşleşmesi yoksa "Eşleşme yok" yazısı
//   • Altta yatay pill sıraları: KLASÖRLER (XDG dir'leri) + OTURUM
class LauncherMenu : public GlassPopup
{
public:
    explicit LauncherMenu(QWidget *parent = nullptr)
        : GlassPopup(QSize(600, 680), parent)
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(18, 18, 18, 18);
        root->setSpacing(10);

        // ===== Search =====
        searchEdit_ = new QLineEdit;
        searchEdit_->setObjectName("popupSearch");
        searchEdit_->setPlaceholderText(
            QCoreApplication::translate("dock", "Uygulama ara..."));
        searchEdit_->setFixedHeight(44);
        searchEdit_->setClearButtonEnabled(true);
        searchEdit_->installEventFilter(this);  // arrow/enter/esc nav için
        root->addWidget(searchEdit_);

        // ===== Sık Kullanılanlar (search empty iken görünür) =====
        frequentSection_ = new QWidget;
        auto *freqV = new QVBoxLayout(frequentSection_);
        freqV->setContentsMargins(0, 0, 0, 0);
        freqV->setSpacing(6);
        freqV->addWidget(makeSectionHeader("starred",
            QCoreApplication::translate("dock", "SIK KULLANILANLAR")));
        auto *freqHolder = new QWidget;
        frequentGrid_ = new QGridLayout(freqHolder);
        frequentGrid_->setContentsMargins(0, 0, 0, 0);
        frequentGrid_->setHorizontalSpacing(8);
        frequentGrid_->setVerticalSpacing(8);
        freqV->addWidget(freqHolder);
        root->addWidget(frequentSection_);

        // ===== Tüm Uygulamalar =====
        root->addWidget(makeSectionHeader("applications-other",
            QCoreApplication::translate("dock", "UYGULAMALAR")));
        appsScroll_ = new QScrollArea;
        appsScroll_->setObjectName("audioScroll");
        appsScroll_->setWidgetResizable(true);
        appsScroll_->setFrameShape(QFrame::NoFrame);
        appsScroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        appsScroll_->setMinimumHeight(180);

        appsList_ = new QWidget;
        appsListLayout_ = new QVBoxLayout(appsList_);
        appsListLayout_->setContentsMargins(2, 2, 2, 2);
        appsListLayout_->setSpacing(2);
        appsListLayout_->addStretch(1);
        appsScroll_->setWidget(appsList_);
        root->addWidget(appsScroll_, 1);

        // Empty state — search match'i yokken görünür
        emptyLabel_ = new QLabel(
            QCoreApplication::translate("dock", "Eşleşme bulunamadı"));
        emptyLabel_->setObjectName("audioEmpty");
        emptyLabel_->setAlignment(Qt::AlignCenter);
        emptyLabel_->hide();
        root->addWidget(emptyLabel_);

        // ===== Klasörler (horizontal pill row) =====
        root->addWidget(makeSectionHeader("folder",
            QCoreApplication::translate("dock", "KLASÖRLER")));
        auto *foldersRow = new QHBoxLayout;
        foldersRow->setSpacing(6);
        foldersRow->setContentsMargins(0, 0, 0, 0);
        for (const auto &f : makeFolderShortcuts()) {
            const QString p = f.path;
            foldersRow->addWidget(makePill(f.icon, f.label, [p]() {
                QProcess::startDetached("xdg-open", {p});
            }), 1);
        }
        root->addLayout(foldersRow);

        // ===== Sistem (horizontal pill row) =====
        root->addWidget(makeSectionHeader("preferences-system",
            QCoreApplication::translate("dock", "SİSTEM")));
        auto *sessionRow = new QHBoxLayout;
        sessionRow->setSpacing(6);
        sessionRow->setContentsMargins(0, 0, 0, 0);
        sessionRow->addWidget(makePill("preferences-system",
            QCoreApplication::translate("dock", "Ayarlar"), []() {
            QProcess::startDetached("systemsettings");
        }), 1);
        sessionRow->addWidget(makePill("system-lock-screen",
            QCoreApplication::translate("dock", "Kilit"), []() {
            QProcess::startDetached("loginctl", {"lock-session"});
        }), 1);
        sessionRow->addWidget(makePill("system-log-out",
            QCoreApplication::translate("dock", "Çıkış"), []() {
            if (!kdetools::startDetached("qdbus6", "qdbus",
                    {"org.kde.LogoutPrompt", "/LogoutPrompt", "promptLogout"})) {
                QProcess::startDetached("loginctl",
                                        {"terminate-user", qgetenv("USER")});
            }
        }), 1);
        sessionRow->addWidget(makePill("system-reboot",
            QCoreApplication::translate("dock", "Yeniden Başlat"), []() {
            if (!kdetools::startDetached("qdbus6", "qdbus",
                    {"org.kde.LogoutPrompt", "/LogoutPrompt", "promptReboot"})) {
                QProcess::startDetached("systemctl", {"reboot"});
            }
        }), 1);
        sessionRow->addWidget(makePill("system-shutdown",
            QCoreApplication::translate("dock", "Kapat"), []() {
            if (!kdetools::startDetached("qdbus6", "qdbus",
                    {"org.kde.LogoutPrompt", "/LogoutPrompt", "promptShutDown"})) {
                QProcess::startDetached("systemctl", {"poweroff"});
            }
        }), 1);
        root->addLayout(sessionRow);

        // ===== Search wiring =====
        QObject::connect(searchEdit_, &QLineEdit::textChanged, this,
                         [this](const QString &q) {
                             filterApps(q);
                             const bool hasQuery = !q.trimmed().isEmpty();
                             frequentSection_->setVisible(!hasQuery && hasFrequent_);
                         });

        populateApps();
        populateFrequent();
    }

protected:
    void showEvent(QShowEvent *e) override
    {
        GlassPopup::showEvent(e);
        searchEdit_->clear();
        searchEdit_->setFocus();
        populateFrequent();  // her açılışta sık kullanılan tazele
        frequentSection_->setVisible(hasFrequent_);
        selectedIndex_ = 0;
        updateSelectionVisual();
    }

    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == searchEdit_ && e->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(e);
            switch (ke->key()) {
            case Qt::Key_Up:    moveSelection(-1); return true;
            case Qt::Key_Down:  moveSelection(+1); return true;
            case Qt::Key_Return:
            case Qt::Key_Enter: runSelected(); return true;
            case Qt::Key_Escape: hide(); return true;
            default: break;
            }
        }
        return GlassPopup::eventFilter(o, e);
    }

private:
    struct FolderShortcut { QString icon; QString label; QString path; };
    struct AppRow {
        QWidget *widget = nullptr;
        QString searchKey;
        QString exec;
    };

    static QString xdgUserDir(const char *envName, const QString &fallback)
    {
        QProcess p;
        p.start("xdg-user-dir", {envName});
        if (p.waitForFinished(250)) {
            const QString out =
                QString::fromUtf8(p.readAllStandardOutput()).trimmed();
            if (!out.isEmpty() && out != QDir::homePath()) return out;
        }
        return QDir::homePath() + "/" + fallback;
    }

    static QList<FolderShortcut> makeFolderShortcuts()
    {
        const QString home = QDir::homePath();
        return {
            {"user-home",
                QCoreApplication::translate("dock", "Ev"), home},
            {"folder-documents",
                QCoreApplication::translate("dock", "Belgeler"),
                xdgUserDir("DOCUMENTS", "Documents")},
            {"folder-download",
                QCoreApplication::translate("dock", "İndirilenler"),
                xdgUserDir("DOWNLOAD", "Downloads")},
            {"folder-pictures",
                QCoreApplication::translate("dock", "Resimler"),
                xdgUserDir("PICTURES", "Pictures")},
            {"folder-videos",
                QCoreApplication::translate("dock", "Videolar"),
                xdgUserDir("VIDEOS", "Videos")},
        };
    }

    QWidget *makeSectionHeader(const QString &iconName, const QString &text)
    {
        auto *w = new QWidget;
        auto *lay = new QHBoxLayout(w);
        lay->setContentsMargins(2, 0, 0, 0);
        lay->setSpacing(6);
        auto *icoLabel = new QLabel;
        icoLabel->setFixedSize(12, 12);
        QIcon ico = Icons::resolve(iconName, "");
        if (!ico.isNull()) icoLabel->setPixmap(ico.pixmap(12, 12));
        auto *txtLabel = new QLabel(text);
        txtLabel->setObjectName("popupSection");
        lay->addWidget(icoLabel);
        lay->addWidget(txtLabel);
        lay->addStretch(1);
        return w;
    }

    QPushButton *makePill(const QString &iconName, const QString &label,
                          std::function<void()> action)
    {
        auto *btn = new QPushButton(label);
        btn->setProperty("class", QStringList{"launcherPill"});
        btn->setIcon(Icons::resolve(iconName, "application-x-executable"));
        btn->setIconSize({16, 16});
        btn->setFixedHeight(34);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(label);
        QObject::connect(btn, &QPushButton::clicked, this,
                         [this, action]() {
                             if (action) action();
                             hide();
                         });
        return btn;
    }

    void populateApps()
    {
        const auto apps = DesktopIconResolver::installedApps();
        for (const auto &app : apps) {
            auto *row = makeAppRow(app);
            appsListLayout_->insertWidget(appsListLayout_->count() - 1, row);
            rows_.append({row, app.name.toLower(), app.exec});
        }
    }

    QWidget *makeAppRow(const DesktopIconResolver::Entry &app)
    {
        auto *row = new ClickableWidget;
        row->setProperty("class", QStringList{"launcherRow"});
        row->setMinimumHeight(38);
        row->setCursor(Qt::PointingHandCursor);
        const QString exec = app.exec;
        row->onClick = [this, exec]() { launchExec(exec); };

        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(10, 4, 10, 4);
        lay->setSpacing(10);

        auto *iconLabel = new QLabel;
        iconLabel->setFixedSize(24, 24);
        iconLabel->setAlignment(Qt::AlignCenter);
        QIcon ico = Icons::resolve(app.icon, "application-x-executable");
        iconLabel->setPixmap(ico.pixmap(20, 20));
        iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        lay->addWidget(iconLabel, 0, Qt::AlignVCenter);

        auto *name = new QLabel(app.name);
        name->setObjectName("audioName");
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        lay->addWidget(name, 1);

        return row;
    }

    void populateFrequent()
    {
        // Eski grid içeriğini temizle
        while (QLayoutItem *item = frequentGrid_->takeAt(0)) {
            if (auto *w = item->widget()) {
                w->setParent(nullptr);
                w->deleteLater();
            }
            delete item;
        }
        // En çok başlatılan 8 uygulamayı al
        const auto allApps = DesktopIconResolver::installedApps();
        const auto usage = LaunchTracker::usage();
        QList<QPair<int, DesktopIconResolver::Entry>> scored;
        for (const auto &app : allApps) {
            const int c = usage.value(LaunchTracker::sanitize(app.exec), 0);
            if (c > 0) scored.append({c, app});
        }
        std::sort(scored.begin(), scored.end(),
                  [](const auto &a, const auto &b) { return a.first > b.first; });
        const int n = std::min<int>(8, scored.size());
        hasFrequent_ = (n > 0);
        constexpr int kCols = 4;
        for (int i = 0; i < n; ++i) {
            const auto &app = scored[i].second;
            frequentGrid_->addWidget(makeFrequentTile(app), i / kCols, i % kCols);
        }
        frequentSection_->setVisible(hasFrequent_ &&
                                      searchEdit_->text().trimmed().isEmpty());
    }

    QToolButton *makeFrequentTile(const DesktopIconResolver::Entry &app)
    {
        auto *btn = new QToolButton;
        btn->setProperty("class", QStringList{"launcherFreq"});
        btn->setText(app.name);
        btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        btn->setIcon(Icons::resolve(app.icon, "application-x-executable"));
        btn->setIconSize({30, 30});
        btn->setFixedSize(130, 70);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(app.name);
        const QString exec = app.exec;
        QObject::connect(btn, &QToolButton::clicked, this,
                         [this, exec]() { launchExec(exec); });
        return btn;
    }

    void launchExec(const QString &exec)
    {
        if (exec.isEmpty()) return;
        QProcess::startDetached("/bin/sh", {"-c", exec});
        LaunchTracker::record(exec);
        hide();
    }

    void filterApps(const QString &query)
    {
        const QString q = query.trimmed().toLower();
        int visibleCount = 0;
        for (const auto &r : std::as_const(rows_)) {
            const bool show = q.isEmpty() || r.searchKey.contains(q);
            r.widget->setVisible(show);
            if (show) ++visibleCount;
        }
        emptyLabel_->setVisible(visibleCount == 0 && !q.isEmpty());
        appsScroll_->setVisible(visibleCount > 0);
        selectedIndex_ = 0;
        updateSelectionVisual();
    }

    void moveSelection(int delta)
    {
        const QList<int> visIdx = visibleIndices();
        if (visIdx.isEmpty()) return;
        // selectedIndex_ visible-list içindeki konum
        selectedIndex_ = qBound(0, selectedIndex_ + delta, visIdx.size() - 1);
        updateSelectionVisual();
    }

    void runSelected()
    {
        const QList<int> visIdx = visibleIndices();
        if (visIdx.isEmpty()) return;
        const int idx = qBound(0, selectedIndex_, visIdx.size() - 1);
        launchExec(rows_[visIdx[idx]].exec);
    }

    QList<int> visibleIndices() const
    {
        QList<int> out;
        for (int i = 0; i < rows_.size(); ++i) {
            if (rows_[i].widget->isVisible()) out << i;
        }
        return out;
    }

    void updateSelectionVisual()
    {
        const QList<int> visIdx = visibleIndices();
        const int sel = visIdx.isEmpty() ? -1 :
            visIdx[qBound(0, selectedIndex_, visIdx.size() - 1)];
        for (int i = 0; i < rows_.size(); ++i) {
            QStringList cls{"launcherRow"};
            if (i == sel) cls << "selected";
            rows_[i].widget->setProperty("class", cls);
            rows_[i].widget->style()->unpolish(rows_[i].widget);
            rows_[i].widget->style()->polish(rows_[i].widget);
        }
        if (sel >= 0 && appsScroll_) {
            appsScroll_->ensureWidgetVisible(rows_[sel].widget, 0, 30);
        }
    }

    QLineEdit *searchEdit_ = nullptr;
    QWidget *frequentSection_ = nullptr;
    QGridLayout *frequentGrid_ = nullptr;
    QScrollArea *appsScroll_ = nullptr;
    QWidget *appsList_ = nullptr;
    QVBoxLayout *appsListLayout_ = nullptr;
    QLabel *emptyLabel_ = nullptr;
    QList<AppRow> rows_;
    bool hasFrequent_ = false;
    int selectedIndex_ = 0;
};

// Renders a DBusMenu (list of TrayMenuEntry) inside a GlassPopup. We don't
// use QMenu because its popup positioning is broken when the parent is a
// Wayland layer-shell surface — but GlassPopup + manual move()+show() does
// position correctly (same pattern as LauncherMenu / NotificationsPanel).
class TrayMenuPopup : public GlassPopup
{
public:
    TrayMenuPopup(TrayItem *item, const QList<TrayMenuEntry> &entries,
                  QWidget *parent = nullptr)
        : GlassPopup(QSize(260, 100), parent), item_(item)
    {
        auto *col = new QVBoxLayout(this);
        col->setContentsMargins(10, 10, 10, 10);
        col->setSpacing(2);

        const int rowHeight = 30;
        const int sepHeight = 7;
        int totalHeight = 20;     // top + bottom padding
        int maxLabelWidth = 120;  // floor

        for (const TrayMenuEntry &e : entries) {
            if (e.separator) {
                auto *line = new QFrame;
                line->setFrameShape(QFrame::HLine);
                line->setFixedHeight(1);
                line->setStyleSheet(
                    "background: rgba(255,255,255, 22); border: none;");
                col->addWidget(line);
                totalHeight += sepHeight;
                continue;
            }

            QString label = e.label;
            if (e.toggleType == "checkmark" || e.toggleType == "radio") {
                label = (e.checked ? "✓  " : "    ") + label;
            }
            if (e.hasSubmenu) label += "  ›";

            auto *btn = new QPushButton(label);
            btn->setProperty("class", "overflowItem");
            btn->setFixedHeight(rowHeight);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setEnabled(e.enabled);

            QIcon ic = QIcon::fromTheme(e.iconName);
            if (!ic.isNull()) {
                btn->setIcon(ic);
                btn->setIconSize({18, 18});
            }

            const QFontMetrics fm(btn->font());
            const int w = fm.horizontalAdvance(label) + 60;  // icon + padding
            if (w > maxLabelWidth) maxLabelWidth = w;

            const int id = e.id;
            const bool isSubmenu = e.hasSubmenu;
            QObject::connect(btn, &QPushButton::clicked, this,
                             [this, id, isSubmenu]() {
                if (isSubmenu) {
                    // Submenu expansion (chained popup) — TBD. For now
                    // just dispatch the click so apps that drive submenus
                    // off the parent id can respond.
                }
                if (item_) item_->invokeMenuClick(id);
                hide();
            });

            col->addWidget(btn);
            totalHeight += rowHeight + 2;
        }

        setFixedSize(std::clamp(maxLabelWidth + 20, 200, 400), totalHeight);
    }

private:
    QPointer<TrayItem> item_;
};

// Freedesktop notification daemon. Plasma'nın daemon'u (broken Plasma config'i
// olan sistemler dahil) name'i alamadığı durumlarda biz alırız ve tüm sistem
// bildirimlerini doğrudan dock'ta gösteririz. Plasma daemon'u alabiliyorsa
// start() false döner, geçmiş listesi boş kalır.  Implementation:
// src/notificationserver.{h,cpp}.
using NotificationManager = NotificationServer;

class NotificationsPanel : public GlassPopup
{
public:
    explicit NotificationsPanel(QWidget *parent = nullptr)
        : GlassPopup(QSize(380, 420), parent)
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(18, 16, 18, 16);
        root->setSpacing(10);

        auto *header = new QHBoxLayout;
        header->setContentsMargins(0, 0, 0, 0);
        auto *title = new QLabel(
            QCoreApplication::translate("dock", "Bildirimler"));
        title->setObjectName("popupTitle");
        header->addWidget(title);
        header->addStretch();
        clearBtn_ = new QPushButton(
            QCoreApplication::translate("dock", "Temizle"));
        clearBtn_->setObjectName("notifClear");
        clearBtn_->setCursor(Qt::PointingHandCursor);
        clearBtn_->setFlat(true);
        clearBtn_->setStyleSheet(
            "QPushButton { color: rgba(161,161,170,220); background: transparent;"
            " border: none; font-size: 11px; padding: 4px 8px; }"
            "QPushButton:hover { color: rgb(244,244,245); }");
        header->addWidget(clearBtn_);
        root->addLayout(header);

        scroll_ = new QScrollArea(this);
        scroll_->setObjectName("notifScroll");
        scroll_->setWidgetResizable(true);
        scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll_->setFrameShape(QFrame::NoFrame);
        scroll_->setStyleSheet("QScrollArea { background: transparent; border: none; }"
                                "QScrollArea > QWidget > QWidget { background: transparent; }");

        listContainer_ = new QWidget;
        listLayout_ = new QVBoxLayout(listContainer_);
        listLayout_->setContentsMargins(0, 0, 4, 0);
        listLayout_->setSpacing(6);
        listLayout_->addStretch();
        scroll_->setWidget(listContainer_);
        root->addWidget(scroll_, 1);

        showItems({});
    }

    QPushButton *clearButton() const { return clearBtn_; }

    // Notification server'ı bağlar — action butonlarına tıklandığında
    // ActionInvoked DBus signal'ini emit etmek için bu pointer kullanılır.
    void setServer(NotificationServer *s) { server_ = s; }

    void showItems(const QList<NotificationManager::Notification> &items)
    {
        // Clear existing rows (stretch dahil hepsini sıfırla)
        QLayoutItem *child;
        while ((child = listLayout_->takeAt(0)) != nullptr) {
            delete child->widget();
            delete child;
        }
        if (items.isEmpty()) {
            auto *empty = new QLabel(
                QCoreApplication::translate("dock", "Bildirim yok"));
            empty->setAlignment(Qt::AlignCenter);
            empty->setStyleSheet("color: rgba(161,161,170,200); font-size: 12px;");
            listLayout_->addWidget(empty);
        } else {
            for (const auto &n : items) {
                listLayout_->addWidget(makeRow(n));
            }
        }
        listLayout_->addStretch();
    }

private:
    QWidget *makeRow(const NotificationManager::Notification &n)
    {
        auto *row = new ClickableWidget;
        row->setObjectName("notifRow");
        row->setStyleSheet(
            "#notifRow { background: rgba(255,255,255,8); border-radius: 10px; }"
            "#notifRow:hover { background: rgba(255,255,255,16); }");
        // Body tıklama davranışı (KDE Plasma'nın yaptığıyla aynı):
        //   1) "default" action key'i varsa onu invoke (uygulamayı asıl event'e
        //      yönlendirir)
        //   2) Yoksa desktop-entry hint'inden uygulamayı launch et
        //   3) Hiçbiri yoksa cursor değişmesin, click no-op
        bool hasDefault = false;
        for (const auto &a : n.actions) {
            if (a.key == QLatin1String("default")) { hasDefault = true; break; }
        }
        const bool canClick = (hasDefault && n.id != 0)
                              || !n.desktopEntry.isEmpty();
        if (canClick) {
            row->setCursor(Qt::PointingHandCursor);
            const uint id = n.id;
            const QString desktopEntry = n.desktopEntry;
            NotificationServer *server = server_;
            row->onClick = [id, hasDefault, desktopEntry, server]() {
                if (hasDefault && id != 0 && server) {
                    server->invokeAction(id, "default");
                } else if (!desktopEntry.isEmpty()) {
                    // SECURITY: desktop-entry notification hint'inden geliyor —
                    // saldırgan kontrolünde olabilir. QProcess argList versiyonu
                    // shell expansion yapmaz, ama yine de '../etc/.../*.desktop'
                    // veya '/abs/path' ile beklenmedik launch'a karşı validate.
                    // Yalnızca .desktop ID konvansiyonu: alphanumeric + . - _,
                    // max 256 char, ilk char alphanumeric.
                    static const QRegularExpression kSafeDesktopEntry(
                        QStringLiteral("\\A[A-Za-z0-9][A-Za-z0-9._-]{0,255}\\z"));
                    if (!kSafeDesktopEntry.match(desktopEntry).hasMatch()) {
                        qCWarning(dockSecurity) << "notification desktop-entry "
                                      "validation failed, ignoring:"
                                   << desktopEntry;
                        return;
                    }
                    // gtk-launch + .desktop adı / kstart6 — biri bulunmazsa
                    // fallback. gtk-launch en hızlı yol.
                    if (!QProcess::startDetached("gtk-launch", {desktopEntry})) {
                        kdetools::startDetached("kstart6", "kstart",
                            {"--application", desktopEntry});
                    }
                }
            };
        }
        auto *outer = new QVBoxLayout(row);
        outer->setContentsMargins(10, 8, 10, 8);
        outer->setSpacing(6);

        auto *topRow = new QHBoxLayout;
        topRow->setContentsMargins(0, 0, 0, 0);
        topRow->setSpacing(10);

        // Sol taraf: app icon — merkezi resolver hint chain'iyle.
        QStringList iconHints;
        if (!n.appIcon.isEmpty()) iconHints << n.appIcon;
        if (!n.desktopEntry.isEmpty()) iconHints << n.desktopEntry;
        if (!n.appName.isEmpty()) iconHints << n.appName;
        const QIcon icon = Icons::resolve(iconHints,
                                          "preferences-desktop-notification");

        auto *iconLabel = new QLabel(row);
        iconLabel->setFixedSize(28, 28);
        iconLabel->setPixmap(icon.pixmap(24, 24));
        iconLabel->setAlignment(Qt::AlignCenter);
        // Mouse event'leri row'a (ClickableWidget) ulaşsın diye child label'ları
        // mouse-şeffaf yap. QLabel default'ta release event'i parent'a propagate
        // ederken, RichText/Links etkin label QTextControl'u event'i tüketebilir.
        iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        topRow->addWidget(iconLabel, 0, Qt::AlignTop);

        auto *textBox = new QWidget(row);
        textBox->setAttribute(Qt::WA_TransparentForMouseEvents);
        auto *tl = new QVBoxLayout(textBox);
        tl->setContentsMargins(0, 0, 0, 0);
        tl->setSpacing(2);

        const QString headLine = n.appName.isEmpty() ? n.summary
                                                      : (n.appName + " · " + n.summary);
        auto *title = new QLabel(headLine);
        title->setAttribute(Qt::WA_TransparentForMouseEvents);
        title->setStyleSheet("color: rgb(244,244,245); font-size: 12px; font-weight: 500;");
        title->setWordWrap(true);
        if (n.urgency >= 2) {
            title->setStyleSheet("color: rgb(244,63,94); font-size: 12px; font-weight: 600;");
        }
        tl->addWidget(title);

        if (!n.body.isEmpty()) {
            auto *body = new QLabel(n.body);
            // Body'de basit HTML markup'a izin var (<b>, <i>, <a>) — Plain
            // text format'a indirgemeyelim. WA_TransparentForMouseEvents ile
            // mouse event'leri row'a iletilir, link tıklaması fonksiyonalitesi
            // kayboluyor ama "body'ye tıkla → uygulamayı aç" UX'ı kazandırıyoruz.
            body->setTextFormat(Qt::RichText);
            body->setStyleSheet("color: rgba(220,220,220,235); font-size: 11px;");
            body->setWordWrap(true);
            body->setAttribute(Qt::WA_TransparentForMouseEvents);
            tl->addWidget(body);
        }

        auto *time = new QLabel(n.time.toString("HH:mm"));
        time->setAttribute(Qt::WA_TransparentForMouseEvents);
        time->setStyleSheet("color: rgba(113,113,122,200); font-size: 10px;");
        tl->addWidget(time);

        topRow->addWidget(textBox, 1);

        // Sağ taraf: notification image preview (varsa image-data ya da
        // image-path hint'ı). Bu daha "zengin" bildirim deneyimi sağlar —
        // örn. müzik bildirimleri album art, mesajlaşma uygulamaları profil
        // resmi gönderir.
        QPixmap preview;
        if (!n.imageRaw.isNull()) {
            preview = QPixmap::fromImage(n.imageRaw);
        } else if (!n.imagePath.isEmpty()) {
            QString path = n.imagePath;
            if (path.startsWith(QLatin1String("file://"))) path = path.mid(7);
            if (QFile::exists(path)) preview.load(path);
        }
        if (!preview.isNull()) {
            const QSize target(72, 72);
            preview = preview.scaled(target, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
            auto *imgLabel = new QLabel(row);
            imgLabel->setFixedSize(target);
            imgLabel->setAlignment(Qt::AlignCenter);
            imgLabel->setPixmap(preview);
            imgLabel->setStyleSheet("border-radius: 6px;");
            imgLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
            topRow->addWidget(imgLabel, 0, Qt::AlignTop);
        }

        outer->addLayout(topRow);

        // Action butonları (varsa). InvokeAction DBus çağrısı ile bus daemon
        // üzerinden asıl gönderici uygulamaya iletilir; uygulama onClick
        // event'ini çalıştırır.
        if (!n.actions.isEmpty() && n.id != 0) {
            auto *actionsRow = new QHBoxLayout;
            actionsRow->setContentsMargins(38, 0, 0, 0);  // icon altına hizala
            actionsRow->setSpacing(6);
            actionsRow->addStretch();
            for (const auto &a : n.actions) {
                // "default" action'ı genelde notification gövdesine tıklama
                // anlamına gelir; ayrı buton göstermek yerine atla.
                if (a.key == QLatin1String("default")) continue;
                auto *btn = new QPushButton(a.label, row);
                btn->setCursor(Qt::PointingHandCursor);
                btn->setFocusPolicy(Qt::NoFocus);
                btn->setStyleSheet(
                    "QPushButton { color: rgb(244,244,245); background: rgba(255,255,255,16);"
                    " border: 1px solid rgba(255,255,255,32); border-radius: 6px;"
                    " padding: 4px 10px; font-size: 11px; }"
                    "QPushButton:hover { background: rgba(255,255,255,28); }");
                const uint id = n.id;
                const QString key = a.key;
                NotificationServer *server = server_;
                QObject::connect(btn, &QPushButton::clicked, btn,
                                 [id, key, server]() {
                    if (server) server->invokeAction(id, key);
                });
                actionsRow->addWidget(btn);
            }
            outer->addLayout(actionsRow);
        }

        return row;
    }

    QScrollArea *scroll_ = nullptr;
    QWidget *listContainer_ = nullptr;
    QVBoxLayout *listLayout_ = nullptr;
    QPushButton *clearBtn_ = nullptr;
    NotificationServer *server_ = nullptr;
};

// ============================================================================
// Network / system panel — network ikonuyla açılır. Bağlantı türüne göre üst
// kısım dinamik (Ethernet vs Wi-Fi vs Offline), altında parlaklık slider ve
// gerçek aksiyon tile'ları (gece modu, screenshot, güç tasarrufu, kilit).
// Volume slider buradan kaldırıldı — AudioPanel zaten o işi yapıyor.
// ============================================================================


// Polls NetworkManager (system bus) every few seconds and reports the active
// connection type — ethernet vs wifi-{strong,medium,weak} vs offline — so
// the network status icon can adapt instead of always showing a Wi-Fi glyph.
class NetworkMonitor : public QObject
{
public:
    enum class State {
        Unknown,
        Offline,
        Ethernet,
        WifiStrong,   // signal >= 66
        WifiMedium,   // 33-65
        WifiWeak,     // 0-32
    };

    // İkonun ötesinde panel için bağlantı detayları. NM PrimaryConnection
    // üzerinden tek bir refresh'te toplanır.
    struct Info {
        enum Kind { Unknown, Offline, Ethernet, Wifi, Other };
        Kind kind = Unknown;
        QString connectionName;  // NM Id, örn. "Wired connection 1"
        QString deviceName;      // örn. "enp12s0", "wlp3s0"
        QString ipv4;            // "192.168.1.10"
        QString ssid;            // wifi ise
        int signalPercent = 0;   // wifi ise
        int linkSpeedMbps = 0;   // ethernet: Speed (Mbps), wifi: Bitrate (kbps→Mbps)
    };

    // Tanımlı VPN / WireGuard / IPsec bağlantısı. Aktif olanlar `activePath`
    // dolu gelir → toggle için DeactivateConnection bunu kullanır. Pasifler
    // için ActivateConnection settingsPath ile başlatılır.
    struct VpnConnection {
        QString id;             // user-friendly ad
        QString uuid;
        QString type;           // "vpn", "wireguard", "ipsec" vs.
        QString settingsPath;   // /org/freedesktop/NetworkManager/Settings/N
        QString activePath;     // /org/freedesktop/NetworkManager/ActiveConnection/N (boş → pasif)
    };

    explicit NetworkMonitor(QObject *parent = nullptr) : QObject(parent)
    {
        auto *t = new QTimer(this);
        QObject::connect(t, &QTimer::timeout, this, [this]() { refresh(); });
        t->start(5000);
        QTimer::singleShot(0, this, [this]() { refresh(); });
    }

    State state() const { return state_; }
    Info info() const { return info_; }
    QList<VpnConnection> vpns() const { return vpns_; }
    std::function<void(State)> onChange;
    // Her refresh sonrası tetiklenir; State değişmese bile IP/sinyal güncelleyince
    // panel yenilensin diye.
    std::function<void(Info)> onInfoChange;

    // Dışarıdan tetiklenebilir refresh (panel açılışında anında veri çekmek için).
    void requestRefresh() { refresh(); }

    // VPN'i başlat/durdur — nmcli subprocess kullanıyoruz.
    void setVpnActive(const VpnConnection &vpn, bool active)
    {
        if (vpn.uuid.isEmpty()) return;
        QProcess::startDetached(
            "nmcli",
            active ? QStringList{"con", "up", vpn.uuid}
                   : QStringList{"con", "down", vpn.uuid});
        // Refresh'i kısa süre sonra zorla — auth/handshake biraz sürebilir.
        QTimer::singleShot(1200, this, [this]() { refresh(); });
    }

private:
    void refresh()
    {
        Info newInfo = queryInfo();
        const State newState = stateFromInfo(newInfo);
        const bool stateChanged = newState != state_;
        info_ = newInfo;
        state_ = newState;
        vpns_ = queryVpns();
        if (stateChanged && onChange) onChange(state_);
        if (onInfoChange) onInfoChange(info_);
    }

    QList<VpnConnection> queryVpns()
    {
        QList<VpnConnection> result;
        // 1) Aktif UUID'leri topla — bunlar "Bağlı" olarak işaretlenecek.
        QSet<QString> activeUuids;
        const auto activeLines = runNmcli(
            {"-t", "-f", "UUID", "con", "show", "--active"});
        for (const QString &line : activeLines) {
            const QString uuid = line.trimmed();
            if (!uuid.isEmpty()) activeUuids.insert(uuid);
        }

        // 2) Tüm bağlantıları listele; type'a göre VPN olanları filtre et.
        const auto allLines = runNmcli(
            {"-t", "-f", "NAME,UUID,TYPE", "con", "show"});
        for (const QString &line : allLines) {
            const QStringList parts = parseNmcliLine(line);
            if (parts.size() < 3) continue;
            const QString name = parts[0];
            const QString uuid = parts[1];
            const QString type = parts[2];
            if (type != QLatin1String("vpn")
                && type != QLatin1String("wireguard")
                && type != QLatin1String("ipsec")) {
                continue;
            }
            VpnConnection vpn;
            vpn.id = name;
            vpn.uuid = uuid;
            vpn.type = type;
            // nmcli mode'unda settingsPath/activePath DBus path'leri lazım değil;
            // activePath'i flag olarak kullanıyoruz (boş → pasif, dolu → aktif).
            vpn.settingsPath = uuid;
            vpn.activePath = activeUuids.contains(uuid) ? QStringLiteral("active") : QString();
            result.push_back(vpn);
        }
        return result;
    }

    static State stateFromInfo(const Info &info)
    {
        switch (info.kind) {
        case Info::Ethernet: return State::Ethernet;
        case Info::Wifi:
            if (info.signalPercent >= 66) return State::WifiStrong;
            if (info.signalPercent >= 33) return State::WifiMedium;
            return State::WifiWeak;
        case Info::Offline: return State::Offline;
        case Info::Other: return State::Ethernet;  // jenerik bağlı fallback
        case Info::Unknown: return State::Unknown;
        }
        return State::Unknown;
    }

    // nmcli -t output: alanlar ':' ile ayrılır, ':' içeren değerler '\:' olarak
    // escape edilir. Backslash kendisi '\\' olarak çıkar.
    static QStringList parseNmcliLine(const QString &line)
    {
        QStringList parts;
        QString current;
        for (int i = 0; i < line.length(); ++i) {
            const QChar c = line[i];
            if (c == '\\' && i + 1 < line.length()) {
                current += line[++i];
            } else if (c == ':') {
                parts << current;
                current.clear();
            } else {
                current += c;
            }
        }
        parts << current;
        return parts;
    }

    static QStringList runNmcli(const QStringList &args, int timeoutMs = 600)
    {
        QProcess p;
        p.start("nmcli", args);
        if (!p.waitForFinished(timeoutMs)) {
            p.kill();
            return {};
        }
        const QString out = QString::fromUtf8(p.readAllStandardOutput());
        return out.split('\n', Qt::SkipEmptyParts);
    }

    Info queryInfo()
    {
        Info info;
        // 1) Aktif bağlantı listesi — sadece ETHERNET veya WIRELESS olanı seç.
        //    nmcli -t output: "NAME:TYPE:DEVICE:UUID" formatında, : escape'li.
        const auto lines = runNmcli(
            {"-t", "-f", "NAME,TYPE,DEVICE,UUID", "con", "show", "--active"});
        QString chosenName, chosenType, chosenDevice;
        for (const QString &line : lines) {
            const QStringList parts = parseNmcliLine(line);
            if (parts.size() < 3) continue;
            const QString type = parts[1];
            const bool isPhysical = type.contains("ethernet", Qt::CaseInsensitive)
                                    || type.contains("wireless", Qt::CaseInsensitive);
            if (!isPhysical) continue;
            chosenName = parts[0];
            chosenType = type;
            chosenDevice = parts[2];
            break;
        }

        if (chosenName.isEmpty()) {
            info.kind = Info::Offline;
            return info;
        }
        info.connectionName = chosenName;
        info.deviceName = chosenDevice;
        info.kind = chosenType.contains("ethernet", Qt::CaseInsensitive)
                    ? Info::Ethernet : Info::Wifi;

        // 2) IP address — nmcli device show ile.
        if (!chosenDevice.isEmpty()) {
            const auto devInfo = runNmcli(
                {"-t", "-f", "IP4.ADDRESS", "device", "show", chosenDevice});
            for (const QString &dline : devInfo) {
                const QStringList p = parseNmcliLine(dline);
                if (p.size() < 2) continue;
                if (p[0].startsWith("IP4.ADDRESS")) {
                    QString addr = p[1];
                    const int slash = addr.indexOf('/');
                    if (slash > 0) addr = addr.left(slash);
                    info.ipv4 = addr;
                    break;
                }
            }
        }

        // 3) Link speed / WiFi extra.
        if (info.kind == Info::Ethernet && !chosenDevice.isEmpty()) {
            QFile f(QStringLiteral("/sys/class/net/%1/speed").arg(chosenDevice));
            if (f.open(QIODevice::ReadOnly)) {
                info.linkSpeedMbps =
                    QString::fromUtf8(f.readAll()).trimmed().toInt();
            }
        } else if (info.kind == Info::Wifi) {
            const auto wifiLines = runNmcli(
                {"-t", "-f", "IN-USE,SSID,SIGNAL,RATE", "dev", "wifi", "list"});
            for (const QString &wline : wifiLines) {
                const QStringList p = parseNmcliLine(wline);
                if (p.size() < 4) continue;
                if (p[0] == "*") {
                    info.ssid = p[1];
                    info.signalPercent = p[2].toInt();
                    static const QRegularExpression nonDigit("[^0-9]");
                    info.linkSpeedMbps =
                        QString(p[3]).remove(nonDigit).toInt();
                    break;
                }
            }
        }

        return info;
    }

    static QString readIpv4(const QDBusObjectPath &ip4Path,
                            const QDBusConnection &bus)
    {
        if (ip4Path.path().isEmpty() || ip4Path.path() == "/") return QString();
        QDBusInterface ip4("org.freedesktop.NetworkManager", ip4Path.path(),
                           "org.freedesktop.NetworkManager.IP4Config", bus);
        if (!ip4.isValid()) return QString();
        // AddressData = aa{sv} → her giriş için {address, prefix} keys.
        const QVariant addrVar = ip4.property("AddressData");
        if (!addrVar.canConvert<QDBusArgument>()) return QString();
        QDBusArgument arg = addrVar.value<QDBusArgument>();
        QList<QVariantMap> addresses;
        arg.beginArray();
        while (!arg.atEnd()) {
            QVariantMap entry;
            arg >> entry;
            addresses.push_back(entry);
        }
        arg.endArray();
        if (addresses.isEmpty()) return QString();
        return addresses.first().value("address").toString();
    }

    State state_ = State::Unknown;
    Info info_;
    QList<VpnConnection> vpns_;
};
class NetworkPanel : public GlassPopup
{
public:
    explicit NetworkPanel(NetworkMonitor *monitor, QWidget *parent = nullptr)
        : GlassPopup(QSize(460, 540), parent), monitor_(monitor)
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(18, 18, 18, 18);
        root->setSpacing(12);

        // 1) Dinamik bağlantı kartı
        auto *connHeader = new QLabel(
            QCoreApplication::translate("dock", "BAĞLANTI"));
        connHeader->setObjectName("popupSection");
        root->addWidget(connHeader);
        connCard_ = new QWidget;
        connCard_->setProperty("class", QStringList{"connCard"});
        connCardLayout_ = new QHBoxLayout(connCard_);
        connCardLayout_->setContentsMargins(12, 10, 12, 10);
        connCardLayout_->setSpacing(12);
        root->addWidget(connCard_);

        // 2) Aksiyon / toggle tile'ları (üst sıra)
        auto *togHeader = new QLabel(
            QCoreApplication::translate("dock", "CİHAZ"));
        togHeader->setObjectName("popupSection");
        root->addWidget(togHeader);

        auto *toggles = new QGridLayout;
        toggles->setSpacing(10);
        bluetoothTile_ = makeTile("preferences-system-bluetooth",
            QCoreApplication::translate("dock", "Bluetooth"),
            QCoreApplication::translate("dock", "Aç →"), false, []() {
                sysctl::openBluetoothSettings();
            });
        nightModeTile_ = makeTile("redshift-status-on",
            QCoreApplication::translate("dock", "Gece Modu"),
            QCoreApplication::translate("dock", "Aç →"), false, []() {
                sysctl::openNightLightSettings();
            });
        auto *settingsTile = makeTile("preferences-system",
            QCoreApplication::translate("dock", "Ayarlar"),
            QCoreApplication::translate("dock", "Tüm ayarlar"), false, []() {
                QProcess::startDetached("systemsettings");
            });
        toggles->addWidget(bluetoothTile_, 0, 0);
        toggles->addWidget(nightModeTile_, 0, 1);

        // Güç profili tile'ı sadece bataryalı cihazda anlamlı; masaüstünde
        // gizle ve Ayarlar tile'ını sola al, ikinci satırı atla.
        if (sysctl::hasBattery()) {
            powerSaverTile_ = makeTile("battery-low",
                QCoreApplication::translate("dock", "Güç Profili"),
                "", false, [this]() {
                                           const QString cur = sysctl::activePowerProfile();
                                           sysctl::setPowerProfile(
                                               cur == "power-saver" ? "balanced"
                                                                    : "power-saver");
                                           refreshToggleStates();
                                       });
            toggles->addWidget(powerSaverTile_, 1, 0);
            toggles->addWidget(settingsTile, 1, 1);
        } else {
            // Bataryasız: 2x1 grid yerine 2x1 üst sıra + tek-hücreli alt
            // sıra. Settings tile'ı tek başına alta düşmesin diye Bluetooth
            // satırının yanına almak yerine, üst sırayı 3 sütun yap.
            // En basit: alt satırda settings tile'ı geniş yerleşim.
            toggles->addWidget(settingsTile, 1, 0, 1, 2);
            settingsTile->setFixedHeight(56);
        }
        root->addLayout(toggles);

        // 3) VPN section — NM'de tanımlı her vpn/wireguard/ipsec için satır.
        //    Sistemde VPN tanımlı değilse header + list tamamen gizli kalır
        //    (gereksiz boşluk olmasın).
        vpnHeader_ = new QLabel("VPN");
        vpnHeader_->setObjectName("popupSection");
        vpnHeader_->hide();
        root->addWidget(vpnHeader_);
        vpnList_ = new QWidget;
        vpnListLayout_ = new QVBoxLayout(vpnList_);
        vpnListLayout_->setContentsMargins(0, 0, 0, 0);
        vpnListLayout_->setSpacing(6);
        vpnList_->hide();
        root->addWidget(vpnList_);

        // 4) Parlaklık (eğer cihaz destekliyorsa)
        const int brMax = sysctl::brightnessMax();
        if (brMax > 0) {
            brightnessRow_ = buildBrightnessRow();
            root->addWidget(brightnessRow_);
        }

        // 5) Eylemler — screenshot + kilit (toggle değil, anlık eylem)
        auto *actionHeader = new QLabel(
            QCoreApplication::translate("dock", "EYLEMLER"));
        actionHeader->setObjectName("popupSection");
        root->addWidget(actionHeader);

        auto *actions = new QGridLayout;
        actions->setSpacing(10);
        actions->addWidget(makeTile("ksnip",
            QCoreApplication::translate("dock", "Ekran Görüntüsü"), "", false,
            []() { sysctl::takeScreenshot(); }),
            0, 0);
        actions->addWidget(makeTile("system-lock-screen",
            QCoreApplication::translate("dock", "Kilit Ekranı"), "", false,
            []() { sysctl::lockSession(); }),
            0, 1);
        root->addLayout(actions);
        root->addStretch(1);

        // NM info güncellemesi
        rebuildConnCard();
        rebuildVpnList();
        if (monitor_) {
            const auto prev = monitor_->onInfoChange;
            monitor_->onInfoChange = [this, prev](NetworkMonitor::Info info) {
                if (prev) prev(info);
                rebuildConnCard();
                rebuildVpnList();
            };
        }
        refreshToggleStates();
    }

protected:
    void showEvent(QShowEvent *e) override
    {
        GlassPopup::showEvent(e);
        // Panel her açıldığında durumu tazele.
        if (monitor_) monitor_->requestRefresh();
        refreshToggleStates();
        refreshBrightness();
    }

private:
    QPushButton *makeTile(const QString &iconName, const QString &line1,
                          const QString &line2, bool active,
                          std::function<void()> onClick)
    {
        const QString text = line2.isEmpty() ? line1 : (line1 + "\n" + line2);
        auto *button = new QPushButton(text);
        button->setProperty("class", QStringList{
            "quickTile", active ? QStringLiteral("active") : QString()
        });
        button->setFixedHeight(56);
        button->setCursor(Qt::PointingHandCursor);
        const QIcon icon = QIcon::fromTheme(iconName);
        if (!icon.isNull()) {
            button->setIcon(icon);
            button->setIconSize({22, 22});
        }
        if (onClick) {
            QObject::connect(button, &QPushButton::clicked, this, onClick);
        }
        return button;
    }

    void setTileState(QPushButton *tile, bool active, const QString &line1,
                      const QString &line2)
    {
        if (!tile) return;
        tile->setProperty("class", QStringList{
            "quickTile", active ? QStringLiteral("active") : QString()
        });
        tile->setText(line2.isEmpty() ? line1 : (line1 + "\n" + line2));
        tile->style()->unpolish(tile);
        tile->style()->polish(tile);
    }

    QWidget *buildBrightnessRow()
    {
        auto *row = new QWidget;
        auto *layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(10);

        auto *iconLabel = new QLabel;
        iconLabel->setFixedSize(20, 20);
        const QIcon icon = QIcon::fromTheme("video-display-brightness");
        if (!icon.isNull()) iconLabel->setPixmap(icon.pixmap(20, 20));
        else iconLabel->setText("☀");

        auto *text = new QLabel(
            QCoreApplication::translate("dock", "Parlaklık"));
        text->setObjectName("sliderLabel");
        text->setFixedWidth(72);

        brightnessSlider_ = new QSlider(Qt::Horizontal);
        brightnessSlider_->setObjectName("quickSlider");
        brightnessSlider_->setRange(0, 100);
        brightnessSlider_->setValue(qMax(0, sysctl::brightnessPercent()));

        brightnessPct_ = new QLabel(QString::number(brightnessSlider_->value()) + "%");
        brightnessPct_->setObjectName("sliderValue");
        brightnessPct_->setFixedWidth(40);
        brightnessPct_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        QObject::connect(brightnessSlider_, &QSlider::valueChanged, this,
                         [this](int v) {
                             brightnessPct_->setText(QString::number(v) + "%");
                             sysctl::setBrightnessPercent(v);
                         });

        layout->addWidget(iconLabel);
        layout->addWidget(text);
        layout->addWidget(brightnessSlider_, 1);
        layout->addWidget(brightnessPct_);
        return row;
    }

    void refreshBrightness()
    {
        if (!brightnessSlider_) return;
        const int cur = sysctl::brightnessPercent();
        if (cur < 0) return;
        QSignalBlocker block(brightnessSlider_);
        brightnessSlider_->setValue(cur);
        brightnessPct_->setText(QString::number(cur) + "%");
    }

    void refreshToggleStates()
    {
        // Gece Modu tile'ı toggle değil — KCM shortcut. Yine de aktif/pasif
        // durumunu altyazıda gösterelim (DBus enabled property salt okunur).
        const bool night = sysctl::nightLightEnabled();
        setTileState(nightModeTile_, night,
            QCoreApplication::translate("dock", "Gece Modu"),
            night ? QCoreApplication::translate("dock", "Açık · Ayarlar →")
                  : QCoreApplication::translate("dock", "Kapalı · Ayarlar →"));

        const QString prof = sysctl::activePowerProfile();
        const bool saver = (prof == QLatin1String("power-saver"));
        QString line2 = QCoreApplication::translate("dock", "Dengeli");
        if (prof == QLatin1String("performance"))
            line2 = QCoreApplication::translate("dock", "Performans");
        else if (saver)
            line2 = QCoreApplication::translate("dock", "Tasarruf");
        setTileState(powerSaverTile_, saver,
            QCoreApplication::translate("dock", "Güç Profili"), line2);
    }

    void rebuildVpnList()
    {
        if (!vpnListLayout_) return;
        destroyLayoutContents(vpnListLayout_);
        if (!monitor_) return;
        const auto list = monitor_->vpns();
        const bool visible = !list.isEmpty();
        if (vpnHeader_) vpnHeader_->setVisible(visible);
        if (vpnList_) vpnList_->setVisible(visible);
        for (const auto &v : list) {
            vpnListLayout_->addWidget(makeVpnRow(v));
        }
    }

    QWidget *makeVpnRow(const NetworkMonitor::VpnConnection &vpn)
    {
        const bool active = !vpn.activePath.isEmpty();
        auto *row = new ClickableWidget;
        QStringList classes{"audioRow"};  // aynı stil chassisi — tutarlı görünüm
        if (active) classes << "active";
        row->setProperty("class", classes);
        row->setMinimumHeight(48);
        row->setCursor(Qt::PointingHandCursor);
        const auto vpnCopy = vpn;
        row->onClick = [this, vpnCopy, active]() {
            if (monitor_) monitor_->setVpnActive(vpnCopy, !active);
        };

        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(12, 8, 12, 8);
        lay->setSpacing(10);

        auto *iconLabel = new QLabel(row);
        iconLabel->setFixedSize(28, 28);
        iconLabel->setAlignment(Qt::AlignCenter);
        QIcon ico = Icons::resolve(QStringList{"network-vpn", "security-high"},
                                   "network-wired");
        iconLabel->setPixmap(ico.pixmap(22, 22));
        iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        lay->addWidget(iconLabel, 0, Qt::AlignVCenter);

        auto *nameCol = new QVBoxLayout;
        nameCol->setContentsMargins(0, 0, 0, 0);
        nameCol->setSpacing(1);
        auto *name = new QLabel(vpn.id.isEmpty() ? "VPN" : vpn.id, row);
        name->setObjectName("audioName");
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        nameCol->addWidget(name);
        auto *sub = new QLabel(row);
        sub->setObjectName("audioHint");
        sub->setAttribute(Qt::WA_TransparentForMouseEvents);
        QString typeName = vpn.type;
        if (typeName == "wireguard") typeName = "WireGuard";
        else if (typeName == "vpn") typeName = "VPN";
        else if (typeName == "ipsec") typeName = "IPsec";
        sub->setText(active
            ? (QCoreApplication::translate("dock", "● Bağlı  ·  ") + typeName)
            : (QCoreApplication::translate("dock", "Bağlı değil  ·  ") + typeName));
        nameCol->addWidget(sub);
        lay->addLayout(nameCol, 1);

        auto *toggleBtn = new QPushButton(active
            ? QCoreApplication::translate("dock", "Kes")
            : QCoreApplication::translate("dock", "Bağlan"), row);
        toggleBtn->setProperty("class", QStringList{"connCardAction"});
        toggleBtn->setCursor(Qt::PointingHandCursor);
        QObject::connect(toggleBtn, &QPushButton::clicked, this,
                         [this, vpnCopy, active]() {
                             if (monitor_) monitor_->setVpnActive(vpnCopy, !active);
                         });
        lay->addWidget(toggleBtn, 0, Qt::AlignVCenter);
        return row;
    }

    // Layout'tan rekürsif olarak hem widget hem de iç layout'ları temizler.
    // Tek başına takeAt + setParent ALT layout'lardaki widget'ları kaçırır
    // çünkü onlar parent widget'a bağlı, alt layout'a değil — alt layout
    // silindikten sonra paint hierarchy'sinde kalmaya devam ederler.
    static void destroyLayoutContents(QLayout *layout)
    {
        if (!layout) return;
        while (QLayoutItem *item = layout->takeAt(0)) {
            if (auto *w = item->widget()) {
                w->setParent(nullptr);
                w->deleteLater();
            } else if (auto *sub = item->layout()) {
                destroyLayoutContents(sub);
            }
            delete item;
        }
    }

    void rebuildConnCard()
    {
        destroyLayoutContents(connCardLayout_);
        if (!monitor_) return;
        const auto info = monitor_->info();

        QString iconName;
        QString line1;
        QString line2;
        QString line3;
        switch (info.kind) {
        case NetworkMonitor::Info::Ethernet:
            iconName = "network-wired";
            line1 = info.connectionName.isEmpty()
                ? QCoreApplication::translate("dock", "Ethernet")
                : info.connectionName;
            line2 = info.deviceName;
            if (!info.ipv4.isEmpty()) line2 += "  ·  " + info.ipv4;
            if (info.linkSpeedMbps > 0) {
                line3 = info.linkSpeedMbps >= 1000
                    ? QCoreApplication::translate("dock", "Bağlandı  ·  %1 Gbps")
                        .arg(info.linkSpeedMbps / 1000)
                    : QCoreApplication::translate("dock", "Bağlandı  ·  %1 Mbps")
                        .arg(info.linkSpeedMbps);
            } else {
                line3 = QCoreApplication::translate("dock", "Bağlandı");
            }
            break;
        case NetworkMonitor::Info::Wifi:
            iconName = info.signalPercent >= 66 ? "network-wireless-signal-excellent"
                     : info.signalPercent >= 33 ? "network-wireless-signal-good"
                     : "network-wireless-signal-weak";
            line1 = info.ssid.isEmpty()
                ? QCoreApplication::translate("dock", "Wi-Fi")
                : info.ssid;
            line2 = info.deviceName;
            if (!info.ipv4.isEmpty()) line2 += "  ·  " + info.ipv4;
            line3 = QCoreApplication::translate("dock", "Sinyal %1%")
                .arg(info.signalPercent);
            if (info.linkSpeedMbps > 0) {
                line3 += QString("  ·  %1 Mbps").arg(info.linkSpeedMbps);
            }
            break;
        case NetworkMonitor::Info::Other:
            iconName = "network-vpn";
            line1 = info.connectionName.isEmpty()
                ? QCoreApplication::translate("dock", "Bağlandı")
                : info.connectionName;
            line2 = info.deviceName;
            if (!info.ipv4.isEmpty()) line2 += "  ·  " + info.ipv4;
            line3 = QCoreApplication::translate("dock", "VPN / diğer");
            break;
        case NetworkMonitor::Info::Offline:
            iconName = "network-offline";
            line1 = QCoreApplication::translate("dock", "Bağlantı yok");
            line2 = QCoreApplication::translate("dock", "Ağ ayarlarından bağlanın");
            break;
        default:
            iconName = "network-offline";
            line1 = QCoreApplication::translate("dock", "Ağ durumu bilinmiyor");
            break;
        }

        auto *iconLabel = new QLabel;
        iconLabel->setFixedSize(36, 36);
        QIcon ico = QIcon::fromTheme(iconName);
        if (ico.isNull()) ico = QIcon::fromTheme("network-wired");
        iconLabel->setPixmap(ico.pixmap(32, 32));
        iconLabel->setAlignment(Qt::AlignCenter);
        connCardLayout_->addWidget(iconLabel, 0, Qt::AlignVCenter);

        auto *col = new QVBoxLayout;
        col->setContentsMargins(0, 0, 0, 0);
        col->setSpacing(1);
        auto *l1 = new QLabel(line1);
        l1->setObjectName("connCardTitle");
        col->addWidget(l1);
        if (!line2.isEmpty()) {
            auto *l2 = new QLabel(line2);
            l2->setObjectName("connCardSub");
            col->addWidget(l2);
        }
        if (!line3.isEmpty()) {
            auto *l3 = new QLabel(line3);
            l3->setObjectName("connCardSub");
            col->addWidget(l3);
        }
        connCardLayout_->addLayout(col, 1);

        // Detay / ayarlar düğmesi sağda
        auto *settingsBtn = new QPushButton(
            QCoreApplication::translate("dock", "Ayarlar"));
        settingsBtn->setProperty("class", QStringList{"connCardAction"});
        settingsBtn->setCursor(Qt::PointingHandCursor);
        QObject::connect(settingsBtn, &QPushButton::clicked, this,
                         []() { sysctl::openNetworkSettings(); });
        connCardLayout_->addWidget(settingsBtn, 0, Qt::AlignVCenter);
    }

    NetworkMonitor *monitor_ = nullptr;
    QWidget *connCard_ = nullptr;
    QHBoxLayout *connCardLayout_ = nullptr;
    QPushButton *bluetoothTile_ = nullptr;
    QPushButton *nightModeTile_ = nullptr;
    QPushButton *powerSaverTile_ = nullptr;
    QWidget *brightnessRow_ = nullptr;
    QSlider *brightnessSlider_ = nullptr;
    QLabel *brightnessPct_ = nullptr;
    QLabel *vpnHeader_ = nullptr;
    QWidget *vpnList_ = nullptr;
    QVBoxLayout *vpnListLayout_ = nullptr;
};


// ============================================================================
// Audio (pactl-based — PulseAudio veya PipeWire-pulse compat üzerinde çalışır)
// ============================================================================


// pactl önyüzü: subscribe event akışı dinler, herhangi bir olay gelince
// debounced biçimde full state'i `pactl list` çağrılarıyla yeniden okur.
// `Q_OBJECT` kullanmıyoruz (main.cpp moc'lanmıyor); değişiklikler
// `std::function onChanged` üzerinden tetiklenir.
class AudioControl : public QObject
{
public:
    explicit AudioControl(QObject *parent = nullptr) : QObject(parent)
    {
        // Subscribe akışı bir saniyede 5-10 event üretebiliyor (slider
        // sürüklenirken her snapshot). 50ms debounce ile tek refresh'e
        // toplanır.
        refreshTimer_ = new QTimer(this);
        refreshTimer_->setSingleShot(true);
        refreshTimer_->setInterval(50);
        QObject::connect(refreshTimer_, &QTimer::timeout, this,
                         [this]() { refreshAll(); });

        startSubscribe();
        QTimer::singleShot(0, this, [this]() { refreshAll(); });
    }

    const QList<audio::Device> &sinks() const { return sinks_; }
    const QList<audio::Device> &sources() const { return sources_; }
    const QList<audio::Stream> &streams() const { return streams_; }
    QString defaultSink() const { return defaultSink_; }
    QString defaultSource() const { return defaultSource_; }

    int defaultSinkVolume() const
    {
        for (const auto &d : sinks_) {
            if (d.isDefault) return d.muted ? 0 : d.volume;
        }
        return 0;
    }
    bool defaultSinkMuted() const
    {
        for (const auto &d : sinks_) {
            if (d.isDefault) return d.muted;
        }
        return false;
    }

    void setSinkVolume(const QString &name, int pct)
    {
        run({"set-sink-volume", name, QString::number(qBound(0, pct, 150)) + "%"});
    }
    void setSinkMute(const QString &name, bool m)
    {
        run({"set-sink-mute", name, m ? "1" : "0"});
    }
    void setSourceVolume(const QString &name, int pct)
    {
        run({"set-source-volume", name, QString::number(qBound(0, pct, 150)) + "%"});
    }
    void setSourceMute(const QString &name, bool m)
    {
        run({"set-source-mute", name, m ? "1" : "0"});
    }
    void setStreamVolume(int id, int pct)
    {
        run({"set-sink-input-volume", QString::number(id),
             QString::number(qBound(0, pct, 150)) + "%"});
    }
    void setStreamMute(int id, bool m)
    {
        run({"set-sink-input-mute", QString::number(id), m ? "1" : "0"});
    }
    void setDefaultSink(const QString &name) { run({"set-default-sink", name}); }
    void setDefaultSource(const QString &name) { run({"set-default-source", name}); }

    std::function<void()> onChanged;

private:
    static QString readProcOutput(const QStringList &args, int timeoutMs = 250)
    {
        QProcess p;
        p.start("pactl", args);
        if (!p.waitForFinished(timeoutMs)) {
            p.kill();
            return QString();
        }
        return QString::fromUtf8(p.readAllStandardOutput());
    }

    static void run(const QStringList &args)
    {
        QProcess::startDetached("pactl", args);
    }

    void startSubscribe()
    {
        subscribe_ = new QProcess(this);
        QObject::connect(subscribe_, &QProcess::readyReadStandardOutput, this,
                         [this]() {
                             // İçeriği okuyup atıyoruz; tip ne olursa olsun
                             // bir full refresh tetikliyoruz. Olay başına filtreleme
                             // yapmak ~3 forklu refresh maliyetinden daha pahalı.
                             subscribe_->readAllStandardOutput();
                             refreshTimer_->start();
                         });
        QObject::connect(subscribe_,
                         qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                         this, [this](int, QProcess::ExitStatus) {
                             // Sunucu yeniden başladıysa 1s sonra yeniden bağlan.
                             QTimer::singleShot(1000, this,
                                                [this]() { startSubscribe(); });
                         });
        subscribe_->start("pactl", QStringList{"subscribe"});
    }

    void refreshAll()
    {
        defaultSink_ = readProcOutput({"get-default-sink"}).trimmed();
        defaultSource_ = readProcOutput({"get-default-source"}).trimmed();
        sinks_ = audio::parseDevices(readProcOutput({"list", "sinks"}), "Sink",
                                     defaultSink_);
        // Source listesinde her sink'in `.monitor` loopback'i de gelir; mic'ler
        // değil. Bunları kullanıcıya gösterme.
        QList<audio::Device> rawSrc = audio::parseDevices(
            readProcOutput({"list", "sources"}), "Source", defaultSource_);
        sources_.clear();
        for (const auto &s : rawSrc)
            if (!audio::isMonitor(s)) sources_.push_back(s);
        streams_ = audio::parseStreams(readProcOutput({"list", "sink-inputs"}));
        // PipeWire anonim stream'lerinde (Web Audio, oyun child process, vs.)
        // application.* metadata stream'de yok client'ta — merge ile tamamla.
        const QHash<int, audio::Client> clients =
            audio::parseClients(readProcOutput({"list", "clients"}));
        for (auto &s : streams_) audio::mergeStreamWithClient(s, clients);
        if (onChanged) onChanged();
    }

    QProcess *subscribe_ = nullptr;
    QTimer *refreshTimer_ = nullptr;
    QList<audio::Device> sinks_;
    QList<audio::Device> sources_;
    QList<audio::Stream> streams_;
    QString defaultSink_;
    QString defaultSource_;
};

// İki sekmeli ses paneli:
//   • Cihazlar: çıkış (sinks) + giriş (sources, monitor olmayanlar)
//   • Uygulamalar: aktif sink-input'lar (Spotify, tarayıcı, oyun, vs.)
// Slider sürüklenirken full rebuild atlanır — aksi takdirde kullanıcının
// elinden slider kayar (subscribe event'leri saniyede 5-10 kez gelir).
class AudioPanel : public GlassPopup
{
public:
    explicit AudioPanel(AudioControl *control, QWidget *parent = nullptr)
        : GlassPopup(QSize(480, 500), parent), control_(control)
    {
        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(18, 18, 18, 18);
        root->setSpacing(12);

        // Tab bar: underline-style indicator (modern app convention).
        // Tab'ların altında ince track line — checked state'in border-bottom'u
        // bu track üzerinde "kayan" indicator gibi görünür.
        auto *tabBar = new QWidget(this);
        auto *tabRow = new QHBoxLayout(tabBar);
        tabRow->setContentsMargins(0, 0, 0, 0);
        tabRow->setSpacing(20);
        devicesTab_ = makeTabButton(
            QCoreApplication::translate("dock", "Cihazlar"));
        appsTab_ = makeTabButton(
            QCoreApplication::translate("dock", "Uygulamalar"));
        devicesTab_->setChecked(true);
        tabRow->addWidget(devicesTab_);
        tabRow->addWidget(appsTab_);
        tabRow->addStretch(1);
        root->addWidget(tabBar);
        auto *tabSep = new QFrame(this);
        tabSep->setFrameShape(QFrame::HLine);
        tabSep->setFrameShadow(QFrame::Plain);
        tabSep->setFixedHeight(1);
        tabSep->setStyleSheet(
            "background: rgba(255,255,255,18); border: none;");
        root->addWidget(tabSep);

        devicesPage_ = buildDevicesPage();
        appsPage_ = buildAppsPage();
        appsPage_->hide();
        root->addWidget(devicesPage_, 1);
        root->addWidget(appsPage_, 1);

        QObject::connect(devicesTab_, &QPushButton::clicked, this, [this]() {
            devicesTab_->setChecked(true);
            appsTab_->setChecked(false);
            devicesPage_->show();
            appsPage_->hide();
        });
        QObject::connect(appsTab_, &QPushButton::clicked, this, [this]() {
            appsTab_->setChecked(true);
            devicesTab_->setChecked(false);
            appsPage_->show();
            devicesPage_->hide();
        });

        control_->onChanged = [this]() { rebuild(); };
        rebuild();
    }

private:
    static QPushButton *makeTabButton(const QString &text)
    {
        auto *b = new QPushButton(text);
        b->setCheckable(true);
        b->setProperty("class", "audioTab");
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(32);
        b->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        return b;
    }

    QWidget *buildDevicesPage()
    {
        auto *page = new QWidget(this);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto *scroll = new QScrollArea(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setObjectName("audioScroll");

        devicesList_ = new QWidget;
        devicesListLayout_ = new QVBoxLayout(devicesList_);
        devicesListLayout_->setContentsMargins(2, 2, 2, 2);
        devicesListLayout_->setSpacing(8);
        devicesListLayout_->addStretch(1);
        scroll->setWidget(devicesList_);

        layout->addWidget(scroll, 1);
        return page;
    }

    QWidget *buildAppsPage()
    {
        auto *page = new QWidget(this);
        auto *layout = new QVBoxLayout(page);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        emptyAppsLabel_ = new QLabel(
            QCoreApplication::translate("dock", "Ses çıkaran uygulama yok."), page);
        emptyAppsLabel_->setAlignment(Qt::AlignCenter);
        emptyAppsLabel_->setObjectName("audioEmpty");
        emptyAppsLabel_->hide();
        layout->addWidget(emptyAppsLabel_);

        auto *scroll = new QScrollArea(page);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setObjectName("audioScroll");

        appsList_ = new QWidget;
        appsListLayout_ = new QVBoxLayout(appsList_);
        appsListLayout_->setContentsMargins(2, 2, 2, 2);
        appsListLayout_->setSpacing(6);
        appsListLayout_->addStretch(1);
        scroll->setWidget(appsList_);
        layout->addWidget(scroll, 1);
        return page;
    }

    bool anySliderDown() const
    {
        for (auto *s : findChildren<QSlider *>()) {
            if (s->isSliderDown()) return true;
        }
        return false;
    }

    void clearListLayout(QVBoxLayout *layout)
    {
        // Sonda stretch var — onu koru, kalanları sil.
        while (layout->count() > 1) {
            auto *item = layout->takeAt(0);
            if (auto *w = item->widget()) w->deleteLater();
            delete item;
        }
    }

    void rebuild()
    {
        // Kullanıcı slider sürüklerken full rebuild atlamak — yoksa widget
        // delete'lenip sürükleme kopuyor. Bırakıldığında bir sonraki event
        // refreshTimer'ı yeniden ateşler.
        if (anySliderDown()) {
            QTimer::singleShot(200, this, [this]() { rebuild(); });
            return;
        }

        clearListLayout(devicesListLayout_);

        auto addSection = [this](const QString &text) {
            auto *lbl = new QLabel(text);
            lbl->setObjectName("audioSection");
            devicesListLayout_->insertWidget(devicesListLayout_->count() - 1, lbl);
        };

        addSection(QCoreApplication::translate("dock", "ÇIKIŞ"));
        for (const auto &d : control_->sinks()) {
            devicesListLayout_->insertWidget(devicesListLayout_->count() - 1,
                                             makeDeviceRow(d, true));
        }
        addSection(QCoreApplication::translate("dock", "GİRİŞ"));
        if (control_->sources().isEmpty()) {
            auto *lbl = new QLabel(
                QCoreApplication::translate("dock", "Mikrofon bulunamadı."));
            lbl->setObjectName("audioEmpty");
            devicesListLayout_->insertWidget(devicesListLayout_->count() - 1, lbl);
        }
        for (const auto &d : control_->sources()) {
            devicesListLayout_->insertWidget(devicesListLayout_->count() - 1,
                                             makeDeviceRow(d, false));
        }

        clearListLayout(appsListLayout_);
        const auto &streams = control_->streams();
        emptyAppsLabel_->setVisible(streams.isEmpty());
        for (const auto &s : streams) {
            appsListLayout_->insertWidget(appsListLayout_->count() - 1,
                                          makeStreamRow(s));
        }
    }

    QWidget *makeDeviceRow(const audio::Device &dev, bool isSink)
    {
        auto *row = new ClickableWidget;
        // Qt QSS'te [class~="X"] selector'ı QStringList property ile çalışır —
        // QString verirsen "audioRow active" tek token olur, asla match etmez.
        QStringList classes{"audioRow"};
        if (dev.isDefault) classes << "active";
        if (dev.muted) classes << "muted";
        row->setProperty("class", classes);
        row->setMinimumHeight(52);

        // Default değilse satır tıklanabilir → tıklayınca bu cihaz varsayılan
        // olur. Slider / mute butonu kendi event'lerini consume ettiği için
        // sadece "boş" bölgelere tıklama bubble eder.
        if (!dev.isDefault) {
            row->setCursor(Qt::PointingHandCursor);
            const QString deviceName = dev.name;
            row->onClick = [this, deviceName, isSink]() {
                if (isSink) control_->setDefaultSink(deviceName);
                else control_->setDefaultSource(deviceName);
            };
        }

        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(10, 6, 10, 6);
        lay->setSpacing(10);

        auto *muteBtn = new QPushButton(row);
        muteBtn->setFixedSize(30, 30);
        muteBtn->setCursor(Qt::PointingHandCursor);
        muteBtn->setIconSize({18, 18});
        muteBtn->setProperty("class", QStringList{"audioMute"});
        muteBtn->setIcon(muteIconForDevice(dev, isSink));
        muteBtn->setToolTip(dev.muted
            ? QCoreApplication::translate("dock", "Sesi aç")
            : QCoreApplication::translate("dock", "Sesi kapat"));
        lay->addWidget(muteBtn, 0, Qt::AlignVCenter);

        // nameCol genişliği: panel inner ~444 − muteBtn 30 − slider 130
        // − pct 40 − spacing 3*10 − row margin 2*10 = 184. Label'lar bu budget'ı
        // aşan açıklamaları elide eder; tam metin tooltip'te durur.
        const int nameBudget = 184;
        auto *nameCol = new QVBoxLayout;
        nameCol->setContentsMargins(0, 0, 0, 0);
        nameCol->setSpacing(1);
        nameCol->addStretch(1);
        const QString fullName =
            dev.description.isEmpty() ? dev.name : dev.description;
        auto *name = new QLabel(row);
        name->setObjectName("audioName");
        name->setText(name->fontMetrics().elidedText(fullName, Qt::ElideRight,
                                                     nameBudget));
        name->setToolTip(fullName);
        name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        name->setMaximumWidth(nameBudget);
        nameCol->addWidget(name);
        auto *hint = new QLabel(row);
        hint->setObjectName("audioHint");
        if (dev.isDefault) {
            hint->setText(isSink
                ? QCoreApplication::translate("dock", "● Varsayılan çıkış")
                : QCoreApplication::translate("dock", "● Varsayılan giriş"));
        } else {
            hint->setText(isSink
                ? QCoreApplication::translate("dock", "Tıkla → varsayılan çıkış yap")
                : QCoreApplication::translate("dock", "Tıkla → varsayılan giriş yap"));
        }
        hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        hint->setMaximumWidth(nameBudget);
        nameCol->addWidget(hint);
        nameCol->addStretch(1);
        lay->addLayout(nameCol, 1);

        auto *slider = new QSlider(Qt::Horizontal, row);
        slider->setObjectName("quickSlider");
        slider->setRange(0, 100);
        slider->setValue(qMin(100, dev.volume));
        slider->setFixedWidth(130);
        slider->setMinimumHeight(22);
        lay->addWidget(slider, 0, Qt::AlignVCenter);

        auto *pct = new QLabel(QString::number(dev.volume) + "%", row);
        pct->setObjectName("audioPercent");
        pct->setFixedWidth(40);
        pct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lay->addWidget(pct, 0, Qt::AlignVCenter);

        const QString deviceName = dev.name;
        const bool curMuted = dev.muted;
        QObject::connect(slider, &QSlider::valueChanged, this,
                         [this, deviceName, isSink, pct](int v) {
                             pct->setText(QString::number(v) + "%");
                             if (isSink) control_->setSinkVolume(deviceName, v);
                             else control_->setSourceVolume(deviceName, v);
                         });
        QObject::connect(muteBtn, &QPushButton::clicked, this,
                         [this, deviceName, isSink, curMuted]() {
                             if (isSink) control_->setSinkMute(deviceName, !curMuted);
                             else control_->setSourceMute(deviceName, !curMuted);
                         });
        return row;
    }

    QWidget *makeStreamRow(const audio::Stream &stream)
    {
        auto *row = new QWidget;
        QStringList classes{"audioRow"};
        if (stream.muted) classes << "muted";
        row->setProperty("class", classes);
        row->setMinimumHeight(52);

        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(10, 6, 10, 6);
        lay->setSpacing(10);

        auto *muteBtn = new QPushButton(row);
        muteBtn->setFixedSize(30, 30);
        muteBtn->setCursor(Qt::PointingHandCursor);
        muteBtn->setIconSize({22, 22});
        muteBtn->setProperty("class", QStringList{"audioMute"});
        muteBtn->setIcon(iconForStream(stream));
        muteBtn->setToolTip(stream.muted
            ? QCoreApplication::translate("dock", "Sesi aç")
            : QCoreApplication::translate("dock", "Sesi kapat"));
        lay->addWidget(muteBtn, 0, Qt::AlignVCenter);

        const int nameBudget = 184;
        auto *nameCol = new QVBoxLayout;
        nameCol->setContentsMargins(0, 0, 0, 0);
        nameCol->setSpacing(1);
        nameCol->addStretch(1);
        const QString displayName = prettyStreamName(stream);
        auto *name = new QLabel(row);
        name->setObjectName("audioName");
        name->setText(name->fontMetrics().elidedText(displayName, Qt::ElideRight,
                                                     nameBudget));
        name->setToolTip(displayName);
        name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        name->setMaximumWidth(nameBudget);
        nameCol->addWidget(name);
        if (!stream.mediaName.isEmpty()) {
            auto *hint = new QLabel(row);
            hint->setObjectName("audioHint");
            hint->setText(hint->fontMetrics().elidedText(stream.mediaName,
                                                         Qt::ElideRight,
                                                         nameBudget));
            hint->setToolTip(stream.mediaName);
            hint->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            hint->setMaximumWidth(nameBudget);
            nameCol->addWidget(hint);
        }
        nameCol->addStretch(1);
        lay->addLayout(nameCol, 1);

        auto *slider = new QSlider(Qt::Horizontal, row);
        slider->setObjectName("quickSlider");
        slider->setRange(0, 100);
        slider->setValue(qMin(100, stream.volume));
        slider->setFixedWidth(130);
        slider->setMinimumHeight(22);
        lay->addWidget(slider, 0, Qt::AlignVCenter);

        auto *pct = new QLabel(QString::number(stream.volume) + "%", row);
        pct->setObjectName("audioPercent");
        pct->setFixedWidth(40);
        pct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lay->addWidget(pct, 0, Qt::AlignVCenter);

        const int id = stream.id;
        const bool curMuted = stream.muted;
        QObject::connect(slider, &QSlider::valueChanged, this,
                         [this, id, pct](int v) {
                             pct->setText(QString::number(v) + "%");
                             control_->setStreamVolume(id, v);
                         });
        QObject::connect(muteBtn, &QPushButton::clicked, this,
                         [this, id, curMuted]() {
                             control_->setStreamMute(id, !curMuted);
                         });
        return row;
    }

    static QIcon muteIconForDevice(const audio::Device &d, bool isSink)
    {
        if (d.muted) {
            QIcon m = Icons::resolve("audio-volume-muted", QString());
            if (!m.isNull()) return m;
        }
        QStringList hints;
        if (!d.iconName.isEmpty()) hints << d.iconName;
        if (!isSink) {
            hints << "audio-input-microphone" << "microphone-sensitivity-high";
        } else {
            const QString n = d.name.toLower();
            if (n.contains("hdmi")) hints << "video-display";
            if (n.contains("bluez") || n.contains("bluetooth"))
                hints << "audio-headphones";
            if (n.contains("usb")) hints << "audio-headset";
            hints << "audio-card" << "audio-speakers-symbolic" << "audio-volume-high";
        }
        return Icons::resolve(hints, "audio-card");
    }

    // PID'den binary basename'i okur (/proc/PID/exe → basename). Hint
    // listesinde process binary'sini öncelikli olarak vererek Icons::resolve
    // /  DesktopIconResolver::findByTokens'ın .desktop dosyalarıyla eşleşme
    // şansını artırır. pactl bazı sink-input'lar için application.name/binary
    // property'lerini boş bırakır (örn. browser tab'lerinde "audio-src"
    // benzeri generic media.name), o yüzden PID exec path tek güvenilir
    // kaynak olabiliyor.
    static QString binaryFromPid(int pid)
    {
        if (pid <= 0) return {};
        const QString exe = QFileInfo(QStringLiteral("/proc/%1/exe")
                                          .arg(pid)).symLinkTarget();
        if (exe.isEmpty()) return {};
        QString base = QFileInfo(exe).fileName();
        // "/usr/bin/firefox (deleted)" → "firefox"
        if (base.endsWith(QLatin1String(" (deleted)"))) {
            base.chop(10);
        }
        return base;
    }

    static QString commFromPid(int pid)
    {
        if (pid <= 0) return {};
        QFile f(QStringLiteral("/proc/%1/comm").arg(pid));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
        return QString::fromUtf8(f.readAll()).trimmed();
    }

    // Flatpak app_id (örn. "com.spotify.Client") .desktop dosya adıyla birebir
    // eşleştiğinden en güvenilir kimliktir. Sırasıyla: flatpak id → appName →
    // appBinary → /proc/hostPid/exe basename → /proc/hostPid/comm. Bulunan
    // hint'i DesktopIconResolver token-fuzzy match'ine sokup .desktop'taki
    // localized Name= alanını döndürürüz; yoksa hint'i title-case'leyip basarız.
    static QString prettyStreamName(const audio::Stream &s)
    {
        if (!s.appName.isEmpty()) return s.appName;

        QString hint = s.flatpakAppId;
        if (hint.isEmpty()) hint = s.appBinary;
        if (hint.isEmpty()) hint = binaryFromPid(s.hostPid);
        if (hint.isEmpty()) hint = binaryFromPid(s.pid);
        if (hint.isEmpty()) hint = commFromPid(s.hostPid);

        if (!hint.isEmpty()) {
            const QString desktopName =
                DesktopIconResolver::findByTokens(hint).name;
            if (!desktopName.isEmpty()) return desktopName;
            // Title-case fallback. Flatpak id ise son segmenti al ("com.spotify.Client" → "Client" değil "Spotify" istiyoruz, o yüzden .desktop bulamadıysa appBinary > flatpak son segment).
            if (!s.appBinary.isEmpty()) {
                QString t = s.appBinary;
                t[0] = t[0].toUpper();
                return t;
            }
            QString t = hint;
            if (t.contains('.')) t = t.section('.', -1);
            if (!t.isEmpty()) t[0] = t[0].toUpper();
            return t;
        }
        return QCoreApplication::translate("dock", "Uygulama");
    }

    static QIcon iconForStream(const audio::Stream &s)
    {
        QStringList hints;
        // Öncelik: explicit icon_name → flatpak app id → binary → display name.
        if (!s.iconName.isEmpty())     hints << s.iconName;
        if (!s.flatpakAppId.isEmpty()) hints << s.flatpakAppId;
        if (!s.appBinary.isEmpty())    hints << s.appBinary;
        if (!s.appName.isEmpty())      hints << s.appName;
        // PID-derived binary basename (kernel-set sec.pid'yi tercih et, container
        // içi PID flatpak'ta yanlış sonuç verir).
        const QString hostBin = binaryFromPid(s.hostPid);
        if (!hostBin.isEmpty() && !hints.contains(hostBin)) hints << hostBin;
        const QString pidBin = binaryFromPid(s.pid);
        if (!pidBin.isEmpty() && !hints.contains(pidBin)) hints << pidBin;

        QIcon found = Icons::resolve(hints, QString());
        if (!found.isNull()) {
            return s.muted ? Icons::resolve("audio-volume-muted", QString()) : found;
        }
        return Icons::resolve(s.muted ? "audio-volume-muted" : "audio-volume-high");
    }

    AudioControl *control_ = nullptr;
    QPushButton *devicesTab_ = nullptr;
    QPushButton *appsTab_ = nullptr;
    QWidget *devicesPage_ = nullptr;
    QWidget *appsPage_ = nullptr;
    QWidget *devicesList_ = nullptr;
    QWidget *appsList_ = nullptr;
    QVBoxLayout *devicesListLayout_ = nullptr;
    QVBoxLayout *appsListLayout_ = nullptr;
    QLabel *emptyAppsLabel_ = nullptr;
};

TrayButton *makeTrayButton(const QString &iconName, const QString &fallbackText, const QString &tooltip)
{
    auto *button = new TrayButton;
    button->setFixedSize(36, 36);
    button->setProperty("class", "trayButton");
    button->setToolTip(tooltip);
    button->setCursor(Qt::PointingHandCursor);

    const QIcon icon = QIcon::fromTheme(iconName);
    if (!icon.isNull()) {
        button->setIcon(icon);
        button->setIconSize({20, 20});
    } else {
        button->setText(fallbackText);
    }

    return button;
}

// Visual styling lives in style/dock.qss and is reloaded while the app runs.
// Dev workflow için binary'nin yanı (./style/dock.qss) önceliklidir; kurulu
// versiyonlarda <prefix>/share/4ztexDock/style/dock.qss aranır.
QStringList styleSearchDirs()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    return {
        appDir + "/style",                                   // dev: yanı yana
        appDir + "/../share/4ztexDock/style",             // <prefix>/bin → <prefix>/share/...
        QDir::homePath() + "/.local/share/4ztexDock/style",
        "/usr/local/share/4ztexDock/style",
        "/usr/share/4ztexDock/style",
    };
}

QString styleDirPath()
{
    for (const QString &d : styleSearchDirs()) {
        if (QFileInfo::exists(d + "/dock.qss")) return d;
    }
    return styleSearchDirs().first();  // hata yolunda ilk aday gösterilir
}

QString stylePath()
{
    return styleDirPath() + "/dock.qss";
}

void loadStyle(QWidget &bar)
{
    QFile file(stylePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning().noquote() << "Could not load style:" << stylePath();
        bar.setStyleSheet("");
        return;
    }

    bar.setStyleSheet(QString::fromUtf8(file.readAll()));
    qInfo().noquote() << "Reloaded style:" << stylePath();
}

void watchStyle(QFileSystemWatcher &watcher)
{
    const QString dir = styleDirPath();
    const QString file = stylePath();

    if (QDir(dir).exists() && !watcher.directories().contains(dir)) {
        watcher.addPath(dir);
    }

    if (QFile::exists(file) && !watcher.files().contains(file)) {
        watcher.addPath(file);
    }
}

QString kdePrimaryConnectorName()
{
    QFile file(QDir::homePath() + "/.config/kwinoutputconfig.json");
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isArray()) {
        return {};
    }

    QJsonArray outputs;
    QJsonArray setups;
    for (const auto &entry : document.array()) {
        const QJsonObject object = entry.toObject();
        if (object.value("name").toString() == "outputs") {
            outputs = object.value("data").toArray();
        } else if (object.value("name").toString() == "setups") {
            setups = object.value("data").toArray();
        }
    }

    if (outputs.isEmpty() || setups.isEmpty()) {
        return {};
    }

    const QJsonArray setupOutputs = setups.first().toObject().value("outputs").toArray();
    for (const auto &entry : setupOutputs) {
        const QJsonObject output = entry.toObject();
        if (!output.value("enabled").toBool() || output.value("priority").toInt() != 1) {
            continue;
        }

        const int outputIndex = output.value("outputIndex").toInt(-1);
        if (outputIndex >= 0 && outputIndex < outputs.size()) {
            return outputs.at(outputIndex).toObject().value("connectorName").toString();
        }
    }

    return {};
}

bool screenMatches(QScreen *screen, const QString &query)
{
    if (!screen || query.trimmed().isEmpty()) {
        return false;
    }

    const QString needle = query.trimmed();
    return screen->name().compare(needle, Qt::CaseInsensitive) == 0
        || screen->manufacturer().compare(needle, Qt::CaseInsensitive) == 0
        || screen->model().compare(needle, Qt::CaseInsensitive) == 0
        || screen->serialNumber().compare(needle, Qt::CaseInsensitive) == 0;
}

QScreen *findScreen(const QString &query)
{
    for (auto *screen : QGuiApplication::screens()) {
        if (screenMatches(screen, query)) {
            return screen;
        }
    }

    return nullptr;
}

// Hedef ekran seçim önceliği:
//   1. CLI --screen / -s
//   2. ZTEX_TOOLBAR_SCREEN env var
//   3. ~/.config/4ztexDock/config.ini [Display]/screen
//   4. KDE'nin kwinoutputconfig.json primary connector'ü
//   5. Qt primary screen
//   6. screens listesindeki ilk ekran (hiçbir primary yoksa garanti)
QScreen *selectTargetScreen(const QString &requestedScreen,
                             const QString &configScreen)
{
    if (auto *screen = findScreen(requestedScreen)) {
        return screen;
    }

    const QString envScreen = qEnvironmentVariable("ZTEX_TOOLBAR_SCREEN");
    if (auto *screen = findScreen(envScreen)) {
        return screen;
    }

    if (auto *screen = findScreen(configScreen)) {
        return screen;
    }

    const QString kdePrimary = kdePrimaryConnectorName();
    if (auto *screen = findScreen(kdePrimary)) {
        return screen;
    }

    if (auto *primary = QGuiApplication::primaryScreen()) {
        return primary;
    }

    const auto screens = QGuiApplication::screens();
    return screens.isEmpty() ? nullptr : screens.first();
}

void logScreens(QScreen *selectedScreen)
{
    qInfo().noquote() << "Available screens:";
    for (auto *screen : QGuiApplication::screens()) {
        qInfo().noquote()
            << QString("  %1%2 geometry=%3x%4+%5+%6 manufacturer=%7 model=%8 serial=%9")
                   .arg(screen == selectedScreen ? "*" : " ")
                   .arg(screen->name())
                   .arg(screen->geometry().width())
                   .arg(screen->geometry().height())
                   .arg(screen->geometry().x())
                   .arg(screen->geometry().y())
                   .arg(screen->manufacturer())
                   .arg(screen->model())
                   .arg(screen->serialNumber());
    }
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("4ztexDock");
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setApplicationDisplayName("4ztex Dock");
    QApplication::setOrganizationName("4ztex");
    QApplication::setOrganizationDomain("4ztex.com");

    // i18n: source language Türkçe; sistem locale'ine göre çeviri yükle.
    // .qm dosyaları translations.qrc ile binary'ye gömülü.
    // ENV override: LANGUAGE veya LANG → tr_TR olarak başlarsa Türkçe (no-op
    // çünkü source zaten Türkçe), en_* olursa English yükle. Diğer dilleri
    // henüz çevirmedik; fallback Türkçe (kaynak).
    QTranslator *translator = new QTranslator(&app);
    const QString locale = QLocale::system().name();  // e.g. "tr_TR", "en_US"
    QString langCode = locale.section('_', 0, 0);     // "tr" / "en" / ...
    if (langCode != QLatin1String("tr") && langCode != QLatin1String("en")) {
        langCode = QStringLiteral("en");  // bilmediğimiz diller için English
    }
    if (translator->load(QStringLiteral(":/translations/4ztexDock_") + langCode)) {
        app.installTranslator(translator);
    }
    // Tarih/sayı formatları için: source dili kullanan kullanıcı locale'i tut.
    // Türkçeden geliyorsa Türkçe locale, diğerleri için system locale.

    // Icon theme setup — layer-shell Wayland app'lerinde Qt platform-theme
    // integration'ı eksik kalıp tüm fromTheme çağrılarını hicolor'a düşürebiliyor.
    // KDE config'ten gerçek temayı (breeze-dark/breeze) okuyup zorla; fallback
    // path'leri ile /usr/share/pixmaps, ~/.local/share/icons da görünür kalsın.
    Icons::configureGlobalTheme();

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("dock",
        "4ztexDock — KDE Plasma 6 için Wayland layer-shell taskbar/launcher.\n"
        "Bir dock olarak çalışır: pinned launcher'lar, open windows task list,\n"
        "audio/network/notification/launcher popup paneller, system tray, clock.\n"
        "Plasma'nın default panel'ini değiştirmek için tasarlanmıştır."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption screenOption(
        QStringList{"s", "screen"},
        QCoreApplication::translate("dock",
            "Belirli bir ekran üzerinde aç (connector adı/manufacturer/model/serial). "
            "Verilmezse primary screen seçilir."),
        "name");
    parser.addOption(screenOption);
    parser.process(app);

    // ---- Environment compatibility checks ---------------------------------
    // İki desktop session tipi destekleniyor:
    //   - "wayland"  → layer-shell ile zwlr_layer_shell_v1 protokolünü kullan
    //                   (anchor + exclusive zone otomatik)
    //   - "xcb"      → X11 dock window: _NET_WM_WINDOW_TYPE_DOCK + STRUT manuel
    // Her iki yolda da KWin scripting (DBus org.kde.KWin) gerekiyor — task list
    // ve workspace bilgisi oradan. Eksik araçları (pactl/nmcli) log'la ama
    // crash etme; panel'leri o tool yokken sessizce devre dışı bırakırız.
    const QString platform = QGuiApplication::platformName();
    const bool isWayland = (platform == QLatin1String("wayland"));
    const bool isX11 = (platform == QLatin1String("xcb"));
    {
        if (!isWayland && !isX11) {
            qWarning().noquote()
                << QCoreApplication::translate("dock",
                       "4ztexDock yalnızca Wayland veya X11 session altında çalışır "
                       "(şu an: '%1').")
                   .arg(platform);
            return 2;
        }
        // KWin scripting interface'i: bizim KWinBridge bunu kullanır.
        // Yoksa task list / window switcher / shortcut çalışmaz.
        if (!QDBusConnection::sessionBus().interface()
                 ->isServiceRegistered("org.kde.KWin").value()) {
            qWarning().noquote()
                << QCoreApplication::translate("dock",
                       "org.kde.KWin DBus servisi bulunamadı.\n"
                       "4ztexDock KDE Plasma 6'nın KWin compositor'unu gerektirir.");
            return 3;
        }
        // Opsiyonel araçlar — eksikse warn, devam et.
        auto checkTool = [](const QString &exe, const QString &purpose) {
            const QString resolved = QStandardPaths::findExecutable(exe);
            if (resolved.isEmpty()) {
                qCWarning(dockEnv).noquote()
                    << QStringLiteral("'%1' bulunamadı — %2 devre dışı.")
                           .arg(exe, purpose);
            }
        };
        checkTool("pactl",   "audio paneli (PipeWire/PulseAudio)");
        checkTool("nmcli",   "network paneli (NetworkManager)");
        // Plasma 6 = kstart6, Plasma 5 = kstart
        if (kdetools::kstart().isEmpty()) {
            qCWarning(dockEnv).noquote()
                << QStringLiteral("[env] kstart6/kstart bulunamadı — "
                                  "KDE app launcher fallback devre dışı.");
        }
    }

    // Kullanıcı config'ini yükle (~/.config/4ztexDock/config.ini). Dosya
    // yoksa defaults: violet accent, 8 frequent app, auto-screen.
    const dockconfig::Config cfg = dockconfig::load();

    QScreen *targetScreen = selectTargetScreen(parser.value(screenOption),
                                                 cfg.screen);
    if (!targetScreen) {
        qCWarning(dockEnv) << "Hiç ekran tespit edilemedi, çıkılıyor.";
        return 4;
    }

    // Hot-plug: hedef ekran disconnect olursa primary'ye düş + uyar.
    QObject::connect(&app, &QGuiApplication::screenRemoved, &app,
                     [](QScreen *removed) {
        qCInfo(dockEnv) << "[hotplug] screen removed:" << removed->name()
                        << "(toolbar restart'ı önerilir)";
    });
    QObject::connect(&app, &QGuiApplication::screenAdded, &app,
                     [](QScreen *added) {
        qCInfo(dockEnv) << "[hotplug] screen added:" << added->name();
    });
    logScreens(targetScreen);

    auto *kwinBridge = new KWinBridge(&app);
    Q_UNUSED(kwinBridge);

    // Toolbar window basics: title, window flags, transparency, and height.
    DockWindow bar;
    bar.setObjectName("dockWindow");
    bar.setWindowTitle("4ztexDock");
    bar.setWindowFlag(Qt::FramelessWindowHint);
    bar.setWindowFlag(Qt::WindowStaysOnTopHint);
    bar.setAttribute(Qt::WA_TranslucentBackground);
    // Qt'nin bg fill yapmasını engelle — paint event'imiz tüm alanı transparent
    // ile clear ediyor. Aksi halde Wayland surface resize sonrası eski opaque
    // buffer compositor'da kalıyor (siyah alan).
    bar.setAttribute(Qt::WA_NoSystemBackground);
    bar.setFixedHeight(WindowHeight);

    auto *launcherMenu = new LauncherMenu(&bar);
    auto *notificationsPanel = new NotificationsPanel(&bar);

    // Main placement proposal: left region, centered launcher, right region.
    // Width hugs the content so the bar stays responsive to what it actually shows.
    auto *root = new QHBoxLayout(&bar);
    // Side margins match the bar visual's rounded-rect padding so the
    // player on the left and the clock on the right hug the bar edges.
    // No leading/trailing stretches: with the master animation snapping
    // bar.width to sizeHint, stretches would collapse to 0 anyway but
    // each one adds a 28px spacing gap. Drop them so leftPanel sits at
    // exactly margin_left and rightPanel ends at width - margin_right.
    root->setContentsMargins(18, BarTopProtrusion, 18, 0);
    root->setSpacing(10);

    // Music island — hep görünür kalır. MPRIS player aktif değilse içeride
    // NowPlayingWidget gizlenir, yerine SystemStatsWidget (CPU/RAM) gösterilir.
    // Bu sayede toolbar simetrisi medya kaynağı yokken bozulmuyor.
    auto *musicNode = new NodeContainer;
    auto *nowPlaying = new NowPlayingWidget;
    auto *systemStats = new SystemStatsWidget;
    musicNode->contentLayout()->addWidget(nowPlaying);
    musicNode->contentLayout()->addWidget(systemStats);
    nowPlaying->hide();
    systemStats->show();
    nowPlaying->setOnActiveChanged([systemStats, nowPlaying](bool active) {
        if (active) {
            systemStats->hide();
            nowPlaying->show();
        } else {
            nowPlaying->hide();
            systemStats->show();
        }
    });
    root->addWidget(musicNode, 0, Qt::AlignVCenter);

    // OpenWindowsBar'ı önceden tanımlıyoruz ki sonraki popup-açan
    // callback'ler (launcher menu, quick settings, vs.) capture'larında
    // kullanabilsin — hepsi popup'tan önce hidePreview çağırıyor.
    auto *openWindows = new OpenWindowsBar;

    // Custom SVG icons (4ztex logo, network status, bell, overflow chevron)
    // installasyona göre `<prefix>/bin/icons/` veya repo'da `./icons/` altında.
    // Absolute path kur ki systemd user service (cwd=/) altında da bulunabilsin.
    const QString iconsDir = QCoreApplication::applicationDirPath() + "/icons/";

    auto *mainButton = new CircleIconButton(&bar);
    mainButton->setObjectName("mainButton");
    mainButton->setIcon(QIcon(iconsDir + "4ztex-icon.svg"));
    mainButton->setIconSize({44, 44});
    mainButton->setFixedSize(MainButtonSize, MainButtonSize);
    mainButton->setToolTip("Main Button");
    auto openLauncher = [mainButton, launcherMenu, openWindows]() {
        // Preview popup açıksa önce kapat — bar window'u shrink eder ki
        // launcher menü doğru pozisyonda açılsın, preview row menünün
        // üzerine taşmasın.
        openWindows->hidePreview(true);
        if (launcherMenu->isVisible()) {
            launcherMenu->hide();
        } else {
            launcherMenu->showAbove(mainButton);
        }
    };
    QObject::connect(mainButton, &QPushButton::clicked, openLauncher);
    // KWin script'ten Meta+Space tetiklenince aynı handler — toggle davranışı.
    QObject::connect(KWinBridge::instance(),
                     &KWinBridge::launcherShortcutTriggered, &bar, openLauncher);

    root->addWidget(openWindows, 0, Qt::AlignVCenter);

    // Right side: system tray island + clock island, adjacent.
    auto *networkButton = makeTrayButton("network-wireless", "Net",
        QCoreApplication::translate("dock", "Ağ"));
    auto *volumeButton = makeTrayButton("audio-volume-high", "Vol",
        QCoreApplication::translate("dock", "Ses"));

    // Ses paneli — pactl backend + iki sekmeli popup (Cihazlar / Uygulamalar).
    auto *audioControl = new AudioControl(&bar);
    auto *audioPanel = new AudioPanel(audioControl, &bar);
    QObject::connect(volumeButton, &QPushButton::clicked, [volumeButton, audioPanel, openWindows]() {
        openWindows->hidePreview(true);
        // AlignHCenter: tongue volume button merkezini hedefler; panel iki yana
        // simetrik yayılır. AlignRight 480px panel için panel'i çok sola
        // ittiriyor, tongue clamp nedeniyle butonu ıskalıyor.
        audioPanel->showAbove(volumeButton, 2, GlassPopup::AlignHCenter);
    });
    // Default sink seviyesi / mute durumuna göre tray ikonu dinamik:
    // muted / low (1-33) / medium (34-66) / high (67+).
    auto refreshVolumeIcon = [volumeButton, audioControl]() {
        QString iconName;
        QString tip;
        if (audioControl->defaultSinkMuted()) {
            iconName = "audio-volume-muted";
            tip = QCoreApplication::translate("dock", "Ses (kapalı)");
        } else {
            const int v = audioControl->defaultSinkVolume();
            if (v <= 0) iconName = "audio-volume-muted";
            else if (v < 34) iconName = "audio-volume-low";
            else if (v < 67) iconName = "audio-volume-medium";
            else iconName = "audio-volume-high";
            tip = QCoreApplication::translate("dock", "Ses %1%").arg(v);
        }
        const QIcon ico = QIcon::fromTheme(iconName);
        if (!ico.isNull()) volumeButton->setIcon(ico);
        volumeButton->setToolTip(tip);
    };
    // AudioControl zaten kendi `onChanged` callback'iyle paneli rebuild ediyor.
    // İkonu da aynı sinyalle güncellemek için zinciri sarmalıyoruz: paneldeki
    // mevcut callback'i zedelemeden ekleyelim.
    auto panelRebuild = audioControl->onChanged;
    audioControl->onChanged = [panelRebuild, refreshVolumeIcon]() {
        if (panelRebuild) panelRebuild();
        refreshVolumeIcon();
    };

    // NetworkManager-driven icon swap. Active connection type drives the
    // glyph (ethernet vs wifi-strength vs offline) — no more hard-coded
    // Wi-Fi when the user is actually on a cable.
    auto *networkMonitor = new NetworkMonitor(&bar);
    auto *networkPanel = new NetworkPanel(networkMonitor, &bar);
    networkMonitor->onChange = [networkButton, iconsDir](NetworkMonitor::State s) {
        QString icon;
        QString tip;
        switch (s) {
        case NetworkMonitor::State::Ethernet:
            icon = "network-wired.svg";
            tip = QCoreApplication::translate("dock", "Ethernet");
            break;
        case NetworkMonitor::State::WifiStrong:
            icon = "network-wifi-strong.svg";
            tip = QCoreApplication::translate("dock", "Wi-Fi (güçlü)");
            break;
        case NetworkMonitor::State::WifiMedium:
            icon = "network-wifi-medium.svg";
            tip = QCoreApplication::translate("dock", "Wi-Fi (orta)");
            break;
        case NetworkMonitor::State::WifiWeak:
            icon = "network-wifi-weak.svg";
            tip = QCoreApplication::translate("dock", "Wi-Fi (zayıf)");
            break;
        case NetworkMonitor::State::Offline:
            icon = "network-offline.svg";
            tip = QCoreApplication::translate("dock", "Bağlantı yok");
            break;
        case NetworkMonitor::State::Unknown:
            icon = "network-offline.svg";
            tip = QCoreApplication::translate("dock", "Ağ durumu bilinmiyor");
            break;
        }
        networkButton->setIcon(QIcon(iconsDir + icon));
        networkButton->setToolTip(tip);
    };

    auto *trayNode = new NodeContainer;
    // Sistem ikonları ile tray ikonları arasında genel olarak biraz daha
    // sıkı duruş — varsayılan NodeContainer padding 14, spacing 6 idi.
    // Pill içinde aktif ikon yoğunluğu yüksek olduğu için 9/2 daha temiz.
    trayNode->contentLayout()->setContentsMargins(9, 0, 9, 0);
    trayNode->contentLayout()->setSpacing(2);

    // Tray-apps row sits at the LEFT of the system icons. We add an empty
    // QHBoxLayout container here; TrayBridge populates it dynamically as
    // apps register/unregister via the freedesktop StatusNotifierItem
    // protocol (Discord/Steam/Telegram/etc).
    auto *trayAppsBox = new QWidget;
    auto *trayAppsLayout = new QHBoxLayout(trayAppsBox);
    trayAppsLayout->setContentsMargins(0, 0, 0, 0);
    trayAppsLayout->setSpacing(2);
    trayAppsBox->hide();
    trayNode->contentLayout()->addWidget(trayAppsBox, 0, Qt::AlignVCenter);

    QObject::connect(networkButton, &QPushButton::clicked,
                     [networkButton, networkPanel, openWindows]() {
                         openWindows->hidePreview(true);
                         networkPanel->showAbove(networkButton, 2,
                                                  GlassPopup::AlignHCenter);
                     });
    trayNode->contentLayout()->addWidget(networkButton, 0, Qt::AlignVCenter);
    trayNode->contentLayout()->addWidget(volumeButton, 0, Qt::AlignVCenter);

    // Overflow chevron — appears when there are more tray apps than fit
    // inline. Opens a popup listing the spillover.
    auto *trayOverflowPopup = new GlassPopup(QSize(280, 240), &bar);
    auto *trayOverflowList = new QVBoxLayout(trayOverflowPopup);
    trayOverflowList->setContentsMargins(16, 16, 16, 16);
    trayOverflowList->setSpacing(4);

    auto *trayOverflowButton = new HoverPressIconButton;
    trayOverflowButton->setFixedSize(36, 36);
    trayOverflowButton->setIconSize({16, 16});
    trayOverflowButton->setRadius(10);
    trayOverflowButton->setIcon(QIcon(iconsDir + "overflow-up.svg"));
    trayOverflowButton->setToolTip("Daha fazla uygulama");
    trayOverflowButton->hide();
    trayNode->contentLayout()->addWidget(trayOverflowButton, 0, Qt::AlignVCenter);

    QObject::connect(trayOverflowButton, &QPushButton::clicked,
                     [trayOverflowButton, trayOverflowPopup, openWindows]() {
        openWindows->hidePreview(true);
        trayOverflowPopup->showAbove(trayOverflowButton, 2, GlassPopup::AlignRight);
    });

    root->addWidget(trayNode, 0, Qt::AlignVCenter);

    // StatusNotifierItem host. Apps that register a tray icon (Discord, Steam,
    // Telegram, etc.) come through here. We show up to 3 inline; the rest
    // fall into the overflow popup.
    auto *trayBridge = new TrayBridge(&bar);
    auto *trayButtons = new QHash<TrayItem *, HoverPressIconButton *>();
    constexpr int kMaxInlineTray = 3;

    // std::function — auto lambda olsa kendisini capture eden iç lambda'lar
    // "use before deduction" hatası verir; std::function imzası önden bilindiği
    // için recursive capture OK.
    std::function<void()> refreshTrayLayout;
    refreshTrayLayout = [trayBridge, trayButtons,
                                    trayAppsBox, trayOverflowButton,
                                    trayOverflowList, trayOverflowPopup,
                                    &refreshTrayLayout]() {
        const auto items = trayBridge->items();

        // Hide all first.
        for (auto it = trayButtons->constBegin(); it != trayButtons->constEnd(); ++it) {
            it.value()->hide();
        }

        int shown = 0;
        QList<TrayItem *> overflow;
        for (TrayItem *item : items) {
            auto *btn = trayButtons->value(item);
            if (!btn) continue;
            if (shown < kMaxInlineTray) {
                btn->show();
                shown++;
            } else {
                overflow.append(item);
            }
        }

        trayAppsBox->setVisible(shown > 0);
        trayOverflowButton->setVisible(!overflow.isEmpty());

        // Rebuild overflow popup list. Cheap: only fires when items change.
        while (auto *child = trayOverflowList->takeAt(0)) {
            if (auto *w = child->widget()) w->deleteLater();
            delete child;
        }
        for (TrayItem *item : std::as_const(overflow)) {
            auto *row = new QPushButton;
            row->setProperty("class", "overflowItem");
            row->setText("  " + item->title());
            row->setIcon(item->icon());
            row->setIconSize({20, 20});
            row->setFixedHeight(36);
            row->setCursor(Qt::PointingHandCursor);
            row->setContextMenuPolicy(Qt::CustomContextMenu);
            QObject::connect(row, &QPushButton::clicked,
                             [item, trayOverflowPopup]() {
                trayOverflowPopup->hide();
                const QPoint p = QCursor::pos();
                item->activate(p.x(), p.y());
            });
            QObject::connect(row, &QWidget::customContextMenuRequested,
                             [item, trayOverflowPopup](const QPoint &) {
                const QList<TrayMenuEntry> entries = item->fetchMenuEntries();
                const QPoint cursor = QCursor::pos();
                if (entries.isEmpty()) {
                    item->contextMenu(cursor.x(), cursor.y());
                    return;
                }
                // TrayMenuPopup'ı overflow popup parent'lı olarak aç (Wayland
                // xdg-popup nested chain). Pozisyon: mouse cursor'a göre —
                // cursor menünün içinde kalsın ki sağ-tık release "outside"
                // sayılmasın ve menü hemen kapanmasın.
                auto *menu = new TrayMenuPopup(item, entries, trayOverflowPopup);
                menu->setAttribute(Qt::WA_DeleteOnClose);
                menu->show();
                menu->move(cursor);
            });
            trayOverflowList->addWidget(row);
        }
        trayOverflowList->addStretch(1);
    };

    QObject::connect(trayBridge, &TrayBridge::itemAdded,
                     [trayButtons, trayAppsLayout, refreshTrayLayout, openWindows](TrayItem *item) {
        auto *btn = new HoverPressIconButton;
        btn->setFixedSize(36, 36);
        btn->setIconSize({20, 20});
        btn->setRadius(10);
        btn->setContextMenuPolicy(Qt::CustomContextMenu);
        btn->setIcon(item->icon());
        btn->setToolTip(item->title());

        QObject::connect(btn, &QPushButton::clicked, [item, btn]() {
            const QPoint p = btn->mapToGlobal(btn->rect().center());
            item->activate(p.x(), p.y());
        });
        QObject::connect(btn, &QWidget::customContextMenuRequested,
                         [item, btn, openWindows](const QPoint &pos) {
            const QList<TrayMenuEntry> entries = item->fetchMenuEntries();
            if (!entries.isEmpty()) {
                openWindows->hidePreview(true);
                // Custom popup positioned with the same mapToGlobal+move
                // pattern as LauncherMenu / NotificationsPanel — that pair
                // works on Wayland layer-shell while QMenu::popup() does
                // not, so we render the DBusMenu ourselves.
                auto *popup = new TrayMenuPopup(item, entries, btn->window());
                popup->setAttribute(Qt::WA_DeleteOnClose);
                popup->showAbove(btn);
            } else {
                // Older apps that only implement the simple ContextMenu()
                // DBus method — let them show their own menu.
                const QPoint global = btn->mapToGlobal(pos);
                item->contextMenu(global.x(), global.y());
            }
        });

        trayButtons->insert(item, btn);
        trayAppsLayout->addWidget(btn, 0, Qt::AlignVCenter);
        refreshTrayLayout();
    });

    QObject::connect(trayBridge, &TrayBridge::itemRemoved,
                     [trayButtons, refreshTrayLayout](TrayItem *item) {
        auto *btn = trayButtons->take(item);
        if (btn) btn->deleteLater();
        refreshTrayLayout();
    });

    QObject::connect(trayBridge, &TrayBridge::itemChanged,
                     [trayButtons, refreshTrayLayout](TrayItem *item) {
        auto *btn = trayButtons->value(item);
        if (!btn) return;
        btn->setIcon(item->icon());
        btn->setToolTip(item->title());
        refreshTrayLayout();
    });

    auto *clockNode = new NodeContainer;
    clockNode->contentLayout()->addWidget(new ClockBlock, 0, Qt::AlignVCenter);
    root->addWidget(clockNode, 0, Qt::AlignVCenter);

    // Notification pill — separate island to the right of the clock that's
    // only visible when there's something to show. Custom white bell asset
    // (icons/bell.svg) avoids depending on the host icon theme having a
    // clean notification glyph.
    auto *notificationButton = makeTrayButton("preferences-desktop-notification",
                                              "Bell", "Bildirimler");
    notificationButton->setIcon(QIcon(iconsDir + "bell.svg"));
    QObject::connect(notificationButton, &QPushButton::clicked,
                     [notificationButton, notificationsPanel, openWindows]() {
        openWindows->hidePreview(true);
        notificationsPanel->showAbove(notificationButton, 2, GlassPopup::AlignRight);
    });

    auto *notificationNode = new NodeContainer;
    notificationNode->contentLayout()->addWidget(notificationButton, 0, Qt::AlignVCenter);
    // Node sabit genişlikte tutulur — count=0 iken görsel olarak fade out
    // edilir ama Qt layout düzeyinde yer kaplamaya devam eder. Aksi halde
    // hide/show toolbar'ı yatay resize edip launcher anchor pozisyonunu
    // bozuyordu.
    notificationNode->setFixedWidth(notificationNode->sizeHint().width());
    auto *notificationFade = new QGraphicsOpacityEffect(notificationNode);
    notificationFade->setOpacity(0.0);
    notificationNode->setGraphicsEffect(notificationFade);
    notificationNode->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    root->addWidget(notificationNode, 0, Qt::AlignVCenter);

    // Count=0 iken node fade out (görsel invisible) + mouse-transparent.
    // Layout pozisyonu sabit kalır → launcher anchor değişmez. Animasyonlu
    // geçiş için QPropertyAnimation kullanılır.
    auto *notifFadeAnim = new QPropertyAnimation(notificationFade, "opacity",
                                                  notificationNode);
    notifFadeAnim->setDuration(160);
    notifFadeAnim->setEasingCurve(QEasingCurve::OutCubic);

    const auto setNotificationCount =
        [notificationButton, notificationNode, notificationFade,
         notifFadeAnim](int count) {
            notificationButton->setBadge(count);
            const qreal target = count > 0 ? 1.0 : 0.0;
            if (qFuzzyCompare(notificationFade->opacity(), target)) return;
            notifFadeAnim->stop();
            notifFadeAnim->setStartValue(notificationFade->opacity());
            notifFadeAnim->setEndValue(target);
            notifFadeAnim->start();
            notificationNode->setAttribute(Qt::WA_TransparentForMouseEvents,
                                            count == 0);
        };
    setNotificationCount(0);

    // Notification daemon: org.freedesktop.Notifications adını biz alıyoruz
    // (Plasma'nın daemon'u name'i alamadığı durumlarda dahi). Tüm Notify
    // çağrıları doğrudan bizim DBus adapter'ımıza düşer, geçmişe eklenir,
    // panelde gösterilir. Plasma daemon'u name'i zaten tutuyorsa start()
    // false döner ve panel boş kalır (zararsız).
    auto *notificationMgr = new NotificationServer(&bar);
    notificationMgr->start();
    notificationsPanel->setServer(notificationMgr);
    const auto syncNotifications = [notificationMgr, notificationsPanel,
                                     setNotificationCount]() {
        const auto items = notificationMgr->notifications();
        notificationsPanel->showItems(items);
        setNotificationCount(items.size());
    };
    notificationMgr->setOnChanged(syncNotifications);
    QObject::connect(notificationsPanel->clearButton(), &QPushButton::clicked,
                     notificationMgr, [notificationMgr]() {
                         notificationMgr->clear();
                     });

    // Development style watcher. Saving style/dock.qss refreshes styling
    // without restarting the app.
    QFileSystemWatcher styleWatcher;
    QTimer styleReloadTimer;
    styleReloadTimer.setSingleShot(true);
    styleReloadTimer.setInterval(75);

    QObject::connect(&styleReloadTimer, &QTimer::timeout, [&bar, &styleWatcher]() {
        watchStyle(styleWatcher);
        loadStyle(bar);
    });

    const auto scheduleStyleReload = [&styleReloadTimer](const QString &) {
        styleReloadTimer.start();
    };

    QObject::connect(&styleWatcher, &QFileSystemWatcher::fileChanged, scheduleStyleReload);
    QObject::connect(&styleWatcher, &QFileSystemWatcher::directoryChanged, scheduleStyleReload);

    watchStyle(styleWatcher);
    loadStyle(bar);

    // Attach the Qt window to the selected monitor before configuring layer shell.
    bar.winId();
    if (targetScreen && bar.windowHandle()) {
        bar.windowHandle()->setScreen(targetScreen);
    }

    const int screenW = targetScreen ? targetScreen->geometry().width()
                                     : QGuiApplication::primaryScreen()->geometry().width();

    if (isWayland) {
        // Wayland layer-shell setup. Pencereyi bottom toolbar haline getirir +
        // screen space rezerve eder (exclusive zone). Compositor (KWin) geometriyi
        // yönetir, Qt sadece içerik boyutunu bildirir.
        auto *layerWindow = LayerShellQt::Window::get(bar.windowHandle());
        if (layerWindow) {
            layerWindow->setScope("4ztex-toolbar");
            layerWindow->setLayer(LayerShellQt::Window::LayerTop);
            layerWindow->setScreen(targetScreen);
            // Anchor to Left+Right+Bottom but drive the width through dynamic
            // setMargins on the side anchors: equal left/right margins center
            // the bar on the output, and shrinking/growing them animates the
            // surface symmetrically (compositor handles the geometry, Qt
            // resizes the widget accordingly).
            layerWindow->setAnchors(LayerShellQt::Window::Anchors(
                LayerShellQt::Window::AnchorLeft
                | LayerShellQt::Window::AnchorRight
                | LayerShellQt::Window::AnchorBottom));
            layerWindow->setExclusiveEdge(LayerShellQt::Window::AnchorBottom);
            layerWindow->setExclusiveZone(BarHeight);
            const int initOuter = bar.sizeHint().width();
            const int initSide = std::max(0, (screenW - initOuter) / 2);
            layerWindow->setMargins(QMargins(initSide, 0, initSide, 0));
            layerWindow->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
            layerWindow->setDesiredSize(QSize(0, WindowHeight));
            bar.setLayerShell(layerWindow, screenW);
            bar.setFloatingButton(mainButton, openWindows->centerAnchor());
        }
    } else if (isX11) {
        // X11 yolu: layer-shell yok. _NET_WM_WINDOW_TYPE_DOCK + STRUT ile WM'e
        // "ben bir dock'um" diye söyle, manual positioning ile yerleştir.
        bar.setWindowFlag(Qt::FramelessWindowHint, true);
        bar.setWindowFlag(Qt::WindowStaysOnTopHint, true);
        // bar.winId() yukarıda zaten çağrıldı → native handle hazır.
        x11dock::markAsDock(&bar);

        // İlk size hint ile en geçerli genişlik (Qt re-layout sırasında değişebilir)
        const int initOuter = bar.sizeHint().width();
        const QRect screenGeo = targetScreen ? targetScreen->geometry()
                                              : QGuiApplication::primaryScreen()->geometry();
        const int barX = screenGeo.x() + std::max(0, (screenGeo.width() - initOuter) / 2);
        const int barY = screenGeo.y() + screenGeo.height() - WindowHeight;
        bar.setGeometry(barX, barY, initOuter, WindowHeight);
        bar.setFloatingButton(mainButton, openWindows->centerAnchor());

        // STRUT: fullscreen uygulamalar BarHeight kadar yukarıda dursun (üst
        // protrusion görsel detay, screen layout için dock height = BarHeight).
        x11dock::reserveBottomStrut(&bar, BarHeight, barX, barX + initOuter - 1);
    }

    bar.show();
    bar.enableFrameHeartbeat();

    // X11'de show() sonrası bazı WM'ler atom'ları yeniden okur — markAsDock'ı
    // bir kez daha tatbik et garantiye almak için.
    if (isX11) {
        x11dock::markAsDock(&bar);
    }

    return app.exec();
}

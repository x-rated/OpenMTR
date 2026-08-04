// ==========================================================================
//  MainWindow.h — UI declarations for OpenMTR
//
//  Declared bottom-up so every piece is defined before it is used:
//    1. Windows accent-colour helpers
//    2. Reusable painting / style primitives
//    3. The Windows-11-style tooltip
//    4. The custom title bar and its caption buttons
//    5. The results-table building blocks (delegate, header, scrollbar...)
//    6. MainWindow itself
//    7. The modal "Mica" dialog
// ==========================================================================

#pragma once

// ==========================================================================
//  Includes
// ==========================================================================

// Qt — widgets.
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStackedWidget>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QStyledItemDelegate>
#include <QtWidgets/QStyleOptionViewItem>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QFocusFrame>
#include <QtWidgets/QScrollBar>
#include <QtWidgets/QAbstractScrollArea>
#include <QtWidgets/QProxyStyle>
#include <QtWidgets/QStyleOption>
#include <QtWidgets/QDialog>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenu>
#include <QtGui/QCursor>
#include <QtGui/QScreen>
#include <QtGui/QGuiApplication>
#include <QtGui/QStyleHints>
#include <QtGui/QWindow>
#include <QtWidgets/QWidget>

// Qt — GUI.
#include <QtGui/QFontMetrics>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtGui/QPen>
#include <QtGui/QPainterPath>
#include <QtGui/QPainterPathStroker>
#include <QtGui/QRegion>
#include <QtGui/QPaintEvent>
#include <QtGui/QShowEvent>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtGui/QMouseEvent>
#include <QtGui/QWheelEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QFocusEvent>
#include <QtGui/QCloseEvent>
#include <QtGui/QDesktopServices>
#include <QtGui/QContextMenuEvent>

// Qt — core.
#include <QtCore/QHash>
#include <QtCore/QRectF>
#include <QtCore/QMargins>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>
#include <QtCore/QDateTime>
#include <QtCore/QVariantAnimation>
#include <QtCore/QEvent>
#include <QtCore/QCoreApplication>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QPointer>
#include <QtCore/QUrl>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>

// Qt — network (GitHub release version checker).
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkRequest>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkProxy>

#ifdef Q_OS_LINUX
// Qt — DBus (live light/dark and accent-colour watching, plus a one-shot
// synchronous accent-colour read, via org.freedesktop.portal.Settings; see
// MainWindow::onPortalSettingChanged() and linuxSystemAccentColor() below).
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusVariant>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusArgument>

// X11/xcb — lets clicks in the transparent kShadowMargin gutter fall through
// to whatever's behind the window instead of being consumed by it (see
// MainWindow::updateLinuxInputShape()). Only affects X11/XWayland sessions:
// there's no public Qt API for the Wayland equivalent (wl_surface's input
// region), so under native Wayland the gutter stays clickable like today.
#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>
#include <xcb/shape.h>
#endif

#ifdef Q_OS_MAC
// Cocoa runtime — used directly (no .mm/Objective-C++ file in this project)
// to read NSColor's system accent colour, the same way ovSystemAccentShade()
// reads the Windows registry accent palette. See macOsSystemAccentColor()
// below and setMacOsDarkAppearance() in MainWindow.cpp for the established
// pattern.
#include <objc/runtime.h>
#include <objc/message.h>
#endif

#ifdef Q_OS_WIN
// Windows — the two defines must precede <winsock2.h>.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windowsx.h>
#include <ws2tcpip.h>
#include <ws2ipdef.h>
#include <dwmapi.h>
#else
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

// Project — the network engine.
#include "tracer.h"

// C++ standard library.
#include <memory>
#include <vector>
#include <functional>
#include <array>
#include <algorithm>
#include <stop_token>
#include <unordered_map>
#include <unordered_set>
#include <string>

// ==========================================================================
//  Windows accent-colour helpers
// ==========================================================================

#ifdef Q_OS_MAC
// Read NSColor.controlAccentColor (macOS 10.14+) through the Cocoa runtime
// and convert it to a QColor. Every step is nil-checked, so any surprise
// (missing class/selector on some future macOS version) just falls through
// to the caller's fallback instead of crashing.
inline QColor macOsSystemAccentColor(const QColor& fallback)
{
    Class nsColorClass = objc_getClass("NSColor");
    Class nsColorSpaceClass = objc_getClass("NSColorSpace");
    if (!nsColorClass || !nsColorSpaceClass) return fallback;

    id accent = reinterpret_cast<id (*)(Class, SEL)>(objc_msgSend)(
        nsColorClass, sel_registerName("controlAccentColor"));
    if (!accent) return fallback;

    id srgb = reinterpret_cast<id (*)(Class, SEL)>(objc_msgSend)(
        nsColorSpaceClass, sel_registerName("sRGBColorSpace"));
    if (!srgb) return fallback;

    // controlAccentColor isn't guaranteed to already be in an RGB-component
    // colour space (it can be a catalog/pattern colour); converting first
    // is what makes redComponent/greenComponent/blueComponent below valid.
    id converted = reinterpret_cast<id (*)(id, SEL, id)>(objc_msgSend)(
        accent, sel_registerName("colorUsingColorSpace:"), srgb);
    if (!converted) return fallback;

    // NSColor's component getters return CGFloat (double on 64-bit). On
    // x86_64 this would need the dedicated objc_msgSend_fpret entry point
    // instead of the plain one - not needed here since this project only
    // targets Apple Silicon (arm64) on macOS, which doesn't have that ABI
    // quirk.
    using FpRetMsgSend = double (*)(id, SEL);
    const auto send = reinterpret_cast<FpRetMsgSend>(objc_msgSend);
    const double r = send(converted, sel_registerName("redComponent"));
    const double g = send(converted, sel_registerName("greenComponent"));
    const double b = send(converted, sel_registerName("blueComponent"));
    return QColor(qRound(r * 255.0), qRound(g * 255.0), qRound(b * 255.0));
}
#endif

#ifdef Q_OS_LINUX
// Read the desktop's accent colour via the freedesktop desktop-portal
// Settings interface (org.freedesktop.appearance / accent-color) - the same
// portal already used elsewhere in this file for the light/dark setting.
// Per spec the value is a struct of three doubles (r, g, b) in [0, 1]:
// https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Settings.html
//
// Support depends on the portal backend (implemented by GNOME's and KDE's
// backends, not by every WM/DE), so any failure along the way - no portal
// running, unknown key, unexpected reply shape - just falls through to the
// caller's fallback rather than asserting or crashing.
inline QColor linuxSystemAccentColor(const QColor& fallback)
{
    QDBusMessage msg = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.portal.Desktop"),
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Settings"),
        QStringLiteral("Read"));
    msg << QStringLiteral("org.freedesktop.appearance") << QStringLiteral("accent-color");

    // Blocking call with a short timeout: this only ever runs on a theme
    // apply (app start, or a light/dark toggle), never in a hot path, so a
    // brief block is fine - but it must not hang the UI thread if some
    // portal implementation misbehaves.
    const QDBusMessage reply = QDBusConnection::sessionBus().call(msg, QDBus::Block, 200);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
        return fallback;

    // Read()'s "v" out-argument wraps the setting's value in a QDBusVariant,
    // but the value itself is stored server-side as a variant too (that's
    // what ReadAll's a{sv} exposes), so struct-typed settings like this one
    // come back double-wrapped: v(v((ddd))). Unwrap QDBusVariant layers in a
    // loop rather than assuming a fixed depth, since the nesting depth isn't
    // guaranteed to be the same across every portal backend/version.
    QVariant v = reply.arguments().constFirst();
    for (int guard = 0; guard < 4 && v.canConvert<QDBusVariant>(); ++guard)
        v = v.value<QDBusVariant>().variant();
    if (!v.canConvert<QDBusArgument>())
        return fallback;

    double r = 0.0, g = 0.0, b = 0.0;
    // Must be const: QDBusArgument's beginStructure()/operator>>/endStructure
    // each have a const (read) overload and a non-const (write) overload. A
    // non-const local here would silently pick the write overload on an
    // argument that's actually in demarshalling mode, which aborts the
    // process rather than failing gracefully.
    const QDBusArgument arg = v.value<QDBusArgument>();
    arg.beginStructure();
    arg >> r >> g >> b;
    arg.endStructure();

    // Spec: components outside [0, 1] mean "no accent colour set" - keep
    // the built-in default rather than clamping into something arbitrary.
    if (r < 0.0 || r > 1.0 || g < 0.0 || g > 1.0 || b < 0.0 || b > 1.0)
        return fallback;
    return QColor(qRound(r * 255.0), qRound(g * 255.0), qRound(b * 255.0));
}
#endif

// Accent colour for the given theme, read from the Windows registry, on
// macOS from NSColor.controlAccentColor, or on Linux from the freedesktop
// desktop-portal Settings interface; returns a sensible built-in default if
// the platform's accent colour can't be read.
inline QColor ovSystemAccentShade(bool darkMode)
{
    const QColor fallback = darkMode ? QColor(0x4C, 0xC2, 0xFF)
                                     : QColor(0x00, 0x67, 0xC0);
#ifdef Q_OS_WIN
    BYTE  palette[32] = {};
    DWORD size = sizeof(palette);
    const LONG rc = ::RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
        L"AccentPalette", RRF_RT_REG_BINARY, nullptr, palette, &size);
    if (rc != ERROR_SUCCESS || size < sizeof(palette))
        return fallback;
    const int off = darkMode ? 4 : 16;
    return QColor(palette[off], palette[off + 1], palette[off + 2]);
#elif defined(Q_OS_MAC)
    return macOsSystemAccentColor(fallback);
#elif defined(Q_OS_LINUX)
    return linuxSystemAccentColor(fallback);
#else
    return fallback;
#endif
}

// Format a colour as a CSS-style rgba() string (used to build stylesheets).
inline QString ovAccentRgba(const QColor& c, double alpha)
{
    return QString("rgba(%1,%2,%3,%4)")
        .arg(c.red()).arg(c.green()).arg(c.blue())
        .arg(alpha, 0, 'f', 2);
}

// Read one colour from the Windows accent palette at the given byte offset.
inline QColor ovAccentSlot(int byteOffset, const QColor& fallback)
{
#ifdef Q_OS_WIN
    BYTE  palette[32] = {};
    DWORD size = sizeof(palette);
    const LONG rc = ::RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
        L"AccentPalette", RRF_RT_REG_BINARY, nullptr, palette, &size);
    if (rc != ERROR_SUCCESS || size < sizeof(palette) ||
        byteOffset < 0 || byteOffset + 2 >= static_cast<int>(sizeof(palette)))
        return fallback;
    return QColor(palette[byteOffset], palette[byteOffset + 1], palette[byteOffset + 2]);
#else
    Q_UNUSED(byteOffset);
    return fallback;
#endif
}

// Three accent shades (rest / hover / pressed) tuned for link text per theme.
struct OvAccentTextShades { QColor primary, secondary, tertiary; };
inline OvAccentTextShades ovAccentTextShades(bool darkMode)
{
    if (darkMode) {
        const QColor light3 = ovAccentSlot(0, QColor(0x99, 0xEB, 0xFF));
        const QColor light2 = ovAccentSlot(4, QColor(0x4C, 0xC2, 0xFF));
        return { light3, light3, light2 };
    }
    const QColor dark1 = ovAccentSlot(16, QColor(0x00, 0x67, 0xC0));
    const QColor dark2 = ovAccentSlot(20, QColor(0x00, 0x3E, 0x92));
    const QColor dark3 = ovAccentSlot(24, QColor(0x00, 0x1A, 0x68));
    return { dark2, dark3, dark1 };
}

// Flatten a translucent colour onto an opaque base (straight "over" composite)
// and return it as "#rrggbb". Control borders must be opaque: Qt draws a
// border with alpha < 1 through its antialiased rounded-rect path, which
// shifts the button half a pixel down and softens its edges. Compositing the
// intended translucent look onto an opaque colour preserves the appearance
// without it.
inline QString ovAccentBlend(const QColor& fg, const QColor& base, double alpha)
{
    const double a = std::clamp(alpha, 0.0, 1.0);
    const int r = qRound(fg.red()   * a + base.red()   * (1.0 - a));
    const int g = qRound(fg.green() * a + base.green() * (1.0 - a));
    const int b = qRound(fg.blue()  * a + base.blue()  * (1.0 - a));
    return QColor(r, g, b).name();
}

// ==========================================================================
//  Reusable painting & style primitives
// ==========================================================================

// Rounded keyboard-focus ring drawn as a translucent overlay on top of a
// widget (Win11 style). Click-through; ring colour is set per theme.
class FocusRing : public QFocusFrame
{
public:
    static constexpr int kHMargin   = 4;
    static constexpr int kVMargin   = 4;
    static constexpr int kInset     = 1;
    static constexpr int kRingWidth = 2;
    // qreal, not int: this is fed straight to drawRoundedRect() on an
    // antialiased QRectF, so the half pixel is meaningful — as an int it
    // silently truncated to 5 and drew a slightly tighter corner than the
    // rest of the focus ring geometry assumes.
    static constexpr qreal kRadius  = 5.5;

    explicit FocusRing(QWidget* parent = nullptr) : QFocusFrame(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setFocusPolicy(Qt::NoFocus);
        setProperty("ovFocusRing", true);
    }

    void setRingColor(const QColor& c) { if (m_color != c) { m_color = c; update(); } }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(m_color, kRingWidth);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        const qreal off = kInset + kRingWidth / 2.0;
        const QRectF r = QRectF(rect()).adjusted(off, off, -off, -off);
        p.drawRoundedRect(r, kRadius, kRadius);
    }

private:
    QColor m_color = QColor(255, 255, 255);
};

// Win11-style indeterminate progress ring (WinUI 3 ProgressRing). Geometry and
// timing reproduce the official composition animation (AnimatedVisuals/
// ProgressRingIndeterminate in microsoft-ui-xaml): one 2.0 s cycle in which the
// arc grows from 0° to 180° during the first half and shrinks back during the
// second while the whole ring turns 900° per cycle — both eased with
// cubic-bezier(0.167, 0.167, 0.833, 0.833). The trim/rotation constants make
// consecutive cycles join seamlessly (900° ≡ 180° plus the half-circle trim).
// Round stroke caps, stroke = 7.5/80 and radius = 35/80 of the control size,
// no background track — all per the official animated visual. Foreground is
// the system accent (ProgressRing's default foreground brush).
class FluentProgressRing : public QWidget
{
public:
    explicit FluentProgressRing(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(32, 32);                       // WinUI default ring size
        setAttribute(Qt::WA_TranslucentBackground, true);
        m_timer.setInterval(16);                    // ~60 fps while visible
        connect(&m_timer, &QTimer::timeout, this, QOverload<>::of(&QWidget::update));
    }

    void setAccentPtr(const QColor* accent) { m_accent = accent; }

protected:
    // Animate only while the ring is actually on screen.
    void showEvent(QShowEvent* e) override { m_clock.start(); m_timer.start(); QWidget::showEvent(e); }
    void hideEvent(QHideEvent* e) override { m_timer.stop(); QWidget::hideEvent(e); }

    void paintEvent(QPaintEvent*) override
    {
        // y(x) on cubic-bezier(0.167, 0.167, 0.833, 0.833); x(u) is monotonic,
        // so u is recovered by bisection.
        auto bez = [](double x) {
            auto comp = [](double u) {   // control points 0.167 / 0.833
                return 3*u*(1-u)*(1-u)*0.167 + 3*u*u*(1-u)*0.833 + u*u*u;
            };
            double lo = 0.0, hi = 1.0;
            for (int i = 0; i < 24; ++i) { const double mid = (lo + hi) / 2.0; (comp(mid) < x ? lo : hi) = mid; }
            return comp((lo + hi) / 2.0);   // control points are symmetric: y(u) == x(u) form
        };

        const double t   = double(m_clock.isValid() ? m_clock.elapsed() % 2000 : 0) / 2000.0;
        const double rot = 900.0 * bez(t);
        double startFrac, sweepFrac;                // fractions of the circle
        if (t < 0.5) { startFrac = 0.0;                      sweepFrac = 0.5 * bez(t * 2.0); }
        else         { startFrac = 0.5 * bez(t * 2.0 - 1.0); sweepFrac = 0.5 - startFrac;    }

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QColor col = (m_accent && m_accent->isValid())
                         ? *m_accent : QColor(0x00, 0x78, 0xd4);
        const qreal s      = qMin(width(), height());
        const qreal stroke = s * 7.5 / 80.0;
        const qreal radius = s * 35.0 / 80.0;
        p.setPen(QPen(col, stroke, Qt::SolidLine, Qt::RoundCap));
        const QRectF r(width() / 2.0 - radius, height() / 2.0 - radius,
                       radius * 2.0, radius * 2.0);
        // Qt arc angles are 1/16° counter-clockwise from 3 o'clock; the ring
        // starts at 12 o'clock and sweeps clockwise.
        const double a0 = 90.0 - (rot + startFrac * 360.0);
        const double sw = -(sweepFrac * 360.0);
        if (sweepFrac > 0.0005)
            p.drawArc(r, qRound(a0 * 16.0), qRound(sw * 16.0));
    }

private:
    QTimer         m_timer;
    QElapsedTimer  m_clock;
    const QColor*  m_accent = nullptr;
};

// Proxy style that hides Qt's default dotted focus rectangle and feeds our
// FocusRing the correct frame margins.
class NoFocusRectStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;
    void drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                       QPainter* painter, const QWidget* widget = nullptr) const override
    {
        if (element == PE_FrameFocusRect) return;
        QProxyStyle::drawPrimitive(element, option, painter, widget);
    }

    int pixelMetric(PixelMetric metric, const QStyleOption* option = nullptr,
                    const QWidget* widget = nullptr) const override
    {
        if (widget && widget->property("ovFocusRing").toBool()) {
            if (metric == PM_FocusFrameHMargin) return FocusRing::kHMargin;
            if (metric == PM_FocusFrameVMargin) return FocusRing::kVMargin;
        }
        return QProxyStyle::pixelMetric(metric, option, widget);
    }

    int styleHint(StyleHint hint, const QStyleOption* option = nullptr,
                  const QWidget* widget = nullptr,
                  QStyleHintReturn* returnData = nullptr) const override
    {
        if (hint == SH_FocusFrame_Mask && widget && widget->property("ovFocusRing").toBool())
            return 0;
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
};

// The accent-coloured underline shown beneath a focused text input.
// Self-syncing: this bar has no layout of its own, so it installs an event
// filter on its parent input and re-derives its own geometry from
// QEvent::Resize whenever the parent's size changes while the bar is
// visible. That keeps ANY input this is attached to correct automatically -
// whatever resizes the input (a responsive layout, a future feature, manual
// code) and whenever it happens (including while the input is focused and
// the bar already showing) - rather than requiring every call site that can
// resize an input to remember to re-sync the bar by hand.
class InputAccentBar : public QWidget
{
    Q_OBJECT
public:
    explicit InputAccentBar(QWidget* parent = nullptr) : QWidget(parent)
    {
        if (parent) parent->installEventFilter(this);
    }

    // wrapWidth/wrapHeight/thickness describe the input this bar sits under;
    // radius/color are purely cosmetic. Called once, when the bar becomes
    // visible (see MainWindow::updateInputStyle()) - after that,
    // eventFilter() below keeps geometry in sync on its own as the parent
    // resizes, so callers don't need to re-invoke this just to reposition it.
    void setBar(int wrapWidth, int wrapHeight, int thickness, qreal radius, const QColor& color)
    {
        m_thickness = thickness;
        m_radius    = radius;
        m_color     = color;
        resizeToParent(wrapWidth, wrapHeight);
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override
    {
        if (obj == parent() && event->type() == QEvent::Resize && isVisible()) {
            if (auto* p = parentWidget())
                resizeToParent(p->width(), p->height());
        }
        return QWidget::eventFilter(obj, event);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath path;
        path.addRoundedRect(QRectF(0, -(m_wrapHeight - m_thickness), m_wrapWidth, m_wrapHeight),
                             m_radius, m_radius);
        p.fillPath(path, m_color);
    }

private:
    // Applies a new parent size to both the painted shape (wrapWidth/
    // wrapHeight, used by paintEvent()'s rounded rect) and the widget's
    // actual on-screen geometry.
    void resizeToParent(int wrapWidth, int wrapHeight)
    {
        m_wrapWidth  = wrapWidth;
        m_wrapHeight = wrapHeight;
        setGeometry(0, wrapHeight - m_thickness, wrapWidth, m_thickness);
        update();
    }

    int    m_wrapWidth  = 0;
    int    m_wrapHeight = 0;
    int    m_thickness  = 0;
    qreal  m_radius     = 0.0;
    QColor m_color;
};

// One-pixel horizontal separator line painted in a fixed colour.
class SepWidget : public QWidget
{
public:
    explicit SepWidget(const QColor& color, QWidget* parent = nullptr)
        : QWidget(parent), m_color(color)
    {
        setFixedHeight(1);
    }
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(rect(), m_color);
    }
private:
    QColor m_color;
};

// Flat single-colour rectangle, used as the dialog footer background. It
// forms the bottom of MicaDialog's rounded card, so its own bottom corners
// need to be rounded to match #micaCard's top corners (8px, see MicaDialog).
//
// On Windows this doesn't actually matter: DwmSetWindowAttribute's
// DWMWA_WINDOW_CORNER_PREFERENCE (see MicaDialog::applyChrome()) rounds the
// whole native window at the compositor level regardless of what gets
// painted here. Linux has no such call — nothing else rounds the dialog's
// bottom edge — so this widget has to paint its own rounded corners there,
// or the dialog reads as rounded on top and square on the bottom.
class FooterWidget : public QWidget
{
public:
    explicit FooterWidget(const QColor& color, QWidget* parent = nullptr)
        : QWidget(parent), m_color(color) {}
protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
#ifdef Q_OS_LINUX
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(m_color);
        QPainterPath path;
        const qreal radius = 8.0;
        path.moveTo(0, 0);
        path.lineTo(rect().width(), 0);
        path.lineTo(rect().width(), rect().height() - radius);
        path.arcTo(rect().width() - 2 * radius, rect().height() - 2 * radius,
                   2 * radius, 2 * radius, 0.0, -90.0);
        path.lineTo(radius, rect().height());
        path.arcTo(0, rect().height() - 2 * radius, 2 * radius, 2 * radius,
                   -90.0, -90.0);
        path.closeSubpath();
        p.drawPath(path);
#else
        p.fillRect(rect(), m_color);
#endif
    }
private:
    QColor m_color;
};

// ==========================================================================
//  Windows-11-style tooltip
// ==========================================================================

// Small frameless tooltip styled like Windows 11 (rounded, translucent,
// themed). A single shared instance is reused — see sharedCaptionTooltip().
class Win11Tooltip : public QWidget
{
public:
    Win11Tooltip()
        : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(11, 6, 11, 7);
        lay->setSpacing(0);
        m_label = new QLabel(this);
        m_label->setAttribute(Qt::WA_TranslucentBackground);
        lay->addWidget(m_label);
    }

    // Show the tooltip relative to the pointer, per WinUI guidance: centred
    // above the cursor by default, flipped below when it would not fit, and
    // kept inside `bounds` (in global coordinates). When `bounds` is null the
    // available screen is used. Callers pass the app window's global rect so a
    // tooltip for content inside the window never spills outside it. The final
    // rect is always intersected with the screen as a hard limit.
    void popup(const QString& text, const QPoint& cursorPos, bool dark,
               const QRect& bounds = QRect())
    {
        m_dark = dark;
        m_label->setText(text);
        m_label->setStyleSheet(QStringLiteral(
            "QLabel{background:transparent;font-family:'Segoe UI';font-size:12px;color:%1;}")
            .arg(dark ? QStringLiteral("#FFFFFF") : QStringLiteral("#1A1A1A")));
        adjustSize();

        const int gap = 20;
        const QRect scr = QApplication::screenAt(cursorPos)
            ? QApplication::screenAt(cursorPos)->availableGeometry()
            : QGuiApplication::primaryScreen()->availableGeometry();
        // Confine to the window (if given) intersected with the screen.
        const QRect box = bounds.isNull() ? scr : bounds.intersected(scr);

        int x = cursorPos.x() - width() / 2;
        // Above the cursor if it fits within the box, otherwise below it.
        int y = cursorPos.y() - height() - gap;
        if (y < box.top())
            y = cursorPos.y() + gap;

        // Clamp fully inside the box (horizontally and vertically).
        x = std::max(box.left(), std::min(x, box.right() - width()));
        y = std::max(box.top(),  std::min(y, box.bottom() - height()));

        move(x, y);
        show();
        applyChrome();
        update();
    }

    // Refresh the tooltip text in place, keeping its current position — used
    // by live updates (ticking counters) so the tooltip does not jump.
    void updateText(const QString& text)
    {
        m_label->setText(text);
        adjustSize();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        const QColor fill   = m_dark ? QColor(0x2C, 0x2C, 0x2C, 244)
                                     : QColor(0xF9, 0xF9, 0xF9, 244);
        const QColor border = m_dark ? QColor(255, 255, 255, 22)
                                     : QColor(0,   0,   0,   10);
        p.setPen(QPen(border, 1.0));
        p.setBrush(fill);
        p.drawRoundedRect(r, 4.0, 4.0);
    }

private:
    void applyChrome()
    {
#ifdef Q_OS_WIN
        HWND hwnd = reinterpret_cast<HWND>(winId());
        if (!hwnd) return;
        BOOL d = m_dark ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &d, sizeof(d));
        DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUNDSMALL;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        const COLORREF borderColor = m_dark ? 0x003A3A3A
                                            : 0xFFFFFFFE;
        DwmSetWindowAttribute(hwnd, 34 , &borderColor, sizeof(borderColor));

        MARGINS margins = {-1, -1, -1, -1};
        DwmExtendFrameIntoClientArea(hwnd, &margins);
        DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_TRANSIENTWINDOW;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
#endif
        // No Mica-equivalent chrome applied on macOS; the tooltip keeps its
        // default window styling there.
    }

    QLabel* m_label = nullptr;
    bool    m_dark  = false;
};

// The one shared tooltip instance reused by all caption / icon buttons.
inline Win11Tooltip* sharedCaptionTooltip()
{
    static Win11Tooltip* tip = new Win11Tooltip();
    return tip;
}

// Localised caption label (Minimize/Maximize/Close) taken from the window's
// system menu, falling back to the supplied text.
inline QString captionLabelFromSystem(HWND hwnd, UINT sc, const QString& fallback)
{
#ifdef Q_OS_WIN
    if (!hwnd) return fallback;

    HMENU sysmenu = ::GetSystemMenu(hwnd, FALSE);
    if (!sysmenu) return fallback;

    wchar_t buf[64] = {};
    const int len = ::GetMenuStringW(sysmenu, sc, buf, static_cast<int>(std::size(buf)), MF_BYCOMMAND);
    if (len <= 0) return fallback;

    QString label = QString::fromWCharArray(buf, len);
    const int tab = label.indexOf(u'\t');
    if (tab >= 0) label.truncate(tab);
    label.remove(u'&');
    return label.trimmed().isEmpty() ? fallback : label.trimmed();
#else
    // macOS has no system-menu string to localise from; the caller's
    // fallback text is already the right label for its native menu bar.
    Q_UNUSED(hwnd);
    Q_UNUSED(sc);
    return fallback;
#endif
}

// ==========================================================================
//  Custom title bar
// ==========================================================================

#ifdef Q_OS_LINUX
// Frameless-window resize border: how close to the *visible* window edge (in
// device-independent pixels) a press/hover counts as "grab this edge",
// mirroring the invisible resize margin a native Win32/X11 decorated window
// frame normally provides for free. Checked via ovEdgesAt() below in
// MainWindow's qApp-wide event filter, which is the only place this happens.
// On Linux this same value is also what MainWindow::updateLinuxInputShape()
// carves a click-through-proof ring out of the shadow gutter for, via
// ovResizeBandInset() below, so mouse events for edge-dragging actually
// reach the window in the first place - see that function's comment.
static constexpr int kResizeMargin = 6;

// Radius (device-independent pixels) of the frameless window's rounded
// corners when restored (see MainWindow::paintEvent()). CaptionButton uses
// this too, to keep its hover/press fill from squaring off past the curve
// where the close button sits flush in the top-right corner.
static constexpr qreal kWindowCornerRadius = 8.0;

// Whether the window's top-right corner currently has nowhere for a curve to
// sit against: either the window is maximized, or (mirroring
// MainWindow::currentGutter(), which squares off the card's own corners for
// the same reason) a tiling/snap window manager has docked it flush against
// the top and/or right edge of the current screen's usable area without
// ever setting a maximized state for it. CaptionButton uses this to decide
// whether its close-button hover/press fill should round that same corner -
// checking isMaximized() alone left the button's fill rounded even once the
// window silhouette itself had already squared off that corner from a snap.
// kCornerSnapTolerance mirrors currentGutter()'s own tolerance for the same
// off-by-one reason (a WM's tiling math can be a pixel or two off).
static constexpr int kCornerSnapTolerance = 2;

inline bool topRightCornerIsSquare(const QWidget* topLevel)
{
    if (!topLevel) return true;
    if (topLevel->isMaximized()) return true;
    if (const QScreen* scr = topLevel->screen()) {
        const QRect avail = scr->availableGeometry();
        const QRect win   = topLevel->geometry();
        if (qAbs(win.top()   - avail.top())   <= kCornerSnapTolerance) return true;
        if (qAbs(win.right() - avail.right()) <= kCornerSnapTolerance) return true;
    }
    return false;
}

// Width (device-independent pixels) of the transparent gutter MainWindow
// reserves around its own visible "card" so a soft drop shadow has
// somewhere to be painted (see paintCardShadow()/MainWindow::paintEvent()
// in MainWindow.cpp, and the layout margin set up in setupUi()). Windows
// and macOS get a real compositor-drawn shadow for free (DWM's extend-frame
// trick / the native NSWindow shadow); on Linux there's no single mechanism
// every current desktop reliably provides that instead. GNOME's Mutter
// never draws one for an undecorated window at all - the application is
// expected to draw its own, same as GTK's own client-side-decorated windows
// do - and KWin's compositor-side shadow protocol either needs a direct
// KWayland dependency or, on plain X11, only applies itself automatically
// as of Plasma 6.8. Painting it by hand instead is the one approach that
// looks and behaves the same on every current distro/DE/session type with
// no per-desktop detection code at all. Collapses to 0 while maximized,
// same trigger as kWindowCornerRadius, since there's no screen space for a
// shadow there anyway.
static constexpr int kShadowMargin = 24;

// How far in (device-independent pixels) from the window's own physical
// edge the kResizeMargin-wide resize-grab band starts, per side - shared by
// ovEdgesAt() and MainWindow::updateLinuxInputShape() so the pixels that are
// actually clickable (the X Shape input region) and the pixels ovEdgesAt()
// treats as a grab can never drift apart from each other.
//
// The band straddles the *visible* border (the card edge, "gutter" in from
// the window's own outer edge - see MainWindow::currentGutter()), not the
// window's raw physical edge: half of it sits over the card, half out in
// the gutter, so grabbing to resize lines up with where the border is
// actually drawn rather than somewhere out in the middle of an otherwise
// invisible shadow, 18px past anything the eye can see. Where an edge has
// no gutter to speak of (flush against a screen edge, or maximized - gutter
// 0 there), the card edge already coincides with the window's own edge, and
// the band can't extend past that into thin air - it's clamped to 0 and
// just sits fully inside instead, exactly like before any of this
// shadow/gutter machinery existed.
inline QMargins ovResizeBandInset(const QMargins& gutter)
{
    const int half = kResizeMargin / 2;
    return QMargins(qMax(0, gutter.left()   - half), qMax(0, gutter.top()    - half),
                     qMax(0, gutter.right()  - half), qMax(0, gutter.bottom() - half));
}

inline Qt::Edges ovEdgesAt(const QPoint& pos, const QSize& size, const QMargins& gutter)
{
    const QMargins in = ovResizeBandInset(gutter);
    Qt::Edges edges;
    if (pos.x() >= in.left() && pos.x() < in.left() + kResizeMargin)
        edges |= Qt::LeftEdge;
    else if (pos.x() >= size.width() - in.right() - kResizeMargin && pos.x() < size.width() - in.right())
        edges |= Qt::RightEdge;
    if (pos.y() >= in.top() && pos.y() < in.top() + kResizeMargin)
        edges |= Qt::TopEdge;
    else if (pos.y() >= size.height() - in.bottom() - kResizeMargin && pos.y() < size.height() - in.bottom())
        edges |= Qt::BottomEdge;
    return edges;
}

inline Qt::CursorShape ovCursorForEdges(Qt::Edges edges)
{
    const bool l = edges & Qt::LeftEdge, r = edges & Qt::RightEdge;
    const bool t = edges & Qt::TopEdge,  b = edges & Qt::BottomEdge;
    if ((t && l) || (b && r)) return Qt::SizeFDiagCursor;
    if ((t && r) || (b && l)) return Qt::SizeBDiagCursor;
    if (l || r)               return Qt::SizeHorCursor;
    if (t || b)                return Qt::SizeVerCursor;
    return Qt::ArrowCursor;
}
#endif

// Which system caption button a CaptionButton represents.
enum class CaptionButtonKind { Minimize, Maximize, Close };

// One custom-drawn caption button (minimize / maximize / close) with hover
// and pressed states, themed colours and a delayed tooltip.
class CaptionButton : public QWidget
{
    Q_OBJECT
public:
    explicit CaptionButton(CaptionButtonKind kind, QWidget* parent = nullptr)
        : QWidget(parent), m_kind(kind)
    {
        setAttribute(Qt::WA_Hover);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setCursor(Qt::ArrowCursor);
        setFocusPolicy(Qt::NoFocus);

        m_tipTimer.setSingleShot(true);
        m_tipTimer.setInterval(600);
        connect(&m_tipTimer, &QTimer::timeout, this, [this]() { showTip(); });
    }

    void setTip(UINT sc, const QString& fallback) { m_tipSC = sc; m_tipFallback = fallback; }

    void setDark(bool dark) { m_dark = dark; update(); }
    void setMaximized(bool maximized)
    {
        m_maximized = maximized;
        update();
        if (m_kind == CaptionButtonKind::Maximize && m_tipSC != 0) {
            m_tipSC = maximized ? SC_RESTORE : SC_MAXIMIZE;
            m_tipFallback = maximized ? QStringLiteral("Restore") : QStringLiteral("Maximize");
        }
    }

    void setHovered(bool h)
    {
        if (m_hovered == h) return;
        m_hovered = h;
        update();
        if (m_tipSC != 0) {
            if (h) m_tipTimer.start();
            else { m_tipTimer.stop(); hideTip(); }
        }
    }
    void setPressed(bool pr) { if (m_pressed != pr) { m_pressed = pr; update(); } }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QColor bg(0, 0, 0, 0);
        if (m_kind == CaptionButtonKind::Close) {
            if (m_pressed)      bg = QColor(0xC4, 0x2B, 0x1C, 0xE6);
            else if (m_hovered) bg = QColor(0xC4, 0x2B, 0x1C);
        } else {
            if (m_pressed)
                bg = m_dark ? QColor(255, 255, 255, 10) : QColor(0, 0, 0, 6);
            else if (m_hovered)
                bg = m_dark ? QColor(255, 255, 255, 15) : QColor(0, 0, 0, 9);
        }
        if (bg.alpha() > 0) {
#ifdef Q_OS_LINUX
            // The close button sits flush in the window's top-right corner
            // (see TitleBarWidget layout). That corner is rounded (unless
            // maximized — square then, so no rounding needed there either);
            // a plain fillRect would draw past the curve, showing a square
            // tip poking out of the rounded silhouette. Round just that one
            // corner to match; the other three corners of this button are
            // interior to the title bar and stay square.
            const QWidget* topWin = window();
            if (m_kind == CaptionButtonKind::Close && !topRightCornerIsSquare(topWin)) {
                const QRectF r = rect();
                const qreal rad = qMin(kWindowCornerRadius, qMin(r.width(), r.height()) / 2.0);
                QPainterPath path;
                path.moveTo(r.left(), r.top());
                path.lineTo(r.right() - rad, r.top());
                path.arcTo(r.right() - 2 * rad, r.top(), 2 * rad, 2 * rad, 90.0, -90.0);
                path.lineTo(r.right(), r.bottom());
                path.lineTo(r.left(), r.bottom());
                path.closeSubpath();
                p.fillPath(path, bg);
            } else {
                p.fillRect(rect(), bg);
            }
#else
            p.fillRect(rect(), bg);
#endif
        }

        QColor iconColor;
        if (m_kind == CaptionButtonKind::Close && m_hovered) {
            iconColor = QColor(255, 255, 255);
        } else {
            const int a = m_dark ? (m_pressed ? 200 : 255)
                                 : (m_pressed ? 155 : 228);
            iconColor = m_dark ? QColor(255, 255, 255, a) : QColor(0, 0, 0, a);
        }

#ifdef Q_OS_WIN
        QFont f(QStringLiteral("Segoe Fluent Icons"));
        f.setPixelSize(10);
        f.setHintingPreference(QFont::PreferFullHinting);
        p.setFont(f);
        p.setPen(iconColor);
        p.drawText(rect(), Qt::AlignCenter, QString(glyphChar()));
#else
        // "Segoe Fluent Icons" doesn't exist outside Windows, so the glyph
        // codepoints above would fall back to a missing-glyph box (or render
        // blank) on Linux. Draw the three icons as plain vector shapes
        // instead — same visual language, no font dependency.
        p.setPen(QPen(iconColor, 1.0));
        p.setBrush(Qt::NoBrush);
        const QRectF r = rect();
        const qreal cx = r.center().x();
        const qreal cy = r.center().y();
        const qreal s  = 5.0; // half-size of the glyph
        switch (m_kind) {
        case CaptionButtonKind::Minimize:
            p.drawLine(QPointF(cx - s, cy), QPointF(cx + s, cy));
            break;
        case CaptionButtonKind::Maximize:
            if (m_maximized) {
                // Restore glyph: two overlapping square outlines.
                const qreal o = 2.0;
                QRectF back(cx - s + o, cy - s, s * 2 - o, s * 2 - o);
                QRectF front(cx - s, cy - s + o, s * 2 - o, s * 2 - o);
                p.drawRect(back);
                p.fillRect(front, m_dark ? QColor(0x20, 0x20, 0x20)
                                          : QColor(0xf3, 0xf3, 0xf3));
                p.drawRect(front);
            } else {
                p.drawRect(QRectF(cx - s, cy - s, s * 2, s * 2));
            }
            break;
        case CaptionButtonKind::Close:
            p.drawLine(QPointF(cx - s, cy - s), QPointF(cx + s, cy + s));
            p.drawLine(QPointF(cx - s, cy + s), QPointF(cx + s, cy - s));
            break;
        }
#endif
    }

#ifdef Q_OS_WIN
    QChar glyphChar() const
    {
        switch (m_kind) {
        case CaptionButtonKind::Minimize: return QChar(0xE921);
        case CaptionButtonKind::Maximize: return QChar(m_maximized ? 0xE923
                                                                    : 0xE922);
        case CaptionButtonKind::Close:    return QChar(0xE8BB);
        }
        return QChar(0xE8BB);
    }
#endif

    void enterEvent(QEnterEvent*) override
    {
        m_hovered = true;
        update();
        if (m_tipSC != 0) m_tipTimer.start();
    }
    void leaveEvent(QEvent*) override
    {
        m_hovered = false;
        m_pressed = false;
        update();
        m_tipTimer.stop();
        hideTip();
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton) {
            m_pressed = true;
            m_hovered = true;
            update();
        }
        m_tipTimer.stop();
        hideTip();
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (!(e->buttons() & Qt::LeftButton)) return;
        const bool inside = rect().contains(e->pos());
        if (m_pressed != inside || m_hovered != inside) {
            m_pressed = inside;
            m_hovered = inside;
            update();
        }
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton) return;
        const bool inside = rect().contains(e->pos());
        m_pressed = false;
        m_hovered = inside;
        update();
        if (inside) emit clicked();
    }

private:
    void showTip()
    {
        if (m_tipSC == 0 || !m_hovered) return;

        HWND hwnd = nullptr;
#ifdef Q_OS_WIN
        if (QWidget* w = window()) hwnd = reinterpret_cast<HWND>(w->winId());
#endif

        const QString label = captionLabelFromSystem(hwnd, m_tipSC, m_tipFallback);

        const QRect winRect = window() ? window()->frameGeometry() : QRect();
        sharedCaptionTooltip()->popup(label, QCursor::pos(), m_dark, winRect);
    }
    void hideTip() { sharedCaptionTooltip()->hide(); }

    CaptionButtonKind m_kind;
    bool    m_dark        = true;
    bool    m_hovered     = false;
    bool    m_pressed     = false;
    bool    m_maximized   = false;
    UINT    m_tipSC       = 0;
    QString m_tipFallback;
    QTimer  m_tipTimer;
};

// The custom title bar: app icon, title text and the three caption buttons.
// Window dragging / resizing is handled by MainWindow's native hit-testing.
class TitleBarWidget : public QWidget
{
    Q_OBJECT
public:
    explicit TitleBarWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setFocusPolicy(Qt::NoFocus);

        const int h = 48;
        setFixedHeight(h);

        const int btnW = 46;
        const int btnH = 32;

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* left = new QHBoxLayout;
        left->setContentsMargins(20, 0, 0, 0);
        left->setSpacing(0);

        m_iconLabel = new QLabel(this);
        m_iconLabel->setFixedSize(20, 20);
        m_iconLabel->setAttribute(Qt::WA_TranslucentBackground);
#ifdef Q_OS_LINUX
        // On Windows, WM_NCHITTEST classifies this whole area as the caption
        // region regardless of which child widget the click physically lands
        // on. Linux has no such hook — a press has to actually reach this
        // widget's own mousePressEvent()  to start the drag — so labels the
        // user has no reason to interact with just pass clicks straight
        // through to it instead of swallowing them.
        m_iconLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
#endif

        m_titleLabel = new QLabel(this);
        m_titleLabel->setAttribute(Qt::WA_TranslucentBackground);
#ifdef Q_OS_LINUX
        m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
#endif

        // Subtitle carries transient state (e.g. the live elapsed time) beside
        // the app name, in the WinUI title/subtitle style: no separator, just a
        // lighter secondary-coloured run after the title. Its 8px lead-in is a
        // stylesheet padding so the label collapses to zero width when hidden.
        m_subtitleLabel = new QLabel(this);
        m_subtitleLabel->setAttribute(Qt::WA_TranslucentBackground);
        m_subtitleLabel->hide();
#ifdef Q_OS_LINUX
        m_subtitleLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
#endif

        left->addWidget(m_iconLabel, 0, Qt::AlignVCenter);
        left->addSpacing(20);
        left->addWidget(m_titleLabel, 0, Qt::AlignVCenter);
        left->addWidget(m_subtitleLabel, 0, Qt::AlignVCenter);

        layout->addLayout(left);
        layout->addStretch(1);

        m_minBtn   = new CaptionButton(CaptionButtonKind::Minimize, this);
        m_maxBtn   = new CaptionButton(CaptionButtonKind::Maximize, this);
        m_closeBtn = new CaptionButton(CaptionButtonKind::Close,    this);

        m_minBtn->setFixedSize(btnW, btnH);
        m_maxBtn->setFixedSize(btnW, btnH);
        m_closeBtn->setFixedSize(btnW, btnH);

        m_minBtn->setTip(SC_MINIMIZE, QStringLiteral("Minimize"));
        // On Windows, hovering this button pops up the native Snap Layouts
        // flyout, so a tooltip here would be redundant. Linux has no such
        // affordance and this would otherwise be the only caption button
        // without one, so give it a plain Maximize/Restore tip there.
#ifdef Q_OS_LINUX
        m_maxBtn->setTip(SC_MAXIMIZE, QStringLiteral("Maximize"));
#endif
        m_closeBtn->setTip(SC_CLOSE,  QStringLiteral("Close"));

        layout->addWidget(m_minBtn,   0, Qt::AlignTop);
        layout->addWidget(m_maxBtn,   0, Qt::AlignTop);
        layout->addWidget(m_closeBtn, 0, Qt::AlignTop);

        connect(m_minBtn,   &CaptionButton::clicked, this, [this]() {
            if (auto* w = window()) w->showMinimized();
        });
        connect(m_maxBtn,   &CaptionButton::clicked, this, [this]() {
            if (auto* w = window()) {
                if (w->isMaximized()) w->showNormal();
                else                  w->showMaximized();
            }
        });
        connect(m_closeBtn, &CaptionButton::clicked, this, [this]() {
            if (auto* w = window()) w->close();
        });
    }

    void setIcon(const QIcon& icon)
    {
        const qreal dpr = devicePixelRatioF() > 0 ? devicePixelRatioF() : 1.0;
        QPixmap pm = icon.pixmap(QSize(20, 20), dpr);
        pm.setDevicePixelRatio(dpr);
        m_iconLabel->setPixmap(pm);
    }

    void setTitle(const QString& title)
    {
        m_titleLabel->setText(title);
    }

    // An empty subtitle hides the label so it takes no layout width.
    void setSubtitle(const QString& subtitle)
    {
        m_subtitleLabel->setText(subtitle);
        m_subtitleLabel->setVisible(!subtitle.isEmpty());
    }

    void setDark(bool dark)
    {
        m_dark = dark;
        const QString col = dark ? "rgba(255,255,255,0.85)" : "rgba(0,0,0,0.85)";
        m_titleLabel->setStyleSheet(QString(
            "QLabel { color: %1; background: transparent;"
            " font-family: 'Segoe UI'; font-size: 12px; }").arg(col));
        // Secondary run, one step lighter than the title. Tunable design value,
        // not a composited token; the nearest official is TextFillColorSecondary
        // (dark #FFFFFF@0.786 / light #000000@0.606) if exact parity is wanted.
        const QString subCol = dark ? "rgba(255,255,255,0.55)" : "rgba(0,0,0,0.55)";
        m_subtitleLabel->setStyleSheet(QString(
            "QLabel { color: %1; background: transparent; padding-left: 8px;"
            " font-family: 'Segoe UI'; font-size: 12px; }").arg(subCol));
        m_minBtn->setDark(dark);
        m_maxBtn->setDark(dark);
        m_closeBtn->setDark(dark);
    }

    void setMaximized(bool maximized)
    {
        m_maxBtn->setMaximized(maximized);
    }

    QRect captionButtonsRect() const
    {
        return m_minBtn->geometry() | m_maxBtn->geometry() | m_closeBtn->geometry();
    }

    CaptionButton* minBtn()   const { return m_minBtn;   }
    CaptionButton* maxBtn()   const { return m_maxBtn;   }
    CaptionButton* closeBtn() const { return m_closeBtn; }

#ifdef Q_OS_LINUX
protected:
    // Windows gets dragging/snapping/double-click-to-maximize for free from
    // WM_NCHITTEST classifying this area as HTCAPTION. Linux has no such
    // hook — there's no native frame at all once Qt::FramelessWindowHint is
    // set — so a plain click-and-drag here has to explicitly hand off to the
    // compositor via QWindow::startSystemMove(). That handoff is also what
    // makes edge-snapping work: the actual move is driven by the window
    // manager from that point on, the same as if it were dragging any other
    // window's native title bar, so whatever snap-to-edge/tile behaviour the
    // desktop normally offers just applies here too.
    //
    // No resize hit-testing of its own: kShadowMargin (see MainWindow.h)
    // keeps a real gutter between the title bar and the window's physical
    // edges on every side — corners included — so MainWindow's own
    // qApp-wide edge detection (see MainWindow::eventFilter()) already
    // covers the whole perimeter uniformly, leaving nothing for this widget
    // to handle beyond a plain drag.
    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton) {
            if (QWidget* w = window()) {
                if (QWindow* wh = w->windowHandle())
                    wh->startSystemMove();
            }
        }
        QWidget::mousePressEvent(e);
    }

    void mouseDoubleClickEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton) {
            if (QWidget* w = window()) {
                if (w->isMaximized()) w->showNormal();
                else                  w->showMaximized();
            }
        }
        QWidget::mouseDoubleClickEvent(e);
    }

#endif

private:
    QLabel*        m_iconLabel     = nullptr;
    QLabel*        m_titleLabel    = nullptr;
    QLabel*        m_subtitleLabel = nullptr;
    CaptionButton* m_minBtn     = nullptr;
    CaptionButton* m_maxBtn     = nullptr;
    CaptionButton* m_closeBtn   = nullptr;
    bool           m_dark       = true;
};

// ==========================================================================
//  Results-table building blocks
// ==========================================================================

// Logical table columns. The two trailing spacer columns sit at ColCount and
// ColCount + 1 (the first of them is moved to visual position 0 as the left
// edge padding). Must stay in sync with the COLUMNS string list.
enum Column {
    ColHop = 0, ColAsn, ColHostname, ColIp, ColLoss, ColSent, ColRecv,
    ColBest, ColAvrg, ColWrst, ColLast, ColJttr,
    ColCount
};

// Paints the results table: zebra striping, row/cell hover highlight, the
// packet-loss bar in the Loss column, and themed, elided text elsewhere.
class MtrTableItemDelegate : public QStyledItemDelegate
{
public:
    explicit MtrTableItemDelegate(const bool* darkMode, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_darkMode(darkMode) {}

    void setHoveredRow(int row) { m_hoveredRow = row; }
    int  hoveredRow() const { return m_hoveredRow; }

    void setScrollDraggingPtr(const bool* p) { m_scrollDragging = p; }

    void setAccentPtr(const QColor* p) { m_accent = p; }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const bool dark        = m_darkMode && *m_darkMode;
        const bool dragging    = m_scrollDragging && *m_scrollDragging;
        const bool spacer      = index.model() && index.column() >= ColCount;
        const bool cellHovered = !dragging && !spacer && option.state.testFlag(QStyle::State_MouseOver);
        const bool rowHovered  = !dragging && index.row() == m_hoveredRow;

        painter->save();

        if (index.row() & 1) {
            const QColor zebra = dark ? QColor(255, 255, 255, 5) : QColor(0, 0, 0, 5);
            painter->fillRect(option.rect, zebra);
        }
        if (rowHovered) {
            const QColor rowFill = dark ? QColor(255, 255, 255, 15) : QColor(0, 0, 0, 9);
            painter->fillRect(option.rect, rowFill);
        }
        if (cellHovered) {
            const QColor cellFill = dark ? QColor(255, 255, 255, 15) : QColor(0, 0, 0, 9);
            painter->fillRect(option.rect, cellFill);
        }
        painter->restore();

        if (index.column() == ColLoss) {
            bool ok = false;
            const QString s = index.data(Qt::DisplayRole).toString();
            // The initial "-" (no counted probes yet) renders exactly like a
            // zero — empty track plus the dash in the number slot — so the
            // cell doesn't jump when the first real value replaces it.
            const bool dash = (s == QLatin1String("-"));
            const int loss  = dash ? 0 : s.toInt(&ok);
            if (ok || dash) {
                const int    trackWIdeal = 80, barH = 6, gap = 8, inset = 8;
                const QColor track  = dark ? QColor(0xff, 0xff, 0xff, 0x33) : QColor(0x00, 0x00, 0x00, 0x1f);
                const QColor trackHov = dark ? QColor(0xff, 0xff, 0xff, 0x55) : QColor(0x00, 0x00, 0x00, 0x37);
                const QColor trackCol = rowHovered ? trackHov : track;
                QColor fill;
                if (loss > 0)
                    fill = (m_accent && m_accent->isValid())
                         ? *m_accent
                         : (dark ? QColor(0x60, 0xcd, 0xff) : QColor(0x00, 0x78, 0xd4));

                painter->save();
                painter->setRenderHint(QPainter::Antialiasing, true);
                painter->setFont(option.font);

                const QRect r    = option.rect;
                const int numCellW = option.fontMetrics.horizontalAdvance(QStringLiteral("100"));
                const int available = r.width() - 2 * inset;
                const int trackW    = qMax(0, qMin(trackWIdeal, available - gap - numCellW));
                const int groupW    = (trackW > 0 ? trackW + gap : 0) + numCellW;
                const int x         = r.left() + (r.width() - groupW) / 2;
                const qreal cy      = r.center().y() + 0.5;

                if (trackW > 0) {
                    const QRectF trackRect(x, cy - barH / 2.0, trackW, barH);
                    painter->setPen(Qt::NoPen);
                    painter->setBrush(trackCol);
                    painter->drawRoundedRect(trackRect, barH / 2.0, barH / 2.0);
                    if (fill.isValid()) {
                        int fw = qRound(trackW * loss / 100.0);
                        fw = qBound(2, fw, trackW);
                        QPainterPath clip;
                        clip.addRoundedRect(trackRect, barH / 2.0, barH / 2.0);
                        painter->save();
                        painter->setClipPath(clip);
                        painter->fillRect(QRectF(x, cy - barH / 2.0, fw, barH), fill);
                        painter->restore();
                    }
                }

                const QColor numCol = (loss <= 0)
                    ? (dark ? QColor(0x6e, 0x6e, 0x6e) : QColor(0x9e, 0x9e, 0x9e))
                    : (dark ? QColor(0xff, 0xff, 0xff) : QColor(0, 0, 0, 227));
                painter->setPen(numCol);
                painter->drawText(QRect(x + (trackW > 0 ? trackW + gap : 0), r.top(), numCellW, r.height()),
                                  Qt::AlignCenter, s);
                painter->restore();
                return;
            }
        }

        const QString text = index.data(Qt::DisplayRole).toString();
        if (!text.isEmpty()) {
            painter->save();
            painter->setFont(option.font);
            QColor penCol = dark ? QColor(0xff, 0xff, 0xff) : QColor(0, 0, 0, 227);
            // Error rows (hop answered with an ICMP status instead of an
            // address) span Hostname+IP and use the same muted shade as a
            // zero in the Loss column plus the header's 12 px Caption size,
            // so they read as status, not data. (Fluent's type ramp puts
            // Caption one step below Body for exactly this purpose.)
            if (index.column() == ColHostname && index.data(Qt::UserRole).toBool()) {
                penCol = dark ? QColor(0x6e, 0x6e, 0x6e) : QColor(0x9e, 0x9e, 0x9e);
                QFont f = option.font;
                f.setPixelSize(12);
                painter->setFont(f);
            }
            if (index.column() == ColSent || index.column() == ColRecv) {
                const QModelIndex lossIdx = index.model() ? index.model()->index(index.row(), ColLoss) : QModelIndex();
                bool lossOk = false;
                const QString lossText = lossIdx.isValid() ? lossIdx.data(Qt::DisplayRole).toString() : QString();
                const int lossVal = lossText.toInt(&lossOk);
                // The initial "-" state dims like a zero, so the row doesn't
                // flip colour once the first counted values arrive.
                if ((lossOk && lossVal == 0) || lossText == QLatin1String("-"))
                    penCol = dark ? QColor(0x6e, 0x6e, 0x6e) : QColor(0x9e, 0x9e, 0x9e);
            }
            painter->setPen(penCol);
            Qt::Alignment align = static_cast<Qt::Alignment>(index.data(Qt::TextAlignmentRole).toInt());
            if (align == 0) align = Qt::AlignLeft | Qt::AlignVCenter;
            QRect textRect = option.rect.adjusted(8, 4, -8, -4);
            // Multipath marker: a Shuffle glyph at the cell's right edge when
            // replies have also arrived from a different address (route
            // change or per-packet load balancing); details are in the
            // tooltip. Drawn by the delegate, so exports stay clean.
            if ((index.column() == ColHostname || index.column() == ColIp)
                && index.data(Qt::UserRole + 1).toBool()) {
                const QRect iconRect(textRect.right() - 11, textRect.top(),
                                     12, textRect.height());
                const QColor iconColor = dark ? QColor(255, 255, 255, 197)
                                               : QColor(0, 0, 0, 158);
                painter->save();
#ifdef Q_OS_WIN
                QFont iconFont(QStringLiteral("Segoe Fluent Icons"));
                iconFont.setPixelSize(11);
                painter->setFont(iconFont);
                painter->setPen(iconColor);
                painter->drawText(iconRect, Qt::AlignCenter, QString(QChar(0xE8B1)));
#else
                // "Segoe Fluent Icons" doesn't exist outside Windows, and
                // the Unicode shuffle emoji (U+1F500) renders in full
                // color regardless of the U+FE0E text-style variation
                // selector — Qt's text shaping doesn't reliably honor VS15
                // against a color emoji font on Linux. U+292E (Supplemental
                // Arrows-B) is a plain arrow symbol, not an emoji-
                // presentation character and within the BMP (no surrogate
                // pair needed like U+1F500), so it renders through the
                // normal text font — monochrome, no font/variation-
                // selector issues.
                QFont f = option.font;
                f.setPixelSize(11);
                painter->setFont(f);
                painter->setPen(iconColor);
                painter->drawText(iconRect, Qt::AlignCenter, QString(QChar(0x292E)));
#endif
                painter->restore();
                // Symmetric inset (not just off the right edge) so the
                // centred address keeps the same midpoint as every other
                // row — the reserved 18px on the left is unused space, not
                // text, which is what keeps the elision width safely clear
                // of the icon while the visual centre stays put.
                textRect.adjust(18, 0, -18, 0);
            }
            const QString elided = option.fontMetrics.elidedText(text, Qt::ElideRight, textRect.width());
            painter->drawText(textRect, align, elided);
            painter->restore();
        }
    }

private:
    const bool*   m_darkMode;
    const bool*   m_scrollDragging = nullptr;
    const QColor* m_accent = nullptr;
    int m_hoveredRow = -1;
};

// QCheckBox that draws its own Fluent-style check glyph over the indicator.
class PaintedCheckBox : public QCheckBox
{
    Q_OBJECT
public:
    using QCheckBox::QCheckBox;

    void setDark(bool d) { if (m_dark != d) { m_dark = d; update(); } }

protected:
    void paintEvent(QPaintEvent* e) override
    {
        QCheckBox::paintEvent(e);
        if (checkState() != Qt::Checked) return;

        QStyleOptionButton opt;
        initStyleOption(&opt);
        const QRect box = style()->subElementRect(QStyle::SE_CheckBoxIndicator, &opt, this);
        if (box.isEmpty()) return;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        QColor tick;
        if (!isEnabled())
            tick = QColor(255, 255, 255, m_dark ? 135 : 255);
        else
            tick = m_dark ? QColor(0, 0, 0) : QColor(255, 255, 255);
        drawTick(p, box, tick);
    }

private:
#ifdef Q_OS_WIN
    // Windows: use the real system checkmark glyph from Segoe Fluent Icons
    // (same font/codepoint the OS's own checkboxes draw), so it matches
    // native Windows 11 controls pixel-for-pixel instead of an approximation.
    static void drawTick(QPainter& p, const QRect& box, const QColor& color)
    {
        QFont f(QStringLiteral("Segoe Fluent Icons"));
        f.setPixelSize(qRound(box.height() * 0.72));
        f.setHintingPreference(QFont::PreferFullHinting);
        p.setFont(f);
        p.setPen(color);
        p.drawText(box, Qt::AlignCenter, QString(QChar(0xE73E)));
    }
#elif defined(Q_OS_LINUX)
    // Same glyph shape as the shared fallback below, but centering it
    // exposed that the checkmark's own ink isn't symmetric inside the
    // square it's drawn in (it sits left-and-up of that square's centre),
    // so centering the square left the visible tick looking off-centre.
    // Centre on the stroked path's actual bounding box instead, and draw
    // it a touch smaller.
    static void drawTick(QPainter& p, const QRect& box, const QColor& color)
    {
        const qreal side = box.height() * 0.56;
        QRectF r(0, 0, side, side);

        QPainterPath path;
        path.moveTo(r.left(),               r.top() + side * 0.55);
        path.lineTo(r.left() + side * 0.40,  r.top() + side * 0.92);
        path.lineTo(r.right(),              r.top() + side * 0.12);

        const qreal penWidth = qMax(1.6, side * 0.16);
        QPen pen(color, penWidth);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);

        QPainterPathStroker stroker;
        stroker.setWidth(penWidth);
        stroker.setCapStyle(pen.capStyle());
        stroker.setJoinStyle(pen.joinStyle());
        const QRectF inkRect = stroker.createStroke(path).boundingRect();
        // QRect::center() is not the true geometric centre: for a 20-wide
        // integer rect it returns x + 9 (from (left()+right())/2 with
        // right() = left+width-1), not x + 10.0. That's exactly the
        // ~1px diagonal offset that made the tick look off-centre —
        // QRectF(box).center() uses the real edge (x + width) instead.
        path.translate(QRectF(box).center() - inkRect.center());

        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
#else
    // Non-Windows, non-Linux (macOS): "Segoe Fluent Icons" doesn't exist
    // here, so the glyph codepoint above would fall back to a missing-glyph
    // box. Draw the same checkmark shape as a plain vector path instead —
    // identical look, no font dependency.
    static void drawTick(QPainter& p, const QRect& box, const QColor& color)
    {
        const qreal side = box.height() * 0.62;
        QRectF r(0, 0, side, side);
        r.moveCenter(QPointF(box.center()));

        QPainterPath path;
        path.moveTo(r.left(),               r.top() + side * 0.55);
        path.lineTo(r.left() + side * 0.40,  r.top() + side * 0.92);
        path.lineTo(r.right(),              r.top() + side * 0.12);

        QPen pen(color, qMax(1.6, side * 0.16));
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
#endif

    bool m_dark = true;
};

// Table header that draws each column as a name plus an optional little unit
// 'pill' (e.g. ms, %).
class PillHeaderView : public QHeaderView
{
    Q_OBJECT
public:
    explicit PillHeaderView(Qt::Orientation o, QWidget* parent = nullptr)
        : QHeaderView(o, parent)
    {
        setMouseTracking(true);
        setSectionsClickable(false);
    }

    void setColumn(int logicalIndex, const QString& name, const QString& unit)
    {
        m_cols.insert(logicalIndex, Col{ name, unit });
        if (QWidget* vp = viewport()) vp->update();
    }

    void setDark(bool dark)
    {
        if (m_dark == dark) return;
        m_dark = dark;
        if (QWidget* vp = viewport()) vp->update();
    }

protected:
    void paintSection(QPainter* painter, const QRect& rect, int logicalIndex) const override
    {
        if (!rect.isValid()) return;

        const QColor border    = m_dark ? QColor(255, 255, 255, 20)
                                         : QColor(0,   0,   0,   20);
        const QColor txt       = m_dark ? QColor(255, 255, 255, 153)
                                         : QColor(0,   0,   0,   140);
        const QColor pillBg    = m_dark ? QColor(255, 255, 255, 26)
                                         : QColor(0,   0,   0,   18);
        const QColor pillTxt   = m_dark ? QColor(255, 255, 255, 102)
                                         : QColor(0,   0,   0,   115);

        const Col col = m_cols.value(logicalIndex);
        const bool hasContent = !col.name.isEmpty();

        painter->save();

        painter->fillRect(QRect(rect.left(), rect.bottom(), rect.width(), 1), border);

        if (!hasContent) { painter->restore(); return; }

        QFont nameFont = font();
        nameFont.setPixelSize(12);
        nameFont.setWeight(QFont::DemiBold);

        QFont pillFont = nameFont;
        pillFont.setPixelSize(12);

        const QFontMetrics fmName(nameFont);
        const QFontMetrics fmPill(pillFont);

        const bool hasPill   = !col.unit.isEmpty();
        const int  nameW     = fmName.horizontalAdvance(col.name);
        const int  gap       = 4;
        const int  pillPadX  = 4;
        const int  pillTextW = hasPill ? fmPill.horizontalAdvance(col.unit) : 0;
        const int  pillW     = hasPill ? pillTextW + 2 * pillPadX : 0;
        // ascent() + descent() + 4px padding, not the font's full height()
        // (which adds extra line-spacing "leading" unrelated to glyph
        // extent — that's what made the badge look top-heavy for text
        // like "ms" with no tall ascenders). Using ascent()/descent()
        // rather than capHeight() keeps headroom for accented capitals
        // whose diacritic rises above the cap-height line (Š, Ž, Č, Ř) and
        // for ordinary descenders (j, g, p, y).
        const int  pillH     = hasPill ? (fmPill.ascent() + fmPill.descent() + 4) : 0;
        const int  groupW    = hasPill ? nameW + gap + pillW : nameW;

        const int  cx = rect.center().x();
        const int  cy = rect.center().y();
        int        x  = cx - groupW / 2;

        const QColor nameColor = txt;

        painter->setFont(nameFont);
        painter->setPen(nameColor);
        painter->drawText(QRect(x, rect.top(), nameW, rect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, col.name);

        if (hasPill) {
            const qreal px = x + nameW + gap;
            const QRectF pillRect(px, cy - pillH / 2.0 + 2.0, pillW, pillH);

            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(Qt::NoPen);
            painter->setBrush(pillBg);
            // Fluent Shapes spec: shapes under 32px use a 2px corner radius
            // (default is 4px for larger rectangles).
            painter->drawRoundedRect(pillRect, 2.0, 2.0);

            painter->setRenderHint(QPainter::Antialiasing, false);
            painter->setFont(pillFont);
            painter->setPen(pillTxt);
            // Draw into the rounded/int QRect (not the QRectF used for the
            // background) so this goes through the same integer-based
            // centering path as the name text above — mixing int and float
            // rects here caused a 1px vertical mismatch between the two at
            // some scale factors (e.g. 200%).
            painter->drawText(pillRect.toRect().translated(0, -1), Qt::AlignCenter, col.unit);
        }

        painter->restore();
    }

private:
    struct Col { QString name; QString unit; };
    QHash<int, Col> m_cols;
    bool  m_dark  = true;
};

// Custom overlay scrollbar (Win11 style): a thin rail that expands on hover,
// fades out when idle and has a draggable thumb with arrow affordances. It
// floats over the scroll area's viewport rather than taking layout space.
class OverlayScrollBar : public QWidget
{
    Q_OBJECT
public:
    explicit OverlayScrollBar(QAbstractScrollArea* area, QWidget* parent = nullptr)
        : QWidget(parent ? parent : area), m_area(area)
    {
        setObjectName(QStringLiteral("overlayScroll"));
        setStyleSheet(QStringLiteral("background: transparent;"));
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setMouseTracking(true);

        m_area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        if (auto* iv = qobject_cast<QAbstractItemView*>(m_area))
            iv->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

        QScrollBar* sb = m_area->verticalScrollBar();
        connect(sb, &QScrollBar::valueChanged, this, [this]{ reveal(); update(); });
        connect(sb, &QScrollBar::rangeChanged, this, [this]{ reveal(); update(); });

        m_fade = new QVariantAnimation(this);
        m_fade->setDuration(140);
        connect(m_fade, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& v){ m_opacity = v.toReal(); update(); });

        m_expand = new QVariantAnimation(this);
        m_expand->setDuration(120);
        connect(m_expand, &QVariantAnimation::valueChanged, this,
                [this](const QVariant& v){ m_expandFrac = v.toReal(); update(); });

        m_hideTimer.setSingleShot(true);
        m_hideTimer.setInterval(1200);
        connect(&m_hideTimer, &QTimer::timeout, this, [this]{
            if (!m_hovered && !m_dragging) fadeTo(0.0);
        });

        m_area->viewport()->installEventFilter(this);
        relayout();
        raise();
    }

    void setDark(bool dark) { if (m_dark != dark) { m_dark = dark; update(); } }
    bool isDragging() const { return m_dragging; }
    const bool* draggingPtr() const { return &m_dragging; }

protected:
    bool eventFilter(QObject* o, QEvent* e) override
    {
        if (o == m_area->viewport()) {
            if (m_dragging && e->type() == QEvent::MouseMove)
                return true;

            switch (e->type()) {
            case QEvent::Resize:
                relayout();
                break;
            case QEvent::MouseMove: {
                const QPoint p = static_cast<QMouseEvent*>(e)->pos();
                const int widgetLeftInViewport = m_area->viewport()->mapFromParent(
                    QPoint(geometry().left(), 0)).x();
                const bool inHotZone = scrollable() && p.x() >= widgetLeftInViewport;
                reveal();
                if (inHotZone) {
                    const bool justEntered = testAttribute(Qt::WA_TransparentForMouseEvents);
                    setAttribute(Qt::WA_TransparentForMouseEvents, false);
                    if (justEntered) clearTableHover();
                    setHovered(true);
                    return true;
                }
                if (!m_dragging) {
                    setAttribute(Qt::WA_TransparentForMouseEvents, true);
                    setHovered(false);
                }
                break;
            }
            case QEvent::MouseButtonPress: {
                if (!scrollable()) break;
                const QPoint p = static_cast<QMouseEvent*>(e)->pos();
                const int widgetLeftInViewport = m_area->viewport()->mapFromParent(
                    QPoint(geometry().left(), 0)).x();
                if (p.x() < widgetLeftInViewport) break;
                setAttribute(Qt::WA_TransparentForMouseEvents, false);
                setHovered(true);
                clearTableHover();
                const QPoint local = mapFromParent(m_area->viewport()->mapToParent(p));
                QMouseEvent press(QEvent::MouseButtonPress, local,
                                  static_cast<QMouseEvent*>(e)->globalPosition(),
                                  static_cast<QMouseEvent*>(e)->button(),
                                  static_cast<QMouseEvent*>(e)->buttons(),
                                  static_cast<QMouseEvent*>(e)->modifiers());
                QCoreApplication::sendEvent(this, &press);
                return true;
            }
            case QEvent::MouseButtonRelease: {
                if (!m_dragging) break;
                const QPoint p = static_cast<QMouseEvent*>(e)->pos();
                const QPoint local = mapFromParent(m_area->viewport()->mapToParent(p));
                QMouseEvent rel(QEvent::MouseButtonRelease, local,
                                static_cast<QMouseEvent*>(e)->globalPosition(),
                                static_cast<QMouseEvent*>(e)->button(),
                                static_cast<QMouseEvent*>(e)->buttons(),
                                static_cast<QMouseEvent*>(e)->modifiers());
                QCoreApplication::sendEvent(this, &rel);
                return true;
            }
            case QEvent::Leave:
                if (m_clearingHover) break;
                if (!m_dragging) {
                    const QPoint globalPos = QCursor::pos();
                    const bool overUs = geometry().contains(
                        m_area->mapFromGlobal(globalPos));
                    if (!overUs) {
                        setHovered(false);
                        setAttribute(Qt::WA_TransparentForMouseEvents, true);
                    }
                }
                break;
            default: break;
            }
        }
        return QWidget::eventFilter(o, e);
    }

    void enterEvent(QEnterEvent*) override
    {
        if (scrollable()) {
            setAttribute(Qt::WA_TransparentForMouseEvents, false);
            reveal();
            setHovered(true);
            clearTableHover();
        }
    }
    void leaveEvent(QEvent*)      override
    {
        if (m_dragging) return;
        setHovered(false);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

    void wheelEvent(QWheelEvent* e) override
    {
        QScrollBar* sb = m_area->verticalScrollBar();
        sb->setValue(sb->value() - e->angleDelta().y());
        reveal();
        e->accept();
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton || !scrollable()) { e->ignore(); return; }
        const QRect thumb = thumbRect();
        if (thumb.contains(e->pos())) {
            m_dragging = true;
            m_grabDelta = e->pos().y() - thumb.top();
            grabMouse();
            m_area->viewport()->installEventFilter(this);
        } else {
            QScrollBar* sb = m_area->verticalScrollBar();
            const int step = sb->pageStep() ? sb->pageStep() : 40;
            sb->setValue(sb->value() + (e->pos().y() < thumb.top() ? -step : step));
        }
        reveal();
        update();
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (!m_dragging) return;
        const QRect tr = thumbTrackRect();
        const int thumbH = thumbRect().height();
        const int span = tr.height() - thumbH;
        if (span <= 0) return;
        const int top = qBound(tr.top(), e->pos().y() - m_grabDelta, tr.top() + span);
        QScrollBar* sb = m_area->verticalScrollBar();
        const double f = double(top - tr.top()) / double(span);
        sb->setValue(sb->minimum() + qRound(f * (sb->maximum() - sb->minimum())));
        reveal();
    }

    void mouseReleaseEvent(QMouseEvent*) override
    {
        if (!m_dragging) return;
        m_dragging = false;
        releaseMouse();
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        if (!m_hovered) m_hideTimer.start();
        update();
        if (QWidget* vp = m_area->viewport()) vp->update();
    }

    void paintEvent(QPaintEvent*) override
    {
        if (m_opacity <= 0.01 || !scrollable()) return;

        const QRect tr = trackRect();
        const QRect th = thumbRect();
        if (th.height() <= 0) return;

        const qreal curW   = kRailW + (kFullW - kRailW) * m_expandFrac;
        const qreal radius = curW / 2.0;
        const qreal rightX = width() - kRightPad;
        const qreal curTrackW = kRailW + (kTrackW - kRailW) * m_expandFrac;
        const qreal thumbCX   = rightX - curW / 2.0;

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setOpacity(m_opacity);

        if (m_expandFrac > 0.01) {
            QColor track = m_dark ? QColor(255, 255, 255, 18) : QColor(0, 0, 0, 16);
            track.setAlphaF(track.alphaF() * m_expandFrac);
            QRectF trackF(thumbCX - curTrackW / 2.0, tr.top(), curTrackW, tr.height());
            p.setBrush(track);
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(trackF, curTrackW / 2.0, curTrackW / 2.0);
        }

        const int a = m_dark ? 139 : 114;
        const QColor thumb = m_dark ? QColor(255, 255, 255, a) : QColor(0, 0, 0, a);

        QRectF thumbF(rightX - curW, th.top(), curW, th.height());
        p.setBrush(thumb);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(thumbF, radius, radius);

        if (m_expandFrac > 0.01) {
            const QColor arrow = m_dark ? QColor(255, 255, 255, qRound(qreal(a) * m_expandFrac))
                                        : QColor(0,   0,   0,   qRound(qreal(a) * m_expandFrac));
            p.setBrush(arrow);
            p.setPen(Qt::NoPen);

            const qreal aw  = curW + 2.0;
            const qreal ah  = aw * 0.72;
            const qreal ax  = thumbCX;
            const qreal cr  = aw * 0.18;
            const qreal edgePad = 3.0;
            const qreal minSpace = ah + edgePad + 1.0;

            auto drawArrow = [&](qreal tipY, qreal baseY) {
                const qreal L   = ax - aw / 2.0;
                const qreal R   = ax + aw / 2.0;
                const qreal dir = (baseY > tipY) ? 1.0 : -1.0;
                QPainterPath path;
                path.moveTo(ax + cr, tipY + dir * cr);
                path.quadTo(ax, tipY,  ax - cr, tipY + dir * cr);
                path.lineTo(L, baseY - dir * cr);
                path.quadTo(L, baseY,  L + cr, baseY);
                path.lineTo(R - cr, baseY);
                path.quadTo(R, baseY,  R, baseY - dir * cr);
                path.lineTo(ax + cr, tipY + dir * cr);
                path.closeSubpath();
                p.drawPath(path);
            };

            const qreal spaceAbove = qreal(th.top()) - tr.top();
            if (spaceAbove >= minSpace) {
                const qreal tipY  = tr.top() + edgePad;
                const qreal baseY = tipY + ah;
                drawArrow(tipY, baseY);
            }

            const qreal spaceBelow = tr.bottom() - qreal(th.bottom());
            if (spaceBelow >= minSpace) {
                const qreal tipY  = tr.bottom() - edgePad;
                const qreal baseY = tipY - ah;
                drawArrow(tipY, baseY);
            }
        }
    }

private:
    static constexpr int   kWidth    = 16;
    static constexpr int   kHotZone  = 16;
    static constexpr int   kRightPad = 5;
    static constexpr int   kVPad     = 1;
    static constexpr int   kVPadBottom = 3;
    static constexpr int   kArrowRes = 14;
    static constexpr int   kMinThumb = 24;
    static constexpr qreal kRailW    = 2.0;
    static constexpr qreal kFullW    = 6.0;
    static constexpr qreal kTrackW   = kFullW + 6.0;

    bool scrollable() const
    {
        QScrollBar* sb = m_area->verticalScrollBar();
        return sb && sb->maximum() > sb->minimum();
    }

    QRect trackRect() const
    {
        return QRect(0, kVPad, width(), height() - kVPad - kVPadBottom);
    }

    QRect thumbTrackRect() const
    {
        const QRect tr = trackRect();
        return QRect(tr.left(), tr.top() + kArrowRes, tr.width(), tr.height() - 2 * kArrowRes);
    }

    QRect thumbRect() const
    {
        QScrollBar* sb = m_area->verticalScrollBar();
        const int range = sb->maximum() - sb->minimum();
        if (range <= 0) return {};
        const QRect ttr = thumbTrackRect();
        const int page  = sb->pageStep();
        const int total = range + page;
        int thumbH = total > 0 ? qRound(double(ttr.height()) * page / total) : ttr.height();
        thumbH = qBound(kMinThumb, thumbH, ttr.height());
        const int span = ttr.height() - thumbH;
        const int y = ttr.top() + (span > 0 ? qRound(double(span) * (sb->value() - sb->minimum()) / range) : 0);
        return QRect(0, y, width(), thumbH);
    }

    void relayout()
    {
        const QRect g = m_area->viewport()->geometry();
        setGeometry(g.right() - kWidth + 1, g.top(), kWidth, g.height());
        raise();
        update();
    }

    void clearTableHover()
    {
        m_clearingHover = true;
        QEvent leave(QEvent::Leave);
        QCoreApplication::sendEvent(m_area->viewport(), &leave);
        m_clearingHover = false;
    }

    void setHovered(bool h)
    {
        if (m_hovered == h) return;
        m_hovered = h;
        expandTo(h ? 1.0 : 0.0);
        if (h) reveal();
        else if (!m_dragging) m_hideTimer.start();
    }

    void reveal()
    {
        if (!scrollable()) return;
        fadeTo(1.0);
        if (!m_hovered && !m_dragging) m_hideTimer.start();
    }

    void fadeTo(qreal target)
    {
        if (qFuzzyCompare(m_opacity, target) && m_fade->state() != QAbstractAnimation::Running) return;
        m_fade->stop();
        m_fade->setStartValue(m_opacity);
        m_fade->setEndValue(target);
        m_fade->start();
    }

    void expandTo(qreal target)
    {
        m_expand->stop();
        m_expand->setStartValue(m_expandFrac);
        m_expand->setEndValue(target);
        m_expand->start();
    }

    QAbstractScrollArea* m_area = nullptr;
    QVariantAnimation*   m_fade   = nullptr;
    QVariantAnimation*   m_expand = nullptr;
    QTimer  m_hideTimer;
    qreal   m_opacity    = 0.0;
    qreal   m_expandFrac = 0.0;
    bool    m_hovered    = false;
    bool    m_dragging   = false;
    bool    m_clearingHover = false;
    int     m_grabDelta  = 0;
    bool    m_dark       = true;
};

// ==========================================================================
//  MainWindow — the application window
// ==========================================================================

// The application's main window. Owns the toolbar, the results table and the
// whole trace lifecycle, implements the custom Win11 chrome (frameless window,
// native hit-testing, themed title bar) and serves the engine its ping size.
class MainWindow : public QMainWindow, public IOpenMTROptionsProvider
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] unsigned getPingSize() const noexcept override;

private slots:
    // Wired to the buttons, the Enter key and the refresh/elapsed/warm-up timers.
    void onStartStop();
    void onCopy();
    void onExport();
    void onRefreshTimer();
    void onElapsedTimer();
    void onWarmupEnd();
    void onToggleTheme();
#ifdef Q_OS_LINUX
    // Fires on org.freedesktop.portal.Settings' SettingChanged signal for
    // any namespace/key (color-scheme, accent-color, ...) - see
    // the connect() call in the constructor. The parameter names/types must
    // match the DBus signal signature exactly (old-style SLOT() connect).
    void onPortalSettingChanged(const QString& ns, const QString& key, const QDBusVariant& value);
#endif

private:
    // Setup, theming, focus handling, native Win32 chrome, ASN lookup, export.
    void    setupUi();
    void    showEvent(QShowEvent* event) override;
    void    resizeEvent(QResizeEvent* event) override;
    void    changeEvent(QEvent* event) override;
    void    mousePressEvent(QMouseEvent* event) override;
#ifdef Q_OS_LINUX
    void    paintEvent(QPaintEvent* event) override;
    void    moveEvent(QMoveEvent* event) override;
    void    updateTableCorners();
    void    updateLinuxInputShape();
    void    syncLinuxGutter();
    QMargins currentGutter() const;
#endif
    bool    eventFilter(QObject* obj, QEvent* event) override;
    bool    nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    void    showIconTooltip(QPushButton* btn, const QString& text);
    void    hideIconTooltip();
    void    applyDarkTheme();
    void    applyLightTheme();
    void    updateAppIcon();
    void    showAboutDialog();
#ifdef Q_OS_MAC
    void    installMacMenuBar();
#endif
    void    showFocusRing(QWidget* w);
    void    hideFocusRing();
    bool    isFocusRingTarget(QWidget* w) const;
    void    applyWin11Chrome(bool dark);
    void    applyFramelessStyle();
    void    updateInputStyle(QWidget* input, InputAccentBar* accent, bool active, bool keyboardFocused);
    void    updateInputAccentGeometry(QWidget* input, InputAccentBar* accent);
    void    applyInputIdleStyle(QWidget* input);
    void    setTracingInputsEnabled(bool enabled);
    void    updateTable();
    qint64  currentTestDurationMs() const;
    QString buildTextExport() const;
    QString buildJsonExport() const;
    static bool    isWindowsDarkMode();
    static QString lookupASN(const QString& ip, bool ipv6);
    QString        getCachedASN(const QString& ip, bool ipv6) const;
    void           checkForUpdates();
    static bool    isNewerVersion(const QString& latest, const QString& current);
    void           updateToolbarResponsiveLayout();
    bool           alignTargetEditToLossBar();
    void           scheduleButtonAlignment(int attemptsLeft = 30);
    void           scheduleInitialButtonAlignment(int attemptsLeft = 30, int stableCount = 0);

    // Title bar, toolbar inputs, action buttons and icon-tooltip state.
    TitleBarWidget* m_titleBar     = nullptr;
#ifdef Q_OS_LINUX
    // Whether eventFilter()'s window-wide edge detection currently has an
    // application override cursor pushed (see setupUi()'s qApp-wide filter
    // install and eventFilter()'s edge-detection branch).
    bool            m_edgeCursorActive = false;
    // centralWidget()'s own layout, kept around so changeEvent() can collapse
    // its shadow-gutter margins (see kShadowMargin) to 0 while maximized.
    QVBoxLayout*    m_mainLayout = nullptr;
#endif
    // Default/floor width for m_targetEdit - see updateToolbarResponsiveLayout().
    static constexpr int kTargetEditDefaultWidth = 395;
    static constexpr int kTargetEditMinWidth     = 150;
    // The width to grow back to (and shrink from) when there's room - starts
    // at kTargetEditDefaultWidth but is refined by alignTargetEditToLossBar(),
    // which measures the real, font/DPI-corrected width and may run more than
    // once (native styles on some platforms can still be adjusting control
    // metrics - checkbox/spin-box indicators etc. - a tick after the first
    // layout pass); see updateToolbarResponsiveLayout().
    int             m_targetEditIdealWidth = kTargetEditDefaultWidth;
    // First showEvent() only: kept true until scheduleInitialButtonAlignment()
    // has settled and revealed the window - see showEvent().
    bool            m_firstShowPending = true;
    QWidget*        m_toolbar      = nullptr;
    QLineEdit*      m_targetEdit   = nullptr;
    std::function<void()> m_targetClearUpdate;
    QSpinBox*       m_pingSizeBox  = nullptr;
    QList<QWidget*> m_inputs;   // the target/ping-size inputs, for idle-style resets
    PaintedCheckBox* m_ipv6Check   = nullptr;
    QPushButton*    m_startStopBtn = nullptr;
    QPushButton*    m_copyBtn      = nullptr;
    QPushButton*    m_exportBtn    = nullptr;
    QPushButton*    m_themeBtn     = nullptr;
    QPushButton*    m_infoBtn      = nullptr;
    QWidget*        m_updateBadge  = nullptr;
    bool            m_updateAvailable = false;
    QString         m_updateVersion;
    QString         m_updateReleaseUrl;
    QTimer          m_iconTipTimer;
    // Restores the Copy button's label after its "Copied" confirmation.
    QTimer          m_copyFeedbackTimer;
    QPushButton*    m_iconTipPending = nullptr;
    QString         m_iconTipText;

    // Focus ring, page stack and the results table with its helpers.
    FocusRing*      m_focusRing    = nullptr;
    QStackedWidget* m_stack        = nullptr;
    QLabel*         m_loadingLabel = nullptr;
    FluentProgressRing* m_loadingRing = nullptr;
    Win11Tooltip*   m_cellTip = nullptr;
    QPersistentModelIndex m_tipIndex;   // cell whose tooltip is on screen
    QString               m_tipText;    // its last rendered text
    QTimer                m_cellTipTimer;   // 600 ms show delay for cell tooltips
    QModelIndex           m_cellTipHover;   // cell currently hovered (delay target)
    QString cellTooltipText(const QModelIndex& idx) const;
    QTableWidget*   m_table        = nullptr;
    PillHeaderView* m_tableHeader  = nullptr;
    MtrTableItemDelegate* m_tableDelegate = nullptr;
    OverlayScrollBar*     m_tableScroll   = nullptr;

    // Trace engine, timers, live statistics and the current accent colour.
    std::shared_ptr<OpenMTRNetWrapper>   m_net;
    std::stop_source                    m_stopSource;
    bool    m_tracing      = false;
    bool    m_darkMode     = true;
    bool    m_counting     = false;
    QTimer* m_refreshTimer = nullptr;
    QTimer* m_elapsedTimer = nullptr;
    QTimer* m_warmupTimer  = nullptr;
    int     m_warmupGen    = 0;
    // Warm-up stabilisation: the discovered route (hop count plus every hop's
    // address) must hold steady for a short window before the "discovering
    // route" overlay is dismissed, so the table is only shown once discovery
    // has actually settled and rows won't change right after the reveal.
    QByteArray m_warmupFingerprint;
    int        m_warmupStableCount = 0;
    QElapsedTimer m_elapsed;
    // Wall-clock time the counting window began (set alongside m_elapsed's
    // restart in onWarmupEnd) and the test's duration for reports. While a
    // test is actively counting, the live duration is read from m_elapsed;
    // m_testDurationMs freezes that value at Stop, since m_elapsed itself
    // keeps ticking wall-clock time and would otherwise overcount a report
    // built well after the test actually stopped.
    QDateTime m_testStartTime;
    qint64    m_testDurationMs = 0;
    mutable std::unordered_map<std::string, QString> m_asnCache;
    mutable std::unordered_set<std::string>           m_asnPending;
    bool    m_keyboardFocus = false;

    QColor  m_accent { 0x4C, 0xC2, 0xFF };
};

// ==========================================================================
//  Modal "Mica" dialog
// ==========================================================================

#ifdef Q_OS_LINUX
// The dimmed backdrop shown behind MicaDialog on Linux. Paints its own
// antialiased rounded-rect fill rather than relying on QWidget::setMask():
// a mask is a binary, non-antialiased QRegion, so the polygon approximation
// QPainterPath::toFillPolygon() produces for the arc never quite lines up
// with the antialiased corner MainWindow::paintEvent() draws underneath —
// each corner ends up with a slightly different ring of mismatched pixels.
// Painting the fill ourselves with Antialiasing on keeps this widget's edge
// blending consistent with the window's, corner to corner.
class SmokeOverlay : public QWidget
{
public:
    explicit SmokeOverlay(QWidget* host)
        : QWidget(host), m_host(host)
    {
        setObjectName("micaSmoke");
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 77)); // rgba(0,0,0,0.30)
        const qreal radius = (m_host && m_host->isMaximized()) ? 0.0 : kWindowCornerRadius;
        p.drawRoundedRect(rect(), radius, radius);
    }

private:
    QPointer<QWidget> m_host;
};
#endif

// Modal dialog used for the About box and error messages, styled like Windows
// 11: rounded card, optional accent links, themed close button, a dimmed
// backdrop behind it and a keyboard focus ring on the focusable controls.
class MicaDialog : public QDialog
{
public:
    static void show(QWidget* parent, const QString& title,
                     const QString& message, bool darkMode,
                     const QString& linkUrl = QString(),
                     const QString& linkText = QString(),
                     const QString& linkUrl2 = QString(),
                     const QString& linkText2 = QString(),
                     const QString& accentUrl = QString(),
                     const QString& accentText = QString(),
                     const QString& infoBarText = QString())
    {
        QPointer<QWidget> smoke;
        if (parent) {
            QWidget* host = parent->window();
#ifdef Q_OS_LINUX
            smoke = new SmokeOverlay(host);
#else
            smoke = new QWidget(host);
            smoke->setObjectName("micaSmoke");
            smoke->setAttribute(Qt::WA_StyledBackground, true);
            smoke->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            smoke->setStyleSheet("background-color: rgba(0,0,0,0.30);");
#endif
            smoke->raise();
#ifdef Q_OS_LINUX
            // host->rect() is the *whole* window on Linux now, gutter
            // included (see kShadowMargin) - dimming that too would paint
            // straight over MainWindow's own shadow and flatten it under a
            // translucent block. Inset by the same amount MainWindow's own
            // paintEvent() does, so the backdrop covers exactly the visible
            // card and leaves the shadow around it alone.
            const int m = host->isMaximized() ? 0 : kShadowMargin;
            smoke->setGeometry(host->rect().adjusted(m, m, -m, -m));
#else
            smoke->setGeometry(host->rect());
#endif
            smoke->show();
        }

        MicaDialog dlg(parent, title, message, darkMode, linkUrl, linkText, linkUrl2, linkText2, accentUrl, accentText, infoBarText);
        dlg.exec();

        if (smoke) { smoke->hide(); smoke->deleteLater(); }
    }

private:
    explicit MicaDialog(QWidget* parent, const QString& title,
                        const QString& message, bool darkMode,
                        const QString& linkUrl = QString(),
                        const QString& linkText = QString(),
                        const QString& linkUrl2 = QString(),
                        const QString& linkText2 = QString(),
                        const QString& accentUrl = QString(),
                        const QString& accentText = QString(),
                        const QString& infoBarText = QString())
        : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setWindowModality(Qt::ApplicationModal);
        setFixedWidth(400);

        m_card = new QWidget(this);
        m_card->setObjectName("micaCard");

        auto* titleLabel = new QLabel(title, m_card);
        titleLabel->setObjectName("micaTitle");
        titleLabel->setWordWrap(false);

        auto* cardLayout = new QVBoxLayout(m_card);
        cardLayout->setContentsMargins(24, 24, 24, 32);
        cardLayout->setSpacing(12);
        cardLayout->addWidget(titleLabel);

        const int bodyContentW = 400 - 24 - 24;
        const QStringList paras = message.split("\n\n", Qt::SkipEmptyParts);
        for (const QString& para : paras) {
            auto* bodyLabel = new QLabel(para, m_card);
            bodyLabel->setObjectName("micaBody");
            bodyLabel->setWordWrap(true);
            QFont bodyFont("Segoe UI");
            bodyFont.setPixelSize(14);
            bodyLabel->setFont(bodyFont);
            bodyLabel->setFixedHeight(bodyLabel->heightForWidth(bodyContentW));
            bodyLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
            cardLayout->addWidget(bodyLabel);
        }

        if (!linkUrl.isEmpty()) {
            m_linkBtn = new QPushButton(linkText.isEmpty() ? linkUrl : linkText, m_card);
            auto* linkBtn = m_linkBtn;
            linkBtn->setObjectName("micaLink");
            linkBtn->setCursor(Qt::PointingHandCursor);
            linkBtn->setFocusPolicy(Qt::StrongFocus);
            linkBtn->setAutoDefault(false);
            linkBtn->setDefault(false);
            linkBtn->installEventFilter(this);

            const OvAccentTextShades acc = ovAccentTextShades(darkMode);
            const QString rest      = acc.primary.name();
            const QString hover     = acc.secondary.name();
            const QString pressed   = acc.tertiary.name();
            linkBtn->setStyleSheet(QString(
                "QPushButton#micaLink {"
                " background: transparent; color: %1; border: none;"
                " font-family: 'Segoe UI'; font-size: 14px; text-align: left;"
                " padding: 0px; outline: none; }"
                "QPushButton#micaLink:hover   { color: %2; }"
                "QPushButton#micaLink:pressed { color: %3; }")
                .arg(rest, hover, pressed));

            QFont bodyFont("Segoe UI");
            bodyFont.setPixelSize(14);
            QLabel bodyProbe(QStringLiteral("Ag"));
            bodyProbe.setFont(bodyFont);
            bodyProbe.setWordWrap(true);
            const int bodyLineH = bodyProbe.sizeHint().height();

            QFont linkFont = linkBtn->font();
            linkFont.setFamily("Segoe UI");
            linkFont.setPixelSize(14);
            linkBtn->setFont(linkFont);
            linkBtn->setFixedHeight(bodyLineH);
            linkBtn->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);

            const QString url = linkUrl;
            connect(linkBtn, &QPushButton::clicked, this, [url]() {
                QDesktopServices::openUrl(QUrl(url));
            });
            auto* linkRow = new QHBoxLayout();
            linkRow->setContentsMargins(0, 0, 0, 0);
            linkRow->setSpacing(0);
            linkRow->addWidget(linkBtn);

            if (!linkUrl2.isEmpty()) {
                m_linkBtn2 = new QPushButton(linkText2.isEmpty() ? linkUrl2 : linkText2, m_card);
                auto* linkBtn2 = m_linkBtn2;
                linkBtn2->setObjectName("micaLink");
                linkBtn2->setCursor(Qt::PointingHandCursor);
                linkBtn2->setFocusPolicy(Qt::StrongFocus);
                linkBtn2->setAutoDefault(false);
                linkBtn2->setDefault(false);
                linkBtn2->installEventFilter(this);
                linkBtn2->setStyleSheet(linkBtn->styleSheet());
                QFont linkFont2 = linkBtn2->font();
                linkFont2.setFamily("Segoe UI");
                linkFont2.setPixelSize(14);
                linkBtn2->setFont(linkFont2);
                linkBtn2->setFixedHeight(bodyLineH);
                linkBtn2->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
                const QString url2 = linkUrl2;
                connect(linkBtn2, &QPushButton::clicked, this, [url2]() {
                    QDesktopServices::openUrl(QUrl(url2));
                });
                linkRow->addSpacing(16);
                linkRow->addWidget(linkBtn2);
            }

            linkRow->addStretch();
            cardLayout->addLayout(linkRow);
        }

        if (!infoBarText.isEmpty()) {
            // A lightweight stand-in for a WinUI3 InfoBar (Informational
            // severity, IsClosable="false" — no dismiss "×"). Per spec,
            // Informational severity uses a neutral card-like surface
            // rather than an accent tint — only the icon carries colour.
            auto* infoBar = new QWidget(m_card);
            infoBar->setObjectName("micaInfoBar");
            infoBar->setAttribute(Qt::WA_StyledBackground, true);

            const QColor accent = ovSystemAccentShade(darkMode);
            // Colours sampled directly from the WinUI3 Gallery's own
            // InfoBar (not estimated from token names): dark theme reads
            // mainly through a fill that's distinctly *lighter* than the
            // page, with a border barely different from it; light theme is
            // the reverse — the fill is nearly the same as the page, and a
            // visible mid-grey border does the work instead. Solid colours,
            // not alpha — no antialiasing concerns either way.
            const QString fill   = darkMode ? "#272727" : "#F4F4F4";
            const QString border = darkMode ? "#1D1D1D" : "#E5E5E5";
            infoBar->setStyleSheet(QString(
                "QWidget#micaInfoBar { background-color: %1; border: 1px solid %2; border-radius: 6px; }")
                .arg(fill, border));

            auto* infoBarLayout = new QHBoxLayout(infoBar);

            auto* infoText = new QLabel(infoBarText, infoBar);
            infoText->setObjectName("micaInfoBarText");
            infoText->setWordWrap(true);
            QFont infoTextFont(QStringLiteral("Segoe UI"));
            infoTextFont.setPixelSize(14);
            infoText->setFont(infoTextFont);
            infoText->setStyleSheet(darkMode
                ? "color: #ffffff; background: transparent;"
                : "color: rgba(0,0,0,0.89); background: transparent;");

            const int iconSize = 15;
            const int iconFontPx = 9;
            auto* icon = new QLabel(infoBar);
            icon->setObjectName("micaInfoBarIcon");
            icon->setAttribute(Qt::WA_StyledBackground, true);
            icon->setText("i");
            icon->setAlignment(Qt::AlignCenter);
            icon->setFixedSize(iconSize, iconSize);
            QFont iconFont(QStringLiteral("Segoe UI"));
            iconFont.setPixelSize(iconFontPx);
            iconFont.setBold(true);
            icon->setFont(iconFont);
            icon->setStyleSheet(QString(
                "background-color: %1; color: %4; border-radius: %2px;"
                " font-family: 'Segoe UI'; font-size: %3px; font-weight: bold;")
                .arg(accent.name())
                .arg(iconSize / 2)
                .arg(iconFontPx)
                .arg(darkMode ? "#000000" : "#ffffff"));

            const int topOffset = 3;
            auto* iconCol = new QVBoxLayout();
            iconCol->setContentsMargins(2, 0, 2, 0);
            iconCol->setSpacing(0);
            iconCol->addSpacing(topOffset);
            iconCol->addWidget(icon);
            iconCol->addStretch();

            infoBarLayout->setContentsMargins(15, 15, 15, 15);
            infoBarLayout->setSpacing(14);
            infoBarLayout->addLayout(iconCol);
            infoBarLayout->addWidget(infoText, 1);
            cardLayout->addWidget(infoBar);
        }

        m_closeBtn = new QPushButton("Close", this);
        auto* closeBtn = m_closeBtn;
        closeBtn->setObjectName("micaClose");
        closeBtn->setFixedHeight(32);
        closeBtn->setDefault(true);
        closeBtn->setAutoDefault(false);
        closeBtn->setFocusPolicy(Qt::StrongFocus);
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
        installEventFilter(this);
        closeBtn->setStyleSheet(darkMode
            ? "QPushButton { background-color: rgba(255,255,255,0.06); color: #ffffff;"
              " border: 1px solid #3c3c3c; border-top-color: #414141;"
              " border-radius: 4px; padding: 0px 12px; outline: none; }"
              " QPushButton:hover { background-color: rgba(255,255,255,0.08);"
              " border-color: #404040; border-top-color: #454545; }"
              " QPushButton:pressed { background-color: rgba(255,255,255,0.03);"
              " border-color: #343434; color: rgba(255,255,255,0.70); }"
            : "QPushButton { background-color: rgba(255,255,255,0.85); color: rgba(0,0,0,0.89);"
              " border: 1px solid #ececec; border-bottom-color: #d3d3d3;"
              " border-radius: 4px; padding: 0px 12px; outline: none; }"
              " QPushButton:hover { background-color: rgba(243,243,243,0.95);"
              " border-color: #e8e8e8; border-bottom-color: #cecece; }"
              " QPushButton:pressed { background-color: rgba(235,235,235,1.0);"
              " border-color: #dfdfdf; color: rgba(0,0,0,0.60); }");
        QTimer::singleShot(0, this, [closeBtn]() { closeBtn->setFocus(); });

        auto* btnRow = new QHBoxLayout;
        btnRow->setContentsMargins(0, 0, 0, 0);
        btnRow->setSpacing(8);   // same gap as the toolbar's right-side buttons

        if (!accentUrl.isEmpty()) {
            m_accentBtn = new QPushButton(accentText.isEmpty() ? "Download" : accentText, this);
            auto* accentBtn = m_accentBtn;
            accentBtn->setObjectName("micaAccent");
            accentBtn->setFocusPolicy(Qt::StrongFocus);
            accentBtn->setFixedHeight(32);

            // The accent button is the primary action here (Enter triggers
            // it via setDefault), but we deliberately leave real keyboard
            // focus on Close: that way the very first Tab press moves focus
            // onto Download and shows its ring, rather than starting focus
            // on Download and immediately tabbing away from it.
            closeBtn->setDefault(false);
            accentBtn->setDefault(true);
            accentBtn->setAutoDefault(false);

            connect(accentBtn, &QPushButton::clicked, this, [this, accentUrl]() {
                QDesktopServices::openUrl(QUrl(accentUrl));
                accept();
            });

            // Same fill/hover/pressed treatment, and the same opaque
            // border-blend tokens (BRDSIDE/BRDBOT/BRDPRESS), as the
            // toolbar's accent ("start") button.
            const QColor accent = ovSystemAccentShade(darkMode);
            const QString acc90 = ovAccentRgba(accent, 0.90);
            const QString acc80 = ovAccentRgba(accent, 0.80);
            const QString brdSide = ovAccentBlend(QColor(255, 255, 255), accent, 20.0 / 255.0);
            const QString brdBot  = darkMode
                ? ovAccentBlend(QColor(0, 0, 0), accent, 35.0 / 255.0)
                : ovAccentBlend(QColor(0, 0, 0), accent, 102.0 / 255.0);
            const QString brdPress = darkMode
                ? ovAccentBlend(accent, QColor(32, 32, 32), 0.80)
                : ovAccentBlend(accent, QColor(243, 243, 243), 0.80);
            accentBtn->setStyleSheet(darkMode
                ? QString("QPushButton { background-color: %1; color: #000000;"
                  " border: 1px solid %2; border-bottom-color: %3;"
                  " border-radius: 4px; padding: 0px 12px; outline: none; }"
                  " QPushButton:hover { background-color: %4; border-bottom-color: %3; }"
                  " QPushButton:pressed { background-color: %5; color: rgba(0,0,0,0.50); border-color: %6; }")
                    .arg(accent.name(), brdSide, brdBot, acc90, acc80, brdPress)
                : QString("QPushButton { background-color: %1; color: #ffffff;"
                  " border: 1px solid %2; border-bottom-color: %3;"
                  " border-radius: 4px; padding: 0px 12px; outline: none; }"
                  " QPushButton:hover { background-color: %4; border-bottom-color: %3; }"
                  " QPushButton:pressed { background-color: %5; color: rgba(255,255,255,0.70); border-color: %6; }")
                    .arg(accent.name(), brdSide, brdBot, acc90, acc80, brdPress));

            // Two equal-width buttons spanning the full footer, primary
            // (Download) on the left, secondary (Close) on the right.
            closeBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            accentBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            btnRow->addWidget(accentBtn, 1);
            btnRow->addWidget(closeBtn, 1);
        } else {
            closeBtn->setFixedWidth(82);
            btnRow->addStretch();
            btnRow->addWidget(closeBtn);
        }

        // Tab order should follow the actual visual layout, top to bottom
        // and left to right: link(s) first, then the footer buttons in the
        // order they appear on screen (accent/primary before Close).
        QWidget* prevTab = nullptr;
        auto chainTab = [&prevTab](QWidget* w) {
            if (!w) return;
            if (prevTab) QWidget::setTabOrder(prevTab, w);
            prevTab = w;
        };
        chainTab(m_linkBtn);
        chainTab(m_linkBtn2);
        chainTab(m_accentBtn);
        chainTab(closeBtn);

        auto* sep = new SepWidget(
            darkMode ? QColor(255, 255, 255, 20) : QColor(255, 255, 255, 175),
            this);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(m_card);
        layout->addWidget(sep);

        auto* footerWidget = new FooterWidget(
            darkMode ? QColor(0x14, 0x14, 0x14) : QColor(0xF3, 0xF3, 0xF3),
            this);
        footerWidget->setObjectName("micaFooter");
        auto* footerWrap = new QVBoxLayout(footerWidget);
        footerWrap->setContentsMargins(24, 24, 24, 24);
        footerWrap->addLayout(btnRow);
        footerWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        layout->addWidget(footerWidget);

        if (darkMode) {
            m_card->setStyleSheet(R"(
                #micaCard {
                    background-color: #202020;
                    border-top-left-radius: 8px; border-top-right-radius: 8px; border-bottom-left-radius: 0px; border-bottom-right-radius: 0px;
                }
                #micaTitle { color: #ffffff; font-family: "Segoe UI"; font-size: 20px; font-weight: 600; background: transparent; }
                #micaBody  { color: rgba(255,255,255,0.78); font-family: "Segoe UI"; font-size: 14px; background: transparent; }
            )");
            setStyleSheet(QString(
                "MicaDialog { background-color: #202020; border: 1px solid %1; border-radius: 8px; }"
                "QPushButton#micaClose { font-family: \"Segoe UI\"; font-size: 14px; outline: none; }")
                .arg(ovAccentBlend(QColor(0, 0, 0), QColor(0x20, 0x20, 0x20), 25.0 / 255.0)));
        } else {
            m_card->setStyleSheet(R"(
                #micaCard {
                    background-color: #FFFFFF;
                    border-top-left-radius: 8px; border-top-right-radius: 8px; border-bottom-left-radius: 0px; border-bottom-right-radius: 0px;
                }
                #micaTitle { color: #1a1a1a; font-family: "Segoe UI"; font-size: 20px; font-weight: 600; background: transparent; }
                #micaBody  { color: rgba(0,0,0,0.78); font-family: "Segoe UI"; font-size: 14px; background: transparent; }
            )");
            setStyleSheet(QString(
                "MicaDialog { background-color: #FFFFFF; border: 1px solid %1; border-radius: 8px; }"
                "QPushButton#micaClose { font-family: \"Segoe UI\"; font-size: 14px; outline: none; }")
                .arg(ovAccentBlend(QColor(0, 0, 0), QColor(255, 255, 255), 15.0 / 255.0)));
        }

        m_focusRing = new FocusRing(this);
        m_focusRing->setRingColor(darkMode ? QColor(255, 255, 255) : QColor(0, 0, 0, 230));
        m_focusRing->hide();

        QTimer::singleShot(0, this, [this, darkMode]() { applyChrome(darkMode); });
    }

    void showEvent(QShowEvent* e) override
    {
        QDialog::showEvent(e);
        if (parentWidget()) {
            QPoint c = parentWidget()->mapToGlobal(parentWidget()->rect().center());
            move(c.x() - width() / 2, c.y() - height() / 2);
        }
        for (auto* w : findChildren<QWidget*>())
            w->installEventFilter(this);
    }

    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Escape) accept();
        else QDialog::keyPressEvent(e);
    }

    void closeEvent(QCloseEvent* e) override
    {
        // Alt+F4 (and any other native "close this window" request) lands
        // here, not in keyPressEvent — Windows intercepts it as a system
        // command before it ever becomes an ordinary key event. A WinUI3
        // ContentDialog isn't a separate top-level window, so from the
        // user's point of view Alt+F4 always closes the whole app; mirror
        // that since our dialog really is its own top-level window.
        if (QWidget* p = parentWidget()) {
            if (QWidget* top = p->window()) top->close();
        }
        QDialog::closeEvent(e);
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        setRingOn(nullptr);
        QDialog::mousePressEvent(e);
    }

    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override
    {
#ifdef Q_OS_WIN
        if (eventType == "windows_generic_MSG") {
            MSG* msg = static_cast<MSG*>(message);
            if (msg->message == WM_LBUTTONDOWN)
                setRingOn(nullptr);
        }
#else
        Q_UNUSED(eventType);
        Q_UNUSED(message);
        Q_UNUSED(result);
#endif
        return QDialog::nativeEvent(eventType, message, result);
    }

    void applyChrome(bool dark)
    {
#ifdef Q_OS_WIN
        HWND hwnd = reinterpret_cast<HWND>(winId());
        if (!hwnd) return;
        BOOL d = dark ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &d, sizeof(d));
        DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        MARGINS margins = {-1, -1, -1, -1};
        DwmExtendFrameIntoClientArea(hwnd, &margins);
        DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_NONE;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
#else
        Q_UNUSED(dark);
#endif
    }

    QWidget*     m_card      = nullptr;
    QPushButton* m_closeBtn  = nullptr;
    QPushButton* m_accentBtn = nullptr;
    QPushButton* m_linkBtn   = nullptr;
    QPushButton* m_linkBtn2  = nullptr;
    FocusRing*   m_focusRing = nullptr;

    bool isRingTarget(QWidget* w) const {
        return w && (w == m_closeBtn || w == m_accentBtn || w == m_linkBtn || w == m_linkBtn2);
    }

    void setRingOn(QWidget* w) {
        if (!m_focusRing) return;
        if (w) { m_focusRing->setWidget(w); m_focusRing->raise(); m_focusRing->show(); }
        else   { m_focusRing->setWidget(nullptr); }
    }

    bool eventFilter(QObject* obj, QEvent* event) override {
        if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab) {
                QTimer::singleShot(0, this, [this]() {
                    QWidget* fw = QApplication::focusWidget();
                    setRingOn(isRingTarget(fw) ? fw : nullptr);
                });
            }
        }
        if (event->type() == QEvent::MouseButtonPress) {
            if (auto* w = qobject_cast<QWidget*>(obj); isRingTarget(w))
                setRingOn(nullptr);
        }
        if (event->type() == QEvent::FocusIn) {
            if (auto* w = qobject_cast<QWidget*>(obj); isRingTarget(w)) {
                const Qt::FocusReason r = static_cast<QFocusEvent*>(event)->reason();
                const bool kbd = (r == Qt::TabFocusReason || r == Qt::BacktabFocusReason);
                setRingOn(kbd ? w : nullptr);
            }
        } else if (event->type() == QEvent::FocusOut) {
            if (auto* w = qobject_cast<QWidget*>(obj); isRingTarget(w))
                setRingOn(nullptr);
        }
        if (event->type() == QEvent::KeyPress && (obj == m_linkBtn || obj == m_linkBtn2)) {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
                static_cast<QPushButton*>(obj)->click();
                return true;
            }
        }
        return QDialog::eventFilter(obj, event);
    }
};

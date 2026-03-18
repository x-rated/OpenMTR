#pragma once

#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QFrame>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtGui/QKeyEvent>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")

// Custom frame that paints its own background:
// - full rounded rect in base color
// - footer strip in darker color, clipped to the same rounded rect
// No child widget backgrounds needed — all painting in one pass.
class DialogFrame : public QWidget {
public:
    explicit DialogFrame(bool dark, QWidget* parent = nullptr)
        : QWidget(parent), m_dark(dark)
    {
        setAttribute(Qt::WA_StyledBackground, false);
        setAttribute(Qt::WA_OpaquePaintEvent, false);
    }

    void setFooterTop(int y) { m_footerTop = y; update(); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QPainterPath clip;
        clip.addRoundedRect(rect(), 12, 12);
        p.setClipPath(clip);

        // Base background
        p.fillRect(rect(), m_dark ? QColor(28, 28, 32, 240) : QColor(249, 249, 249, 247));

        // Footer darker strip (only if footer position is known)
        if (m_footerTop > 0) {
            QRect footer(0, m_footerTop, width(), height() - m_footerTop);
            p.fillRect(footer, m_dark ? QColor(0, 0, 0, 50) : QColor(0, 0, 0, 10));
        }

        // Separator line
        if (m_footerTop > 0) {
            p.fillRect(0, m_footerTop - 1, width(), 1,
                m_dark ? QColor(255, 255, 255, 30) : QColor(0, 0, 0, 20));
        }

        // Border
        p.setClipping(false);
        QPen pen(m_dark ? QColor(255,255,255,20) : QColor(0,0,0,20));
        pen.setWidth(1);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5,0.5,-0.5,-0.5), 12, 12);
    }

private:
    bool m_dark;
    int  m_footerTop = 0;
};

class MicaDialog : public QDialog
{
public:
    static void show(QWidget* parent, const QString& title,
                     const QString& message, bool darkMode)
    {
        MicaDialog dlg(parent, title, message, darkMode);
        dlg.exec();
    }

private:
    explicit MicaDialog(QWidget* parent, const QString& title,
                        const QString& message, bool darkMode)
        : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setModal(true);
        setFixedWidth(400);

        auto* frame = new DialogFrame(darkMode, this);

        auto* titleLabel = new QLabel(title, frame);
        titleLabel->setObjectName("micaTitle");

        auto* bodyLabel = new QLabel(message, frame);
        bodyLabel->setObjectName("micaBody");
        bodyLabel->setWordWrap(true);

        auto* closeBtn = new QPushButton("Close", frame);
        closeBtn->setObjectName("micaClose");
        closeBtn->setFixedWidth(82);
        closeBtn->setDefault(true);
        closeBtn->setAutoDefault(true);
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

        auto* btnRow = new QHBoxLayout;
        btnRow->setContentsMargins(0, 0, 0, 0);
        btnRow->addStretch();
        btnRow->addWidget(closeBtn);

        // Spacer widget that tells DialogFrame where footer starts
        auto* footerSpacer = new QWidget(frame);
        footerSpacer->setFixedHeight(0);
        footerSpacer->setObjectName("footerMarker");

        auto* layout = new QVBoxLayout(frame);
        layout->setContentsMargins(24, 20, 24, 0);
        layout->setSpacing(0);
        layout->addWidget(titleLabel);
        layout->addSpacing(8);
        layout->addWidget(bodyLabel);
        layout->addSpacing(20);
        layout->addWidget(footerSpacer); // marks footer top
        layout->addSpacing(10);
        layout->addLayout(btnRow);
        layout->addSpacing(12);

        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->addWidget(frame);

        // Tell frame where footer starts after layout settles
        QTimer::singleShot(0, this, [frame, footerSpacer]() {
            frame->setFooterTop(footerSpacer->mapTo(frame, QPoint(0,0)).y());
        });

        // Apply button styles
        if (darkMode) {
            frame->setStyleSheet(R"(
                QLabel#micaTitle {
                    color: #ffffff;
                    font-family: "Segoe UI"; font-size: 20px; font-weight: 600;
                    background: transparent;
                }
                QLabel#micaBody {
                    color: rgba(255,255,255,0.75);
                    font-family: "Segoe UI"; font-size: 14px;
                    background: transparent;
                }
                QPushButton#micaClose {
                    background-color: rgba(255,255,255,0.06);
                    color: #ffffff;
                    border: 1px solid rgba(255,255,255,0.09);
                    border-radius: 4px;
                    padding: 5px 12px;
                    font-family: "Segoe UI"; font-size: 14px;
                }
                QPushButton#micaClose:hover   { background-color: rgba(255,255,255,0.10); border-color: rgba(255,255,255,0.13); }
                QPushButton#micaClose:pressed { background-color: rgba(255,255,255,0.04); }
            )");
        } else {
            frame->setStyleSheet(R"(
                QLabel#micaTitle {
                    color: #1a1a1a;
                    font-family: "Segoe UI"; font-size: 20px; font-weight: 600;
                    background: transparent;
                }
                QLabel#micaBody {
                    color: #1a1a1a;
                    font-family: "Segoe UI"; font-size: 14px;
                    background: transparent;
                }
                QPushButton#micaClose {
                    background-color: rgba(255,255,255,0.7);
                    color: #1a1a1a;
                    border: 1px solid rgba(0,0,0,0.14);
                    border-radius: 4px;
                    padding: 5px 12px;
                    font-family: "Segoe UI"; font-size: 14px;
                }
                QPushButton#micaClose:hover   { background-color: rgba(255,255,255,0.9); border-color: rgba(0,0,0,0.22); }
                QPushButton#micaClose:pressed { background-color: rgba(0,0,0,0.04); }
            )");
        }

        QTimer::singleShot(0, this, [this, darkMode]() { applyChrome(darkMode); });
    }

    void showEvent(QShowEvent* e) override
    {
        QDialog::showEvent(e);
        if (parentWidget()) {
            QPoint c = parentWidget()->geometry().center();
            move(c.x() - width() / 2, c.y() - height() / 2);
        }
    }

    void keyPressEvent(QKeyEvent* e) override
    {
        if (e->key() == Qt::Key_Escape) accept();
        else QDialog::keyPressEvent(e);
    }

    void applyChrome(bool dark)
    {
        HWND hwnd = reinterpret_cast<HWND>(winId());
        if (!hwnd) return;
        BOOL d = dark ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &d, sizeof(d));
        DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
        MARGINS margins = {-1, -1, -1, -1};
        DwmExtendFrameIntoClientArea(hwnd, &margins);
        DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_MAINWINDOW;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
    }
};

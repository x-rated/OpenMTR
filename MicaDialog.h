#pragma once

#include <QtWidgets/QDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QWidget>
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
        , m_dark(darkMode)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setModal(true);
        setFixedWidth(400);

        // ── Content card (title + body, rounded top, lighter bg) ──────────
        m_card = new QWidget(this);
        m_card->setObjectName("micaCard");

        auto* titleLabel = new QLabel(title, m_card);
        titleLabel->setObjectName("micaTitle");
        titleLabel->setWordWrap(false);

        auto* bodyLabel = new QLabel(message, m_card);
        bodyLabel->setObjectName("micaBody");
        bodyLabel->setWordWrap(true);

        auto* cardLayout = new QVBoxLayout(m_card);
        cardLayout->setContentsMargins(24, 20, 24, 20);
        cardLayout->setSpacing(8);
        cardLayout->addWidget(titleLabel);
        cardLayout->addWidget(bodyLabel);

        // ── Footer (button row, same bg as outer frame = toolbar feel) ────
        auto* closeBtn = new QPushButton("Close", this);
        closeBtn->setObjectName("micaClose");
        closeBtn->setFixedWidth(82);
        closeBtn->setDefault(true);
        closeBtn->setAutoDefault(true);
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

        auto* btnRow = new QHBoxLayout;
        btnRow->setContentsMargins(0, 0, 0, 0);
        btnRow->addStretch();
        btnRow->addWidget(closeBtn);

        // ── Outer layout ──────────────────────────────────────────────────
        auto* sep = new QWidget(this);
        sep->setObjectName("micaSep");
        sep->setFixedHeight(1);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addWidget(m_card);
        layout->addWidget(sep);

        // Footer padding inside the outer dialog
        auto* footerWrap = new QVBoxLayout;
        footerWrap->setContentsMargins(25, 25, 25, 25);
        footerWrap->addLayout(btnRow);
        layout->addLayout(footerWrap);

        // ── Styles ────────────────────────────────────────────────────────
        if (darkMode) {
            // Card: slightly lighter than outer
            m_card->setStyleSheet(R"(
                #micaCard {
                    background-color: rgba(48, 48, 54, 0.97);
                    border-top-left-radius: 12px; border-top-right-radius: 12px; border-bottom-left-radius: 0px; border-bottom-right-radius: 0px;
                }
                #micaTitle {
                    color: #ffffff;
                    font-family: "Segoe UI"; font-size: 20px; font-weight: 600;
                    background: transparent;
                }
                #micaBody {
                    color: rgba(255,255,255,0.75);
                    font-family: "Segoe UI"; font-size: 14px;
                    background: transparent;
                }
            )");
            setStyleSheet(R"(
                MicaDialog {
                    background-color: rgba(28, 28, 32, 0.97);
                    border: 1px solid rgba(255,255,255,0.08);
                    border-radius: 12px;
                }
                QWidget#micaSep { background-color: rgba(255,255,255,0.12); }
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
            // Card: white content area on top of light-gray footer base
            m_card->setStyleSheet(R"(
                #micaCard {
                    background-color: rgba(255, 255, 255, 0.98);
                    border-top-left-radius: 12px; border-top-right-radius: 12px; border-bottom-left-radius: 0px; border-bottom-right-radius: 0px;
                }
                #micaTitle {
                    color: #1a1a1a;
                    font-family: "Segoe UI"; font-size: 20px; font-weight: 600;
                    background: transparent;
                }
                #micaBody {
                    color: #1a1a1a;
                    font-family: "Segoe UI"; font-size: 14px;
                    background: transparent;
                }
            )");
            setStyleSheet(R"(
                MicaDialog {
                    background-color: rgba(238, 238, 238, 0.98);
                    border: 1px solid rgba(0,0,0,0.10);
                    border-radius: 12px;
                }
                QWidget#micaSep { background-color: rgba(0,0,0,0.09); }
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

    bool    m_dark;
    QWidget* m_card = nullptr;
};

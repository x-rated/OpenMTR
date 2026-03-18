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
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setModal(true);
        setFixedWidth(400);

        // ── Outer frame ───────────────────────────────────────────────────
        auto* frame = new QWidget(this);
        frame->setObjectName("micaFrame");

        // ── Content area (title + body, padded) ───────────────────────────
        auto* content = new QWidget(frame);
        content->setObjectName("micaContent");

        auto* titleLabel = new QLabel(title, content);
        titleLabel->setObjectName("micaTitle");
        titleLabel->setWordWrap(false);

        auto* bodyLabel = new QLabel(message, content);
        bodyLabel->setObjectName("micaBody");
        bodyLabel->setWordWrap(true);

        auto* contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(24, 20, 24, 20);
        contentLayout->setSpacing(8);
        contentLayout->addWidget(titleLabel);
        contentLayout->addWidget(bodyLabel);

        // ── Separator — zero margins so it spans full width ───────────────
        auto* sep = new QFrame(frame);
        sep->setObjectName("micaSep");
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Plain);
        sep->setFixedHeight(1);

        // ── Footer (darker toolbar-like background) ───────────────────────
        auto* footer = new QWidget(frame);
        footer->setObjectName("micaFooter");

        auto* closeBtn = new QPushButton("Close", footer);
        closeBtn->setObjectName("micaClose");
        closeBtn->setFixedWidth(82);
        closeBtn->setDefault(true);
        closeBtn->setAutoDefault(true);
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

        auto* btnRow = new QHBoxLayout(footer);
        btnRow->setContentsMargins(24, 10, 24, 12);
        btnRow->addStretch();
        btnRow->addWidget(closeBtn);

        // ── Frame layout — no margins so sep stretches edge to edge ──────
        auto* frameLayout = new QVBoxLayout(frame);
        frameLayout->setContentsMargins(0, 0, 0, 0);
        frameLayout->setSpacing(0);
        frameLayout->addWidget(content);
        frameLayout->addWidget(sep);
        frameLayout->addWidget(footer);

        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->addWidget(frame);

        if (darkMode) {
            frame->setStyleSheet(R"(
                #micaFrame {
                    background-color: rgba(28, 28, 32, 0.94);
                    border: 1px solid rgba(255,255,255,0.08);
                    border-radius: 12px;
                }
                #micaContent { background: transparent; }
                #micaTitle {
                    color: #ffffff;
                    font-family: "Segoe UI"; font-size: 20px; font-weight: 600;
                    background: transparent;
                }
                #micaBody {
                    color: rgba(255,255,255,0.65);
                    font-family: "Segoe UI"; font-size: 14px;
                    background: transparent;
                }
                #micaSep {
                    background-color: rgba(255,255,255,0.06);
                    border: none;
                }
                #micaFooter {
                    background-color: rgba(0,0,0,0.25);
                    border-bottom-left-radius: 12px;
                    border-bottom-right-radius: 12px;
                }
                #micaClose {
                    background-color: rgba(255,255,255,0.06);
                    color: #ffffff;
                    border: 1px solid rgba(255,255,255,0.09);
                    border-radius: 4px;
                    padding: 5px 12px;
                    font-family: "Segoe UI"; font-size: 14px;
                }
                #micaClose:hover   { background-color: rgba(255,255,255,0.10); border-color: rgba(255,255,255,0.13); }
                #micaClose:pressed { background-color: rgba(255,255,255,0.04); }
            )");
        } else {
            frame->setStyleSheet(R"(
                #micaFrame {
                    background-color: rgba(249, 249, 249, 0.97);
                    border: 1px solid rgba(0,0,0,0.08);
                    border-radius: 12px;
                }
                #micaContent { background: transparent; }
                #micaTitle {
                    color: #1a1a1a;
                    font-family: "Segoe UI"; font-size: 20px; font-weight: 600;
                    background: transparent;
                }
                #micaBody {
                    color: rgba(0,0,0,0.55);
                    font-family: "Segoe UI"; font-size: 14px;
                    background: transparent;
                }
                #micaSep {
                    background-color: rgba(0,0,0,0.06);
                    border: none;
                }
                #micaFooter {
                    background-color: rgba(0,0,0,0.04);
                    border-bottom-left-radius: 12px;
                    border-bottom-right-radius: 12px;
                }
                #micaClose {
                    background-color: rgba(255,255,255,0.7);
                    color: #1a1a1a;
                    border: 1px solid rgba(0,0,0,0.14);
                    border-radius: 4px;
                    padding: 5px 12px;
                    font-family: "Segoe UI"; font-size: 14px;
                }
                #micaClose:hover   { background-color: rgba(255,255,255,0.9); border-color: rgba(0,0,0,0.22); }
                #micaClose:pressed { background-color: rgba(0,0,0,0.04); }
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

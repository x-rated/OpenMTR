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
    // title   = bold heading  e.g. "OpenMTR"
    // message = body text     e.g. "Could not resolve..."
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

        auto* frame = new QWidget(this);
        frame->setObjectName("micaFrame");

        auto* titleLabel = new QLabel(title, frame);
        titleLabel->setObjectName("micaTitle");
        titleLabel->setWordWrap(false);

        auto* bodyLabel = new QLabel(message, frame);
        bodyLabel->setObjectName("micaBody");
        bodyLabel->setWordWrap(true);

        auto* sep = new QFrame(frame);
        sep->setObjectName("micaSep");
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Plain);
        sep->setFixedHeight(1);

        auto* closeBtn = new QPushButton("Close", frame);
        closeBtn->setObjectName("micaClose");
        closeBtn->setFixedWidth(88);
        closeBtn->setDefault(true);
        closeBtn->setAutoDefault(true);
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

        auto* btnRow = new QHBoxLayout;
        btnRow->setContentsMargins(0, 0, 0, 0);
        btnRow->addStretch();
        btnRow->addWidget(closeBtn);

        auto* btnArea = new QWidget(frame);
        btnArea->setObjectName("micaBtnArea");
        auto* btnAreaLayout = new QVBoxLayout(btnArea);
        btnAreaLayout->setContentsMargins(24, 12, 24, 12);
        btnAreaLayout->addLayout(btnRow);

        auto* inner = new QVBoxLayout(frame);
        inner->setContentsMargins(24, 20, 24, 0);
        inner->setSpacing(0);
        inner->addWidget(titleLabel);
        inner->addSpacing(8);
        inner->addWidget(bodyLabel);
        inner->addSpacing(20);
        inner->addWidget(sep);
        inner->addWidget(btnArea);

        auto* outer = new QVBoxLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        outer->addWidget(frame);

        if (darkMode) {
            frame->setStyleSheet(R"(
                #micaFrame {
                    background-color: rgba(32, 32, 36, 0.96);
                    border: 1px solid rgba(255,255,255,0.08);
                    border-radius: 8px;
                }
                #micaTitle {
                    color: #ffffff;
                    font-family: "Segoe UI";
                    font-size: 20px;
                    font-weight: 600;
                    background: transparent;
                }
                #micaBody {
                    color: rgba(255,255,255,0.75);
                    font-family: "Segoe UI";
                    font-size: 14px;
                    background: transparent;
                }
                #micaSep { background-color: rgba(255,255,255,0.08); border: none; }
                #micaBtnArea { background: transparent; }
                #micaClose {
                    background-color: #3b7de8;
                    color: #ffffff;
                    border: none;
                    border-radius: 4px;
                    padding: 6px 16px;
                    font-family: "Segoe UI";
                    font-size: 14px;
                }
                #micaClose:hover   { background-color: #4f8ef0; }
                #micaClose:pressed { background-color: #2563c9; }
            )");
        } else {
            frame->setStyleSheet(R"(
                #micaFrame {
                    background-color: rgba(249, 249, 249, 0.98);
                    border: 1px solid rgba(0,0,0,0.10);
                    border-radius: 8px;
                }
                #micaTitle {
                    color: #1a1a1a;
                    font-family: "Segoe UI";
                    font-size: 20px;
                    font-weight: 600;
                    background: transparent;
                }
                #micaBody {
                    color: rgba(0,0,0,0.65);
                    font-family: "Segoe UI";
                    font-size: 14px;
                    background: transparent;
                }
                #micaSep { background-color: rgba(0,0,0,0.08); border: none; }
                #micaBtnArea { background: transparent; }
                #micaClose {
                    background-color: #3b7de8;
                    color: #ffffff;
                    border: none;
                    border-radius: 4px;
                    padding: 6px 16px;
                    font-family: "Segoe UI";
                    font-size: 14px;
                }
                #micaClose:hover   { background-color: #4f8ef0; }
                #micaClose:pressed { background-color: #2563c9; }
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

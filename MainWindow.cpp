#include "MainWindow.h"
#include "WinMTRNetWrapper.h"
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QLabel>
#include <QtGui/QClipboard>
#include <QtGui/QPalette>
#include <QtGui/QFont>
#include <QtGui/QIcon>
#include <QtGui/QResizeEvent>
#include <QtCore/QEvent>
#include <QtGui/QKeyEvent>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QDateTime>
#include <QtCore/QPointer>
#include <QtCore/QTimer>
#include "MicaDialog.h"
#include <dwmapi.h>
#include <windows.h>
#include <windns.h>
#pragma comment(lib, "dnsapi.lib")
#pragma comment(lib, "dwmapi.lib")

static const QStringList COLUMNS = {
    "Hop", "ASN", "Hostname", "IP", "Loss %", "Sent", "Recv",
    "Best ms", "Avrg ms", "Wrst ms", "Last ms"
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle("OpenMTR v1.0.0");
    setMinimumSize(1020, 420);
    resize(1100, 650);
    setWindowIcon(QIcon(":/app.ico"));

    m_darkMode = isWindowsDarkMode();
    setupUi();
    if (m_darkMode) applyDarkTheme();
    else            applyLightTheme();

    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(1000);
    connect(m_refreshTimer, &QTimer::timeout, this, &MainWindow::onRefreshTimer);

    m_elapsedTimer = new QTimer(this);
    m_elapsedTimer->setInterval(1000);
    connect(m_elapsedTimer, &QTimer::timeout, this, &MainWindow::onElapsedTimer);

    m_warmupTimer = new QTimer(this);
    m_warmupTimer->setSingleShot(true);
    m_warmupTimer->setInterval(3000);
    connect(m_warmupTimer, &QTimer::timeout, this, &MainWindow::onWarmupEnd);

    QTimer::singleShot(50, this, [this]() { applyWin11Chrome(m_darkMode); });
    m_targetEdit->setFocus();
}

MainWindow::~MainWindow() = default;

unsigned MainWindow::getPingSize() const noexcept { return static_cast<unsigned>(m_pingSizeBox->value()); }
double   MainWindow::getInterval() const noexcept { return 1.0; }
bool     MainWindow::getUseDNS()   const noexcept { return true; }

bool MainWindow::isWindowsDarkMode()
{
    DWORD value = 1, size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_DWORD, nullptr, &value, &size);
    return value == 0;
}

void MainWindow::setupUi()
{
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_toolbar = new QWidget(this);
    m_toolbar->setFixedHeight(54);
    m_toolbar->setObjectName("toolbar");
    auto* tbLayout = new QHBoxLayout(m_toolbar);
    tbLayout->setContentsMargins(16, 8, 16, 8);
    tbLayout->setSpacing(8);

    auto* targetLabel = new QLabel("Target:", this);
    targetLabel->setObjectName("toolLabel");
    tbLayout->addWidget(targetLabel);

    m_targetEdit = new QLineEdit(this);
    m_targetEdit->setObjectName("targetEdit");
    m_targetEdit->setPlaceholderText("hostname or IP");
    m_targetEdit->setFrame(false);
    m_targetEdit->setTextMargins(0, 0, 0, 0);
    m_targetEdit->setMinimumHeight(30);
    m_targetEdit->setContextMenuPolicy(Qt::NoContextMenu);
    connect(m_targetEdit, &QLineEdit::returnPressed, this, &MainWindow::onStartStop);

    auto* targetWrap = new QWidget(this);
    targetWrap->setFixedWidth(220); targetWrap->setFixedHeight(32);
    auto* targetWrapOuter = new QVBoxLayout(targetWrap);
    targetWrapOuter->setContentsMargins(0,0,0,0); targetWrapOuter->setSpacing(0);
    auto* targetInner = new QWidget(targetWrap);
    targetInner->setObjectName("inputWrap");
    auto* targetInnerLayout = new QHBoxLayout(targetInner);
    targetInnerLayout->setContentsMargins(0,0,0,0);
    targetInnerLayout->addWidget(m_targetEdit);
    auto* targetAccent = new QWidget(targetWrap);
    targetAccent->setObjectName("inputAccent"); targetAccent->setFixedHeight(2);
    targetAccent->setProperty("inputRef", QVariant::fromValue(m_targetEdit));
    targetWrapOuter->addWidget(targetInner);
    targetWrapOuter->addWidget(targetAccent);
    tbLayout->addWidget(targetWrap);
    tbLayout->addSpacing(8);

    auto* pingSizeLabel = new QLabel("Ping size:", this);
    pingSizeLabel->setObjectName("toolLabel");
    tbLayout->addWidget(pingSizeLabel);

    m_pingSizeBox = new QSpinBox(this);
    m_pingSizeBox->setObjectName("pingSizeBox");
    m_pingSizeBox->setRange(64, 8192); m_pingSizeBox->setValue(64);
    m_pingSizeBox->setFixedWidth(70);
    m_pingSizeBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_pingSizeBox->setFrame(false); m_pingSizeBox->setMinimumHeight(30);
    m_pingSizeBox->setContextMenuPolicy(Qt::NoContextMenu);

    auto* pingSizeWrap = new QWidget(this);
    pingSizeWrap->setFixedWidth(70); pingSizeWrap->setFixedHeight(32);
    auto* pingSizeWrapOuter = new QVBoxLayout(pingSizeWrap);
    pingSizeWrapOuter->setContentsMargins(0,0,0,0); pingSizeWrapOuter->setSpacing(0);
    auto* pingSizeInner = new QWidget(pingSizeWrap);
    pingSizeInner->setObjectName("inputWrap");
    auto* pingSizeInnerLayout = new QHBoxLayout(pingSizeInner);
    pingSizeInnerLayout->setContentsMargins(0,0,0,0);
    pingSizeInnerLayout->addWidget(m_pingSizeBox);
    auto* pingSizeAccent = new QWidget(pingSizeWrap);
    pingSizeAccent->setObjectName("inputAccent"); pingSizeAccent->setFixedHeight(2);
    pingSizeAccent->setProperty("inputRef", QVariant::fromValue(m_pingSizeBox));
    pingSizeWrapOuter->addWidget(pingSizeInner);
    pingSizeWrapOuter->addWidget(pingSizeAccent);
    tbLayout->addWidget(pingSizeWrap);

    auto wireFocus = [this](QWidget* input, QWidget* inner, QWidget* accent) {
        input->installEventFilter(this);
        input->setProperty("focusWrap",   QVariant::fromValue(inner));
        input->setProperty("focusAccent", QVariant::fromValue(accent));
    };
    wireFocus(m_targetEdit,  targetInner,   targetAccent);
    wireFocus(m_pingSizeBox, pingSizeInner, pingSizeAccent);

    auto* bytesLabel = new QLabel("bytes", this);
    bytesLabel->setObjectName("toolLabel");
    tbLayout->addWidget(bytesLabel);
    tbLayout->addSpacing(8);

    m_ipv6Check = new QCheckBox("IPv6", this);
    m_ipv6Check->setObjectName("ipv6Check");
    tbLayout->addWidget(m_ipv6Check);
    tbLayout->addSpacing(8);

    m_startStopBtn = new QPushButton("Start", this);
    m_startStopBtn->setObjectName("startBtn");
    m_startStopBtn->setFixedWidth(88);
    connect(m_startStopBtn, &QPushButton::clicked, this, &MainWindow::onStartStop);
    tbLayout->addWidget(m_startStopBtn);
    tbLayout->addStretch();

    m_copyBtn = new QPushButton("Copy", this);
    m_copyBtn->setObjectName("actionBtn"); m_copyBtn->setFixedWidth(82);
    connect(m_copyBtn, &QPushButton::clicked, this, &MainWindow::onCopy);
    tbLayout->addWidget(m_copyBtn);

    m_exportBtn = new QPushButton("Export", this);
    m_exportBtn->setObjectName("actionBtn"); m_exportBtn->setFixedWidth(82);
    connect(m_exportBtn, &QPushButton::clicked, this, &MainWindow::onExport);
    tbLayout->addWidget(m_exportBtn);

    m_themeBtn = new QPushButton(this);
    m_themeBtn->setObjectName("themeBtn"); m_themeBtn->setFixedWidth(82);
    m_themeBtn->setText(m_darkMode ? "Light" : "Dark");
    connect(m_themeBtn, &QPushButton::clicked, this, &MainWindow::onToggleTheme);
    tbLayout->addWidget(m_themeBtn);
    mainLayout->addWidget(m_toolbar);

    m_stack = new QStackedWidget(this);
    auto* loadPage = new QWidget();
    auto* loadLayout = new QVBoxLayout(loadPage);
    loadLayout->setContentsMargins(0,0,0,0);
    m_loadingLabel = new QLabel("Discovering route...", loadPage);
    m_loadingLabel->setObjectName("loadingLabel");
    m_loadingLabel->setAlignment(Qt::AlignCenter);
    loadLayout->addWidget(m_loadingLabel);
    m_stack->addWidget(loadPage);

    m_table = new QTableWidget(0, COLUMNS.size(), this);
    m_table->setObjectName("mtrTable");
    m_table->setHorizontalHeaderLabels(COLUMNS);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->setContextMenuPolicy(Qt::NoContextMenu);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->setShowGrid(false);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->verticalHeader()->setDefaultSectionSize(32);
    m_table->setColumnWidth(0, 45);
    m_table->setColumnWidth(1, 80);
    m_table->setColumnWidth(2, 200);
    m_table->setColumnWidth(3, 140);
    m_table->setColumnWidth(4, 65);
    m_table->setColumnWidth(5, 60);
    m_table->setColumnWidth(6, 60);
    m_table->setColumnWidth(7, 70);
    m_table->setColumnWidth(8, 70);
    m_table->setColumnWidth(9, 70);
    m_table->setColumnWidth(10, 70);

    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        auto* item = m_table->item(row, col);
        if (item && !item->text().isEmpty() && item->text() != "-")
            QApplication::clipboard()->setText(item->text());
    });
    m_stack->addWidget(m_table);
    m_stack->setCurrentIndex(1);
    mainLayout->addWidget(m_stack);
}

void MainWindow::applyDarkTheme()
{
    m_darkMode = true;
    if (m_themeBtn) m_themeBtn->setText("Light");
    setStyleSheet(R"(
QMainWindow, QWidget { background-color: transparent; color: #e8eaf6; font-family: "Segoe UI"; font-size: 14px; }
#toolbar { background: transparent; border-bottom: 1px solid rgba(255,255,255,0.06); }
#toolLabel { color: #ffffff; font-weight: normal; background: transparent; }
#inputWrap { background-color: rgba(255,255,255,0.06); border: 1px solid rgba(255,255,255,0.09); border-radius: 4px; }
#inputWrap:hover { background-color: rgba(255,255,255,0.09); border: 1px solid rgba(255,255,255,0.13); }
#inputWrap[focused="true"] { background-color: rgba(255,255,255,0.14); border: 1px solid rgba(255,255,255,0.18); }
#inputAccent { background-color: transparent; }
#inputAccent[focused="true"] { background-color: transparent; }
#targetEdit, #pingSizeBox { background: transparent; color: #ffffff; border: none; padding: 0px 11px; spacing: 6px; }
#targetEdit:disabled, #pingSizeBox:disabled { color: rgba(255,255,255,0.22); }
#ipv6Check:disabled { color: rgba(255,255,255,0.22); }
#ipv6Check::indicator:disabled { border: 1px solid rgba(255,255,255,0.15); background: rgba(255,255,255,0.02); }
#ipv6Check::indicator { width: 18px; height: 18px; border: 1px solid rgba(255,255,255,0.55); border-radius: 3px; background: rgba(255,255,255,0.04); }
#ipv6Check::indicator:hover { border: 1px solid rgba(255,255,255,0.75); background: rgba(255,255,255,0.08); }
#ipv6Check::indicator:checked { background-color: #3b7de8; border-color: #3b7de8; image: url(:/check.svg); }
#ipv6Check::indicator:checked:hover { background-color: #2563c9; border-color: #2563c9; image: url(:/check.svg); }
#ipv6Check::indicator:checked:disabled { background-color: rgba(59,125,232,0.35); border-color: rgba(59,125,232,0.35); image: url(:/check.svg); }
#startBtn { background-color: #3b7de8; color: white; border: none; border-radius: 4px; padding: 5px 12px; font-weight: semibold; }
#startBtn:hover { background-color: #4f8ef0; }
#startBtn:pressed { background-color: #2563c9; }
#startBtn:disabled { background-color: rgba(255,255,255,0.12); color: rgba(255,255,255,0.35); }
#startBtn[tracing="true"] { background-color: #c42b1c; }
#startBtn[tracing="true"]:hover { background-color: #d13a28; }
#actionBtn, #themeBtn { background-color: rgba(255,255,255,0.06); color: #ffffff; border: 1px solid rgba(255,255,255,0.09); border-radius: 4px; padding: 5px 12px; }
#actionBtn:hover, #themeBtn:hover { background-color: rgba(255,255,255,0.10); border-color: rgba(255,255,255,0.13); }
#actionBtn:pressed, #themeBtn:pressed { background-color: rgba(255,255,255,0.04); }
#loadingLabel { color: rgba(255,255,255,0.5); font-size: 14px; background: transparent; }
#mtrTable { background-color: rgba(10,12,20,0.55); alternate-background-color: rgba(255,255,255,0.06); color: #e8eaf6; border: none; gridline-color: transparent; selection-background-color: rgba(79,156,249,0.25); selection-color: #e8eaf6; }
#mtrTable QHeaderView::section { background-color: rgba(0,0,0,0.35); color: rgba(255,255,255,0.6); font-weight: bold; border: none; border-bottom: 1px solid rgba(255,255,255,0.08); padding: 6px 8px; }
#mtrTable QHeaderView::section:hover { background-color: rgba(0,0,0,0.3); }
#mtrTable::item { padding: 4px 8px; }
#mtrTable::item:selected { background-color: rgba(79,156,249,0.25); }
QScrollBar:vertical { background: transparent; width: 8px; }
QScrollBar::handle:vertical { background: rgba(255,255,255,0.2); border-radius: 4px; }
QScrollBar::handle:vertical:hover { background: rgba(255,255,255,0.35); }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
)");
    applyWin11Chrome(true);
}

void MainWindow::applyLightTheme()
{
    m_darkMode = false;
    if (m_themeBtn) m_themeBtn->setText("Dark");
    setStyleSheet(R"(
QMainWindow, QWidget { background-color: transparent; color: #1a1d27; font-family: "Segoe UI"; font-size: 14px; }
#toolbar { background: transparent; border-bottom: 1px solid rgba(0,0,0,0.06); }
#toolLabel { color: #1a1a1a; font-weight: normal; background: transparent; }
#inputWrap { background-color: rgba(255,255,255,0.75); border: 1px solid rgba(0,0,0,0.1); border-radius: 4px; }
#inputWrap:hover { background-color: rgba(255,255,255,0.88); border: 1px solid rgba(0,0,0,0.14); }
#inputWrap[focused="true"] { background-color: rgba(255,255,255,1.0); border: 1px solid rgba(0,0,0,0.18); }
#inputAccent { background-color: transparent; }
#inputAccent[focused="true"] { background-color: transparent; }
#targetEdit, #pingSizeBox { background: transparent; color: #1a1a1a; border: none; padding: 0px 11px; }
#ipv6Check { color: #1a1a1a; background: transparent; spacing: 6px; }
#targetEdit:disabled, #pingSizeBox:disabled { color: rgba(0,0,0,0.22); }
#ipv6Check:disabled { color: rgba(0,0,0,0.22); }
#ipv6Check::indicator:disabled { border: 1px solid rgba(0,0,0,0.12); background: rgba(255,255,255,0.15); }
#ipv6Check::indicator { width: 18px; height: 18px; border: 1px solid rgba(0,0,0,0.55); border-radius: 3px; background: rgba(255,255,255,0.7); }
#ipv6Check::indicator:hover { border: 1px solid rgba(0,0,0,0.75); background: rgba(255,255,255,0.9); }
#ipv6Check::indicator:checked { background-color: #3b7de8; border-color: #3b7de8; image: url(:/check.svg); }
#ipv6Check::indicator:checked:hover { background-color: #2563c9; border-color: #2563c9; image: url(:/check.svg); }
#ipv6Check::indicator:checked:disabled { background-color: rgba(59,125,232,0.35); border-color: rgba(59,125,232,0.35); image: url(:/check.svg); }
#startBtn { background-color: #3b7de8; color: white; border: none; border-radius: 4px; padding: 5px 12px; font-weight: semibold; }
#startBtn:hover { background-color: #4f8ef0; }
#startBtn:pressed { background-color: #2563c9; }
#startBtn:disabled { background-color: rgba(0,0,0,0.03); color: rgba(0,0,0,0.22); }
#startBtn[tracing="true"] { background-color: #c42b1c; }
#startBtn[tracing="true"]:hover { background-color: #d13a28; }
#actionBtn, #themeBtn { background-color: rgba(255,255,255,0.7); color: #1a1a1a; border: 1px solid rgba(0,0,0,0.14); border-radius: 4px; padding: 5px 12px; }
#actionBtn:hover, #themeBtn:hover { background-color: rgba(255,255,255,0.9); border-color: rgba(0,0,0,0.22); }
#actionBtn:pressed, #themeBtn:pressed { background-color: rgba(0,0,0,0.04); }
#loadingLabel { color: rgba(0,0,0,0.45); font-size: 14px; background: transparent; }
#mtrTable { background-color: #f3f3f3; alternate-background-color: rgba(0,0,0,0.04); color: #1a1d27; border: none; gridline-color: transparent; selection-background-color: rgba(59,125,232,0.18); selection-color: #1a1d27; }
#mtrTable QHeaderView::section { background-color: rgba(255,255,255,0.7); color: rgba(0,0,0,0.55); font-weight: bold; border: none; border-bottom: 1px solid rgba(0,0,0,0.08); padding: 6px 8px; }
#mtrTable QHeaderView::section:hover { background-color: rgba(255,255,255,0.7); }
#mtrTable::item { padding: 4px 8px; }
#mtrTable::item:selected { background-color: rgba(59,125,232,0.18); }
QScrollBar:vertical { background: transparent; width: 8px; }
QScrollBar::handle:vertical { background: rgba(0,0,0,0.15); border-radius: 4px; }
QScrollBar::handle:vertical:hover { background: rgba(0,0,0,0.3); }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
)");
    applyWin11Chrome(false);
}

void MainWindow::applyWin11Chrome(bool dark)
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

void MainWindow::onToggleTheme()
{
    if (m_darkMode) applyLightTheme();
    else            applyDarkTheme();
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType == "windows_generic_MSG") {
        MSG* msg = static_cast<MSG*>(message);
        if (msg->message == WM_NCHITTEST) {
            LRESULT dwmResult = 0;
            if (DwmDefWindowProc(msg->hwnd, msg->message, msg->wParam, msg->lParam, &dwmResult)) {
                *result = dwmResult;
                return true;
            }
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_targetEdit && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            && !m_startStopBtn->isEnabled())
            return true;
    }
    if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
        auto* widget = qobject_cast<QWidget*>(obj);
        if (widget) {
            bool focused = (event->type() == QEvent::FocusIn);
            auto wrap = widget->property("focusWrap").value<QWidget*>();
            if (wrap) { wrap->setProperty("focused", focused); wrap->style()->polish(wrap); }
            auto accent = widget->property("focusAccent").value<QWidget*>();
            if (accent) { accent->setProperty("focused", focused); accent->style()->polish(accent); }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::resizeEvent(QResizeEvent* event) { QMainWindow::resizeEvent(event); }

QString MainWindow::lookupASN(const QString& ip, bool ipv6)
{
    if (ip.isEmpty() || ip == "0.0.0.0" || ip == "::"
        || ip.startsWith("192.168.") || ip.startsWith("10.")
        || ip.startsWith("172.")     || ip.startsWith("127.")
        || ip.startsWith("169.254")  || ip.startsWith("fe80")
        || ip.startsWith("fc")       || ip.startsWith("fd"))
        return QString();

    QString query;
    if (!ipv6) {
        QStringList parts = ip.split('.');
        if (parts.size() != 4) return QString();
        std::reverse(parts.begin(), parts.end());
        query = parts.join('.') + ".origin.asn.cymru.com";
    } else {
        struct addrinfo hints = {}, *res = nullptr;
        hints.ai_family = AF_INET6;
        hints.ai_flags  = AI_NUMERICHOST;
        if (getaddrinfo(ip.toStdString().c_str(), nullptr, &hints, &res) != 0) return QString();
        auto* sa6 = reinterpret_cast<sockaddr_in6*>(res->ai_addr);
        QString hex;
        for (int b = 0; b < 16; ++b)
            hex += QString("%1").arg(sa6->sin6_addr.s6_addr[b], 2, 16, QChar('0'));
        freeaddrinfo(res);
        QString reversed;
        for (int i = 31; i >= 0; --i) { reversed += hex[i]; if (i > 0) reversed += '.'; }
        query = reversed + ".origin6.asn.cymru.com";
    }

    PDNS_RECORD pDnsRecord = nullptr;
    DNS_STATUS status = DnsQuery_W(query.toStdWString().c_str(), DNS_TYPE_TEXT,
        DNS_QUERY_STANDARD, nullptr, &pDnsRecord, nullptr);
    if (status != ERROR_SUCCESS || !pDnsRecord) return QString();

    QString result;
    for (PDNS_RECORD r = pDnsRecord; r; r = r->pNext) {
        if (r->wType == DNS_TYPE_TEXT && r->Data.TXT.dwStringCount > 0) {
            QString txt = QString::fromWCharArray(r->Data.TXT.pStringArray[0]);
            QString asn = txt.split('|').first().trimmed();
            if (!asn.isEmpty() && asn != "0") result = asn;
            break;
        }
    }
    DnsFree(pDnsRecord, DnsFreeRecordList);
    return result;
}

QString MainWindow::getCachedASN(const QString& ip, bool ipv6) const
{
    auto key = ip.toStdString();
    auto it  = m_asnCache.find(key);
    if (it != m_asnCache.end()) return it->second.isEmpty() ? "-" : it->second;
    QString asn = lookupASN(ip, ipv6);
    m_asnCache[key] = asn;
    return asn.isEmpty() ? "-" : asn;
}

void MainWindow::onStartStop()
{
    if (m_tracing) {
        m_stopSource.request_stop();
        m_stopSource = std::stop_source{};
        m_tracing  = false;
        m_counting = false;
        m_refreshTimer->stop();
        m_elapsedTimer->stop();
        m_warmupTimer->stop();
        m_startStopBtn->setText("Start");
        m_startStopBtn->setProperty("tracing", false);
        m_startStopBtn->style()->polish(m_startStopBtn);
        m_targetEdit->setEnabled(true);
        m_ipv6Check->setEnabled(true);
        m_pingSizeBox->setEnabled(true);
        setWindowTitle("OpenMTR v1.0.0");
        if (m_net) updateTable();
        m_baseline.clear();
        m_asnCache.clear();
        m_stack->setCurrentIndex(1);

        m_startStopBtn->setEnabled(false);
        auto oldNet = m_net;
        m_net.reset();

        auto* pollTimer = new QTimer(this);
        pollTimer->setInterval(100);
        QPointer<MainWindow> self(this);
        connect(pollTimer, &QTimer::timeout, this, [self, pollTimer, oldNet]() mutable {
            if (!self) { pollTimer->stop(); pollTimer->deleteLater(); oldNet.reset(); return; }
            if (oldNet && !oldNet->isDone()) return;
            pollTimer->stop();
            pollTimer->deleteLater();
            oldNet.reset();
            if (!self->m_tracing) self->m_startStopBtn->setEnabled(true);
        });
        pollTimer->start();

    } else {
        if (!m_startStopBtn->isEnabled()) return;
        QString target = m_targetEdit->text().trimmed();
        if (target.isEmpty()) return;

        // Create fresh wrapper for each trace
        m_net = std::make_shared<WinMTRNetWrapper>(this);

        SOCKADDR_INET addr = {};
        struct addrinfo hints = {}, *res = nullptr;
        hints.ai_family = m_ipv6Check->isChecked() ? AF_INET6 : AF_INET;
        int rc = getaddrinfo(target.toStdString().c_str(), nullptr, &hints, &res);
        if (rc != 0 || !res) {
            m_ipv6Check->setChecked(!m_ipv6Check->isChecked());
            hints.ai_family = m_ipv6Check->isChecked() ? AF_INET6 : AF_INET;
            rc = getaddrinfo(target.toStdString().c_str(), nullptr, &hints, &res);
            if (rc != 0 || !res) {
                MicaDialog::show(this, "OpenMTR", QString("Could not resolve \"%1\".").arg(target), m_darkMode);
                return;
            }
        }

        addrinfo* match = nullptr;
        for (addrinfo* r = res; r; r = r->ai_next)
            if (r->ai_family == hints.ai_family) { match = r; break; }
        if (!match) {
            freeaddrinfo(res);
            MicaDialog::show(this, "OpenMTR", QString("Could not resolve \"%1\".").arg(target), m_darkMode);
            return;
        }
        memcpy(&addr, match->ai_addr,
            match->ai_addrlen < sizeof(addr) ? match->ai_addrlen : sizeof(addr));
        freeaddrinfo(res);

        m_tracing  = true;
        m_counting = false;
        m_table->setRowCount(0);
        m_stack->setCurrentIndex(0);
        m_startStopBtn->setText("Stop");
        m_startStopBtn->setProperty("tracing", true);
        m_startStopBtn->style()->polish(m_startStopBtn);
        m_targetEdit->setEnabled(false);
        m_ipv6Check->setEnabled(false);
        m_pingSizeBox->setEnabled(false);

        [[maybe_unused]] auto trace = m_net->DoTrace(m_stopSource.get_token(), addr);
        m_refreshTimer->start();
        m_elapsedTimer->start();
        m_warmupTimer->start();
        m_elapsed.start();
        ++m_warmupGen;
    }
}

void MainWindow::onRefreshTimer()  { if (m_net && m_tracing) updateTable(); }

void MainWindow::onElapsedTimer()
{
    if (!m_tracing || !m_counting) return;
    qint64 secs = m_elapsed.elapsed() / 1000;
    int h = secs / 3600, m = (secs % 3600) / 60, s = secs % 60;
    QString elapsed = h > 0
        ? QString("%1:%2:%3").arg(h).arg(m,2,10,QChar('0')).arg(s,2,10,QChar('0'))
        : QString("%1:%2").arg(m).arg(s,2,10,QChar('0'));
    setWindowTitle(QString("OpenMTR v1.0.0 — %1").arg(elapsed));
}

void MainWindow::onWarmupEnd()
{
    if (!m_net) return;
    constexpr qint64 kWarmupDeadlineMs = 4000;
    const bool deadlineReached = m_elapsed.elapsed() >= kWarmupDeadlineMs;

    int maxHops = m_net->GetMax();
    if (maxHops >= 30 && !deadlineReached) {
        const int gen = m_warmupGen;
        QTimer::singleShot(250, this, [this, gen]() { if (m_warmupGen == gen) onWarmupEnd(); });
        return;
    }

    auto state = m_net->getCurrentState();
    if (!deadlineReached) {
        for (int i = 0; i < maxHops; ++i) {
            if (state[i].xmit == 0) {
                const int gen = m_warmupGen;
                QTimer::singleShot(250, this, [this, gen]() { if (m_warmupGen == gen) onWarmupEnd(); });
                return;
            }
        }
    }

    const int gen = m_warmupGen;
    QTimer::singleShot(1100, this, [this, gen]() {
        if (!m_net || !m_tracing || m_warmupGen != gen) return;
        auto st = m_net->getCurrentState();
        m_baseline.clear();
        m_asnCache.clear();
        for (auto& h : st)
            m_baseline.push_back({h.xmit, h.returned});
        m_counting = true;
        m_elapsed.restart();
        m_stack->setCurrentIndex(1);
        updateTable();
    });
}

void MainWindow::updateTable()
{
    if (!m_net) return;
    auto state = m_net->getCurrentState();
    int rows = static_cast<int>(state.size());
    m_table->setRowCount(rows);

    for (int i = 0; i < rows; ++i) {
        const auto& h = state[i];
        QString ip      = QString::fromStdWString(addr_to_wstring(h.addr));
        bool hasAddr    = (h.addr.si_family != AF_UNSPEC);
        QString name    = hasAddr ? QString::fromStdWString(h.getName()) : "?";
        if (hasAddr && name.isEmpty()) name = ip;

        auto setCell = [&](int col, const QString& text, Qt::Alignment align) {
            auto* item = m_table->item(i, col);
            if (!item) { item = new QTableWidgetItem(text); item->setTextAlignment(align); m_table->setItem(i, col, item); }
            else item->setText(text);
        };
        constexpr auto C = Qt::AlignCenter | Qt::AlignVCenter;
        constexpr auto L = Qt::AlignLeft   | Qt::AlignVCenter;

        setCell(0, QString::number(i + 1), C);
        setCell(1, hasAddr ? getCachedASN(ip, h.addr.si_family == AF_INET6) : "-", C);
        setCell(2, hasAddr ? name : "?", L);
        setCell(3, hasAddr ? ip   : "?", L);

        if (h.xmit == 0) {
            for (int c = 4; c < COLUMNS.size(); ++c) setCell(c, "-", C);
        } else {
            int bXmit     = (i < (int)m_baseline.size()) ? m_baseline[i][0] : 0;
            int bReturned = (i < (int)m_baseline.size()) ? m_baseline[i][1] : 0;
            int xmit      = std::max(0, h.xmit     - bXmit);
            int returned  = std::max(0, h.returned  - bReturned);
            int loss      = (xmit == 0) ? 0 : (100 - (100 * returned / xmit));

            setCell(4,  QString::number(loss), C);
            setCell(5,  QString::number(xmit), C);
            setCell(6,  QString::number(returned), C);
            // Latency values (best/avg/worst/last) are raw engine values, not
            // baselined. Guard on h.returned (total ever received) not the
            // post-baseline `returned`, so hops that only replied during warmup
            // still show their RTT instead of all dashes.
            setCell(7,  h.returned == 0 ? "-" : QString::number(h.best),     C);
            setCell(8,  h.returned == 0 ? "-" : QString::number(h.getAvg()), C);
            setCell(9,  h.returned == 0 ? "-" : QString::number(h.worst),    C);
            setCell(10, h.returned == 0 ? "-" : QString::number(h.last),     C);

            if (auto* lossItem = m_table->item(i, 4)) {
                QColor fg;
                if      (loss == 0)  fg = m_darkMode ? QColor(0x4c,0xaf,0x50) : QColor(0x2e,0x7d,0x32);
                else if (loss < 5)   fg = m_darkMode ? QColor(0xff,0xee,0x58) : QColor(0xf5,0x7f,0x17);
                else if (loss < 20)  fg = QColor(0xff,0xa7,0x26);
                else                 fg = QColor(0xef,0x53,0x50);
                lossItem->setForeground(fg);
            }
        }
    }
}

QString MainWindow::buildTextExport() const
{
    QString target = m_targetEdit->text().trimmed();
    constexpr int W[] = {5, 7, 61, 17, 7, 6, 6, 6, 6, 6, 6};
    constexpr int NCOLS = 11;
    auto pad = [](const QString& s, int w) { return s.leftJustified(w, ' ', true); };
    QString sep = "+";
    for (int c = 0; c < NCOLS; ++c) sep += QString(W[c] + 2, '-') + "+";
    QString out;
    out += "OpenMTR Export\n";
    out += QString("Target  : %1\n").arg(target);
    out += QString("Date    : %1\n\n").arg(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
    out += sep + "\n";
    QString hdr = "|";
    for (int c = 0; c < NCOLS; ++c) hdr += " " + pad(COLUMNS[c], W[c]) + " |";
    out += hdr + "\n" + sep + "\n";
    for (int i = 0; i < m_table->rowCount(); ++i) {
        QString row = "|";
        for (int c = 0; c < NCOLS; ++c) {
            auto* item = m_table->item(i, c);
            row += " " + pad(item ? item->text() : "-", W[c]) + " |";
        }
        out += row + "\n";
    }
    out += sep + "\n";
    return out;
}

void MainWindow::onCopy()   { QApplication::clipboard()->setText(buildTextExport()); }

void MainWindow::onExport()
{
    QString target = m_targetEdit->text().trimmed();
    QString stamp  = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString defaultName = QString("OpenMTR_%1_%2").arg(target.isEmpty() ? "export" : target, stamp);
    QString filter = "Text files (*.txt);;CSV files (*.csv);;All files (*)";
    QString path   = QFileDialog::getSaveFileName(this, "Export results", defaultName, filter);
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        MicaDialog::show(this, "OpenMTR", QString("Could not write to \"%1\".").arg(path), m_darkMode);
        return;
    }
    QTextStream ts(&f);
    if (path.endsWith(".csv", Qt::CaseInsensitive)) {
        ts << COLUMNS.join(',') << "\n";
        for (int i = 0; i < m_table->rowCount(); ++i) {
            QStringList cells;
            for (int c = 0; c < COLUMNS.size(); ++c) {
                auto* item = m_table->item(i, c);
                QString val = item ? item->text() : "";
                if (val.contains(',') || val.contains('"'))
                    val = "\"" + val.replace("\"", "\"\"") + "\"";
                cells << val;
            }
            ts << cells.join(',') << "\n";
        }
    } else {
        ts << buildTextExport();
    }
}

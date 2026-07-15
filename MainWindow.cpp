// ==========================================================================
//  MainWindow.cpp — implementation of the OpenMTR main window
//
//  Grouped by concern: construction, UI building, theming, input styling,
//  focus ring, the custom frameless-window plumbing (native Win32 events),
//  Qt event handling, ASN lookup, the trace lifecycle, and table / export.
// ==========================================================================

// ==========================================================================
//  Includes
// ==========================================================================

// Project headers.
#include "MainWindow.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include "version.h"
#include "tracer.h"

// Qt.
#include <QtWidgets/QApplication>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QFrame>
#include <QtWidgets/QLabel>
#include <QtWidgets/QStyleFactory>
#include <QtGui/QPalette>
#include <QtGui/QIcon>
#include <QtGui/QResizeEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QShortcut>
#include <QtGui/QCursor>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QDateTime>
#include <QtCore/QPointer>
#include <QtCore/QSysInfo>
#include <QtCore/QTimer>

// Windows.
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commdlg.h>
#include <windns.h>
#include <winreg.h>

// C++ standard library.
#include <algorithm>
#include <thread>
#include <cstring>

// ==========================================================================
//  File-local constants & helpers
// ==========================================================================

// Logical column order of the results table. Two spacer columns are added on
// top of these in setupUi().
static const QStringList COLUMNS = {
    "Hop", "ASN", "Hostname", "IP", "Loss %", "Sent", "Recv",
    "Best ms", "Avrg ms", "Wrst ms", "Last ms", "Jttr ms"
};

// Flatten a translucent colour onto an opaque base (straight "over" composite)
// and return it as "#rrggbb". Control borders must be opaque: Qt draws a border
// with alpha < 1 through its antialiased rounded-rect path, which shifts the
// button half a pixel down and softens its edges. Compositing the intended
// translucent look onto an opaque colour preserves the appearance without it.
static QString ovAccentBlend(const QColor& fg, const QColor& base, double alpha)
{
    const double a = std::clamp(alpha, 0.0, 1.0);
    const int r = qRound(fg.red()   * a + base.red()   * (1.0 - a));
    const int g = qRound(fg.green() * a + base.green() * (1.0 - a));
    const int b = qRound(fg.blue()  * a + base.blue()  * (1.0 - a));
    return QColor(r, g, b).name();
}

// Put text on the Windows clipboard as UTF-16. No-op for empty text or if the
// clipboard can't be opened.
static void copyTextToClipboard(const QString& text)
{
    if (text.isEmpty() || !OpenClipboard(nullptr))
        return;
    EmptyClipboard();
    const size_t bytes = (static_cast<size_t>(text.size()) + 1) * sizeof(wchar_t);
    if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* dst = GlobalLock(mem)) {
            std::memcpy(dst, text.utf16(), bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem))
                GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

// Format a duration as H:MM:SS (or M:SS under an hour). Shared by the
// window-title elapsed display and the exported report's Duration field, so
// the two never disagree on formatting.
static QString formatDuration(qint64 ms)
{
    qint64 secs = ms / 1000;
    int h = static_cast<int>(secs / 3600), m = static_cast<int>((secs % 3600) / 60), s = static_cast<int>(secs % 60);
    return h > 0
        ? QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'))
        : QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

// ==========================================================================
//  Construction & lifecycle
// ==========================================================================

// Build the window: detect the system theme, install the focus-rect-free
// style, create the UI, wire focus tracking and the timers, then (once the
// native handle exists) apply the Win11 chrome and register shortcuts.
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(OPENMTR_VERSION_TITLE);
    setMinimumSize(1100, 500);
    resize(1300, 650);

    m_darkMode = isWindowsDarkMode();
    qApp->setStyle(new NoFocusRectStyle(qApp->style()));
    setupUi();
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget*, QWidget* now) {
        bool isInteractive = now && (now->inherits("QPushButton") || now->inherits("QCheckBox")
                                  || now->inherits("QLineEdit")   || now->inherits("QAbstractSpinBox"));
        if (!isInteractive) {
            m_keyboardFocus = false;
            hideFocusRing();
            for (auto* w : std::initializer_list<QWidget*>{
                    m_startStopBtn, m_copyBtn, m_exportBtn, m_themeBtn, m_infoBtn, m_ipv6Check,
                    m_targetEdit, m_pingSizeBox}) {
                w->setProperty("focused", false);
                w->style()->polish(w);
            }
            for (auto* f : m_inputs)
                applyInputIdleStyle(f);
        }
    });
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
    // First state check; the reveal is gated purely by the conditions in
    // onWarmupEnd(), so this is only how soon checking begins.
    m_warmupTimer->setInterval(1500);
    connect(m_warmupTimer, &QTimer::timeout, this, &MainWindow::onWarmupEnd);

    QTimer::singleShot(50, this, [this]() {
        applyFramelessStyle();
        applyWin11Chrome(m_darkMode);
        if (m_titleBar) m_titleBar->setMaximized(isMaximized());
    });
    m_targetEdit->setFocus();

    auto* scCopy   = new QShortcut(QKeySequence::Copy, this);
    auto* scExport = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_S), this);
    connect(scCopy,   &QShortcut::activated, this, &MainWindow::onCopy);
    connect(scExport, &QShortcut::activated, this, &MainWindow::onExport);
    // Ctrl+C / Ctrl+S are window-wide shortcuts (copy the results table / export
    // the report). While a text field holds focus they are disabled so the key
    // reaches the field instead: Ctrl+C is then picked up by eventFilter() and
    // copied via the Win32 path (this static build's Qt clipboard write, which
    // QLineEdit::copy() relies on, is broken), and Ctrl+S is simply inert while
    // editing. Restored once focus moves elsewhere.
    auto tuneEditShortcuts = [scCopy, scExport](QWidget* now) {
        const bool inTextField = now && (now->inherits("QLineEdit")
                                      || now->inherits("QAbstractSpinBox"));
        scCopy->setEnabled(!inTextField);
        scExport->setEnabled(!inTextField);
    };
    connect(qApp, &QApplication::focusChanged, this,
            [tuneEditShortcuts](QWidget*, QWidget* now) { tuneEditShortcuts(now); });
    tuneEditShortcuts(m_targetEdit);   // the app opens with the target field focused
}

// Members tear themselves down; nothing to do by hand.
MainWindow::~MainWindow() = default;

// Current ICMP payload size from the spin box (IOpenMTROptionsProvider).
unsigned MainWindow::getPingSize() const noexcept { return static_cast<unsigned>(m_pingSizeBox->value()); }

// ==========================================================================
//  UI construction
// ==========================================================================

// Assemble the whole UI: the custom title bar, the toolbar (target / ping
// size / IPv6 / Start-Stop / Copy / Export / theme / about) and a stacked
// widget with three pages — empty, "discovering route" and the results table.
void MainWindow::setupUi()
{
    auto* central = new QWidget(this);
    setCentralWidget(central);
    central->installEventFilter(this);
    auto* mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_titleBar = new TitleBarWidget(this);
    m_titleBar->setTitle(windowTitle());
    m_titleBar->setDark(m_darkMode);
    mainLayout->addWidget(m_titleBar);

    m_toolbar = new QWidget(this);
    m_toolbar->setFixedHeight(46);
    m_toolbar->setObjectName("toolbar");
    m_toolbar->installEventFilter(this);
    auto* tbLayout = new QHBoxLayout(m_toolbar);
    tbLayout->setContentsMargins(20, 0, 16, 8);
    tbLayout->setSpacing(8);

    auto* targetLabel = new QLabel("Target:", this);
    targetLabel->setObjectName("toolLabel");
    tbLayout->addWidget(targetLabel);

    m_targetEdit = new QLineEdit(this);
    m_targetEdit->setObjectName("targetEdit");
    m_targetEdit->setPlaceholderText("Hostname or IP");
    m_targetEdit->setFrame(false);
    m_targetEdit->setAttribute(Qt::WA_Hover);
    m_targetEdit->setCursor(Qt::IBeamCursor);
    m_targetEdit->setFixedWidth(395); m_targetEdit->setFixedHeight(32);
    m_targetEdit->setContextMenuPolicy(Qt::NoContextMenu);
    connect(m_targetEdit, &QLineEdit::returnPressed, this, &MainWindow::onStartStop);

    // Clear button (Fluent "clear" glyph), shown only while the field has
    // focus and non-empty text; its background appears on hover only. Sits at
    // the right edge of the input, before the accent overlay.
    auto* clearBtn = new QPushButton(QString(QChar(0xE711)), m_targetEdit);
    clearBtn->setObjectName("inputClear");
    clearBtn->setCursor(Qt::ArrowCursor);
    clearBtn->setFocusPolicy(Qt::NoFocus);
    clearBtn->setFixedSize(28, 24);
    clearBtn->hide();
    auto updateClear = [this, clearBtn]() {
        const bool show = m_targetEdit->hasFocus() && !m_targetEdit->text().isEmpty();
        if (show) {
            const int btnX = m_targetEdit->width() - 28 - 4;
            clearBtn->move(btnX, (m_targetEdit->height() - 24) / 2);
            clearBtn->raise();
            // Reserve room on the right so typed text and the cursor never
            // scroll under the button; combined with the 6px QSS padding this
            // gives the same 6px gap in front of the × as before.
            m_targetEdit->setTextMargins(0, 0, 32, 0);
        } else {
            m_targetEdit->setTextMargins(0, 0, 0, 0);
        }
        clearBtn->setVisible(show);
    };
    connect(m_targetEdit, &QLineEdit::textChanged, this, [updateClear](const QString&){ updateClear(); });
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        m_targetEdit->clear();
        m_targetEdit->setFocus(Qt::OtherFocusReason);
    });
    m_targetClearUpdate = updateClear;
    auto* targetAccent = new InputAccentBar(m_targetEdit);
    targetAccent->setObjectName("inputAccent");
    targetAccent->setAttribute(Qt::WA_TransparentForMouseEvents);
    targetAccent->hide();
    tbLayout->addWidget(m_targetEdit);
    tbLayout->addSpacing(8);

    auto* pingSizeLabel = new QLabel("Ping size:", this);
    pingSizeLabel->setObjectName("toolLabel");
    tbLayout->addWidget(pingSizeLabel);

    m_pingSizeBox = new QSpinBox(this);
    m_pingSizeBox->setObjectName("pingSizeBox");
    m_pingSizeBox->setRange(64, 8192); m_pingSizeBox->setValue(64);
    m_pingSizeBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_pingSizeBox->setFrame(false);
    m_pingSizeBox->setAttribute(Qt::WA_Hover);
    m_pingSizeBox->setCursor(Qt::IBeamCursor);
    m_pingSizeBox->setFixedWidth(54); m_pingSizeBox->setFixedHeight(32);
    m_pingSizeBox->setContextMenuPolicy(Qt::NoContextMenu);
    m_pingSizeBox->installEventFilter(this);
    // The spin box's key events go to its internal QLineEdit, so filter that too
    // (needed to intercept Ctrl+C there — see eventFilter()).
    if (auto* sbEdit = m_pingSizeBox->findChild<QLineEdit*>())
        sbEdit->installEventFilter(this);
    auto* pingSizeAccent = new InputAccentBar(m_pingSizeBox);
    pingSizeAccent->setObjectName("inputAccent");
    pingSizeAccent->setAttribute(Qt::WA_TransparentForMouseEvents);
    pingSizeAccent->hide();
    tbLayout->addWidget(m_pingSizeBox);
    m_inputs = {m_targetEdit, m_pingSizeBox};

    m_targetEdit->installEventFilter(this);
    m_pingSizeBox->installEventFilter(this);

    auto* bytesLabel = new QLabel("bytes", this);
    bytesLabel->setObjectName("toolLabel");
    tbLayout->addWidget(bytesLabel);
    tbLayout->addSpacing(8);

    m_ipv6Check = new PaintedCheckBox("IPv6", this);
    m_ipv6Check->setObjectName("ipv6Check");
    m_ipv6Check->setChecked(true);   // IPv6 tracing is the default
    m_ipv6Check->installEventFilter(this);
    tbLayout->addWidget(m_ipv6Check);
    tbLayout->addSpacing(8);

    m_startStopBtn = new QPushButton("Start", this);
    m_startStopBtn->setObjectName("actionBtn");
    m_startStopBtn->setProperty("variant", "start");
    m_startStopBtn->setFixedWidth(80);
    m_startStopBtn->setFixedHeight(32);
    m_startStopBtn->setAutoDefault(false);
    m_startStopBtn->setEnabled(false);
    m_startStopBtn->installEventFilter(this);
    connect(m_startStopBtn, &QPushButton::clicked, this, &MainWindow::onStartStop);
    connect(m_targetEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (!m_tracing)
            m_startStopBtn->setEnabled(!text.trimmed().isEmpty());
    });
    tbLayout->addWidget(m_startStopBtn);
    tbLayout->addStretch();

    m_copyBtn = new QPushButton("Copy", this);
    m_copyBtn->setObjectName("actionBtn"); m_copyBtn->setFixedWidth(80); m_copyBtn->setFixedHeight(32);
    m_copyBtn->setAutoDefault(false);
    m_copyBtn->setEnabled(false);
    m_copyBtn->installEventFilter(this);
    connect(m_copyBtn, &QPushButton::clicked, this, &MainWindow::onCopy);
    tbLayout->addWidget(m_copyBtn);

    m_exportBtn = new QPushButton("Export", this);
    m_exportBtn->setObjectName("actionBtn"); m_exportBtn->setFixedWidth(80); m_exportBtn->setFixedHeight(32);
    m_exportBtn->setAutoDefault(false);
    m_exportBtn->setEnabled(false);
    m_exportBtn->installEventFilter(this);
    connect(m_exportBtn, &QPushButton::clicked, this, &MainWindow::onExport);
    tbLayout->addWidget(m_exportBtn);

    m_themeBtn = new QPushButton(this);
    m_themeBtn->setObjectName("themeBtn"); m_themeBtn->setFixedWidth(36); m_themeBtn->setFixedHeight(32);
    m_themeBtn->setAutoDefault(false);
    m_themeBtn->installEventFilter(this);
    m_themeBtn->setText(QString(QChar(0xE793)));   // sun glyph in both themes
    connect(m_themeBtn, &QPushButton::clicked, this, &MainWindow::onToggleTheme);
    tbLayout->addWidget(m_themeBtn);

    m_infoBtn = new QPushButton(this);
    m_infoBtn->setObjectName("iconBtn"); m_infoBtn->setFixedWidth(36); m_infoBtn->setFixedHeight(32);
    m_infoBtn->setAutoDefault(false);
    m_infoBtn->installEventFilter(this);
    m_infoBtn->setText(QString(QChar(0xE946)));
    connect(m_infoBtn, &QPushButton::clicked, this, [this]() {
        MicaDialog::show(this,
            "OpenMTR",
            QString("Version %1 (%2) · Qt %3\n\n"
                    "Continuously traces the route to a host and shows per-hop latency and packet loss statistics in real time.\n\n"
                    "© slamb.eu · GPL-2.0 license")
                .arg(OPENMTR_VERSION)
                .arg(QSysInfo::buildCpuArchitecture().toUpper()
                         .replace("X86_64", "AMD64"))
                .arg(QT_VERSION_STR),
            m_darkMode,
            "https://github.com/x-rated/OpenMTR",
            "GitHub");
    });
    tbLayout->addWidget(m_infoBtn);
    mainLayout->addWidget(m_toolbar);

    m_iconTipTimer.setSingleShot(true);
    m_iconTipTimer.setInterval(600);
    connect(&m_iconTipTimer, &QTimer::timeout, this, [this]() {
        if (m_iconTipPending)
            showIconTooltip(m_iconTipPending, m_iconTipText);
    });

    // Cell tooltip: same 600 ms delay; captures its position once on show.
    m_cellTipTimer.setSingleShot(true);
    m_cellTipTimer.setInterval(600);
    connect(&m_cellTipTimer, &QTimer::timeout, this, [this]() {
        if (!m_cellTipHover.isValid()) return;
        const QString tip = cellTooltipText(m_cellTipHover);
        if (tip.isEmpty()) return;
        if (!m_cellTip) m_cellTip = new Win11Tooltip();
        m_tipIndex = m_cellTipHover;
        m_tipText  = tip;
        m_cellTip->popup(tip, QCursor::pos(), m_darkMode, window()->frameGeometry());
    });

    m_focusRing = new FocusRing(this);
    m_focusRing->setRingColor(m_darkMode ? QColor(255, 255, 255) : QColor(0, 0, 0, 230));
    m_focusRing->hide();

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(new QWidget());
    auto* loadPage = new QWidget();
    auto* loadLayout = new QVBoxLayout(loadPage);
    loadLayout->setContentsMargins(0,0,0,0);
    loadLayout->setSpacing(12);
    loadLayout->addStretch(1);
    m_loadingRing = new FluentProgressRing(loadPage);
    m_loadingRing->setAccentPtr(&m_accent);
    loadLayout->addWidget(m_loadingRing, 0, Qt::AlignHCenter);
    m_loadingLabel = new QLabel("Discovering route...", loadPage);
    m_loadingLabel->setObjectName("loadingLabel");
    m_loadingLabel->setAlignment(Qt::AlignCenter);
    loadLayout->addWidget(m_loadingLabel, 0, Qt::AlignHCenter);
    loadLayout->addStretch(1);
    m_stack->addWidget(loadPage);

    m_table = new QTableWidget(0, COLUMNS.size() + 2, this);
    m_table->setObjectName("mtrTable");
    m_tableHeader = new PillHeaderView(Qt::Horizontal, m_table);
    m_table->setHorizontalHeader(m_tableHeader);
    m_tableHeader->setDark(m_darkMode);
    if (QWidget* headerViewport = m_tableHeader->viewport())
        headerViewport->installEventFilter(this);
    QStringList headers = COLUMNS;
    headers << QString() << QString();
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->moveSection(ColCount, 0);
    m_table->horizontalHeader()->setHighlightSections(false);
    m_table->setContextMenuPolicy(Qt::NoContextMenu);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->horizontalHeader()->setSectionResizeMode(ColHostname, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(ColIp, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setAlternatingRowColors(false);
    m_table->setShowGrid(false);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->verticalHeader()->setDefaultSectionSize(32);
    m_table->horizontalHeader()->setMinimumSectionSize(1);
    m_table->setColumnWidth(ColHop, 54);
    for (int c : {ColAsn, ColSent, ColRecv, ColBest, ColAvrg, ColWrst, ColLast, ColJttr})
        m_table->setColumnWidth(c, 70);
    m_table->setColumnWidth(ColLoss, 140);
    Q_ASSERT(COLUMNS.size() == ColCount);
    m_table->setColumnWidth(ColCount,     7);
    m_table->setColumnWidth(ColCount + 1, 7);

    // Align the Start/Stop button's left edge with the Loss column's bar at
    // the startup window width by trimming the target input. Real layouted
    // positions are measured, so fonts and DPI are accounted for. Columns
    // right of (and including) Loss are all fixed-width, so the bar's x is
    // derived from the right edge; the bar group is centred inside the cell
    // exactly as the item delegate lays it out.
    QTimer::singleShot(0, this, [this]() {
        const int lossCellX = width() - (140 + 7 * 70 + 7);
        const QFontMetrics fm(m_table->font());
        const int numCellW  = fm.horizontalAdvance(QStringLiteral("100"));
        const int groupW    = 80 + 8 + numCellW;            // track + gap + number
        const int barX      = lossCellX + (140 - groupW) / 2;
        const int btnX      = m_startStopBtn->mapTo(this, QPoint(0, 0)).x();
        const int newWidth  = m_targetEdit->width() - (btnX - barX);
        if (newWidth >= 200 && newWidth != m_targetEdit->width())
            m_targetEdit->setFixedWidth(newWidth);
    });

    for (int c = 0; c < COLUMNS.size(); ++c) {
        QString name = COLUMNS[c];
        QString unit;
        if      (name.endsWith(QStringLiteral(" %")))  { unit = QStringLiteral("%");  name.chop(2); }
        else if (name.endsWith(QStringLiteral(" ms"))) { unit = QStringLiteral("ms"); name.chop(3); }
        m_tableHeader->setColumn(c, name.trimmed(), unit);
    }

    m_tableDelegate = new MtrTableItemDelegate(&m_darkMode, m_table);
    m_tableDelegate->setAccentPtr(&m_accent);
    m_table->setItemDelegate(m_tableDelegate);
    if (QStyle* fusion = QStyleFactory::create("Fusion")) {
        fusion->setParent(m_table);
        m_table->setStyle(fusion);
    }
    m_table->viewport()->setMouseTracking(true);
    m_table->viewport()->installEventFilter(this);

    m_tableScroll = new OverlayScrollBar(m_table);
    m_tableScroll->setDark(m_darkMode);
    m_tableDelegate->setScrollDraggingPtr(m_tableScroll->draggingPtr());

    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        auto* item = m_table->item(row, col);
        if (item && !item->text().isEmpty() && item->text() != "-")
            copyTextToClipboard(item->text());
    });
    m_stack->addWidget(m_table);
    m_stack->setCurrentIndex(0);
    mainLayout->addWidget(m_stack);
}

// ==========================================================================
//  Theming
// ==========================================================================

// True if Windows is set to a dark apps theme (registry).
bool MainWindow::isWindowsDarkMode()
{
    DWORD value = 1, size = sizeof(value);
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_DWORD, nullptr, &value, &size);
    return value == 0;
}

// Apply the dark theme: stylesheet, accent colour, text palettes and themed
// sub-widgets, then refresh the Win11 chrome and the app icon.
void MainWindow::applyDarkTheme()
{
    m_darkMode = true;
    m_accent = ovSystemAccentShade(true);
    if (m_themeBtn) m_themeBtn->setText(QString(QChar(0xE793)));
    QString sheet = R"(
QMainWindow, QWidget { background-color: transparent; color: #ffffff; font-family: "Segoe UI"; font-size: 14px; }
#toolbar { background: transparent; border: none; }
#toolLabel { color: #ffffff; font-weight: normal; background: transparent; }
#inputAccent { background: transparent; }
#targetEdit { background: transparent; color: #ffffff; border: none; border-radius: 0px; padding: 0px 6px 0px 9px; selection-background-color: $ACCENT; selection-color: #ffffff; placeholder-text-color: rgba(255,255,255,0.36); }
#inputClear { font-family: "Segoe Fluent Icons"; font-size: 12px; color: rgba(255,255,255,0.60); background: transparent; border: none; border-radius: 4px; padding: 0px; text-align: center; }
#inputClear:hover { background: rgba(255,255,255,0.0605); color: rgba(255,255,255,0.90); }
#inputClear:pressed { background: rgba(255,255,255,0.0419); color: rgba(255,255,255,0.60); }
#pingSizeBox { background: transparent; color: #ffffff; border: none; border-radius: 0px; padding: 0px 6px 0px 9px; selection-background-color: $ACCENT; selection-color: #ffffff; }
#targetEdit:disabled, #pingSizeBox:disabled { color: rgba(255,255,255,0.36); }
#ipv6Check { color: #ffffff; background: transparent; }
#ipv6Check:hover { color: rgba(255,255,255,0.87); }
#ipv6Check:disabled { color: rgba(255,255,255,0.36); }
#ipv6Check::indicator { width: 18px; height: 18px; border: 1px solid #9e9e9e; border-radius: 4px; background: rgba(255,255,255,0.06); }
#ipv6Check::indicator:hover { border-color: #c7c7c7; background-color: #424242; }
#ipv6Check::indicator:pressed { border-color: #828282; background: rgba(255,255,255,0.03); }
#ipv6Check::indicator:checked { background-color: $ACCENT; border-color: $ACCENT; }
#ipv6Check::indicator:checked:hover { background-color: $ACC90; border-color: $ACC90; }
#ipv6Check::indicator:checked:pressed { background-color: $ACC80; border-color: $ACC80; }
#ipv6Check::indicator:disabled { border: 1px solid #434343; background: rgba(255,255,255,0.04); }
#ipv6Check::indicator:checked:disabled { background-color: #575757; border-color: #434343; }
#actionBtn, #themeBtn { background-color: rgba(255,255,255,0.06); color: #ffffff; border: 1px solid #3c3c3c; border-top-color: #414141; border-radius: 4px; padding: 0px 12px; outline: none; }
#themeBtn, #iconBtn { font-family: "Segoe Fluent Icons"; font-size: 16px; padding: 0px; }
#iconBtn { background-color: rgba(255,255,255,0.06); color: #ffffff; border: 1px solid #3c3c3c; border-top-color: #414141; border-radius: 4px; outline: none; }
#actionBtn:hover, #themeBtn:hover, #iconBtn:hover { background-color: rgba(255,255,255,0.08); border-color: #404040; border-top-color: #454545; }
#actionBtn:pressed, #themeBtn:pressed, #iconBtn:pressed { background-color: rgba(255,255,255,0.03); border-color: #343434; color: rgba(255,255,255,0.70); }
#actionBtn:disabled, #iconBtn:disabled { background-color: rgba(255,255,255,0.043); border-color: #393939; color: rgba(255,255,255,0.36); }
#actionBtn[variant="start"] { background-color: $ACCENT; color: #000000; border-color: $BRDSIDE; border-bottom-color: $BRDBOT; }
#actionBtn[variant="start"]:hover { background-color: $ACC90; border-bottom-color: $BRDBOT; }
#actionBtn[variant="start"]:pressed { background-color: $ACC80; color: rgba(0,0,0,0.50); border-color: $BRDPRESS; }
#actionBtn[variant="start"]:disabled { background-color: rgba(255,255,255,0.157); color: rgba(255,255,255,0.529); border-color: #434343; }
#actionBtn[variant="start"][tracing="true"] { background-color: #ff99a4; color: #000000; border-color: #ffa1ab; border-bottom-color: #dc848d; }
#actionBtn[variant="start"][tracing="true"]:hover { background-color: rgba(255,153,164,0.90); border-bottom-color: #dc848d; }
#actionBtn[variant="start"][tracing="true"]:pressed { background-color: rgba(255,153,164,0.80); color: rgba(0,0,0,0.50); border-color: #d2818a; }
#loadingLabel { color: rgba(255,255,255,0.773); background: transparent; }
#mtrTable { background-color: transparent; color: #ffffff; border: none; }
#mtrTable QHeaderView::section { background-color: transparent; color: rgba(255,255,255,0.60); font-weight: bold; border: none; border-bottom: 1px solid rgba(255,255,255,0.08); padding: 6px 8px; }
#mtrTable QHeaderView::section:hover { background-color: transparent; color: rgba(255,255,255,0.60); cursor: default; }
#mtrTable::item { padding: 4px 8px; }
QScrollBar:vertical { background: transparent; width: 8px; }
QScrollBar::handle:vertical { background: rgba(255,255,255,0.18); border-radius: 4px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: rgba(255,255,255,0.28); }
QScrollBar::handle:vertical:pressed { background: rgba(255,255,255,0.20); }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
)";
    sheet.replace("$ACCENT", m_accent.name());
    sheet.replace("$ACC90", ovAccentRgba(m_accent, 0.90));
    sheet.replace("$ACC80", ovAccentRgba(m_accent, 0.80));
    // Opaque start-button borders. Qt draws any border edge with alpha < 1
    // through its antialiased rounded-rect path, which shifts the button half a
    // pixel down and softens its edges, so every edge must be fully opaque. We
    // flatten the Fluent AccentControlElevationBorderBrush stops onto the accent
    // fill: the gradient reads as a light top/side hairline over a darker bottom
    // edge, so the colour is unchanged — only the alpha is baked out.
    //   top/sides = ControlStrokeColorOnAccentDefault   #14FFFFFF (20/255 white)
    //   bottom    = ControlStrokeColorOnAccentSecondary #23000000 (35/255 black, dark theme)
    sheet.replace("$BRDSIDE",  ovAccentBlend(QColor(255, 255, 255), m_accent, 20.0 / 255.0)); // top/side hairline
    sheet.replace("$BRDBOT",   ovAccentBlend(QColor(0, 0, 0),       m_accent, 35.0 / 255.0)); // bottom elevation
    sheet.replace("$BRDPRESS", ovAccentBlend(m_accent, QColor(32, 32, 32), 0.80));    // pressed fill twin, over dark Mica
    setStyleSheet(sheet);
    {
        QPalette p = m_targetEdit->palette();
        p.setColor(QPalette::Text, Qt::white);
        m_targetEdit->setPalette(p);
        m_pingSizeBox->setPalette(p);
    }
    for (auto* w : m_inputs)
        applyInputIdleStyle(w);
    m_table->viewport()->setStyleSheet("background-color: rgba(10,12,20,0.75);");
    if (m_tableHeader) m_tableHeader->setDark(true);
    if (m_tableScroll) m_tableScroll->setDark(true);
    if (m_ipv6Check)   m_ipv6Check->setDark(true);
    if (m_focusRing) m_focusRing->setRingColor(QColor(255, 255, 255));
    if (m_titleBar)  m_titleBar->setDark(true);
    applyWin11Chrome(true);
    updateAppIcon();
}

// Apply the light theme — mirror image of applyDarkTheme().
void MainWindow::applyLightTheme()
{
    m_darkMode = false;
    m_accent = ovSystemAccentShade(false);
    if (m_themeBtn) m_themeBtn->setText(QString(QChar(0xE793)));
    QString sheet = R"(
QMainWindow, QWidget { background-color: transparent; color: rgba(0,0,0,0.89); font-family: "Segoe UI"; font-size: 14px; }
#toolbar { background: transparent; border: none; }
#toolLabel { color: rgba(0,0,0,0.89); font-weight: normal; background: transparent; }
#inputAccent { background: transparent; }
#targetEdit { background: transparent; color: rgba(0,0,0,0.89); border: none; border-radius: 0px; padding: 0px 6px 0px 9px; selection-background-color: $ACCENT; selection-color: #ffffff; placeholder-text-color: rgba(0,0,0,0.36); }
#inputClear { font-family: "Segoe Fluent Icons"; font-size: 12px; color: rgba(0,0,0,0.60); background: transparent; border: none; border-radius: 4px; padding: 0px; text-align: center; }
#inputClear:hover { background: rgba(0,0,0,0.0373); color: rgba(0,0,0,0.90); }
#inputClear:pressed { background: rgba(0,0,0,0.0241); color: rgba(0,0,0,0.60); }
#pingSizeBox { background: transparent; color: rgba(0,0,0,0.89); border: none; border-radius: 0px; padding: 0px 6px 0px 9px; selection-background-color: $ACCENT; selection-color: #ffffff; }
#targetEdit:disabled, #pingSizeBox:disabled { color: rgba(0,0,0,0.36); }
#ipv6Check { color: rgba(0,0,0,0.89); background: transparent; }
#ipv6Check:hover { color: rgba(0,0,0,0.78); }
#ipv6Check:disabled { color: rgba(0,0,0,0.36); }
#ipv6Check::indicator { width: 18px; height: 18px; border: 1px solid #676767; border-radius: 4px; background: rgba(255,255,255,0.70); }
#ipv6Check::indicator:hover { border-color: #555555; background-color: #f3f3f3; }
#ipv6Check::indicator:pressed { border-color: #999999; background: rgba(235,235,235,1.0); }
#ipv6Check::indicator:checked { background-color: $ACCENT; border-color: $ACCENT; }
#ipv6Check::indicator:checked:hover { background-color: $ACC90; border-color: $ACC90; }
#ipv6Check::indicator:checked:pressed { background-color: $ACC80; border-color: $ACC80; }
#ipv6Check::indicator:disabled { border: 1px solid #bfbfbf; background: rgba(0,0,0,0.03); }
#ipv6Check::indicator:checked:disabled { background-color: #a6a6a6; border-color: #bfbfbf; }
#actionBtn, #themeBtn { background-color: rgba(255,255,255,0.85); color: rgba(0,0,0,0.89); border: 1px solid #ececec; border-bottom-color: #d3d3d3; border-radius: 4px; padding: 0px 12px; outline: none; }
#themeBtn, #iconBtn { font-family: "Segoe Fluent Icons"; font-size: 16px; padding: 0px; }
#iconBtn { background-color: rgba(255,255,255,0.85); color: rgba(0,0,0,0.89); border: 1px solid #ececec; border-bottom-color: #d3d3d3; border-radius: 4px; outline: none; }
#actionBtn:hover, #themeBtn:hover, #iconBtn:hover { background-color: rgba(243,243,243,0.95); border-color: #e8e8e8; border-bottom-color: #cecece; }
#actionBtn:pressed, #themeBtn:pressed, #iconBtn:pressed { background-color: rgba(235,235,235,1.0); border-color: #dfdfdf; color: rgba(0,0,0,0.60); }
#actionBtn:disabled, #iconBtn:disabled { background-color: rgba(249,249,249,0.302); border-color: #e7e7e7; color: rgba(0,0,0,0.36); }
#actionBtn[variant="start"] { background-color: $ACCENT; color: white; border-color: $BRDSIDE; border-bottom-color: $BRDBOT; }
#actionBtn[variant="start"]:hover { background-color: $ACC90; border-bottom-color: $BRDBOT; }
#actionBtn[variant="start"]:pressed { background-color: $ACC80; color: rgba(255,255,255,0.70); border-color: $BRDPRESS; }
#actionBtn[variant="start"]:disabled { background-color: rgba(0,0,0,0.216); color: #ffffff; border-color: #bebebe; }
#actionBtn[variant="start"][tracing="true"] { background-color: #c42b1c; border-color: #c93c2e; border-bottom-color: #761a11; }
#actionBtn[variant="start"][tracing="true"]:hover { background-color: rgba(196,43,28,0.90); border-bottom-color: #761a11; }
#actionBtn[variant="start"][tracing="true"]:pressed { background-color: rgba(196,43,28,0.80); border-color: #cd5347; }
#loadingLabel { color: rgba(0,0,0,0.62); background: transparent; }
#mtrTable { background-color: transparent; color: rgba(0,0,0,0.89); border: none; }
#mtrTable QHeaderView::section { background-color: transparent; color: rgba(0,0,0,0.55); font-weight: bold; border: none; border-bottom: 1px solid rgba(0,0,0,0.08); padding: 6px 8px; }
#mtrTable QHeaderView::section:hover { background-color: transparent; color: rgba(0,0,0,0.55); cursor: default; }
#mtrTable::item { padding: 4px 8px; }
QScrollBar:vertical { background: transparent; width: 8px; }
QScrollBar::handle:vertical { background: rgba(0,0,0,0.14); border-radius: 4px; min-height: 24px; }
QScrollBar::handle:vertical:hover { background: rgba(0,0,0,0.22); }
QScrollBar::handle:vertical:pressed { background: rgba(0,0,0,0.16); }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }
)";
    sheet.replace("$ACCENT", m_accent.name());
    sheet.replace("$ACC90", ovAccentRgba(m_accent, 0.90));
    sheet.replace("$ACC80", ovAccentRgba(m_accent, 0.80));
    // Opaque start-button borders. Qt draws any border edge with alpha < 1
    // through its antialiased rounded-rect path, which shifts the button half a
    // pixel down and softens its edges, so every edge must be fully opaque. We
    // flatten the Fluent AccentControlElevationBorderBrush stops onto the accent
    // fill: the gradient reads as a light top/side hairline over a darker bottom
    // edge, so the colour is unchanged — only the alpha is baked out.
    //   top/sides = ControlStrokeColorOnAccentDefault   #14FFFFFF (20/255 white)
    //   bottom    = ControlStrokeColorOnAccentSecondary #66000000 (102/255 black, light theme)
    sheet.replace("$BRDSIDE",  ovAccentBlend(QColor(255, 255, 255), m_accent, 20.0 / 255.0));  // top/side hairline
    sheet.replace("$BRDBOT",   ovAccentBlend(QColor(0, 0, 0),       m_accent, 102.0 / 255.0)); // bottom elevation
    sheet.replace("$BRDPRESS", ovAccentBlend(m_accent, QColor(243, 243, 243), 0.80)); // pressed fill twin, over light Mica
    setStyleSheet(sheet);
    {
        QPalette p = m_targetEdit->palette();
        p.setColor(QPalette::Text, QColor(0, 0, 0, 227));
        // Selected text inside the accent highlight: white at ~90 % alpha
        // reproduces the slightly muted shade Win11 renders in light theme
        // (the raw token is #FFFFFF; DirectWrite's rendering reads softer).
        p.setColor(QPalette::HighlightedText, QColor(255, 255, 255, 230));
        m_targetEdit->setPalette(p);
        m_pingSizeBox->setPalette(p);
    }
    for (auto* w : m_inputs)
        applyInputIdleStyle(w);
    m_table->viewport()->setStyleSheet("background-color: rgba(255,255,255,0.75);");
    if (m_tableHeader) m_tableHeader->setDark(false);
    if (m_tableScroll) m_tableScroll->setDark(false);
    if (m_ipv6Check)   m_ipv6Check->setDark(false);
    if (m_focusRing) m_focusRing->setRingColor(QColor(0, 0, 0, 230));
    if (m_titleBar)  m_titleBar->setDark(false);
    applyWin11Chrome(false);
    updateAppIcon();
}

// Flip between light and dark.
void MainWindow::onToggleTheme()
{
    if (m_darkMode) applyLightTheme();
    else            applyDarkTheme();
}

// Load the icon matching the current theme for the window and title bar.
void MainWindow::updateAppIcon()
{
    const QIcon icon(m_darkMode ? QStringLiteral(":/app_dark.ico")
                                : QStringLiteral(":/app_light.ico"));
    setWindowIcon(icon);
    if (m_titleBar) m_titleBar->setIcon(icon);
}

// Push the current theme to the DWM: immersive dark mode, rounded corners,
// extended frame and the Mica backdrop.
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

// ==========================================================================
//  Input-field styling
// ==========================================================================

// Style one input control for its state (idle / active / keyboard-focused),
// including showing or hiding the accent underline.
void MainWindow::updateInputStyle(QWidget* input, InputAccentBar* accent, bool active, bool keyboardFocused)
{
    if (!input) return;

    // Opaque WinUI stroke colours: each translucent token composited onto the
    // field background actually behind it (idle strokes over the idle fill,
    // active strokes over the active fill), so the border is crisp with no
    // alpha-channel rendering artefacts — the same trick as the accent button.
    const QString sideIdle    = m_darkMode ? "#303030" : "#eaecee";
    const QString sideActive  = m_darkMode ? "#303030" : "#f0f0f0";
    const QString bottomIdle  = m_darkMode ? "#9a9a9a" : "#868686";
    const QString hoverBg     = m_darkMode ? "#424242" : "#f3f3f3";
    const QString idleBg      = m_darkMode ? "#2d2d2d" : "#f9fbfd";
    const QString activeBg    = m_darkMode ? "#1e1e1e" : "#ffffff";
    // Keyboard focus is signalled solely by the bottom accent bar; the border
    // itself keeps its active colour, unchanged from the mouse-active state.
    const QString sideColor   = active ? sideActive : sideIdle;

    // QSpinBox renders its text through an internal child QLineEdit, which
    // also needs the "no border / transparent" override — otherwise it would
    // paint its own default frame on top of the outer control's border.
    const bool isSpinBox = (qobject_cast<QSpinBox*>(input) != nullptr);
    const QString sel = isSpinBox ? QStringLiteral("QSpinBox") : QStringLiteral("QLineEdit");
    const QString innerEditRule = isSpinBox
        ? (sel + QStringLiteral(" QLineEdit { border: none; background: transparent; padding: 0; }"))
        : QString();

    if (active) {
        input->setStyleSheet(
            sel + QStringLiteral(" { border: 1px solid ") + sideColor +
            QStringLiteral("; border-radius: 4px; background-color: ") + activeBg +
            QStringLiteral("; padding: 0px 6px 0px 9px; }") + innerEditRule);

        if (accent) {
            const int thickness = 2;
            accent->setGeometry(0, input->height() - thickness, input->width(), thickness);
            accent->setBar(input->width(), input->height(), thickness, 4.0, m_accent);
            accent->raise();
            accent->show();
        }
    } else {
        input->setStyleSheet(
            sel + QStringLiteral(" { border: 1px solid ") + sideColor +
            QStringLiteral("; border-bottom-color: ") + bottomIdle +
            QStringLiteral("; border-radius: 4px; background-color: ") + idleBg +
            QStringLiteral("; padding: 0px 6px 0px 9px; }") +
            sel + QStringLiteral(":hover { background-color: ") + hoverBg + QStringLiteral("; }") +
            innerEditRule);

        if (accent) accent->hide();
    }
}

// Reset an input to its idle look, or a dimmed look when disabled.
void MainWindow::applyInputIdleStyle(QWidget* input)
{
    if (!input) return;
    auto* accent = input->findChild<InputAccentBar*>("inputAccent");
    if (input->isEnabled()) {
        updateInputStyle(input, accent, false, false);
        return;
    }
    // WinUI disabled TextBox: border = ControlStrokeColorDefault, fill =
    // ControlFillColorDisabled, both composited onto the Mica base to stay
    // opaque (crisp, no alpha-channel artefacts).
    const QString disabledBorder = m_darkMode ? "#303030" : "#e5e5e5";
    const QString disabledFill   = m_darkMode ? "#2a2a2a" : "#f5f5f5";
    const bool isSpinBox = (qobject_cast<QSpinBox*>(input) != nullptr);
    const QString sel = isSpinBox ? QStringLiteral("QSpinBox") : QStringLiteral("QLineEdit");
    const QString innerEditRule = isSpinBox
        ? (sel + QStringLiteral(" QLineEdit { border: none; background: transparent; padding: 0; }"))
        : QString();
    input->setStyleSheet(
        sel + QStringLiteral(" { border: 1px solid ") + disabledBorder +
        QStringLiteral("; border-radius: 4px; background-color: ") + disabledFill +
        QStringLiteral("; padding: 0px 6px 0px 9px; }") + innerEditRule);
    if (accent) accent->hide();
}

// Enable/disable the target, IPv6 and ping-size inputs while a trace runs.
void MainWindow::setTracingInputsEnabled(bool enabled)
{
    m_targetEdit->setEnabled(enabled);
    m_ipv6Check->setEnabled(enabled);
    m_pingSizeBox->setEnabled(enabled);

    for (auto* w : std::initializer_list<QWidget*>{m_targetEdit, m_pingSizeBox})
        applyInputIdleStyle(w);
}

// ==========================================================================
//  Focus ring
// ==========================================================================

// True for the controls that should show the keyboard focus ring.
bool MainWindow::isFocusRingTarget(QWidget* w) const
{
    return w && (w == m_startStopBtn || w == m_copyBtn || w == m_exportBtn
              || w == m_themeBtn     || w == m_infoBtn  || w == m_ipv6Check);
}

// Show the focus ring around a focus-ring target widget.
void MainWindow::showFocusRing(QWidget* w)
{
    if (!m_focusRing || !isFocusRingTarget(w)) return;
    m_focusRing->setWidget(w);
    m_focusRing->raise();
    m_focusRing->show();
}

// Hide the focus ring.
void MainWindow::hideFocusRing()
{
    if (m_focusRing) m_focusRing->setWidget(nullptr);
}

// ==========================================================================
//  Frameless window & native Win32 events
// ==========================================================================

// Strip the standard caption while keeping a resizable frame, so we can draw
// our own title bar.
void MainWindow::applyFramelessStyle()
{
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) return;

    const LONG_PTR cur  = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
    const LONG_PTR want = (cur & ~static_cast<LONG_PTR>(WS_CAPTION))
                        |  static_cast<LONG_PTR>(WS_THICKFRAME | WS_SYSMENU |
                                                 WS_MINIMIZEBOX | WS_MAXIMIZEBOX);
    if (want == cur) return;

    ::SetWindowLongPtrW(hwnd, GWL_STYLE, want);
    ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                   SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE |
                   SWP_NOZORDER | SWP_NOACTIVATE);
}

// Which screen edge (if any) hosts an auto-hide taskbar on this window's
// monitor — used so a maximized window doesn't cover it.
static UINT autoHideTaskbarEdge(HWND hwnd)
{
    HMONITOR mon = ::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(MONITORINFO);
    if (!::GetMonitorInfoW(mon, &mi)) return static_cast<UINT>(-1);

    auto edgeHasBar = [&](UINT edge) -> bool {
        APPBARDATA abd{};
        abd.cbSize = sizeof(APPBARDATA);
        abd.uEdge  = edge;
        abd.rc     = mi.rcMonitor;
        return ::SHAppBarMessage(ABM_GETAUTOHIDEBAREX, &abd) != 0;
    };

    if (edgeHasBar(ABE_BOTTOM)) return ABE_BOTTOM;
    if (edgeHasBar(ABE_TOP))    return ABE_TOP;
    if (edgeHasBar(ABE_LEFT))   return ABE_LEFT;
    if (edgeHasBar(ABE_RIGHT))  return ABE_RIGHT;
    return static_cast<UINT>(-1);
}

// Intercept native Win32 messages to implement the frameless window: custom
// non-client sizing, hit-testing for our drawn caption/buttons, and reacting
// to system theme changes.
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (eventType == "windows_generic_MSG") {
        MSG* msg = static_cast<MSG*>(message);

        constexpr qintptr HTCLOSEBUTTON = 0xC10;

        switch (msg->message) {
        // WM_NCCALCSIZE: drop the standard frame (we draw our own). When maximized,
        // pull the client rect in by 1px on an auto-hide-taskbar edge so it stays reachable.
        case WM_NCCALCSIZE: {
            if (msg->wParam != TRUE) break;
            auto* p  = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
            RECT& rc = p->rgrc[0];

            if (::IsZoomed(msg->hwnd)) {
                const UINT edge = autoHideTaskbarEdge(msg->hwnd);
                if      (edge == ABE_TOP)    rc.top    += 1;
                else if (edge == ABE_BOTTOM) rc.bottom -= 1;
                else if (edge == ABE_LEFT)   rc.left   += 1;
                else if (edge == ABE_RIGHT)  rc.right  -= 1;
            }
            *result = 0;
            return true;
        }

        // WM_GETMINMAXINFO: clamp the maximized size to the monitor work area and
        // enforce our minimum size, converted to physical pixels.
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(msg->lParam);
            HMONITOR mon = ::MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{};
            mi.cbSize = sizeof(MONITORINFO);
            if (::GetMonitorInfoW(mon, &mi)) {
                const RECT& w = mi.rcWork;
                const RECT& m = mi.rcMonitor;
                mmi->ptMaxPosition.x = w.left - m.left;
                mmi->ptMaxPosition.y = w.top  - m.top;
                mmi->ptMaxSize.x     = w.right  - w.left;
                mmi->ptMaxSize.y     = w.bottom - w.top;
            }
            const qreal dpr = devicePixelRatioF() > 0 ? devicePixelRatioF() : 1.0;
            const QSize minLogical = minimumSize();
            mmi->ptMinTrackSize.x = qRound(minLogical.width()  * dpr);
            mmi->ptMinTrackSize.y = qRound(minLogical.height() * dpr);
            *result = 0;
            return true;
        }

        // WM_NCACTIVATE: keep the custom title bar looking active even when unfocused.
        case WM_NCACTIVATE:
            *result = ::DefWindowProcW(msg->hwnd, WM_NCACTIVATE, msg->wParam, -1);
            return true;

        // WM_NCHITTEST: classify the cursor for a frameless window — caption buttons,
        // the resize borders, the draggable caption, or plain client area.
        case WM_NCHITTEST: {
            const QPoint ptScreen(GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam));

            POINT clientOrigin{ 0, 0 };
            ::ClientToScreen(msg->hwnd, &clientOrigin);
            const qreal dpr = devicePixelRatioF() > 0 ? devicePixelRatioF() : 1.0;
            const QPoint pos(qRound((ptScreen.x() - clientOrigin.x) / dpr),
                             qRound((ptScreen.y() - clientOrigin.y) / dpr));

            auto rectInWindow = [this](QWidget* w) -> QRect {
                return w ? QRect(w->mapTo(this, QPoint(0, 0)), w->size()) : QRect();
            };

            if (m_titleBar) {
                const QRect minR   = rectInWindow(m_titleBar->minBtn());
                const QRect maxR   = rectInWindow(m_titleBar->maxBtn());
                const QRect closeR = rectInWindow(m_titleBar->closeBtn());

                if (maxR.contains(pos)) {
                    *result = HTMAXBUTTON;
                    return true;
                }
                if (minR.contains(pos)) {
                    *result = HTCLIENT;
                    return true;
                }
                if (!closeR.isNull() &&
                    pos.y() >= closeR.top() && pos.y() <= closeR.bottom() &&
                    pos.x() >= closeR.left()) {
                    *result = HTCLOSEBUTTON;
                    return true;
                }
            }

            if (!::IsZoomed(msg->hwnd)) {
                const int m  = 8;
                const int mR = 5;
                const int w = width();
                const int h = height();
                const bool L = pos.x() < m;
                const bool R = pos.x() >= w - mR;
                const bool T = pos.y() < m;
                const bool B = pos.y() >= h - m;
                if (T && L) { *result = HTTOPLEFT;     return true; }
                if (T && R) { *result = HTTOPRIGHT;    return true; }
                if (B && L) { *result = HTBOTTOMLEFT;  return true; }
                if (B && R) { *result = HTBOTTOMRIGHT; return true; }
                if (L)      { *result = HTLEFT;        return true; }
                if (R)      { *result = HTRIGHT;       return true; }
                if (T)      { *result = HTTOP;         return true; }
                if (B)      { *result = HTBOTTOM;      return true; }
            }

            if (m_titleBar && rectInWindow(m_titleBar).contains(pos)) {
                *result = HTCAPTION;
                return true;
            }

            *result = HTCLIENT;
            return true;
        }

        // WM_NCMOUSEMOVE: the max/close buttons live in the non-client area; mirror
        // hover onto them and request a leave notification.
        case WM_NCMOUSEMOVE: {
            const bool onMax   = (msg->wParam == HTMAXBUTTON);
            const bool onClose = (msg->wParam == HTCLOSEBUTTON);
            if (m_titleBar && m_titleBar->maxBtn()) {
                m_titleBar->maxBtn()->setHovered(onMax);
                if (!onMax) m_titleBar->maxBtn()->setPressed(false);
            }
            if (m_titleBar && m_titleBar->closeBtn()) {
                m_titleBar->closeBtn()->setHovered(onClose);
                if (!onClose) m_titleBar->closeBtn()->setPressed(false);
            }
            if (onMax || onClose) {
                TRACKMOUSEEVENT tme{};
                tme.cbSize    = sizeof(TRACKMOUSEEVENT);
                tme.dwFlags   = TME_LEAVE | TME_NONCLIENT;
                tme.hwndTrack = msg->hwnd;
                ::TrackMouseEvent(&tme);
            }
            if (onClose) {
                *result = 0;
                return true;
            }
            break;
        }

        // WM_NCMOUSELEAVE: clear hover/press on the max & close buttons.
        case WM_NCMOUSELEAVE:
            if (m_titleBar && m_titleBar->maxBtn()) {
                m_titleBar->maxBtn()->setHovered(false);
                m_titleBar->maxBtn()->setPressed(false);
            }
            if (m_titleBar && m_titleBar->closeBtn()) {
                m_titleBar->closeBtn()->setHovered(false);
                m_titleBar->closeBtn()->setPressed(false);
            }
            break;

        // WM_NCLBUTTONDOWN: show pressed state on the custom max/close buttons.
        case WM_NCLBUTTONDOWN:
            if (msg->wParam == HTMAXBUTTON && m_titleBar && m_titleBar->maxBtn()) {
                m_titleBar->maxBtn()->setPressed(true);
                *result = 0;
                return true;
            }
            if (msg->wParam == HTCLOSEBUTTON && m_titleBar && m_titleBar->closeBtn()) {
                m_titleBar->closeBtn()->setHovered(true);
                m_titleBar->closeBtn()->setPressed(true);
                *result = 0;
                return true;
            }
            break;

        // WM_NCLBUTTONUP: release over a custom max/close button fires its click.
        case WM_NCLBUTTONUP:
            if (msg->wParam == HTMAXBUTTON && m_titleBar && m_titleBar->maxBtn()) {
                m_titleBar->maxBtn()->setPressed(false);
                emit m_titleBar->maxBtn()->clicked();
                *result = 0;
                return true;
            }
            if (msg->wParam == HTCLOSEBUTTON && m_titleBar && m_titleBar->closeBtn()) {
                m_titleBar->closeBtn()->setPressed(false);
                emit m_titleBar->closeBtn()->clicked();
                *result = 0;
                return true;
            }
            break;

        // WM_SETTINGCHANGE: a system light/dark switch — re-apply the matching theme.
        case WM_SETTINGCHANGE:
            if (msg->lParam &&
                std::wstring(reinterpret_cast<LPCWSTR>(msg->lParam)) == L"ImmersiveColorSet") {
                const bool newDark = isWindowsDarkMode();
                if (newDark != m_darkMode) {
                    if (newDark) applyDarkTheme();
                    else         applyLightTheme();
                } else {
                    if (m_darkMode) applyDarkTheme();
                    else            applyLightTheme();
                }
            }
            break;
        default:
            break;
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}

// ==========================================================================
//  Qt event handling
// ==========================================================================

// Central event filter. In order: delayed tooltips for the theme/about icon
// buttons; tracking whether focus arrived via keyboard; table-viewport hover
// and focus clearing; focus-ring and input-underline styling on focus in/out;
// and Enter handling for inputs and buttons.
// Tooltip text for one results-table cell: the item's own tooltip (loss
// anomalies, multipath detail), preceded by the full text of an elided
// Hostname/IP cell. Shared by the hover handler and the live refresh.
QString MainWindow::cellTooltipText(const QModelIndex& idx) const
{
    if (!idx.isValid()) return QString();
    QString tip;
    if (auto* it = m_table->item(idx.row(), idx.column()))
        tip = it->toolTip();
    if (idx.column() == ColHostname || idx.column() == ColIp) {
        const QString full = idx.data(Qt::DisplayRole).toString();
        const QRect cell = m_table->visualRect(idx);
        // Mirror the delegate's text area exactly: 8 px padding on each side,
        // minus the space the multipath glyph takes when it is shown —
        // otherwise a cell elided because of the glyph would be missed.
        int avail = cell.width() - 16;
        if (idx.data(Qt::UserRole + 1).toBool())
            avail -= 18;
        if (m_table->fontMetrics().horizontalAdvance(full) > avail)
            tip = tip.isEmpty() ? full : full + QStringLiteral("\n") + tip;
    }
    return tip;
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    // Unified Fluent tooltips for the results table: any cell tooltip (loss
    // anomalies, multipath details) plus the full text of elided Hostname/IP
    // cells is shown through the same Win11Tooltip used by the caption
    // buttons, and the default QToolTip is suppressed.
    if (m_table && obj == m_table->viewport()) {
        // Cell tooltips use the same 600 ms show delay as the caption/icon
        // tooltips (rather than Qt's own ToolTip timing). The position is
        // captured once when the tooltip appears and is NOT updated while the
        // pointer keeps moving over the same cell — per WinUI, a tooltip does
        // not follow the pointer. Moving to a different cell restarts the
        // delay; leaving the table hides it immediately.
        if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            const QModelIndex idx = m_table->indexAt(me->pos());
            if (idx != m_cellTipHover) {
                m_cellTipHover = idx;
                m_cellTipTimer.stop();
                if (m_cellTip) { m_cellTip->hide(); m_tipIndex = QPersistentModelIndex(); }
                if (idx.isValid() && !cellTooltipText(idx).isEmpty())
                    m_cellTipTimer.start();   // 600 ms, single shot
            }
        }
        if (event->type() == QEvent::ToolTip)
            return true;   // suppress Qt's built-in tooltip entirely
        if (event->type() == QEvent::Leave) {
            m_cellTipTimer.stop();
            m_cellTipHover = QModelIndex();
            if (m_cellTip) { m_cellTip->hide(); m_tipIndex = QPersistentModelIndex(); }
        }
    }

    if (obj == m_themeBtn || obj == m_infoBtn) {
        auto* iconBtn = static_cast<QPushButton*>(obj);
        const QString tipText = (obj == m_themeBtn) ? QStringLiteral("Switch theme")
                                                     : QStringLiteral("About");
        switch (event->type()) {
        case QEvent::Enter:
            m_iconTipPending = iconBtn;
            m_iconTipText    = tipText;
            m_iconTipTimer.start();
            break;
        case QEvent::FocusIn: {
            const auto reason = static_cast<QFocusEvent*>(event)->reason();
            if (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason) {
                m_iconTipPending = iconBtn;
                m_iconTipText    = tipText;
                m_iconTipTimer.start();
            }
            break;
        }
        case QEvent::Leave:
        case QEvent::MouseButtonPress:
        case QEvent::FocusOut:
        case QEvent::Hide:
            hideIconTooltip();
            break;
        default:
            break;
        }
    }

    if (event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        m_keyboardFocus = (ke->key() == Qt::Key_Tab || ke->key() == Qt::Key_Backtab);
    }
    if (event->type() == QEvent::MouseButtonPress) {
        m_keyboardFocus = false;
        if (auto* w = qobject_cast<QWidget*>(obj); w && isFocusRingTarget(w)) {
            w->setProperty("focused", false);
            w->style()->polish(w);
            hideFocusRing();
        }
    }

    if (m_table && obj == m_table->viewport()) {
        if (event->type() == QEvent::MouseMove) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (m_tableDelegate) {
                const bool scrollDragging = m_tableScroll && m_tableScroll->isDragging();
                const bool inScrollZone   = m_tableScroll &&
                    me->pos().x() >= m_table->viewport()->width() - m_tableScroll->width();
                const int row = (scrollDragging || inScrollZone) ? -1 : m_table->indexAt(me->pos()).row();
                if (m_tableDelegate->hoveredRow() != row) {
                    m_tableDelegate->setHoveredRow(row);
                    m_table->viewport()->update();
                }
            }
        } else if (event->type() == QEvent::Leave) {
            if (m_tableDelegate && m_tableDelegate->hoveredRow() != -1) {
                m_tableDelegate->setHoveredRow(-1);
                m_table->viewport()->update();
            }
        } else if (event->type() == QEvent::MouseButtonPress) {
            m_keyboardFocus = false;
            hideFocusRing();
            if (auto* fw = QApplication::focusWidget()) fw->clearFocus();
            for (auto* w : std::initializer_list<QWidget*>{
                    m_startStopBtn, m_copyBtn, m_exportBtn, m_themeBtn, m_ipv6Check,
                    m_targetEdit, m_pingSizeBox}) {
                w->setProperty("focused", false);
                w->style()->polish(w);
            }
            for (auto* f : m_inputs)
                applyInputIdleStyle(f);
            QTimer::singleShot(0, this, [this]() {
                if (m_toolbar) m_toolbar->setFocus(Qt::NoFocusReason);
            });
        }
    }

    // Clicking the (non-focusable) column header should drop keyboard focus
    // the same way clicking the table body already does. Mouse events land on
    // the header's own internal viewport, same as m_table's.
    if (m_tableHeader && obj == m_tableHeader->viewport() && event->type() == QEvent::MouseButtonPress) {
        m_keyboardFocus = false;
        hideFocusRing();
        if (auto* fw = QApplication::focusWidget()) fw->clearFocus();
        for (auto* w : std::initializer_list<QWidget*>{
                m_startStopBtn, m_copyBtn, m_exportBtn, m_themeBtn, m_ipv6Check,
                m_targetEdit, m_pingSizeBox}) {
            w->setProperty("focused", false);
            w->style()->polish(w);
        }
        for (auto* f : m_inputs)
            applyInputIdleStyle(f);
        QTimer::singleShot(0, this, [this]() {
            if (m_toolbar) m_toolbar->setFocus(Qt::NoFocusReason);
        });
    }

    if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
        // Keep the target's clear button in sync with focus changes.
        if (obj == m_targetEdit && m_targetClearUpdate) m_targetClearUpdate();
        const Qt::FocusReason reason = (event->type() == QEvent::FocusIn)
            ? static_cast<QFocusEvent*>(event)->reason()
            : Qt::NoFocusReason;
        const bool keyboardFocus = (event->type() == QEvent::FocusIn) &&
            (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason) &&
            m_keyboardFocus;
        const bool focused = keyboardFocus;
        auto* w = qobject_cast<QWidget*>(obj);
        if (w) {
            w->setProperty("focused", focused);
            w->style()->polish(w);
            if (event->type() == QEvent::FocusIn) {
                if (keyboardFocus && isFocusRingTarget(w)) showFocusRing(w);
                else                                       hideFocusRing();
            } else if (isFocusRingTarget(w)) {
                hideFocusRing();
            }
            // Map the focused widget back to the input control that owns the
            // border/accent-bar styling — a spin box's real editor can be an
            // internal child QLineEdit rather than the QSpinBox itself.
            QWidget* input = nullptr;
            if (w == m_targetEdit || w == m_pingSizeBox) input = w;
            else if (w->parentWidget() == m_pingSizeBox) input = m_pingSizeBox;
            if (input) {
                const bool silentFocus = (event->type() == QEvent::FocusIn) &&
                    (reason == Qt::NoFocusReason);
                bool inputActive = (event->type() == QEvent::FocusIn) && !silentFocus;
                updateInputStyle(input, input->findChild<InputAccentBar*>("inputAccent"), inputActive, focused);
            }
        }
    }

    // Ctrl+C inside a text field. This static build's Qt clipboard write is
    // broken (hence the Win32 copyTextToClipboard used everywhere else), so
    // QLineEdit's own copy silently does nothing. Intercept it on the target box
    // and the spin box's internal editor and copy the selection via Win32.
    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->matches(QKeySequence::Copy)) {
            if (auto* le = qobject_cast<QLineEdit*>(obj)) {
                if (le == m_targetEdit || le->parent() == m_pingSizeBox) {
                    if (le->hasSelectedText())
                        copyTextToClipboard(le->selectedText());
                    return true;
                }
            }
        }
    }

    if (obj == m_targetEdit && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            && !m_startStopBtn->isEnabled())
            return true;
    }
    if (obj == m_pingSizeBox && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            onStartStop();
            return true;
        }
    }
    if (obj == m_ipv6Check && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            m_ipv6Check->toggle();
            return true;
        }
    }
    if (event->type() == QEvent::KeyPress) {
        auto* btn = qobject_cast<QPushButton*>(obj);
        if (btn && btn->isEnabled()) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                if (btn == m_startStopBtn)   onStartStop();
                else if (btn == m_copyBtn)   onCopy();
                else if (btn == m_exportBtn) onExport();
                else if (btn == m_themeBtn)  onToggleTheme();
                else if (btn == m_infoBtn)   m_infoBtn->click();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

// Clicking empty chrome drops keyboard focus and clears all focus styling.
void MainWindow::mousePressEvent(QMouseEvent* event)
{
    QWidget* clicked = childAt(event->pos());
    bool isInteractive = clicked && (clicked->inherits("QPushButton") || clicked->inherits("QCheckBox")
                                  || clicked->inherits("QLineEdit")   || clicked->inherits("QAbstractSpinBox"));
    if (!isInteractive) {
        m_keyboardFocus = false;
        hideFocusRing();
        if (auto* fw = QApplication::focusWidget()) fw->clearFocus();
        for (auto* w : std::initializer_list<QWidget*>{
                m_startStopBtn, m_copyBtn, m_exportBtn, m_themeBtn, m_ipv6Check,
                m_targetEdit, m_pingSizeBox}) {
            w->setProperty("focused", false);
            w->style()->polish(w);
        }
        for (auto* f : m_inputs)
            applyInputIdleStyle(f);
        QTimer::singleShot(0, this, [this]() {
            if (m_toolbar) m_toolbar->setFocus(Qt::NoFocusReason);
        });
    }
    QMainWindow::mousePressEvent(event);
}

// Re-layout the table contents while a trace is live.
void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (m_net && m_tracing) updateTable();
}

// Keep the frameless style and the maximize-button glyph in sync with the
// window state.
void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        applyFramelessStyle();
        if (m_titleBar) m_titleBar->setMaximized(isMaximized());
    }
}

// ==========================================================================
//  Icon tooltips
// ==========================================================================

// Show the shared tooltip under a toolbar icon button.
void MainWindow::showIconTooltip(QPushButton* btn, const QString& text)
{
    if (!btn || !btn->isVisible() || text.isEmpty()) return;
    sharedCaptionTooltip()->popup(text, QCursor::pos(), m_darkMode, window()->frameGeometry());
}

// Hide the shared icon tooltip and cancel any pending show.
void MainWindow::hideIconTooltip()
{
    m_iconTipTimer.stop();
    m_iconTipPending = nullptr;
    sharedCaptionTooltip()->hide();
}

// ==========================================================================
//  ASN lookup
// ==========================================================================

// Resolve an IP to its ASN via Team Cymru's DNS service. Skips private and
// link-local ranges. Blocking — must be called off the UI thread.
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
        auto resGuard = std::unique_ptr<addrinfo, decltype(&freeaddrinfo)>(res, freeaddrinfo);
        auto* sa6 = reinterpret_cast<sockaddr_in6*>(res->ai_addr);
        QString hex;
        for (int b = 0; b < 16; ++b)
            hex += QString("%1").arg(sa6->sin6_addr.s6_addr[b], 2, 16, QChar('0'));
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

// Cached ASN for an IP. Returns '-' immediately; on the first request it
// resolves in the background and fills the cache for next time.
QString MainWindow::getCachedASN(const QString& ip, bool ipv6) const
{
    auto key = ip.toStdString();
    auto it  = m_asnCache.find(key);
    if (it != m_asnCache.end()) return it->second.isEmpty() ? "-" : it->second;

    if (m_asnPending.insert(key).second) {
        QPointer<MainWindow> self(const_cast<MainWindow*>(this));
        std::thread([self, ip, ipv6, key]() {
            QString asn = lookupASN(ip, ipv6);
            QMetaObject::invokeMethod(qApp, [self, key, asn]() {
                if (!self) return;
                self->m_asnCache[key] = asn;
                self->m_asnPending.erase(key);
            }, Qt::QueuedConnection);
        }).detach();
    }
    return "-";
}

// ==========================================================================
//  Trace lifecycle
// ==========================================================================

// Start or stop tracing (toggle).
void MainWindow::onStartStop()
{
    if (m_tracing) {
        // Stop path: tear down without blocking the UI — ask the worker to stop,
        // reset the UI now, then poll a timer until the thread has finished.
        m_stopSource.request_stop();
        m_stopSource = std::stop_source{};
        // Freeze the duration for the report now, while m_counting still
        // reflects whether counting had actually started — m_elapsed itself
        // keeps ticking wall-clock time and would overcount a report built
        // well after this point.
        m_testDurationMs = m_counting ? m_elapsed.elapsed() : 0;
        m_tracing  = false;
        m_counting = false;
        m_refreshTimer->stop();
        m_elapsedTimer->stop();
        m_warmupTimer->stop();
        m_startStopBtn->setText("Start");
        m_startStopBtn->setProperty("tracing", false);
        m_startStopBtn->style()->polish(m_startStopBtn);
        setTracingInputsEnabled(true);
        // The title bar subtitle keeps the final elapsed time of the finished
        // test; it is cleared when a new test starts.
        if (m_net) updateTable();
        m_asnCache.clear();
        m_asnPending.clear();
        if (m_stack->currentIndex() == 1)
            m_stack->setCurrentIndex(0);

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
            if (!self->m_tracing) self->m_startStopBtn->setEnabled(!self->m_targetEdit->text().trimmed().isEmpty());
        });
        pollTimer->start();
        m_targetEdit->setFocus();
    } else {
        // Start path: resolve the target on a background thread (with v4/v6
        // fallback), then begin tracing and start the refresh / warm-up timers.
        if (!m_startStopBtn->isEnabled()) return;
        QString target = m_targetEdit->text().trimmed();
        if (target.isEmpty()) return;

        m_tracing = true;
        m_startStopBtn->setText("Stop");
        m_startStopBtn->setProperty("tracing", true);
        m_startStopBtn->style()->polish(m_startStopBtn);
        setTracingInputsEnabled(false);
        // Clear the finished test's final elapsed; the subtitle stays empty
        // until counting begins. The window title itself never changes.
        if (m_titleBar) m_titleBar->setSubtitle(QString());

        const int wantFamily = m_ipv6Check->isChecked() ? AF_INET6 : AF_INET;
        const bool darkMode  = m_darkMode;
        IOpenMTROptionsProvider* provider = this;
        QPointer<MainWindow> self(this);

        QTimer::singleShot(0, this, [self, provider, target, wantFamily, darkMode]() {
            if (!self) return;
            std::thread([self, provider, target, wantFamily, darkMode]() {
                auto net = std::make_shared<OpenMTRNetWrapper>(provider);

                struct addrinfo hints = {}, *res = nullptr;
                hints.ai_family = AF_UNSPEC;
                bool resolved = false;
                bool ipv6 = (wantFamily == AF_INET6);
                SOCKADDR_INET addr = {};

                if (getaddrinfo(target.toStdString().c_str(), nullptr, &hints, &res) == 0 && res) {
                    addrinfo* match = nullptr;
                    for (addrinfo* r = res; r; r = r->ai_next)
                        if (r->ai_family == wantFamily) { match = r; break; }
                    if (!match)
                        for (addrinfo* r = res; r; r = r->ai_next)
                            if (r->ai_family == AF_INET || r->ai_family == AF_INET6) { match = r; break; }
                    if (match) {
                        memcpy(&addr, match->ai_addr,
                            match->ai_addrlen < sizeof(addr) ? match->ai_addrlen : sizeof(addr));
                        ipv6 = (match->ai_family == AF_INET6);
                        resolved = true;
                    }
                    freeaddrinfo(res);
                }

                QMetaObject::invokeMethod(qApp, [self, net, target, addr, resolved, ipv6, darkMode]() {
                    if (!self) return;
                    if (!resolved) {
                        bool userStopped = !self->m_tracing;
                        self->m_tracing = false;
                        self->m_startStopBtn->setText("Start");
                        self->m_startStopBtn->setProperty("tracing", false);
                        self->m_startStopBtn->style()->polish(self->m_startStopBtn);
                        self->m_startStopBtn->setEnabled(!self->m_targetEdit->text().trimmed().isEmpty());
                        self->setTracingInputsEnabled(true);
                        if (!userStopped)
                            MicaDialog::show(self, "OpenMTR", QString("Could not resolve \"%1\".").arg(target), darkMode);
                        return;
                    }
                    if (!self->m_tracing) return;
                    self->m_ipv6Check->setChecked(ipv6);
                    self->m_net      = net;
                    self->m_counting = false;
                    self->m_testStartTime = QDateTime();
                    self->m_testDurationMs = 0;
                    self->m_table->setRowCount(0);
                    self->m_stack->setCurrentIndex(1);
                    self->m_copyBtn->setEnabled(false);
                    self->m_exportBtn->setEnabled(false);
                    self->m_net->DoTrace(self->m_stopSource.get_token(), addr);
                    self->m_refreshTimer->start();
                    self->m_elapsedTimer->start();
                    self->m_warmupTimer->start();
                    self->m_elapsed.start();
                    ++self->m_warmupGen;
                    self->m_warmupFingerprint.clear();
                    self->m_warmupStableCount = 0;
                }, Qt::QueuedConnection);
            }).detach();
        });
    }
}

// Periodic table refresh while tracing.
void MainWindow::onRefreshTimer()  { if (m_net && m_tracing) updateTable(); }

// Update the window title with elapsed time, once counting has started.
void MainWindow::onElapsedTimer()
{
    if (!m_tracing || !m_counting) return;
    // The elapsed time is transient state, not window identity, so it goes to
    // the title bar's subtitle only. The OS window title stays fixed at the app
    // name — updating it every second would churn the Alt+Tab entry, taskbar
    // tooltip and screen-reader announcements once a second.
    if (m_titleBar) m_titleBar->setSubtitle(formatDuration(m_elapsed.elapsed()));
}

// Warm-up state machine. Waits until the discovered route holds steady and
// every hop has had a fair chance to answer (or a deadline passes), kicks off
// ASN lookups, and once those settle it resets the engine statistics,
// switches to the live table and starts the real loss counting.
void MainWindow::onWarmupEnd()
{
    if (!m_net) return;
    constexpr qint64 kWarmupDeadlineMs = 12000;
    // The route fingerprint (hop count plus every hop's address) must hold
    // steady for this many 250 ms ticks before we dismiss the overlay.
    // Responding hops probe on a ~1 s cycle, so the window spans one full
    // cycle with margin; silent hops (5 s timeout cycles) are covered by the
    // per-hop guard below rather than by this window.
    constexpr int    kWarmupStableTicks = 5;
    const bool deadlineReached = m_elapsed.elapsed() >= kWarmupDeadlineMs;

    int maxHops   = m_net->GetMax();
    auto state    = m_net->getCurrentState();
    int checkHops = std::min(maxHops, (int)state.size());

    // Warm the ASN cache as addresses appear, so the HTTP lookups run
    // concurrently with route discovery and are typically resolved by the
    // time the table is revealed.
    for (const auto& h : state)
        if (h.addr.si_family != AF_UNSPEC)
            getCachedASN(QString::fromStdWString(addr_to_wstring(h.addr)),
                         h.addr.si_family == AF_INET6);

    const int gen = m_warmupGen;
    auto waitMore = [this, gen]() {
        QTimer::singleShot(250, this, [this, gen]() { if (m_warmupGen == gen) onWarmupEnd(); });
    };

    if (!deadlineReached) {
        // Fingerprint the discovered route: hop count plus every hop's
        // address. Requiring the whole fingerprint — not just the count — to
        // hold steady also catches middle hops that are still filling in
        // while the destination has already answered. An undiscovered route
        // (hop count pinned at the ceiling) needs no special case: the
        // fingerprint plus the per-hop guard below settle it as well, so
        // even an unreachable target gets a complete, stable reveal. The
        // deadline is only a backstop for routes that never stop changing.
        QByteArray fp;
        fp.append(static_cast<char>(checkHops));
        for (int i = 0; i < checkHops; ++i) {
            const auto& a = state[i].addr;
            if (a.si_family == AF_INET)
                fp.append(reinterpret_cast<const char*>(&a.Ipv4.sin_addr),
                          sizeof(a.Ipv4.sin_addr));
            else if (a.si_family == AF_INET6)
                fp.append(reinterpret_cast<const char*>(&a.Ipv6.sin6_addr),
                          sizeof(a.Ipv6.sin6_addr));
            else
                fp.append('\0');
        }
        // Any change to the route restarts the stability window.
        if (fp != m_warmupFingerprint) {
            m_warmupFingerprint = fp;
            m_warmupStableCount = 1;
            waitMore();
            return;
        }
        if (++m_warmupStableCount < kWarmupStableTicks) {
            waitMore();
            return;
        }
        // Every hop we are about to show must have either answered at least
        // once or sat through two full probe windows without answering.
        // xmit increments only after IcmpSendEcho2 returns, so for a silent
        // hop xmit >= 2 means two complete 5 s timeouts — it is almost
        // certainly a genuinely silent hop, not one whose first reply is
        // still in flight and would pop into the table after the reveal.
        for (int i = 0; i < checkHops; ++i) {
            if (state[i].returned == 0 && state[i].xmit < 2) { waitMore(); return; }
        }
    }

    QTimer::singleShot(400, this, [this, gen]() {
        if (!m_net || !m_tracing || m_warmupGen != gen) return;
        auto* pollAsn = new QTimer(this);
        pollAsn->setInterval(150);
        // Reverse-DNS progress trackers: highest resolved-name count seen so far
        // and how many ticks have passed with no new name. Lets us give fast PTR
        // records a moment to land without ever waiting on hops that have no PTR.
        auto dnsSeen  = std::make_shared<int>(-1);
        auto dnsStall = std::make_shared<int>(0);
        connect(pollAsn, &QTimer::timeout, this, [this, gen, pollAsn, dnsSeen, dnsStall]() {
            if (!m_net || !m_tracing || m_warmupGen != gen) {
                pollAsn->stop(); pollAsn->deleteLater(); return;
            }

            // Consider reverse-DNS "settled" when every addressed hop shows a
            // name other than its bare IP, or when no new name has appeared for
            // a few ticks (the remaining hops simply have no PTR record). This
            // never blocks on a result that isn't coming.
            auto snap = m_net->getCurrentState();
            int addressed = 0, named = 0;
            for (auto& h : snap) {
                if (h.addr.si_family == AF_UNSPEC) continue;
                ++addressed;
                if (h.getName() != addr_to_wstring(h.addr)) ++named;
            }
            bool dnsSettled;
            if (addressed == 0 || named >= addressed)      dnsSettled = true;
            else if (*dnsSeen < 0)      { *dnsSeen = named; *dnsStall = 0; dnsSettled = false; }
            else if (named > *dnsSeen)  { *dnsSeen = named; *dnsStall = 0; dnsSettled = false; }
            else                          dnsSettled = (++*dnsStall >= 3);   // ~450 ms without a new name

            if ((!m_asnPending.empty() || !dnsSettled) && m_elapsed.elapsed() < 16000) return;
            pollAsn->stop(); pollAsn->deleteLater();
            m_asnPending.clear();
            // Restart the engine's statistics so every displayed figure
            // (Loss/Sent/Recv and the RTT columns) describes the counting
            // window only, instead of mixing in warm-up probes. This also
            // arms the engine's parking of probes beyond the route edge.
            m_net->resetStats();
            m_counting = true;
            m_elapsed.restart();
            m_testStartTime = QDateTime::currentDateTime();
            // Build the table before the page flips: every row appears in one
            // paint with hostnames/IPs/ASNs filled and all statistics columns
            // showing their uniform initial "-" (counters were just reset, so
            // every hop has xmit == 0). Real values then replace the dashes
            // row by row as counting probes complete. Flipping the page first
            // would flash stale or empty content for a frame.
            updateTable();
            m_stack->setCurrentIndex(2);
            m_copyBtn->setEnabled(true);
            m_exportBtn->setEnabled(true);
        });
        pollAsn->start();
    });
}

// ==========================================================================
//  Results table & export
// ==========================================================================

// Rebuild the table rows from the latest engine snapshot. Statistics come
// straight from the engine — they are reset at reveal, so no baseline math
// is needed here.
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
        QString name    = QString::fromStdWString(h.getName());
        if (hasAddr && name.isEmpty()) name = ip;
        // Hops without an address show the engine's status text ("Request
        // timed out.", "Destination host unreachable.", ...) so active ICMP
        // refusals are visible instead of hiding behind a dash. Before the
        // first probe completes there is no status yet, hence the dash.
        if (!hasAddr && name.isEmpty()) name = QStringLiteral("-");

        auto setCell = [&](int col, const QString& text, Qt::Alignment align) {
            auto* item = m_table->item(i, col);
            if (!item) { item = new QTableWidgetItem(text); item->setTextAlignment(align); m_table->setItem(i, col, item); }
            else if (item->text() != text) item->setText(text);   // skip no-op writes
        };
        constexpr auto C = Qt::AlignCenter | Qt::AlignVCenter;

        setCell(ColHop, QString::number(i + 1), C);
        setCell(ColAsn, hasAddr ? getCachedASN(ip, h.addr.si_family == AF_INET6) : "-", C);
        setCell(ColHostname, name, C);
        setCell(ColIp, hasAddr ? ip : "-", C);

        // Rows showing an ICMP status instead of an address merge the
        // Hostname and IP cells so the text sits centred across both; the
        // delegate renders it in the muted shade via the UserRole flag. The
        // underlying IP cell keeps its "-" so text/CSV exports are unchanged.
        const bool errRow = !hasAddr && name != QLatin1String("-");
        if (auto* hostItem = m_table->item(i, ColHostname))
            hostItem->setData(Qt::UserRole, errRow);
        // Multipath / route-change marker for the delegate + Fluent tooltip.
        const bool multipath = hasAddr && h.altCount > 0;
        const QString mpTip = multipath
            ? QString("Also replies: %1 (%2\u00d7)")
                  .arg(QString::fromStdWString(addr_to_wstring(h.altAddr)))
                  .arg(h.altCount)
            : QString();
        for (int c : {(int)ColHostname, (int)ColIp}) {
            if (auto* it = m_table->item(i, c)) {
                it->setData(Qt::UserRole + 1, multipath);
                it->setToolTip(mpTip);
            }
        }

        const int wantSpan = errRow ? 2 : 1;
        if (m_table->columnSpan(i, ColHostname) != wantSpan)
            m_table->setSpan(i, ColHostname, 1, wantSpan);

        if (h.xmit == 0) {
            for (int c = ColLoss; c < ColCount; ++c) setCell(c, "-", C);
        } else {
            int loss = 100 - (100 * h.returned / h.xmit);

            setCell(ColLoss, QString::number(loss), C);
            // Diagnostic: hovering the Loss cell explains what every missing
            // reply actually was — a genuine timeout, or an anomalous
            // completion (a reply carrying a non-success ICMP status, or a
            // soft failure of the send call), with the most recent code.
            if (auto* lossItem = m_table->item(i, ColLoss)) {
                const int timeouts = h.xmit - h.returned - h.anomalyCount;
                QString tip;
                if (timeouts > 0)
                    tip = QString("%1\u00d7 timeout (no reply within 5 s)").arg(timeouts);
                if (h.anomalyCount > 0) {
                    if (!tip.isEmpty()) tip += ", ";
                    tip += QString("%1\u00d7 unexpected ICMP status/error (last: %2)")
                               .arg(h.anomalyCount).arg(h.anomalyLast);
                }
                lossItem->setData(Qt::ToolTipRole, tip.isEmpty() ? QVariant() : QVariant(tip));
            }
            setCell(ColSent, QString::number(h.xmit), C);
            setCell(ColRecv, QString::number(h.returned), C);
            setCell(ColBest, h.returned == 0 ? "-" : QString::number(h.best),      C);
            setCell(ColAvrg, h.returned == 0 ? "-" : QString::number(h.getAvg()),  C);
            setCell(ColWrst, h.returned == 0 ? "-" : QString::number(h.worst),     C);
            setCell(ColLast, h.returned == 0 ? "-" : QString::number(h.last),      C);
            setCell(ColJttr, h.returned < 2 ? "-" : QString::number(h.getJitter()), C);
        }
    }

    m_table->viewport()->update();

    // Live tooltip: if one is on screen, re-render it from the fresh data so
    // counters inside it (timeouts, anomalies, multipath) keep ticking.
    if (m_cellTip && m_cellTip->isVisible() && m_tipIndex.isValid()) {
        const QModelIndex idx = m_tipIndex;
        const QString tip = cellTooltipText(idx);
        if (tip.isEmpty()) {
            m_cellTip->hide();
            m_tipIndex = QPersistentModelIndex();
        } else if (tip != m_tipText) {
            m_tipText = tip;
            m_cellTip->updateText(tip);   // refresh content without moving
        }
    }
}

// The test's duration so far: live from m_elapsed while a test is actively
// counting, or the value frozen at Stop otherwise. Copy/Export are only
// enabled once counting has started at least once, so by the time either
// export builder below runs, one of these two is always meaningful.
qint64 MainWindow::currentTestDurationMs() const
{
    return (m_tracing && m_counting) ? m_elapsed.elapsed() : m_testDurationMs;
}

// Render the current table as a fixed-width ASCII box (for clipboard / .txt),
// sizing each column to its actual content.
// Machine-readable export: one object per hop, numbers as numbers, missing
// values ("-") as null. Merged error rows carry the ICMP status text in
// "hostname" and null in "ip", mirroring the on-screen table.
QString MainWindow::buildJsonExport() const
{
    static const QStringList keys = {
        "hop", "asn", "hostname", "ip", "loss", "sent", "recv",
        "best", "avrg", "wrst", "last", "jttr"
    };
    QJsonArray hops;
    for (int i = 0; i < m_table->rowCount(); ++i) {
        QJsonObject o;
        for (int c = 0; c < COLUMNS.size(); ++c) {
            auto* item = m_table->item(i, c);
            const QString v = item ? item->text() : QString();
            if (v.isEmpty() || v == QLatin1String("-")) {
                o[keys[c]] = QJsonValue::Null;
                continue;
            }
            bool numeric = false;
            const int n = v.toInt(&numeric);
            o[keys[c]] = numeric ? QJsonValue(n) : QJsonValue(v);
        }
        if (m_net) {
            const auto st = m_net->getCurrentState();
            if (i < static_cast<int>(st.size()) && st[i].altCount > 0) {
                o["alt_ip"]    = QString::fromStdWString(addr_to_wstring(st[i].altAddr));
                o["alt_count"] = st[i].altCount;
            }
        }
        hops.append(o);
    }
    QJsonObject root;
    root["target"]          = m_targetEdit->text().trimmed();
    // Wall-clock time the counting window began (falls back to "now" if
    // somehow queried before that, though Copy/Export stay disabled until
    // then in practice) and how long it has run — see currentTestDurationMs().
    root["test_started"]    = (m_testStartTime.isValid() ? m_testStartTime : QDateTime::currentDateTime()).toString(Qt::ISODate);
    root["duration_seconds"] = static_cast<qint64>(currentTestDurationMs() / 1000);
    root["generated"]       = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["hops"]            = hops;
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QString MainWindow::buildTextExport() const
{
    QString target = m_targetEdit->text().trimmed();
    const int NCOLS = static_cast<int>(COLUMNS.size());
    std::vector<int> W(NCOLS);
    for (int c = 0; c < NCOLS; ++c) {
        int w = static_cast<int>(COLUMNS[c].length());
        for (int i = 0; i < m_table->rowCount(); ++i) {
            auto* item = m_table->item(i, c);
            const int len = static_cast<int>((item ? item->text() : QStringLiteral("-")).length());
            if (len > w) w = len;
        }
        W[c] = w;
    }
    auto pad = [](const QString& s, int w) { return s.leftJustified(w, ' '); };
    QString sep = "+";
    for (int c = 0; c < NCOLS; ++c) sep += QString(W[c] + 2, '-') + "+";
    QString out;
    out += "OpenMTR Export\n";
    out += QString("Target  : %1\n").arg(target);
    out += QString("Date    : %1\n").arg((m_testStartTime.isValid() ? m_testStartTime : QDateTime::currentDateTime())
                                              .toString("yyyy-MM-dd hh:mm:ss"));
    out += QString("Duration: %1\n\n").arg(formatDuration(currentTestDurationMs()));
    out += sep + "\n";
    QString hdr = "|";
    for (int c = 0; c < NCOLS; ++c) hdr += " " + pad(COLUMNS[c], W[c]) + " |";
    out += hdr + "\n" + sep + "\n";
    for (int i = 0; i < m_table->rowCount(); ++i) {
        auto* hostItem = m_table->item(i, ColHostname);
        const bool errRow = hostItem && hostItem->data(Qt::UserRole).toBool();
        QString row = "|";
        for (int c = 0; c < NCOLS; ++c) {
            if (errRow && c == ColHostname) {
                // Error rows merge Hostname+IP into one left-aligned field
                // spanning the combined width of both columns.
                const int wSpan = W[ColHostname] + W[ColIp] + 3;
                row += " " + pad(hostItem->text(), wSpan) + " |";
                ++c;   // the IP column is consumed by the span
                continue;
            }
            auto* item = m_table->item(i, c);
            row += " " + pad(item ? item->text() : "-", W[c]) + " |";
        }
        out += row + "\n";
    }
    out += sep + "\n";
    // Anomalous probe completions (a reply carrying an uncounted ICMP status,
    // or a soft failure of the send call) are invisible in the table but
    // matter when diagnosing unexplained single-packet losses — list them.
    if (m_net) {
        QString notes;
        auto st = m_net->getCurrentState();
        for (int i = 0; i < static_cast<int>(st.size()); ++i)
            if (st[i].altCount > 0)
                notes += QString("  Hop %1: replies also arrived from %2 (%3 time(s)) \u2014 route change or per-packet load balancing\n")
                             .arg(i + 1)
                             .arg(QString::fromStdWString(addr_to_wstring(st[i].altAddr)))
                             .arg(st[i].altCount);
        for (int i = 0; i < static_cast<int>(st.size()); ++i)
            if (st[i].anomalyCount > 0)
                notes += QString("  Hop %1: %2 probe(s) ended with unexpected ICMP status/error %3\n")
                             .arg(i + 1).arg(st[i].anomalyCount).arg(st[i].anomalyLast);
        if (!notes.isEmpty())
            out += "\nNotes:\n" + notes;
    }
    return out;
}

// Copy the full text report to the clipboard.
void MainWindow::onCopy()
{
    if (!m_copyBtn->isEnabled())
        return;
    copyTextToClipboard(buildTextExport());
}

// Save the report via the native Save dialog, as .txt (ASCII box) or .csv.
void MainWindow::onExport()
{
    if (!m_exportBtn->isEnabled()) return;
    QString target = m_targetEdit->text().trimmed();
    QString stamp  = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString defaultName = QString("OpenMTR_%1_%2").arg(target.isEmpty() ? "export" : target, stamp);

    wchar_t fileBuf[MAX_PATH] = {};
    wcsncpy_s(fileBuf, defaultName.toStdWString().c_str(), _TRUNCATE);

    OPENFILENAMEW ofn = {};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = reinterpret_cast<HWND>(winId());
    ofn.lpstrFilter  = L"Text files (*.txt)\0*.txt\0CSV files (*.csv)\0*.csv\0"
                       L"JSON files (*.json)\0*.json\0All files (*.*)\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrFile    = fileBuf;
    ofn.nMaxFile     = static_cast<DWORD>(ARRAYSIZE(fileBuf));
    ofn.lpstrTitle   = L"Export results";
    ofn.Flags        = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameW(&ofn)) return;

    QString path = QString::fromWCharArray(fileBuf);
    if (!path.endsWith(QLatin1String(".txt"), Qt::CaseInsensitive) &&
        !path.endsWith(QLatin1String(".csv"), Qt::CaseInsensitive) &&
        !path.endsWith(QLatin1String(".json"), Qt::CaseInsensitive))
        path += (ofn.nFilterIndex == 2) ? QStringLiteral(".csv")
              : (ofn.nFilterIndex == 3) ? QStringLiteral(".json")
                                        : QStringLiteral(".txt");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        MicaDialog::show(this, "OpenMTR", QString("Could not write to \"%1\".").arg(path), m_darkMode);
        return;
    }
    QTextStream ts(&f);
    if (path.endsWith(".json", Qt::CaseInsensitive)) {
        ts << buildJsonExport();
    } else if (path.endsWith(".csv", Qt::CaseInsensitive)) {
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

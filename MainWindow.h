#pragma once

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QStackedWidget>
#include <QtCore/QTimer>
#include <QtCore/QElapsedTimer>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ws2ipdef.h>
#include <winreg.h>

#include "WinMTRNetWrapper.h"

#include <memory>
#include <algorithm>
#include <vector>
#include <array>
#include <stop_token>
#include <unordered_map>
#include <string>

// Options interface — implemented by MainWindow, consumed by WinMTRNetWrapper
struct IWinMTROptionsProvider {
    virtual ~IWinMTROptionsProvider() = default;
    [[nodiscard]] virtual unsigned getPingSize() const noexcept = 0;
    [[nodiscard]] virtual double   getInterval() const noexcept = 0;
    [[nodiscard]] virtual bool     getUseDNS()   const noexcept = 0;
};

class MainWindow : public QMainWindow, public IWinMTROptionsProvider
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] unsigned getPingSize() const noexcept override;
    [[nodiscard]] double   getInterval() const noexcept override;
    [[nodiscard]] bool     getUseDNS()   const noexcept override;

private slots:
    void onStartStop();
    void onCopy();
    void onExport();
    void onRefreshTimer();
    void onElapsedTimer();
    void onWarmupEnd();
    void onToggleTheme();

private:
    void setupUi();
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
    void applyDarkTheme();
    void applyLightTheme();
    void applyWin11Chrome(bool dark);
    void updateTable();
    QString buildTextExport() const;
    static bool    isWindowsDarkMode();
    static QString lookupASN(const QString& ip, bool ipv6);
    QString        getCachedASN(const QString& ip, bool ipv6) const;

    // UI
    QWidget*        m_toolbar      = nullptr;
    QLineEdit*      m_targetEdit   = nullptr;
    QSpinBox*       m_pingSizeBox  = nullptr;
    QCheckBox*      m_ipv6Check    = nullptr;
    QPushButton*    m_startStopBtn = nullptr;
    QPushButton*    m_copyBtn      = nullptr;
    QPushButton*    m_exportBtn    = nullptr;
    QPushButton*    m_themeBtn     = nullptr;
    QStackedWidget* m_stack        = nullptr;
    QLabel*         m_loadingLabel = nullptr;
    QTableWidget*   m_table        = nullptr;

    // State
    std::shared_ptr<WinMTRNetWrapper>    m_net;
    std::stop_source                     m_stopSource;
    std::vector<std::array<int, 2>>      m_baseline;
    bool    m_tracing  = false;
    bool    m_darkMode = true;
    bool    m_counting = false;
    QTimer* m_refreshTimer = nullptr;
    QTimer* m_elapsedTimer = nullptr;
    QTimer* m_warmupTimer  = nullptr;
    int     m_warmupGen    = 0;
    QElapsedTimer m_elapsed;
    mutable std::unordered_map<std::string, QString> m_asnCache;
};

#include "versioninfo.h"
#include "version.h"

#include <QtCore/QSysInfo>
#include <QtCore/qglobal.h>

QString openMtrVersionLine()
{
    return QString("%1 (%2) \u00b7 Qt %3")
        .arg(QStringLiteral(OPENMTR_VERSION))
        .arg(QSysInfo::buildCpuArchitecture().toUpper().replace("X86_64", "AMD64"))
        .arg(QStringLiteral(QT_VERSION_STR));
}

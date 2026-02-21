#include <QDebug>
#include "adbinstallworker.h"
#include "processutils.h"

AdbInstallWorker::AdbInstallWorker(const QString &apk, const QString &deviceId, QObject *parent)
    : QObject(parent), m_Apk(apk), m_DeviceId(deviceId)
{
}

void AdbInstallWorker::install()
{
    emit started();
#ifdef QT_DEBUG
    qDebug() << "正在安装" << m_Apk;
#endif
    const QString adb = ProcessUtils::adbExe();
    if (adb.isEmpty()) {
        emit installFailed(m_Apk);
        return;
    }
    QStringList args;
    args << "install";
    if (!m_DeviceId.isEmpty()) {
        args << "-s" << m_DeviceId;
    }
    args << "-r" << m_Apk;
    ProcessResult result = ProcessUtils::runCommand(adb, args);
#ifdef QT_DEBUG
    qDebug() << "ADB 返回代码" << result.code;
#endif
    if (result.code != 0) {
        emit installFailed(m_Apk);
        return;
    }
    emit installFinished(m_Apk);
    emit finished();
}

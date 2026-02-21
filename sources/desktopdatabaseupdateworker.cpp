#include <QDebug>
#include <QObject>
#include <QProcess>
#include "desktopdatabaseupdateworker.h"

DesktopDatabaseUpdateWorker::DesktopDatabaseUpdateWorker(const QString &applicationsDir, QObject *parent)
    : QObject(parent), m_ApplicationsDir(applicationsDir)
{
}

void DesktopDatabaseUpdateWorker::updateDatabase()
{
    emit started();
    
    // 检查系统中是否存在 update-desktop-database 命令
    QProcess checkProcess;
    checkProcess.start("which", QStringList() << "update-desktop-database");
    if (!checkProcess.waitForFinished(1000)) {
        // 命令不存在或执行超时，静默完成
        emit finished();
        return;
    }
    
    if (checkProcess.exitCode() != 0) {
        // 未找到 update-desktop-database 命令，静默完成
        emit finished();
        return;
    }
    
    // 执行 update-desktop-database 命令
    QProcess updateProcess;
    updateProcess.start("update-desktop-database", QStringList() << m_ApplicationsDir);
    
    if (!updateProcess.waitForFinished(10000)) { // 最多等待 10 秒
        emit error(QObject::tr("桌面数据库更新超时"));
        emit finished();
        return;
    }
    
    if (updateProcess.exitCode() != 0) {
        QString errorOutput = QString::fromUtf8(updateProcess.readAllStandardError());
        emit error(QObject::tr("桌面数据库更新失败：%1").arg(errorOutput));
    }
    
    emit finished();
}



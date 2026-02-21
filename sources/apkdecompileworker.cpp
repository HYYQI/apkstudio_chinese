#include <QDebug>
#include <QRegularExpression>
#include "apkdecompileworker.h"
#include "processutils.h"

ApkDecompileWorker::ApkDecompileWorker(const QString &apk, const QString &folder, const bool smali, const bool resources, const bool java, const QString &frameworkTag, const QString &extraArguments, QObject *parent)
    : QObject(parent), m_Apk(apk), m_Folder(folder), m_Java(java), m_Resources(resources), m_Smali(smali), m_FrameworkTag(frameworkTag), m_ExtraArguments(extraArguments)
{
}

void ApkDecompileWorker::decompile()
{
    emit started();
#ifdef QT_DEBUG
    qDebug() << "正在反编译" << m_Apk << "到目录" << m_Folder;
#endif
    const QString java = ProcessUtils::javaExe();
    const QString apktool = ProcessUtils::apktoolJar();
    if (java.isEmpty() || apktool.isEmpty()) {
        emit decompileFailed(m_Apk);
        return;
    }
    emit decompileProgress(25, tr("正在运行 apktool..."));
    QString heap("-Xmx%1m");
    heap = heap.arg(QString::number(ProcessUtils::javaHeapSize()));
    QStringList args;
    args << heap << "-jar" << apktool;
    args << "d";
    if (!m_Smali) {
        args << "-s";
    }
    if (!m_Resources) {
        args << "-r";
    }
    if (!m_FrameworkTag.isEmpty()) {
        args << "-t" << m_FrameworkTag;
    }
    // 解析并添加额外参数
    if (!m_ExtraArguments.isEmpty()) {
        QStringList extraArgs = m_ExtraArguments.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        args << extraArgs;
    }
    args << "-o" << m_Folder << m_Apk;
    ProcessResult result = ProcessUtils::runCommand(java, args);
#ifdef QT_DEBUG
    qDebug() << "Apktool 返回代码" << result.code;
#endif
    if (result.code != 0) {
        emit decompileFailed(m_Apk);
        return;
    }
    if (m_Java) {
        emit decompileProgress(75, tr("正在运行 jadx..."));
        const QString jadx = ProcessUtils::jadxExe();
        if (jadx.isEmpty()) {
            emit decompileFailed(m_Apk);
            return;
        }
        args.clear();
        args << "-r" << "-d" << m_Folder << m_Apk;
        result = ProcessUtils::runCommand(jadx, args, PROCESS_TIMEOUT_SECS);
#ifdef QT_DEBUG
        qDebug() << "Jadx 返回代码" << result.code;
#endif
    }
    emit decompileFinished(m_Apk, m_Folder);
    emit finished();
}

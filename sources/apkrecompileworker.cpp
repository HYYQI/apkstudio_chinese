#include <QDebug>
#include <QRegularExpression>
#include "apkrecompileworker.h"
#include "processutils.h"

ApkRecompileWorker::ApkRecompileWorker(const QString &folder, bool aapt2, const QString &extraArguments, QObject *parent)
    : QObject(parent), m_Aapt2(aapt2), m_Folder(folder), m_ExtraArguments(extraArguments)
{
}

void ApkRecompileWorker::recompile()
{
    emit started();
#ifdef QT_DEBUG
    qDebug() << "正在重新编译" << m_Folder;
#endif
    const QString java = ProcessUtils::javaExe();
    const QString apktool = ProcessUtils::apktoolJar();
    if (java.isEmpty() || apktool.isEmpty()) {
        emit recompileFailed(m_Folder);
        return;
    }
    QString heap("-Xmx%1m");
    heap = heap.arg(QString::number(ProcessUtils::javaHeapSize()));
    QStringList args;
    args << heap << "-jar" << apktool;
    args << "b" << m_Folder;
    // Apktool 2.12.1+ 默认使用 aapt2，仅在需要使用 aapt1 时指定 --use-aapt1
    if (!m_Aapt2) {
        args << "--use-aapt1";
    }
    // 解析并添加额外参数
    if (!m_ExtraArguments.isEmpty()) {
        QStringList extraArgs = m_ExtraArguments.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        args << extraArgs;
    }
    ProcessResult result = ProcessUtils::runCommand(java, args);
#ifdef QT_DEBUG
    qDebug() << "Apktool 返回代码" << result.code;
#endif
    if (result.code != 0) {
        emit recompileFailed(m_Folder);
        return;
    }
    emit recompileFinished(m_Folder);
    emit finished();
}

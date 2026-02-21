#include <QDebug>
#include <QRegularExpression>
#include "versionresolveworker.h"
#include "processutils.h"

#define REGEXP_ADB_VERSION "version (\\d+\\.\\d+\\.\\d+)$"
#define REGEXP_JAVA_VERSION "version \"([\\d._]+)\""
#define REGEXP_UAS_VERSION "Version: (\\d+\\.\\d+\\.\\d+)$"

VersionResolveWorker::VersionResolveWorker(QObject *parent)
    : QObject(parent)
{
}

void VersionResolveWorker::resolve()
{
    emit started();
#ifdef QT_DEBUG
    qDebug() << "使用来自" << ProcessUtils::javaExe() << "的 'java'";
#endif
    bool found = false;
    const QString java = ProcessUtils::javaExe();
    if (!java.isEmpty()) {
        ProcessResult result = ProcessUtils::runCommand(java, QStringList() << "-version");
#ifdef QT_DEBUG
        qDebug() << "Java 返回代码" << result.code;
#endif
        if ((result.code == 0) && !result.output.isEmpty()) {
#ifdef QT_DEBUG
            qDebug() << "Java 返回" << result.output[0];
#endif
            QRegularExpression regexp(REGEXP_JAVA_VERSION);
            QRegularExpressionMatch match = regexp.match(result.output[0]);
            if (match.hasMatch()) {
                emit versionResolved("java", match.captured(1));
                found = true;
            }
        }
    }
    if (!found) {
        emit versionResolved("java", QString());
    }
#ifdef QT_DEBUG
    qDebug() << "使用来自" << ProcessUtils::apktoolJar() << "的 'apktool'";
#endif
    found = false;
    const QString apktool = ProcessUtils::apktoolJar();
    if (!java.isEmpty() && !apktool.isEmpty()) {
        QStringList args;
        args << "-jar" << apktool;
        args << "--version";
        ProcessResult result = ProcessUtils::runCommand(java, args);
#ifdef QT_DEBUG
        qDebug() << "Apktool 返回代码" << result.code;
#endif
        if ((result.code == 0) && !result.output.isEmpty()) {
#ifdef QT_DEBUG
            qDebug() << "Apktool 返回" << result.output.first();
#endif
            emit versionResolved("apktool", result.output.first().trimmed());
            found = true;
        }
    }
    if (!found) {
        emit versionResolved("apktool", QString());
    }
#ifdef QT_DEBUG
    qDebug() << "使用来自" << ProcessUtils::jadxExe() << "的 'jadx'";
#endif
    found = false;
    const QString jadx = ProcessUtils::jadxExe();
    if (!jadx.isEmpty()) {
#ifdef QT_DEBUG
        qDebug() << "尝试从以下位置获取 JADX 版本：" << jadx;
#endif
        ProcessResult result = ProcessUtils::runCommand(jadx, QStringList() << "--version");
#ifdef QT_DEBUG
        qDebug() << "Jadx 返回代码" << result.code;
        qDebug() << "Jadx 输出行数：" << result.output.size();
        for (int i = 0; i < result.output.size(); ++i) {
            qDebug() << "  输出[" << i << "]：" << result.output[i];
        }
        qDebug() << "Jadx 错误行数：" << result.error.size();
        for (int i = 0; i < result.error.size(); ++i) {
            qDebug() << "  错误[" << i << "]：" << result.error[i];
        }
#endif
        if ((result.code == 0) && !result.output.isEmpty()) {
#ifdef QT_DEBUG
            qDebug() << "Jadx 返回" << result.output.first();
#endif
            emit versionResolved("jadx", result.output.first().trimmed());
            found = true;
        } else {
#ifdef QT_DEBUG
            qDebug() << "Jadx 版本检查失败 - 代码：" << result.code << "输出为空：" << result.output.isEmpty();
#endif
        }
    } else {
#ifdef QT_DEBUG
        qDebug() << "JADX 可执行文件路径为空";
#endif
    }
    if (!found) {
        emit versionResolved("jadx", QString());
    }
#ifdef QT_DEBUG
    qDebug() << "使用来自" << ProcessUtils::adbExe() << "的 'adb'";
#endif
    found = false;
    const QString adb = ProcessUtils::adbExe();
    if (!adb.isEmpty()) {
        ProcessResult result = ProcessUtils::runCommand(adb, QStringList() << "--version");
#ifdef QT_DEBUG
        qDebug() << "ADB 返回代码" << result.code;
#endif
        if ((result.code == 0) && !result.output.isEmpty()) {
#ifdef QT_DEBUG
            qDebug() << "ADB 返回" << result.output.first();
#endif
            QRegularExpression regexp(REGEXP_ADB_VERSION);
            QRegularExpressionMatch match = regexp.match(result.output.first());
            if (match.hasMatch()) {
                emit versionResolved("adb", match.captured(1));
                found = true;
            }
        }
    }
    if (!found) {
        emit versionResolved("adb", QString());
    }
#ifdef QT_DEBUG
    qDebug() << "使用来自" << ProcessUtils::uberApkSignerJar() << "的 'uas'";
#endif
    found = false;
    const QString uas = ProcessUtils::uberApkSignerJar();
    if (!java.isEmpty() && !uas.isEmpty()) {
        QStringList args;
        args << "-jar" << uas;
        args << "--version";
        ProcessResult result = ProcessUtils::runCommand(java, args);
#ifdef QT_DEBUG
        qDebug() << "Uber APK signer 返回代码" << result.code;
#endif
        if ((result.code == 0) && !result.output.isEmpty()) {
#ifdef QT_DEBUG
            qDebug() << "Uber APK signer 返回" << result.output.first();
#endif
            QRegularExpression regexp(REGEXP_UAS_VERSION);
            QRegularExpressionMatch match = regexp.match(result.output.first());
            if (match.hasMatch()) {
                emit versionResolved("uas", match.captured(1));
                found = true;
            }
        }
        if (!found) {
            emit versionResolved("uas", QString());
        }
    } else {
        emit versionResolved("uas", QString());
    }
    emit finished();
}

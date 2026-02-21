#include <QDebug>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSettings>
#include "processutils.h"

#define REGEXP_CRLF "[\\r\\n]"

ProcessOutput* ProcessOutput::m_Self = nullptr;

void ProcessOutput::emitCommandFinished(const ProcessResult &result)
{
    emit commandFinished(result);
}

void ProcessOutput::emitCommandStarting(const QString &exe, const QStringList &args)
{
    emit commandStarting(exe, args);
}

ProcessOutput *ProcessOutput::instance()
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    qRegisterMetaType<ProcessResult>("ProcessResult");
#else
    qRegisterMetaType<ProcessResult>();
#endif
    if (!m_Self) {
        m_Self = new ProcessOutput();
    }
    return m_Self;
}

QString ProcessUtils::adbExe()
{
    QSettings settings;
    QString exe = settings.value("adb_exe").toString();
    if (!exe.isEmpty() && QFile::exists(exe)) {
        return exe;
    }
    QString name("adb");
#ifdef Q_OS_WIN
    name.append(".exe");
#endif
    return findInPath(name);
}

QString ProcessUtils::apktoolJar()
{
    QSettings settings;
    const QString jar = settings.value("apktool_jar").toString();
    return (!jar.isEmpty() && QFile::exists(jar)) ? jar : QString();
}

QString ProcessUtils::findInPath(const QString &exe)
{
    auto result = runCommand(
#ifdef Q_OS_WIN
                "where",
#else
                "which",
#endif
                QStringList(exe));
    if ((result.code == 0) && (result.output.count() >= 1)) {
        auto location = result.output.first();
#ifdef QT_DEBUG
        qDebug() << exe << "找到于" << location;
#endif
        return location;
    }
    return QString();
}

QString ProcessUtils::jadxExe()
{
    QSettings settings;
    QString exe = settings.value("jadx_exe").toString();
    return (!exe.isEmpty() && QFile::exists(exe)) ? exe : QString();
}

QString ProcessUtils::javaExe()
{
    QSettings settings;
    QString exe;
    exe = settings.value("java_exe").toString();
    if (!exe.isEmpty() && QFile::exists(exe)) {
        return exe;
    }
    QString name("java");
#ifdef Q_OS_WIN
    name.append(".exe");
#endif
    return findInPath(name);
}

int ProcessUtils::javaHeapSize()
{
    QSettings settings;
    return settings.value("java_heap", 256).toInt();
}

ProcessResult ProcessUtils::runCommand(const QString &exe, const QStringList &args, const int timeout)
{
#ifdef QT_DEBUG
    qDebug() << "正在运行" << exe << args;
#endif
    ProcessOutput::instance()->emitCommandStarting(exe, args);
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    
#ifdef Q_OS_WIN
    // 在 Windows 上，.bat 和 .cmd 文件需要通过 cmd.exe 执行
    QString actualExe = exe;
    QStringList actualArgs = args;
    QString workingDir;
    
    if (exe.endsWith(".bat", Qt::CaseInsensitive) || exe.endsWith(".cmd", Qt::CaseInsensitive)) {
        // 使用 cmd.exe /c 包装 .bat/.cmd 文件
        // 语法：cmd.exe /c "path\to\file.bat" arg1 arg2
        // 同时将工作目录设置为批处理文件所在目录，以便它能找到相对路径
        QFileInfo fileInfo(exe);
        workingDir = fileInfo.absolutePath();
        actualExe = "cmd.exe";
        // 正确的顺序：先 /c，然后是批处理文件路径，最后是参数
        // QProcess 会自动处理引号，所以不要手动添加引号
        actualArgs.prepend(exe);  // 将批处理文件路径放在前面（QProcess 会处理引号）
        actualArgs.prepend("/c"); // 然后在批处理文件前加 /c
#ifdef QT_DEBUG
        qDebug() << "使用 cmd.exe 包装 .bat/.cmd 文件：" << actualExe << actualArgs;
        qDebug() << "设置工作目录为：" << workingDir;
#endif
    }
    
    if (!workingDir.isEmpty()) {
        process.setWorkingDirectory(workingDir);
    }
    
    // 当 Java 位置已知时为所有命令设置 JAVA_HOME
    // 但跳过搜索 java 本身的情况（which/where 命令）
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString javaPath;
    if (actualExe != "which" && actualExe != "where") {
        javaPath = ProcessUtils::javaExe(); // 从设置获取 Java 路径
    }
    if (!javaPath.isEmpty()) {
        QFileInfo javaInfo(javaPath);
        QString javaHome = javaInfo.absolutePath();
        // 从 bin 目录上移一级获取 JAVA_HOME
        if (javaHome.endsWith("/bin") || javaHome.endsWith("\\bin")) {
            QDir javaDir(javaHome);
            javaDir.cdUp();
            javaHome = javaDir.absolutePath();
        }
        env.insert("JAVA_HOME", javaHome);
        // 同时将 java bin 添加到 PATH
        QString currentPath = env.value("PATH");
        QString javaBinPath = javaInfo.absolutePath();
        if (!currentPath.contains(javaBinPath, Qt::CaseInsensitive)) {
            env.insert("PATH", javaBinPath + ";" + currentPath);
        }
#ifdef QT_DEBUG
        qDebug() << "设置 JAVA_HOME 为：" << javaHome;
        qDebug() << "添加到 PATH：" << javaBinPath;
#endif
    }
    process.setProcessEnvironment(env);
    
    process.start(actualExe, actualArgs, QIODevice::ReadOnly);
#else
    // 当 Java 位置已知时为所有命令设置 JAVA_HOME
    // 但跳过搜索 java 本身的情况（which/where 命令）
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString javaPath;
    if (exe != "which" && exe != "where") {
        javaPath = ProcessUtils::javaExe(); // 从设置获取 Java 路径
    }
    if (!javaPath.isEmpty()) {
        QFileInfo javaInfo(javaPath);
        QString javaHome = javaInfo.absolutePath();
        // 从 bin 目录上移一级获取 JAVA_HOME
        if (javaHome.endsWith("/bin")) {
            QDir javaDir(javaHome);
            javaDir.cdUp();
            javaHome = javaDir.absolutePath();
        }
        env.insert("JAVA_HOME", javaHome);
        // 同时将 java bin 添加到 PATH
        QString currentPath = env.value("PATH");
        QString javaBinPath = javaInfo.absolutePath();
        if (!currentPath.contains(javaBinPath, Qt::CaseInsensitive)) {
            env.insert("PATH", javaBinPath + ":" + currentPath);
        }
#ifdef QT_DEBUG
        qDebug() << "设置 JAVA_HOME 为：" << javaHome;
        qDebug() << "添加到 PATH：" << javaBinPath;
#endif
    }
    process.setProcessEnvironment(env);
    
    process.start(exe, args, QIODevice::ReadOnly);
#endif
    
    ProcessResult result;
    if (process.waitForStarted(timeout * 1000)) {
        if (!process.waitForFinished(timeout * 1000)) {
            process.kill();
        }
        result.code = process.exitCode();
        QString error(process.readAllStandardError());
        QString output(process.readAllStandardOutput());
        QRegularExpression crlf(REGEXP_CRLF);
        result.error = error.split(crlf, Qt::SkipEmptyParts);
        result.output = output.split(crlf, Qt::SkipEmptyParts);
    } else {
        result.code = -1;
    }
    ProcessOutput::instance()->emitCommandFinished(result);
    return result;
}

QString ProcessUtils::uberApkSignerJar()
{
    QSettings settings;
    const QString jar = settings.value("uas_jar").toString();
    return (!jar.isEmpty() && QFile::exists(jar)) ? jar : QString();
}

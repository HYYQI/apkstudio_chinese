#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include "tooldownloadworker.h"

#ifdef Q_OS_WIN
// Windows 下的解压缩通过 PowerShell 或 7-Zip 处理
#else
#include <sys/stat.h>
#endif

ToolDownloadWorker::ToolDownloadWorker(ToolType tool, QObject *parent)
    : QObject(parent), m_Tool(tool), m_NetworkManager(nullptr), m_NetworkReply(nullptr), m_DownloadFile(nullptr)
{
}

ToolDownloadWorker::~ToolDownloadWorker()
{
    abort();
}

void ToolDownloadWorker::abort()
{
    if (m_NetworkReply) {
        m_NetworkReply->abort();
        m_NetworkReply->deleteLater();
        m_NetworkReply = nullptr;
    }
    if (m_DownloadFile) {
        if (m_DownloadFile->isOpen()) {
            m_DownloadFile->close();
        }
        m_DownloadFile->deleteLater();
        m_DownloadFile = nullptr;
    }
    // 注意：m_NetworkManager 是此对象的子对象，会自动删除
}

void ToolDownloadWorker::download()
{
    emit started();
    
    QString downloadUrl = getDownloadUrl();
    if (downloadUrl.isEmpty()) {
        emit failed(tr("无法确定此工具的下载 URL"));
        return;
    }

    emit progress(0, tr("准备下载..."));

    // 创建下载目录
    QString downloadDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/apkstudio_downloads";
    QDir().mkpath(downloadDir);

    QString fileName = QUrl(downloadUrl).fileName();
    if (fileName.isEmpty()) {
        fileName = QString("tool_%1").arg(static_cast<int>(m_Tool));
    }
    QString filePath = downloadDir + "/" + fileName;

    emit progress(5, tr("正在下载 %1...").arg(fileName));

    // 创建网络管理器和文件作为成员变量以保持它们存活
    m_NetworkManager = new QNetworkAccessManager(this);
    m_DownloadFile = new QFile(filePath, this);
    
    if (!m_DownloadFile->open(QIODevice::WriteOnly)) {
        emit failed(tr("无法创建下载文件：%1").arg(m_DownloadFile->errorString()));
        m_DownloadFile->deleteLater();
        m_DownloadFile = nullptr;
        return;
    }

    // 下载文件
    QNetworkRequest request;
    request.setUrl(QUrl(downloadUrl));
    request.setRawHeader("User-Agent", "APK Studio");
    m_NetworkReply = m_NetworkManager->get(request);

    QObject::connect(m_NetworkReply, &QNetworkReply::downloadProgress, this, [this, fileName](qint64 bytesReceived, qint64 bytesTotal) {
        if (bytesTotal > 0) {
            int percentage = 5 + (bytesReceived * 70 / bytesTotal); // 5-75% 用于下载
            double receivedMB = bytesReceived / 1024.0 / 1024.0;
            double totalMB = bytesTotal / 1024.0 / 1024.0;
            QString progressStr = tr("已下载 %1 MB / %2 MB")
                .arg(receivedMB, 0, 'f', 2)
                .arg(totalMB, 0, 'f', 2);
            emit progress(percentage, progressStr);
        }
    }, Qt::QueuedConnection);

    QObject::connect(m_NetworkReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_DownloadFile && m_DownloadFile->isOpen()) {
            m_DownloadFile->write(m_NetworkReply->readAll());
        }
    }, Qt::QueuedConnection);

    QObject::connect(m_NetworkReply, &QNetworkReply::finished, this, [this, filePath, fileName]() {
        if (!m_DownloadFile) {
            return;
        }
        
        m_DownloadFile->close();
        
        if (m_NetworkReply->error() != QNetworkReply::NoError) {
            m_DownloadFile->remove();
            emit failed(tr("下载失败：%1").arg(m_NetworkReply->errorString()));
            m_NetworkReply->deleteLater();
            m_NetworkReply = nullptr;
            m_DownloadFile->deleteLater();
            m_DownloadFile = nullptr;
            return;
        }

        // 写入任何剩余数据
        if (m_NetworkReply->bytesAvailable() > 0) {
            if (m_DownloadFile->open(QIODevice::Append)) {
                m_DownloadFile->write(m_NetworkReply->readAll());
                m_DownloadFile->close();
            }
        }

        // 将文件解压缩或复制到目标位置
        QString extractPath = getExtractPath();
        if (extractPath.isEmpty()) {
            m_DownloadFile->remove();
            emit failed(tr("无法确定解压路径"));
            m_NetworkReply->deleteLater();
            m_NetworkReply = nullptr;
            m_DownloadFile->deleteLater();
            m_DownloadFile = nullptr;
            return;
        }

        QDir().mkpath(extractPath);

        bool extracted = false;
        if (fileName.endsWith(".zip", Qt::CaseInsensitive)) {
            emit progress(75, tr("正在解压 %1...").arg(fileName));
            extracted = extractZip(filePath, extractPath);
        } else if (fileName.endsWith(".jar", Qt::CaseInsensitive)) {
            // JAR 文件不需要解压，只需复制到解压路径
            emit progress(75, tr("正在复制 %1...").arg(fileName));
            QString targetPath = QDir(extractPath).filePath(fileName);
            
            // 如果目标文件已存在，则删除
            if (QFile::exists(targetPath)) {
                QFile::remove(targetPath);
            }
            
            // 复制 JAR 文件
            if (QFile::copy(filePath, targetPath)) {
                extracted = true;
            } else {
                QString errorMsg = tr("无法将 JAR 文件复制到 %1").arg(targetPath);
#ifdef QT_DEBUG
                qDebug() << errorMsg;
#endif
                emit failed(errorMsg);
                m_DownloadFile->remove();
                m_NetworkReply->deleteLater();
                m_NetworkReply = nullptr;
                m_DownloadFile->deleteLater();
                m_DownloadFile = nullptr;
                return;
            }
        } else if (fileName.endsWith(".tar.gz", Qt::CaseInsensitive) || fileName.endsWith(".tgz", Qt::CaseInsensitive)) {
            emit progress(75, tr("正在解压 %1...").arg(fileName));
            extracted = extractTarGz(filePath, extractPath);
        } else if (fileName.endsWith(".pkg", Qt::CaseInsensitive)) {
            emit progress(75, tr("正在安装 %1...").arg(fileName));
            extracted = installPkg(filePath, extractPath);
        } else if (fileName.endsWith(".msi", Qt::CaseInsensitive)) {
            emit progress(75, tr("正在安装 %1...").arg(fileName));
            extracted = installMsi(filePath, extractPath);
        } else {
            emit progress(75, tr("正在提取 %1...").arg(fileName));
            // 未知文件类型，尝试复制
            QString targetPath = QDir(extractPath).filePath(fileName);
            if (QFile::exists(targetPath)) {
                QFile::remove(targetPath);
            }
            extracted = QFile::copy(filePath, targetPath);
        }

        if (!extracted) {
            QString errorMsg = tr("解压或复制下载文件失败");
#ifdef QT_DEBUG
            qDebug() << errorMsg << "从" << filePath << "到" << extractPath;
#endif
            m_DownloadFile->remove();
            emit failed(errorMsg);
            m_NetworkReply->deleteLater();
            m_NetworkReply = nullptr;
            m_DownloadFile->deleteLater();
            m_DownloadFile = nullptr;
            return;
        }

        emit progress(90, tr("正在定位可执行文件..."));

        // 查找可执行文件
        QString executablePath = findExecutableInExtracted(extractPath);
        
        // 对于 MSI/PKG 安装，也检查系统位置
        if (executablePath.isEmpty() && (fileName.endsWith(".msi", Qt::CaseInsensitive) || fileName.endsWith(".pkg", Qt::CaseInsensitive))) {
            executablePath = findExecutableInSystemLocations();
        }
        
        if (executablePath.isEmpty()) {
            emit failed(tr("安装后找不到可执行文件"));
            m_NetworkReply->deleteLater();
            m_NetworkReply = nullptr;
            m_DownloadFile->deleteLater();
            m_DownloadFile = nullptr;
            return;
        }

        // 在 Unix 上设置可执行权限
        setExecutablePermissions(executablePath);

        // 将路径保存到设置中
        QSettings settings;
        switch (m_Tool) {
        case Java:
            settings.setValue("java_exe", executablePath);
            break;
        case Apktool:
            settings.setValue("apktool_jar", executablePath);
            break;
        case Jadx:
            settings.setValue("jadx_exe", executablePath);
            break;
        case Adb:
            settings.setValue("adb_exe", executablePath);
            break;
        case UberApkSigner:
            settings.setValue("uas_jar", executablePath);
            break;
        }
        settings.sync();

        // 清理下载文件
        m_DownloadFile->remove();
        m_DownloadFile->deleteLater();
        m_DownloadFile = nullptr;

        emit progress(100, tr("安装完成！"));
        emit finished(executablePath);
        m_NetworkReply->deleteLater();
        m_NetworkReply = nullptr;
    }, Qt::QueuedConnection);
}

QString ToolDownloadWorker::getDownloadUrl()
{
    QString platform;
#ifdef Q_OS_WIN
    platform = "windows";
#elif defined(Q_OS_MACOS)
    platform = "darwin";
#else
    platform = "linux";
#endif

    switch (m_Tool) {
    case Java:
    {
        // Microsoft OpenJDK 平台特定 URL
        if (platform == "darwin") {
            // macOS - 使用 aarch64（Apple Silicon）版本
            // 注意：对于 Intel Mac，可能需要提供 x64 URL
            return "https://aka.ms/download-jdk/microsoft-jdk-11.0.29-macos-aarch64.pkg";
        } else if (platform == "linux") {
            // Linux
            return "https://aka.ms/download-jdk/microsoft-jdk-11.0.29-linux-x64.tar.gz";
        } else {
            // Windows
            return "https://aka.ms/download-jdk/microsoft-jdk-11.0.29-windows-x64.msi";
        }
    }
        
    case Apktool:
        return getLatestGitHubRelease("iBotPeaches/Apktool", "apktool.*\\.jar$");
        
    case Jadx:
    {
        QString url = getLatestGitHubRelease("skylot/jadx", QString("jadx-.*-%1\\.zip$").arg(platform));
        if (url.isEmpty()) {
            // 回退到通用 zip
            url = getLatestGitHubRelease("skylot/jadx", "jadx-.*\\.zip$");
        }
        return url;
    }
    
    case Adb:
    {
        if (platform == "darwin") {
            return "https://dl.google.com/android/repository/platform-tools-latest-darwin.zip";
        } else if (platform == "linux") {
            return "https://dl.google.com/android/repository/platform-tools-latest-linux.zip";
        } else {
            return "https://dl.google.com/android/repository/platform-tools-latest-windows.zip";
        }
    }
    
    case UberApkSigner:
        return getLatestGitHubRelease("patrickfav/uber-apk-signer", "uber-apk-signer.*\\.jar$");
    }
    
    return QString();
}

QString ToolDownloadWorker::getExtractPath()
{
    QString basePath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (basePath.isEmpty()) {
        basePath = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/.apkstudio";
    }
    
    QString toolName;
    switch (m_Tool) {
    case Java:
        toolName = "java";
        break;
    case Apktool:
        toolName = "apktool";
        break;
    case Jadx:
        toolName = "jadx";
        break;
    case Adb:
        toolName = "adb";
        break;
    case UberApkSigner:
        toolName = "uber-apk-signer";
        break;
    }
    
    return basePath + "/tools/" + toolName;
}

QString ToolDownloadWorker::findExecutableInExtracted(const QString &extractedPath)
{
    QDir dir(extractedPath);
    
    // 查找常见的可执行文件名称
    QStringList executableNames;
    switch (m_Tool) {
    case Java:
        // Java 可执行文件通常在 bin/ 子目录中
        executableNames << "bin/java" << "bin/java.exe" << "java" << "java.exe";
        // 也检查嵌套的 jdk 目录
        {
            QDir dir(extractedPath);
            QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
            for (const QString &entry : entries) {
                if (entry.contains("jdk", Qt::CaseInsensitive) || entry.contains("java", Qt::CaseInsensitive)) {
                    executableNames << entry + "/bin/java" << entry + "/bin/java.exe";
                }
            }
        }
        break;
    case Apktool:
        // 查找文件名中包含 "apktool" 的任何 JAR 文件（处理带版本号的名称，如 apktool_2.9.3.jar）
        {
            QDir dir(extractedPath);
            QStringList files = dir.entryList(QDir::Files, QDir::Name);
            for (const QString &file : files) {
                if (file.contains("apktool", Qt::CaseInsensitive) && file.endsWith(".jar", Qt::CaseInsensitive)) {
                    return dir.absoluteFilePath(file);
                }
            }
        }
        // 回退到精确名称（将在下面的递归搜索中检查）
        executableNames << "apktool.jar";
        break;
    case Jadx:
#ifdef Q_OS_WIN
        executableNames << "jadx/bin/jadx.bat" << "bin/jadx.bat";
#else
        executableNames << "jadx/bin/jadx" << "bin/jadx";
#endif
        break;
    case Adb:
        executableNames << "platform-tools/adb" << "adb/adb" << "adb";
#ifdef Q_OS_WIN
        executableNames << "platform-tools/adb.exe" << "adb/adb.exe" << "adb.exe";
#endif
        break;
    case UberApkSigner:
        // 查找文件名中包含 "uber-apk-signer" 的任何 JAR 文件（处理带版本号的名称）
        {
            QDir dir(extractedPath);
            QStringList files = dir.entryList(QDir::Files, QDir::Name);
            for (const QString &file : files) {
                if (file.contains("uber-apk-signer", Qt::CaseInsensitive) && file.endsWith(".jar", Qt::CaseInsensitive)) {
                    return dir.absoluteFilePath(file);
                }
            }
        }
        // 回退到精确名称（将在下面的递归搜索中检查）
        executableNames << "uber-apk-signer.jar";
        break;
    }
    
    // 递归搜索
    QStringList files = dir.entryList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &file : files) {
        QString filePath = dir.absoluteFilePath(file);
        QFileInfo info(filePath);
        
        if (info.isDir()) {
            QString found = findExecutableInExtracted(filePath);
            if (!found.isEmpty()) {
                return found;
            }
        } else {
            for (const QString &exeName : executableNames) {
                if (info.fileName().compare(exeName, Qt::CaseInsensitive) == 0 ||
                    filePath.endsWith(exeName, Qt::CaseInsensitive)) {
                    return filePath;
                }
            }
            
            // 对于 JAR 文件，检查文件名是否匹配模式
            if (filePath.endsWith(".jar", Qt::CaseInsensitive)) {
                if ((m_Tool == Apktool && file.contains("apktool", Qt::CaseInsensitive)) ||
                    (m_Tool == UberApkSigner && file.contains("uber-apk-signer", Qt::CaseInsensitive))) {
                    return filePath;
                }
            }
        }
    }
    
    return QString();
}

QString ToolDownloadWorker::findExecutableInSystemLocations()
{
    // 在系统位置搜索已安装的可执行文件（用于 MSI/PKG 安装）
    QStringList searchPaths;
    
    switch (m_Tool) {
    case Java:
    {
#ifdef Q_OS_WIN
        // Windows：检查 Program Files 位置
        searchPaths << "C:/Program Files/Microsoft"
                    << "C:/Program Files (x86)/Microsoft"
                    << "C:/Program Files/Java"
                    << "C:/Program Files (x86)/Java";
#elif defined(Q_OS_MACOS)
        // macOS：检查 Library 位置
        searchPaths << "/Library/Java/JavaVirtualMachines"
                    << QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/Library/Java/JavaVirtualMachines";
#else
        // Linux：检查常见位置
        searchPaths << "/usr/lib/jvm"
                    << "/opt/java"
                    << QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/.local/share/java";
#endif
        break;
    }
    default:
        return QString(); // 目前只有 Java 使用系统位置
    }
    
    for (const QString &basePath : searchPaths) {
        QDir baseDir(basePath);
        if (!baseDir.exists()) {
            continue;
        }
        
        QStringList entries = baseDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &entry : entries) {
            if (entry.contains("jdk", Qt::CaseInsensitive) || entry.contains("java", Qt::CaseInsensitive)) {
                QString jdkPath = baseDir.absoluteFilePath(entry);
#ifdef Q_OS_WIN
                QString javaExe = jdkPath + "/bin/java.exe";
                if (QFile::exists(javaExe)) {
                    return javaExe;
                }
#elif defined(Q_OS_MACOS)
                QString javaExe = jdkPath + "/Contents/Home/bin/java";
                if (QFile::exists(javaExe)) {
                    return javaExe;
                }
#else
                QString javaExe = jdkPath + "/bin/java";
                if (QFile::exists(javaExe)) {
                    return javaExe;
                }
#endif
            }
        }
    }
    
    return QString();
}

bool ToolDownloadWorker::extractZip(const QString &zipPath, const QString &extractPath)
{
#ifdef Q_OS_WIN
    // 首先尝试 PowerShell（Windows 7+）
    QProcess process;
    QStringList args;
    args << "-Command" << QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
        .arg(zipPath).arg(extractPath);
    process.start("powershell", args);
    if (process.waitForFinished(120000)) { // 2 分钟超时
        if (process.exitCode() == 0) {
            return true;
        }
    }
    
    // 回退：如果可用，尝试 7-Zip
    QStringList sevenZipPaths = {
        "C:/Program Files/7-Zip/7z.exe",
        "C:/Program Files (x86)/7-Zip/7z.exe"
    };
    for (const QString &sevenZip : sevenZipPaths) {
        if (QFile::exists(sevenZip)) {
            process.start(sevenZip, QStringList() << "x" << zipPath << QString("-o%1").arg(extractPath) << "-y");
            if (process.waitForFinished(120000) && process.exitCode() == 0) {
                return true;
            }
        }
    }
    
    return false;
#else
    // 在 Unix 上使用 unzip 命令
    QProcess process;
    process.setWorkingDirectory(extractPath);
    process.start("unzip", QStringList() << "-q" << "-o" << zipPath);
    if (!process.waitForFinished(120000)) { // 2 分钟超时
        return false;
    }
    return process.exitCode() == 0;
#endif
}

bool ToolDownloadWorker::extractTarGz(const QString &tarPath, const QString &extractPath)
{
    // 使用 tar 命令（Unix 系统可用）
    QProcess process;
    process.setWorkingDirectory(extractPath);
    process.start("tar", QStringList() << "-xzf" << tarPath);
    if (!process.waitForFinished(300000)) { // 大文件 5 分钟超时
        return false;
    }
    return process.exitCode() == 0;
}

bool ToolDownloadWorker::installPkg(const QString &pkgPath, const QString &installPath)
{
#ifdef Q_OS_MACOS
    // 执行 PKG 文件的无头安装
    // 对于 Java，等待成功完成并验证安装
    
#ifdef QT_DEBUG
    qDebug() << "[installPkg] 开始 PKG 安装，工具：" << m_Tool;
    qDebug() << "[installPkg] PKG 路径：" << pkgPath;
    qDebug() << "[installPkg] 安装路径：" << installPath;
#endif
    
    // 验证 PKG 文件存在且可读
    QFileInfo pkgInfo(pkgPath);
    if (!pkgInfo.exists()) {
#ifdef QT_DEBUG
        qDebug() << "[installPkg] 错误：PKG 文件不存在：" << pkgPath;
#endif
        return false;
    }
    if (!pkgInfo.isReadable()) {
#ifdef QT_DEBUG
        qDebug() << "[installPkg] 错误：PKG 文件不可读：" << pkgPath;
#endif
        return false;
    }
#ifdef QT_DEBUG
    qDebug() << "[installPkg] PKG 文件存在，大小：" << pkgInfo.size() << "字节";
#endif
    
    QProcess process;
    int exitCode = 0;
    QString stdOut;
    QString stdErr;
    
    // 对于 Java，需要管理员权限。使用 osascript 提升安装程序权限
    if (m_Tool == Java) {
#ifdef QT_DEBUG
        qDebug() << "[installPkg] Java 安装需要管理员权限。正在请求提升权限...";
#endif
        
        // 使用 osascript 以管理员权限运行安装程序
        // 转义 PKG 路径以便在 AppleScript 中使用
        QString escapedPkgPath = pkgPath;
        escapedPkgPath.replace("\\", "\\\\");
        escapedPkgPath.replace("\"", "\\\"");
        
        // 构建 AppleScript 命令
        QString script = QString("do shell script \"installer -pkg \\\"%1\\\" -target / -verboseR\" with administrator privileges")
                            .arg(escapedPkgPath);
        
        QStringList args;
        args << "-e" << script;
        
#ifdef QT_DEBUG
        qDebug() << "[installPkg] 运行带提升权限的 osascript...";
        qDebug() << "[installPkg] AppleScript 命令：" << script;
#endif
        
        process.start("osascript", args);
    } else {
        // 对于其他工具，先尝试不提升权限
        QStringList args;
        args << "-pkg" << pkgPath;
        args << "-target" << "/";
        args << "-verboseR";
        
#ifdef QT_DEBUG
        qDebug() << "[installPkg] 运行安装程序，参数：" << args;
#endif
        process.start("installer", args);
    }
    
    if (!process.waitForStarted(30000)) {
#ifdef QT_DEBUG
        qDebug() << "[installPkg] 启动安装程序进程失败";
#endif
        return false;
    }
    
#ifdef QT_DEBUG
    qDebug() << "[installPkg] 安装程序进程已启动，等待完成...";
#endif
    
    // 等待安装完成（Java 最多 10 分钟）
    int timeout = (m_Tool == Java) ? 600000 : 300000; // Java 10 分钟，其他 5 分钟
    if (!process.waitForFinished(timeout)) {
#ifdef QT_DEBUG
        qDebug() << "[installPkg] 安装程序进程超时，" << (timeout / 1000) << "秒后";
#endif
        return false;
    }
    
    exitCode = process.exitCode();
    stdOut = QString::fromUtf8(process.readAllStandardOutput());
    stdErr = QString::fromUtf8(process.readAllStandardError());
    
#ifdef QT_DEBUG
    qDebug() << "[installPkg] 安装程序进程完成，退出代码：" << exitCode;
    if (!stdOut.isEmpty()) {
        qDebug() << "[installPkg] 安装程序标准输出：" << stdOut;
    }
    if (!stdErr.isEmpty()) {
        qDebug() << "[installPkg] 安装程序标准错误：" << stdErr;
    }
    
    // 检查常见错误消息
    if (exitCode != 0) {
        if (stdErr.contains("User canceled", Qt::CaseInsensitive) || stdOut.contains("User canceled", Qt::CaseInsensitive)) {
            qDebug() << "[installPkg] 错误：用户取消安装（管理员密码对话框被关闭）";
        } else if (stdErr.contains("not authorized", Qt::CaseInsensitive) || stdOut.contains("not authorized", Qt::CaseInsensitive)) {
            qDebug() << "[installPkg] 错误：安装未授权 - 需要管理员权限";
        }
    }
#endif
    
    // 对于 Java，通过检查退出代码和查找可执行文件来验证成功安装
    if (m_Tool == Java) {
        if (exitCode != 0) {
#ifdef QT_DEBUG
            qDebug() << "[installPkg] 安装程序返回非零退出代码，等待 2 秒后仍检查文件是否存在...";
#endif
            // 安装失败，但稍等片刻，检查文件是否仍然出现
            QThread::msleep(2000); // 等待 2 秒让文件系统更新
        }
        
        // 在常见位置搜索已安装的 Java
        QStringList searchPaths = {
            "/Library/Java/JavaVirtualMachines",
            QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/Library/Java/JavaVirtualMachines"
        };
        
#ifdef QT_DEBUG
        qDebug() << "[installPkg] 在路径中搜索 Java：" << searchPaths;
#endif
        
        // 如果文件系统仍在更新，重试搜索几次
        for (int retry = 0; retry < 5; retry++) {
#ifdef QT_DEBUG
            qDebug() << "[installPkg] 搜索尝试" << (retry + 1) << "/ 5";
#endif
            
            for (const QString &searchPath : searchPaths) {
                QDir dir(searchPath);
#ifdef QT_DEBUG
                qDebug() << "[installPkg] 检查路径：" << searchPath << "存在：" << dir.exists();
#endif
                
                if (dir.exists()) {
                    QStringList entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
#ifdef QT_DEBUG
                    qDebug() << "[installPkg] 在" << searchPath << "中找到" << entries.size() << "个目录";
#endif
                    
                    for (const QString &entry : entries) {
#ifdef QT_DEBUG
                        qDebug() << "[installPkg] 检查条目：" << entry;
#endif
                        
                        if (entry.contains("jdk", Qt::CaseInsensitive) || entry.contains("java", Qt::CaseInsensitive)) {
                            QString jdkPath = dir.absoluteFilePath(entry);
                            QString javaExe = jdkPath + "/Contents/Home/bin/java";
#ifdef QT_DEBUG
                            qDebug() << "[installPkg] 找到 JDK 目录：" << jdkPath;
                            qDebug() << "[installPkg] 检查 Java 可执行文件位置：" << javaExe;
#endif
                            
                            if (QFile::exists(javaExe)) {
                                // 找到 Java，验证其是否可执行
                                QFileInfo info(javaExe);
#ifdef QT_DEBUG
                                qDebug() << "[installPkg] 找到 Java 可执行文件，是否可执行：" << info.isExecutable();
#endif
                                
                                if (info.isExecutable()) {
#ifdef QT_DEBUG
                                    qDebug() << "[installPkg] Java 安装验证成功：" << javaExe;
#endif
                                    return true; // 成功安装并找到
                                }
                            } else {
#ifdef QT_DEBUG
                                qDebug() << "[installPkg] Java 可执行文件未找到：" << javaExe;
#endif
                            }
                        }
                    }
                }
            }
            
            if (retry < 4) {
#ifdef QT_DEBUG
                qDebug() << "[installPkg] 未找到 Java，等待 1 秒后重试...";
#endif
                QThread::msleep(1000); // 重试前等待 1 秒
            }
        }
        
        // 安装后未找到 Java
#ifdef QT_DEBUG
        qDebug() << "[installPkg] Java 安装失败 - 所有重试后仍未找到可执行文件";
#endif
        return false;
    }
    
    // 对于其他工具，只检查退出代码
    bool success = (exitCode == 0);
#ifdef QT_DEBUG
    qDebug() << "[installPkg] 安装结果：" << (success ? "成功" : "失败");
#endif
    return success;
#else
    return false; // PKG 文件仅适用于 macOS
#endif
}

bool ToolDownloadWorker::installMsi(const QString &msiPath, const QString &installPath)
{
#ifdef Q_OS_WIN
    // 执行 MSI 文件的无头安装
    // 对于 Java，等待成功完成并验证安装
    
#ifdef QT_DEBUG
    qDebug() << "[installMsi] 开始 MSI 安装，工具：" << m_Tool;
    qDebug() << "[installMsi] MSI 路径：" << msiPath;
    qDebug() << "[installMsi] 安装路径：" << installPath;
#endif
    
    // 验证 MSI 文件存在且可读
    QFileInfo msiInfo(msiPath);
    if (!msiInfo.exists()) {
#ifdef QT_DEBUG
        qDebug() << "[installMsi] 错误：MSI 文件不存在：" << msiPath;
#endif
        return false;
    }
    if (!msiInfo.isReadable()) {
#ifdef QT_DEBUG
        qDebug() << "[installMsi] 错误：MSI 文件不可读：" << msiPath;
#endif
        return false;
    }
#ifdef QT_DEBUG
    qDebug() << "[installMsi] MSI 文件存在，大小：" << msiInfo.size() << "字节";
#endif
    
    QProcess process;
    
    // 使用 QDir::toNativeSeparators 确保 Windows 的正确路径格式
    QString nativeMsiPath = QDir::toNativeSeparators(msiPath);
    
    // 对于 Java，需要管理员权限。使用 PowerShell 提升 msiexec 权限
    if (m_Tool == Java) {
#ifdef QT_DEBUG
        qDebug() << "[installMsi] Java 安装需要管理员权限。正在请求提升权限...";
#endif
        
        // 构建带参数的 msiexec 命令
        QString msiexecCmd = QString("msiexec.exe /i \"%1\" /qn /norestart").arg(nativeMsiPath);
        
        // 使用 PowerShell 以提升权限启动 msiexec（显示 UAC 提示）
        // Start-Process 使用 -Verb RunAs 将显示 UAC 对话框
        QStringList psArgs;
        psArgs << "-Command";
        psArgs << QString("Start-Process -FilePath 'msiexec.exe' -ArgumentList '/i', '%1', '/qn', '/norestart' -Verb RunAs -Wait -PassThru | ForEach-Object { exit $_.ExitCode }").arg(nativeMsiPath);
        
#ifdef QT_DEBUG
        qDebug() << "[installMsi] 运行带提升权限的 PowerShell...";
        qDebug() << "[installMsi] PowerShell 命令：" << psArgs;
#endif
        
        process.start("powershell.exe", psArgs);
    } else {
        // 对于其他工具，先尝试不提升权限
        QStringList args;
        args << "/i" << nativeMsiPath;
        args << "/qn"; // 安静模式，无 UI
        args << "/norestart"; // 不重启
        
#ifdef QT_DEBUG
        qDebug() << "[installMsi] 运行 msiexec，参数：" << args;
#endif
        process.start("msiexec", args);
    }
    
    if (!process.waitForStarted(30000)) {
#ifdef QT_DEBUG
        qDebug() << "[installMsi] 启动 msiexec 进程失败";
#endif
        return false;
    }
    
#ifdef QT_DEBUG
    qDebug() << "[installMsi] msiexec 进程已启动，等待完成...";
#endif
    
    // 等待安装完成（Java 最多 10 分钟）
    int timeout = (m_Tool == Java) ? 600000 : 300000; // Java 10 分钟，其他 5 分钟
    if (!process.waitForFinished(timeout)) {
#ifdef QT_DEBUG
        qDebug() << "[installMsi] msiexec 进程超时，" << (timeout / 1000) << "秒后";
#endif
        return false;
    }
    
    int exitCode = process.exitCode();
    QString stdOut = QString::fromUtf8(process.readAllStandardOutput());
    QString stdErr = QString::fromUtf8(process.readAllStandardError());
    
#ifdef QT_DEBUG
    qDebug() << "[installMsi] msiexec 进程完成，退出代码：" << exitCode;
    if (!stdOut.isEmpty()) {
        qDebug() << "[installMsi] msiexec 标准输出：" << stdOut;
    }
    if (!stdErr.isEmpty()) {
        qDebug() << "[installMsi] msiexec 标准错误：" << stdErr;
    }
#endif
    
    // 对于 Java，通过检查退出代码和查找可执行文件来验证成功安装
    if (m_Tool == Java) {
        if (exitCode != 0) {
#ifdef QT_DEBUG
            qDebug() << "[installMsi] msiexec 返回非零退出代码（" << exitCode << "）";
            if (exitCode == 1925) {
                qDebug() << "[installMsi] 错误：退出代码 1925 = 权限不足。此 MSI 可能需要管理员权限。";
            } else if (exitCode == 1603) {
                qDebug() << "[installMsi] 错误：退出代码 1603 = 安装过程中出现致命错误。";
            } else if (exitCode == 1619) {
                qDebug() << "[installMsi] 错误：退出代码 1619 = 无法打开安装包。";
            }
            qDebug() << "[installMsi] 等待 2 秒后仍检查文件是否存在...";
#endif
            // 安装失败，但稍等片刻，检查文件是否仍然出现
            QThread::msleep(2000); // 等待 2 秒让文件系统更新
        }
        
        // 在默认安装位置搜索已安装的 Java
        QStringList defaultPaths = {
            "C:/Program Files/Microsoft",


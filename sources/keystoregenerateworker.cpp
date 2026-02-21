#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include "keystoregenerateworker.h"
#include "processutils.h"

KeystoreGenerateWorker::KeystoreGenerateWorker(const QString &keystorePath,
                                               const QString &keystorePassword,
                                               const QString &alias,
                                               const QString &aliasPassword,
                                               int validity,
                                               const QString &keyAlgorithm,
                                               int keySize,
                                               QObject *parent)
    : QObject(parent),
      m_KeystorePath(keystorePath),
      m_KeystorePassword(keystorePassword),
      m_Alias(alias),
      m_AliasPassword(aliasPassword),
      m_Validity(validity),
      m_KeyAlgorithm(keyAlgorithm),
      m_KeySize(keySize)
{
}

void KeystoreGenerateWorker::generate()
{
    emit started();
#ifdef QT_DEBUG
    qDebug() << "正在生成密钥库：" << m_KeystorePath;
#endif
    
    // 获取 Java 可执行文件路径
    const QString java = ProcessUtils::javaExe();
    if (java.isEmpty()) {
        emit generateFailed(tr("未找到 Java 可执行文件，请在设置中配置 Java。"));
        emit finished();
        return;
    }
    
    // 查找 keytool（通常与 java 在同一目录下）
    QFileInfo javaInfo(java);
    QString keytoolPath = javaInfo.absolutePath();
#ifdef Q_OS_WIN
    keytoolPath += "/keytool.exe";
#else
    keytoolPath += "/keytool";
#endif
    
    QFileInfo keytoolInfo(keytoolPath);
    if (!keytoolInfo.exists()) {
        emit generateFailed(tr("在 %1 未找到 keytool，请确保 JDK 已正确安装。").arg(keytoolPath));
        emit finished();
        return;
    }
    
    emit generateProgress(25, tr("正在生成密钥库..."));
    
    // 确保目标目录存在
    QFileInfo keystoreInfo(m_KeystorePath);
    QDir keystoreDir = keystoreInfo.absoluteDir();
    if (!keystoreDir.exists()) {
        if (!keystoreDir.mkpath(".")) {
            emit generateFailed(tr("无法创建密钥库目录：%1").arg(keystoreDir.absolutePath()));
            emit finished();
            return;
        }
    }
    
    // 构建 keytool 命令
    // keytool -genkeypair -v -keystore <密钥库路径> -alias <别名> -keyalg <算法> -keysize <大小> -validity <天数> -storepass <密码> -keypass <密码>
    QStringList args;
    args << "-genkeypair";
    args << "-v";
    args << "-keystore" << m_KeystorePath;
    args << "-alias" << m_Alias;
    args << "-keyalg" << m_KeyAlgorithm;
    args << "-keysize" << QString::number(m_KeySize);
    args << "-validity" << QString::number(m_Validity);
    args << "-storepass" << m_KeystorePassword;
    args << "-keypass" << m_AliasPassword;
    
    // 添加默认证书信息（keytool 必需）
    // 使用 -dname 以非交互方式提供所有必填字段
    QString dname = QString("CN=APK Studio, OU=Development, O=APK Studio, L=Unknown, ST=Unknown, C=US");
    args << "-dname" << dname;
    
    // 运行 keytool
    ProcessResult result = ProcessUtils::runCommand(keytoolPath, args, 60); // keytool 超时时间 60 秒
    
#ifdef QT_DEBUG
    qDebug() << "keytool 返回代码：" << result.code;
    if (result.code != 0) {
        qDebug() << "keytool 错误输出：" << result.error;
    }
#endif
    
    if (result.code != 0) {
        QString errorMsg = tr("生成密钥库失败。");
        if (!result.error.isEmpty()) {
            errorMsg += "\n" + result.error.join("\n");
        }
        emit generateFailed(errorMsg);
        emit finished();
        return;
    }
    
    // 验证密钥库文件是否已创建
    keystoreInfo.refresh();
    if (!keystoreInfo.exists()) {
        emit generateFailed(tr("密钥库文件未生成。"));
        emit finished();
        return;
    }
    
    emit generateProgress(100, tr("密钥库生成成功。"));
    emit generateFinished(m_KeystorePath);
    emit finished();
}



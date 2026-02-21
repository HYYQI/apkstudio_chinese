#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include "keystoregeneratedialog.h"
#include "keystoregenerateworker.h"
#include "signingconfigwidget.h"

SigningConfigWidget::SigningConfigWidget(QWidget *parent)
    : QWidget(parent), m_ProgressDialog(nullptr)
{
    setLayout(buildLayout());
    m_EditKeystore->selectAll();
    m_EditKeystore->setFocus();
    m_PendingKeystorePassword.clear();
    m_PendingAlias.clear();
    m_PendingAliasPassword.clear();
}

QString SigningConfigWidget::alias() const
{
    return m_EditAlias->text();
}

QString SigningConfigWidget::aliasPassword() const
{
    return m_EditAliasPassword->text();
}

QLayout *SigningConfigWidget::buildLayout()
{
    auto layout = new QFormLayout(this);
    layout->addRow(tr("密钥库"), m_EditKeystore = new QLineEdit(this));
    QPushButton* browseButton;
    QPushButton* generateButton;
    QHBoxLayout* child = new QHBoxLayout();
    child->addWidget(browseButton = new QPushButton(tr("浏览..."), this));
    connect(browseButton, &QPushButton::pressed, this, &SigningConfigWidget::handleBrowseKeystore);
    child->addWidget(generateButton = new QPushButton(tr("生成"), this));
    connect(generateButton, &QPushButton::pressed, this, &SigningConfigWidget::handleGenerateKeystore);
    auto empty = new QWidget(this);
    empty->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    child->addWidget(empty);
    layout->addRow("", child);
    layout->addRow(tr("密钥库密码"), m_EditKeystorePassword = new QLineEdit(this));
    m_EditKeystorePassword->setEchoMode(QLineEdit::Password);
    layout->addRow(tr("别名"), m_EditAlias = new QLineEdit(this));
    layout->addRow(tr("别名密码"), m_EditAliasPassword = new QLineEdit(this));
    m_EditAliasPassword->setEchoMode(QLineEdit::Password);
    layout->addRow(tr("Zipalign？"), m_CheckZipalign = new QCheckBox(this));
    QSettings settings;
    m_EditKeystore->setText(settings.value("signing_keystore").toString());
    m_EditKeystorePassword->setText(settings.value("signing_keystore_password").toString());
    m_EditAlias->setText(settings.value("signing_alias").toString());
    m_EditAliasPassword->setText(settings.value("signing_alias_password").toString());
    m_CheckZipalign->setChecked(settings.value("signing_zipalign", true).toBool());
    return layout;
}

void SigningConfigWidget::handleBrowseKeystore()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("浏览 Java 密钥库"));
    if (!path.isEmpty()) {
        m_EditKeystore->setText(QDir::toNativeSeparators(path));
    }
}

void SigningConfigWidget::save()
{
    QSettings settings;
    settings.setValue("signing_keystore", m_EditKeystore->text());
    settings.setValue("signing_keystore_password", m_EditKeystorePassword->text());
    settings.setValue("signing_alias", m_EditAlias->text());
    settings.setValue("signing_alias_password", m_EditAliasPassword->text());
    settings.setValue("signing_zipalign", m_CheckZipalign->isChecked());
    settings.sync();
}

QString SigningConfigWidget::keystore() const
{
    return m_EditKeystore->text();
}

QString SigningConfigWidget::keystorePassword() const
{
    return m_EditKeystorePassword->text();
}

bool SigningConfigWidget::zipalign() const
{
    return m_CheckZipalign->isChecked();
}

void SigningConfigWidget::handleGenerateKeystore()
{
    KeystoreGenerateDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    
    // 存储对话框值供后续使用
    m_PendingKeystorePassword = dialog.keystorePassword();
    m_PendingAlias = dialog.alias();
    m_PendingAliasPassword = dialog.aliasPassword();
    
    // 创建工作线程
    auto thread = new QThread();
    auto worker = new KeystoreGenerateWorker(
        dialog.keystorePath(),
        dialog.keystorePassword(),
        dialog.alias(),
        dialog.aliasPassword(),
        dialog.validity(),
        dialog.keyAlgorithm(),
        dialog.keySize()
    );
    worker->moveToThread(thread);
    
    connect(thread, &QThread::started, worker, &KeystoreGenerateWorker::generate);
    connect(worker, &KeystoreGenerateWorker::generateFinished, this, &SigningConfigWidget::handleKeystoreGenerated);
    connect(worker, &KeystoreGenerateWorker::generateFailed, this, &SigningConfigWidget::handleKeystoreGenerateFailed);
    connect(worker, &KeystoreGenerateWorker::finished, thread, &QThread::quit);
    connect(worker, &KeystoreGenerateWorker::finished, worker, &QObject::deleteLater);
    connect(worker, &KeystoreGenerateWorker::finished, this, &SigningConfigWidget::handleKeystoreGenerateFinished);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    
    // 显示进度对话框
    m_ProgressDialog = new QProgressDialog(this);
    m_ProgressDialog->setCancelButton(nullptr);
    m_ProgressDialog->setLabelText(tr("正在生成密钥库..."));
    m_ProgressDialog->setRange(0, 0); // 不确定进度
    m_ProgressDialog->setWindowFlags(m_ProgressDialog->windowFlags() & ~Qt::WindowCloseButtonHint);
    m_ProgressDialog->setWindowTitle(tr("生成密钥库"));
    
    thread->start();
    m_ProgressDialog->exec();
}

void SigningConfigWidget::handleKeystoreGenerated(const QString &keystorePath)
{
    // 关闭进度对话框
    if (m_ProgressDialog) {
        m_ProgressDialog->close();
        m_ProgressDialog = nullptr;
    }
    
    // 使用生成的密钥库信息填充字段
    m_EditKeystore->setText(QDir::toNativeSeparators(keystorePath));
    m_EditKeystorePassword->setText(m_PendingKeystorePassword);
    m_EditAlias->setText(m_PendingAlias);
    m_EditAliasPassword->setText(m_PendingAliasPassword);
    
    // 清除存储的值
    m_PendingKeystorePassword.clear();
    m_PendingAlias.clear();
    m_PendingAliasPassword.clear();
    
    QMessageBox::information(this, tr("密钥库已生成"), tr("密钥库生成成功！"));
}

void SigningConfigWidget::handleKeystoreGenerateFailed(const QString &error)
{
    if (m_ProgressDialog) {
        m_ProgressDialog->close();
        m_ProgressDialog = nullptr;
    }
    
    QMessageBox::critical(this, tr("密钥库生成失败"), error);
}

void SigningConfigWidget::handleKeystoreGenerateFinished()
{
    if (m_ProgressDialog) {
        m_ProgressDialog->close();
        m_ProgressDialog = nullptr;
    }
}

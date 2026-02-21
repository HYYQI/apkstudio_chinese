#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include "tooldownloaddialog.h"
#include "tooldownloadworker.h"

ToolDownloadDialog::ToolDownloadDialog(const QList<ToolDownloadWorker::ToolType> &tools, QWidget *parent)
    : QDialog(parent), m_Tools(tools), m_CurrentToolIndex(0), m_Success(false), m_Cancelled(false), m_CurrentWorker(nullptr), m_CurrentThread(nullptr)
{
    setWindowTitle(tr("下载二进制文件"));
    setModal(true);
    setMinimumWidth(500);
    setMinimumHeight(200);
    
    auto layout = new QVBoxLayout(this);
    layout->setSpacing(10);
    layout->setContentsMargins(15, 15, 15, 15);
    
    m_ToolLabel = new QLabel("", this);
    m_ToolLabel->setWordWrap(true);
    QFont toolFont = m_ToolLabel->font();
    toolFont.setBold(true);
    toolFont.setPointSize(toolFont.pointSize() + 1);
    m_ToolLabel->setFont(toolFont);
    layout->addWidget(m_ToolLabel);
    
    m_StatusLabel = new QLabel("", this);
    m_StatusLabel->setWordWrap(true);
    layout->addWidget(m_StatusLabel);
    
    m_ProgressBar = new QProgressBar(this);
    m_ProgressBar->setRange(0, 100);
    m_ProgressBar->setValue(0);
    m_ProgressBar->setTextVisible(true);
    layout->addWidget(m_ProgressBar);
    
    m_DetailLabel = new QLabel("", this);
    m_DetailLabel->setWordWrap(true);
    m_DetailLabel->setStyleSheet("QLabel { color: gray; }");
    layout->addWidget(m_DetailLabel);
    
    auto buttonLayout = new QHBoxLayout;
    buttonLayout->addStretch();
    m_CancelButton = new QPushButton(tr("取消"), this);
    connect(m_CancelButton, &QPushButton::clicked, this, &ToolDownloadDialog::handleCancel);
    buttonLayout->addWidget(m_CancelButton);
    layout->addLayout(buttonLayout);
    
    if (m_Tools.isEmpty()) {
        m_StatusLabel->setText(tr("没有需要下载的工具。"));
        m_CancelButton->setText(tr("关闭"));
        return;
    }
    
    // 开始第一个下载任务
    startNextDownload();
}

void ToolDownloadDialog::startNextDownload()
{
    if (m_Cancelled || m_CurrentToolIndex >= m_Tools.size()) {
        if (!m_Cancelled && m_Success) {
            m_StatusLabel->setText(tr("所有工具下载成功！"));
            m_DetailLabel->setText(tr("已下载 %1 个工具").arg(m_DownloadedPaths.size()));
        }
        m_CancelButton->setText(tr("关闭"));
        m_ProgressBar->setValue(100);
        return;
    }
    
    ToolDownloadWorker::ToolType tool = m_Tools[m_CurrentToolIndex];
    QString toolName = getToolName(tool);
    
    m_ToolLabel->setText(tr("%1（第 %2/%3 个）...")
                         .arg(toolName)
                         .arg(m_CurrentToolIndex + 1)
                         .arg(m_Tools.size()));
    m_StatusLabel->setText(tr("准备下载 %1...").arg(toolName));
    m_ProgressBar->setValue(0);
    m_DetailLabel->setText("");
    
    // 在后台线程中开始下载
    m_CurrentThread = new QThread(this);
    m_CurrentWorker = new ToolDownloadWorker(tool, nullptr);
    m_CurrentWorker->moveToThread(m_CurrentThread);
    
    connect(m_CurrentThread, &QThread::started, m_CurrentWorker, &ToolDownloadWorker::download);
    connect(m_CurrentWorker, &ToolDownloadWorker::progress, this, &ToolDownloadDialog::handleProgress);
    connect(m_CurrentWorker, &ToolDownloadWorker::finished, this, &ToolDownloadDialog::handleFinished);
    connect(m_CurrentWorker, &ToolDownloadWorker::failed, this, &ToolDownloadDialog::handleFailed);
    connect(m_CurrentWorker, &ToolDownloadWorker::finished, m_CurrentThread, &QThread::quit);
    connect(m_CurrentWorker, &ToolDownloadWorker::failed, m_CurrentThread, &QThread::quit);
    connect(m_CurrentThread, &QThread::finished, m_CurrentWorker, &QObject::deleteLater);
    connect(m_CurrentThread, &QThread::finished, m_CurrentThread, &QObject::deleteLater);
    connect(m_CurrentThread, &QThread::finished, [this]() {
        m_CurrentWorker = nullptr;
        m_CurrentThread = nullptr;
    });
    
    m_CurrentThread->start();
}

void ToolDownloadDialog::handleProgress(int percentage, const QString &message)
{
    if (m_Cancelled) return;
    
    // 计算所有工具的总体进度
    int toolProgress = (m_CurrentToolIndex * 100) + percentage;
    int overallProgress = toolProgress / m_Tools.size();
    m_ProgressBar->setValue(overallProgress);
    m_StatusLabel->setText(message);
    m_DetailLabel->setText(tr("总体进度：%1%%（已完成 %2/%3 个工具）")
                          .arg(overallProgress)
                          .arg(m_CurrentToolIndex + 1)
                          .arg(m_Tools.size()));
}

void ToolDownloadDialog::handleFinished(const QString &path)
{
    if (m_Cancelled) return;
    
    m_DownloadedPaths.append(path);
    m_CurrentToolIndex++;
    
    if (m_CurrentToolIndex < m_Tools.size()) {
        // 开始下一个下载任务
        QTimer::singleShot(500, this, &ToolDownloadDialog::startNextDownload);
    } else {
        // 所有下载完成
        m_Success = true;
        m_ProgressBar->setValue(100);
        m_StatusLabel->setText(tr("所有工具下载并安装成功！"));
        m_DetailLabel->setText(tr("已下载 %1 个工具").arg(m_DownloadedPaths.size()));
        m_ToolLabel->setText("");
        m_CancelButton->setText(tr("关闭"));
    }
}

void ToolDownloadDialog::handleFailed(const QString &error)
{
    if (m_Cancelled) return;
    
    QString toolName = getToolName(m_Tools[m_CurrentToolIndex]);
    m_StatusLabel->setText(tr("下载 %1 失败：%2").arg(toolName, error));
    m_DetailLabel->setText(tr("请从设置页面手动下载 %1。").arg(toolName));
    m_ProgressBar->setValue(0);
    m_CancelButton->setText(tr("关闭"));
    m_Success = false;
}

void ToolDownloadDialog::closeEvent(QCloseEvent *event)
{
    // 如果所有下载已完成且成功，允许关闭
    if (m_CurrentToolIndex >= m_Tools.size() && m_Success && !m_Cancelled) {
        event->accept();
        return;
    }
    
    // 如果下载仍在进行中，取消下载
    if (!m_Cancelled && m_CurrentWorker) {
        handleCancel();
    }
    event->accept();
}

void ToolDownloadDialog::handleCancel()
{
    // 如果所有下载已完成且成功，直接关闭对话框
    if (m_CurrentToolIndex >= m_Tools.size() && m_Success && !m_Cancelled) {
        accept();
        return;
    }
    
    if (m_Cancelled) {
        reject();
        return;
    }
    
    m_Cancelled = true;
    m_Success = false;
    
    if (m_CurrentWorker && m_CurrentThread) {
        // 首先中止下载
        m_CurrentWorker->abort();
        
        // 断开工作器的信号连接，防止取消后触发回调
        disconnect(m_CurrentWorker, &ToolDownloadWorker::progress, this, nullptr);
        disconnect(m_CurrentWorker, &ToolDownloadWorker::finished, this, nullptr);
        disconnect(m_CurrentWorker, &ToolDownloadWorker::failed, this, nullptr);
        disconnect(m_CurrentWorker, &ToolDownloadWorker::finished, m_CurrentThread, nullptr);
        disconnect(m_CurrentWorker, &ToolDownloadWorker::failed, m_CurrentThread, nullptr);
        
        // 断开线程信号连接
        disconnect(m_CurrentThread, &QThread::started, m_CurrentWorker, nullptr);
        disconnect(m_CurrentThread, &QThread::finished, m_CurrentWorker, nullptr);
        disconnect(m_CurrentThread, &QThread::finished, m_CurrentThread, nullptr);
        disconnect(m_CurrentThread, &QThread::finished, this, nullptr);
        
        // 停止当前下载线程
        m_CurrentThread->quit();
        if (!m_CurrentThread->wait(1000)) {
            // 如果线程无响应，强制终止
            m_CurrentThread->terminate();
            m_CurrentThread->wait(500);
        }
        
        // 清理资源 - 使用 deleteLater 确保正确的清理顺序
        if (m_CurrentWorker) {
            m_CurrentWorker->deleteLater();
            m_CurrentWorker = nullptr;
        }
        if (m_CurrentThread) {
            m_CurrentThread->deleteLater();
            m_CurrentThread = nullptr;
        }
    }
    
    m_StatusLabel->setText(tr("下载已取消。"));
    m_DetailLabel->setText(tr("某些工具可能未安装。请从设置页面手动下载。"));
    m_CancelButton->setText(tr("关闭"));
}

QString ToolDownloadDialog::getToolName(ToolDownloadWorker::ToolType tool) const
{
    switch (tool) {
    case ToolDownloadWorker::Java:
        return tr("Java");
    case ToolDownloadWorker::Apktool:
        return tr("Apktool");
    case ToolDownloadWorker::Jadx:
        return tr("JADX");
    case ToolDownloadWorker::Adb:
        return tr("ADB");
    case ToolDownloadWorker::UberApkSigner:
        return tr("Uber APK Signer");
    }
    return tr("工具");
}

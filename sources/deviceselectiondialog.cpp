#include <QAbstractItemView>
#include <QDebug>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>
#include "deviceselectiondialog.h"
#include "devicelistworker.h"

DeviceSelectionDialog::DeviceSelectionDialog(QWidget *parent)
    : QDialog(parent), m_ProgressDialog(nullptr)
{
    setWindowTitle(tr("选择设备"));
    setMinimumSize(480, 240);
    resize(480, 240);
#ifdef Q_OS_WIN
    setWindowIcon(QIcon(":/icons/fugue/android.png"));
#endif

    auto layout = new QVBoxLayout(this);
    
    m_DeviceTree = new QTreeWidget(this);
    m_DeviceTree->setHeaderLabels(QStringList() << tr("设备 ID") << tr("型号") << tr("SDK 版本") << tr("平台版本") << tr("状态"));
    m_DeviceTree->setRootIsDecorated(false);
    m_DeviceTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_DeviceTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_DeviceTree->header()->setStretchLastSection(false);
    m_DeviceTree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_DeviceTree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_DeviceTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_DeviceTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_DeviceTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    layout->addWidget(m_DeviceTree);

    // 创建按钮（按顺序：刷新、安装、取消）
    m_RefreshButton = new QPushButton(tr("刷新"), this);
    m_InstallButton = new QPushButton(tr("安装"), this);
    auto cancelButton = new QPushButton(tr("取消"), this);
    
    m_InstallButton->setDefault(true);
    m_InstallButton->setEnabled(false);
    
    connect(m_RefreshButton, &QPushButton::clicked, this, &DeviceSelectionDialog::refreshDevices);
    connect(m_InstallButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    
    connect(m_DeviceTree, &QTreeWidget::itemSelectionChanged, this, &DeviceSelectionDialog::updateInstallButtonState);
    
    // 按钮布局（按顺序：刷新、弹性空间、安装、取消）
    auto buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(m_RefreshButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_InstallButton);
    buttonLayout->addWidget(cancelButton);
    
    layout->addLayout(buttonLayout);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    refreshDevices();
}

QString DeviceSelectionDialog::selectedDeviceSerial() const
{
    auto selected = m_DeviceTree->selectedItems();
    if (selected.isEmpty()) {
        return QString();
    }
    return selected.first()->data(0, Qt::UserRole).toString();
}

void DeviceSelectionDialog::refreshDevices()
{
    m_DeviceTree->clear();
    m_Devices.clear();
    m_RefreshButton->setEnabled(false);
    startDeviceListWorker();
}

void DeviceSelectionDialog::startDeviceListWorker()
{
    // 显示不确定进度的进度对话框
    if (!m_ProgressDialog) {
        m_ProgressDialog = new QProgressDialog(this);
        m_ProgressDialog->setCancelButton(nullptr);
        m_ProgressDialog->setRange(0, 0); // 不确定进度
        m_ProgressDialog->setLabelText(tr("正在刷新..."));
        m_ProgressDialog->setWindowFlags(m_ProgressDialog->windowFlags() & ~Qt::WindowCloseButtonHint);
        m_ProgressDialog->setWindowTitle(tr("加载设备中"));
        m_ProgressDialog->setModal(true);
    }
    m_ProgressDialog->show();
    
    auto thread = new QThread(this);
    auto worker = new DeviceListWorker();
    worker->moveToThread(thread);
    
    connect(thread, &QThread::started, worker, &DeviceListWorker::listDevices);
    connect(worker, &DeviceListWorker::devicesListed, this, &DeviceSelectionDialog::handleDevicesListed);
    connect(worker, &DeviceListWorker::error, this, &DeviceSelectionDialog::handleDeviceListError);
    connect(worker, &DeviceListWorker::finished, thread, &QThread::quit);
    connect(worker, &DeviceListWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    connect(thread, &QThread::finished, this, &DeviceSelectionDialog::handleWorkerFinished);
    
    thread->start();
}

void DeviceSelectionDialog::handleWorkerFinished()
{
    m_RefreshButton->setEnabled(true);
    if (m_ProgressDialog) {
        m_ProgressDialog->hide();
    }
}

void DeviceSelectionDialog::handleDevicesListed(const QList<DeviceInfo> &devices)
{
    m_Devices = devices;
    
    if (m_Devices.isEmpty()) {
        QMessageBox::information(this, tr("未找到设备"), tr("未找到任何设备。请连接设备并启用 USB 调试。"));
        return;
    }
    
    // 检查是否有至少一台设备处于 "device" 状态
    bool hasInstallableDevice = false;
    for (const DeviceInfo &device : m_Devices) {
        if (device.status == "device") {
            hasInstallableDevice = true;
            break;
        }
    }
    
    if (!hasInstallableDevice) {
        QMessageBox::information(this, tr("无可安装设备"), tr("没有处于可安装状态的设备。请确保至少有一台设备已连接并授权。"));
    }

    populateDeviceList(m_Devices);
}

void DeviceSelectionDialog::handleDeviceListError(const QString &message)
{
    QMessageBox::warning(this, tr("错误"), message);
}

void DeviceSelectionDialog::populateDeviceList(const QList<DeviceInfo> &devices)
{
    for (const DeviceInfo &device : devices) {
        auto item = new QTreeWidgetItem(m_DeviceTree);
        item->setData(0, Qt::UserRole, device.serial);
        item->setText(0, device.serial);
        item->setText(1, device.model.isEmpty() ? tr("未知") : device.model);
        item->setText(2, device.androidSdkVersion.isEmpty() ? tr("未知") : device.androidSdkVersion);
        item->setText(3, device.androidVersion.isEmpty() ? tr("未知") : device.androidVersion);
        item->setText(4, translateStatus(device.status));
    }
    
    if (m_DeviceTree->topLevelItemCount() > 0) {
        m_DeviceTree->setCurrentItem(m_DeviceTree->topLevelItem(0));
    }
    
    updateInstallButtonState();
}

void DeviceSelectionDialog::updateInstallButtonState()
{
    auto selected = m_DeviceTree->selectedItems();
    if (selected.isEmpty()) {
        m_InstallButton->setEnabled(false);
        return;
    }
    QString serial = selected.first()->data(0, Qt::UserRole).toString();
    // 查找选中的设备，检查其状态是否为 "device"
    bool canInstall = false;
    for (const DeviceInfo &device : m_Devices) {
        if (device.serial == serial && device.status == "device") {
            canInstall = true;
            break;
        }
    }
    m_InstallButton->setEnabled(canInstall);
}

QString DeviceSelectionDialog::translateStatus(const QString &status) const
{
    if (status == "device") {
        return tr("已连接");
    } else if (status == "offline") {
        return tr("离线");
    } else if (status == "unauthorized") {
        return tr("未授权");
    } else if (status == "bootloader") {
        return tr("引导加载模式");
    } else if (status == "recovery") {
        return tr("恢复模式");
    } else if (status == "sideload") {
        return tr("侧载模式");
    } else if (status == "authorizing") {
        return tr("授权中");
    } else if (status == "no permissions") {
        return tr("无权限");
    } else if (status == "host") {
        return tr("主机模式");
    } else {
        // 对于未知状态，首字母大写后返回
        if (status.isEmpty()) {
            return tr("未知");
        }
        return status.at(0).toUpper() + status.mid(1);
    }
}

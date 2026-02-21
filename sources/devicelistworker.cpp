#include <QDebug>
#include <QRegularExpression>
#include "devicelistworker.h"
#include "processutils.h"

DeviceListWorker::DeviceListWorker(QObject *parent)
    : QObject(parent)
{
}

void DeviceListWorker::listDevices()
{
    emit started();
    
    const QString adb = ProcessUtils::adbExe();
    if (adb.isEmpty()) {
        emit error(tr("未找到 ADB，请在设置中配置。"));
        emit finished();
        return;
    }

    // 获取带详细信息的设备列表
    ProcessResult result = ProcessUtils::runCommand(adb, QStringList() << "devices" << "-l");
    if (result.code != 0) {
        emit error(tr("查询设备失败：%1").arg(result.error.join("\n")));
        emit finished();
        return;
    }

    QList<DeviceInfo> devices;
    
    // 解析设备列表
    bool foundHeader = false;
    for (const QString &line : result.output) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        if (!foundHeader) {
            if (trimmed.startsWith("List of devices")) {
                foundHeader = true;
            }
            continue;
        }
        
        DeviceInfo device = parseDeviceLine(trimmed);
        if (!device.serial.isEmpty()) {
            // 如果未获取到设备型号，则通过 getprop 获取
            if (device.model.isEmpty()) {
                device.model = getDeviceProperty(device.serial, "ro.product.model");
            }
            device.androidSdkVersion = getDeviceProperty(device.serial, "ro.build.version.sdk");
            device.androidVersion = getDeviceProperty(device.serial, "ro.build.version.release");
            devices.append(device);
        }
    }

    emit devicesListed(devices);
    emit finished();
}

DeviceInfo DeviceListWorker::parseDeviceLine(const QString &line)
{
    DeviceInfo device;
    
    // 格式：序列号    状态 [属性:值]...
    // 示例：emulator-5554    device product:sdk_gphone64_arm64 model:sdk_gphone64_arm64 device:emu64xa
    QRegularExpression regex(R"(^(\S+)\s+(\S+)(?:\s+(.+))?$)");
    QRegularExpressionMatch match = regex.match(line);
    
    if (match.hasMatch()) {
        device.serial = match.captured(1);
        device.status = match.captured(2);
        
        // 解析行中的属性信息
        QString properties = match.captured(3);
        if (!properties.isEmpty()) {
            QRegularExpression propRegex(R"((\w+):([^\s]+))");
            QRegularExpressionMatchIterator propIter = propRegex.globalMatch(properties);
            while (propIter.hasNext()) {
                QRegularExpressionMatch propMatch = propIter.next();
                QString key = propMatch.captured(1);
                QString value = propMatch.captured(2);
                if (key == "model") {
                    device.model = value;
                }
            }
        }
    }
    
    return device;
}

QString DeviceListWorker::getDeviceProperty(const QString &serial, const QString &property)
{
    const QString adb = ProcessUtils::adbExe();
    if (adb.isEmpty()) {
        return QString();
    }

    QStringList args;
    args << "-s" << serial << "shell" << "getprop" << property;
    ProcessResult result = ProcessUtils::runCommand(adb, args, 5);
    
    if (result.code == 0 && !result.output.isEmpty()) {
        return result.output.first().trimmed();
    }
    
    return QString();
}



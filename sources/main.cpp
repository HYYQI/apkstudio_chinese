#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStyleHints>
#include <QTextStream>
#include <QDateTime>
#include "splashwindow.h"

#define CODE_RESTART 60600

void myMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QTextStream cout(stdout, QIODevice::WriteOnly);
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
    QString typeStr;
    switch (type) {
    case QtDebugMsg:
        typeStr = "DEBUG";
        break;
    case QtInfoMsg:
        typeStr = "INFO";
        break;
    case QtWarningMsg:
        typeStr = "WARNING";
        break;
    case QtCriticalMsg:
        typeStr = "CRITICAL";
        break;
    case QtFatalMsg:
        typeStr = "FATAL";
        break;
    }
    cout << QString("[%1] [%2] %3").arg(timestamp, typeStr, msg) << Qt::endl;
    cout.flush();
}

int main(int argc, char *argv[])
{
    qInstallMessageHandler(myMessageOutput);
    
    QApplication::setApplicationName("APK Studio");
    QApplication::setOrganizationDomain("vaibhavpandey.com");
    QApplication::setOrganizationName("Vaibhav Pandey");
    int code = 0;
    do {
        QApplication app(argc, argv);
        app.setWindowIcon(QIcon(":/images/icon.png"));

        QSettings settings;
        const bool dark = settings.value("dark_theme", false).toBool();
        
        // 使用 Qt 6 原生 Fusion 样式
        app.setStyle("Fusion");
        
        // 使用 Qt 6.5+ 原生配色方案 API（如果可用）
        // 这会自动处理所有调色板颜色
        #if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
            app.styleHints()->setColorScheme(dark ? Qt::ColorScheme::Dark : Qt::ColorScheme::Light);
        #else
            // Qt 6.0-6.4 的备选方案：手动设置调色板
            QPalette palette;
        if (dark) {
                palette.setColor(QPalette::Window, QColor(53, 53, 53));
                palette.setColor(QPalette::WindowText, Qt::white);
                palette.setColor(QPalette::Base, QColor(25, 25, 25));
                palette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
                palette.setColor(QPalette::ToolTipBase, Qt::white);
                palette.setColor(QPalette::ToolTipText, Qt::white);
                palette.setColor(QPalette::Text, Qt::white);
                palette.setColor(QPalette::Button, QColor(53, 53, 53));
                palette.setColor(QPalette::ButtonText, Qt::white);
                palette.setColor(QPalette::BrightText, Qt::red);
                palette.setColor(QPalette::Link, QColor(42, 130, 218));
                palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
                palette.setColor(QPalette::HighlightedText, Qt::black);
            } else {
                palette.setColor(QPalette::Window, Qt::white);
                palette.setColor(QPalette::WindowText, Qt::black);
                palette.setColor(QPalette::Base, Qt::white);
                palette.setColor(QPalette::AlternateBase, QColor(240, 240, 240));
                palette.setColor(QPalette::ToolTipBase, Qt::white);
                palette.setColor(QPalette::ToolTipText, Qt::black);
                palette.setColor(QPalette::Text, Qt::black);
                palette.setColor(QPalette::Button, QColor(240, 240, 240));
                palette.setColor(QPalette::ButtonText, Qt::black);
                palette.setColor(QPalette::BrightText, Qt::red);
                palette.setColor(QPalette::Link, QColor(0, 0, 255));
                palette.setColor(QPalette::Highlight, QColor(0, 120, 215));
                palette.setColor(QPalette::HighlightedText, Qt::white);
            }
            app.setPalette(palette);
        #endif
        
        // 检查命令行参数中是否包含 APK 文件路径
        QString apkFilePath;
        if (argc > 1) {
            QString arg = QString::fromLocal8Bit(argv[1]);
            QFileInfo fileInfo(arg);
            if (fileInfo.exists() && fileInfo.suffix().toLower() == "apk") {
                apkFilePath = QDir::toNativeSeparators(fileInfo.absoluteFilePath());
        }
        }
        
        SplashWindow window(apkFilePath);
        window.show();
        code = app.exec();
    } while (code == CODE_RESTART);
    return code;
}

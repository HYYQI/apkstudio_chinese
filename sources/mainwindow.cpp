#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDebug>
#include <QDesktopServices>
#include <QEvent>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenuBar>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextStream>
#include <QTextDocumentFragment>
#include <QThread>
#include <QTimer>
#include <QTreeWidgetItem>
#include <QUrl>
#include "adbinstallworker.h"
#include "apkdecompiledialog.h"
#include "apkdecompileworker.h"
#include "apkrecompileworker.h"
#include "apksignworker.h"
#include "desktopdatabaseupdateworker.h"
#include "deviceselectiondialog.h"
#include "findinfilesdialog.h"
#include "findreplacedialog.h"
#include "hexedit.h"
#include "imageviewerwidget.h"
#include "tooldownloaddialog.h"
#include "tooldownloadworker.h"
#include "versionresolveworker.h"
#include "mainwindow.h"
#include "settingsdialog.h"
#include "signingconfigdialog.h"
#include "sourcecodeedit.h"

#define CODE_RESTART 60600

#define COLOR_CODE 0x2ad2c9
#define COLOR_COMMAND 0xd0d2d3
#define COLOR_OUTPUT 0xffffff
#define COLOR_ERROR 0xfb0a2a

#define IMAGE_EXTENSIONS "gif|jpeg|jpg|png"
#define TEXT_EXTENSIONS "java|html|properties|smali|txt|xml|yaml|yml"

#define URL_CONTRIBUTE "https://github.com/vaibhavpandeyvpz/apkstudio"
#define URL_DOCUMENTATION "https://vaibhavpandey.com/apkstudio/"
#define URL_ISSUES "https://github.com/vaibhavpandeyvpz/apkstudio/issues"
#define URL_THANKS "https://forum.xda-developers.com/showthread.php?t=2493107"

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

MainWindow::MainWindow(const QMap<QString, QString> &versions, QWidget *parent)
    : QMainWindow(parent), m_FindReplaceDialog(nullptr), m_FindInFilesDialog(nullptr)
{
    addDockWidget(Qt::LeftDockWidgetArea, m_DockProject = buildProjectsDock());
    addDockWidget(Qt::LeftDockWidgetArea, m_DockFiles = buildFilesDock());
    addDockWidget(Qt::BottomDockWidgetArea, m_DockConsole = buildConsoleDock());
    addToolBar(Qt::LeftToolBarArea, m_MainToolBar = buildMainToolBar());
    // 安装事件过滤器，当工具栏通过上下文菜单隐藏/显示时同步菜单操作
    m_MainToolBar->installEventFilter(this);
    setCentralWidget(buildCentralWidget());
    setMenuBar(buildMenuBar());
    setMinimumSize(WINDOW_WIDTH, WINDOW_HEIGHT);
    setStatusBar(buildStatusBar(versions));
    updateWindowTitle();
    
#ifdef Q_OS_LINUX
    // 为 Linux 窗口管理器显式设置窗口图标
    // 确保窗口运行时图标显示在侧边栏/坞站中
    setWindowIcon(QIcon(":/images/icon.png"));
#endif
    
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, &MainWindow::handleClipboardDataChanged);
    QSettings settings;
    if (settings.value("app_maximized").toBool()) {
        showMaximized();
    } else {
        resize(settings.value("app_size", QSize(WINDOW_WIDTH, WINDOW_HEIGHT)).toSize());
    }
    const QVariant state = settings.value("dock_state");
    if (state.isValid()) {
        restoreState(state.toByteArray());
    }
    m_ActionViewProject->setChecked(m_DockProject->isVisible());
    m_ActionViewFiles->setChecked(m_DockFiles->isVisible());
    m_ActionViewConsole->setChecked(m_DockConsole->isVisible());
    m_ActionViewToolBar->setChecked(m_MainToolBar->isVisible());
    
    // 确保主窗口获得焦点而不是搜索框
    setFocus();
    
#ifdef Q_OS_LINUX
    // 在 Linux 上检查并安装桌面文件（窗口显示后）
    QTimer::singleShot(500, this, &MainWindow::checkAndInstallDesktopFile);
#endif
    
    QTimer::singleShot(500, [=] {
        QSettings settings;
        const QStringList files = settings.value("open_files").toStringList();
        foreach (const QString &file, files) {
            if (QFile::exists(file)) {
                openFile(file);
            }
        }
        bool missing = false;
        foreach (const QString &binary, versions.keys()) {
            if (versions[binary].isEmpty()) {
#ifdef QT_DEBUG
                qDebug() << binary << "缺失";
#endif
                missing = true;
                break;
            }
        }
        if (missing) {
            QMessageBox msgBox(this);
            msgBox.setWindowTitle(tr("需求"));
            msgBox.setText(tr("一个或多个必需的第三方二进制文件缺失。"));
            msgBox.setInformativeText(tr("是否要自动下载它们或手动配置？"));
            QPushButton *downloadButton = msgBox.addButton(tr("下载"), QMessageBox::AcceptRole);
            QPushButton *settingsButton = msgBox.addButton(tr("设置"), QMessageBox::ActionRole);
            msgBox.addButton(tr("取消"), QMessageBox::RejectRole);
            
            int result = msgBox.exec();
            if (msgBox.clickedButton() == downloadButton) {
                // 下载所有缺失的工具
                QList<ToolDownloadWorker::ToolType> toolsToDownload;
                foreach (const QString &binary, versions.keys()) {
                    if (versions[binary].isEmpty()) {
                        if (binary == "java") {
                            toolsToDownload.append(ToolDownloadWorker::Java);
                        } else if (binary == "apktool") {
                            toolsToDownload.append(ToolDownloadWorker::Apktool);
                        } else if (binary == "jadx") {
                            toolsToDownload.append(ToolDownloadWorker::Jadx);
                        } else if (binary == "adb") {
                            toolsToDownload.append(ToolDownloadWorker::Adb);
                        } else if (binary == "uas") {
                            toolsToDownload.append(ToolDownloadWorker::UberApkSigner);
                        }
                    }
                }
                
                if (!toolsToDownload.isEmpty()) {
                    ToolDownloadDialog downloadDialog(toolsToDownload, this);
                    if (downloadDialog.exec() == QDialog::Accepted && downloadDialog.wasSuccessful()) {
                        // 重启应用以检测新下载的工具
                        QApplication::exit(CODE_RESTART);
                    }
                }
            } else if (msgBox.clickedButton() == settingsButton) {
                // 设置
                (new SettingsDialog(1, this))->exec();
            }
        }
    });
}

QWidget *MainWindow::buildCentralWidget()
{
    m_CentralStack = new QStackedWidget(this);
    auto empty = new QLabel(m_CentralStack);
    empty->setAlignment(Qt::AlignCenter);
    empty->setMargin(32);
    empty->setStyleSheet("QLabel { color: rgba(0, 0, 0, 50%) }");
    empty->setText(tr("<h1>%1</h1><p>%2</p>")
                .arg("没有打开的文件。")
                .arg("您需要先反编译一个 APK 或打开一个已反编译的文件夹。完成后，单击左侧树中的任何文件即可查看/编辑。"));
    empty->setWordWrap(true);
    m_CentralStack->addWidget(empty);
    m_CentralStack->addWidget(m_TabEditors = new QTabWidget(this));
    connect(m_TabEditors, &QTabWidget::currentChanged, this, &MainWindow::handleTabChanged);
    connect(m_TabEditors, &QTabWidget::tabCloseRequested, this, &MainWindow::handleTabCloseRequested);
    m_TabEditors->setTabsClosable(true);
    m_CentralStack->setCurrentIndex(0);
    return m_CentralStack;
}

QDockWidget *MainWindow::buildConsoleDock()
{
    auto dock = new QDockWidget(tr("控制台"), this);
    QFont font;
#ifdef Q_OS_WIN
    font.setFamily("Courier New");
#elif defined(Q_OS_MACOS)
    font.setFamily("Monaco");
#else
    font.setFamily("Ubuntu Mono");
#endif
    font.setFixedPitch(true);
    font.setPointSize(10);
    font.setStyleHint(QFont::Monospace);
    QFontMetrics metrics(font);
    QPalette palette;
    palette.setColor(QPalette::Active, QPalette::Base, QColor("#000000"));
    palette.setColor(QPalette::Inactive, QPalette::Base, QColor("#111111"));
    m_EditConsole = new QTextEdit(this);
    m_EditConsole->setFont(font);
    m_EditConsole->setFrameStyle(QFrame::NoFrame);
    m_EditConsole->setPalette(palette);
    m_EditConsole->setReadOnly(true);
    m_EditConsole->setTabStopDistance(4 * metrics.horizontalAdvance('8'));
    m_EditConsole->setWordWrapMode(QTextOption::NoWrap);
    connect(ProcessOutput::instance(), &ProcessOutput::commandFinished, this, &MainWindow::handleCommandFinished);
    connect(ProcessOutput::instance(), &ProcessOutput::commandStarting, this, &MainWindow::handleCommandStarting);
    setContentsMargins(2, 2, 2, 2);
    dock->setObjectName("ConsoleDock");
    dock->setWidget(m_EditConsole);
    return dock;
}

QDockWidget *MainWindow::buildFilesDock()
{
    auto dock = new QDockWidget(tr("文件"), this);
    auto widget = new QWidget(this);
    auto layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    
    m_SearchFiles = new QLineEdit(this);
    m_SearchFiles->setPlaceholderText(tr("在打开的文件中搜索..."));
    m_SearchFiles->setClearButtonEnabled(true);
    connect(m_SearchFiles, &QLineEdit::textChanged, this, &MainWindow::handleFilesSearchChanged);
    layout->addWidget(m_SearchFiles);
    
    m_ListOpenFiles = new QListView(this);
    m_ListOpenFiles->setContextMenuPolicy(Qt::CustomContextMenu);
    m_ListOpenFiles->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_ListOpenFiles->setMinimumWidth(240);
    m_ModelOpenFiles = new QStandardItemModel(m_ListOpenFiles);
    m_FilesProxyModel = new QSortFilterProxyModel(this);
    m_FilesProxyModel->setSourceModel(m_ModelOpenFiles);
    m_FilesProxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_FilesProxyModel->setFilterRole(Qt::DisplayRole);
    m_ListOpenFiles->setModel(m_FilesProxyModel);
    m_ListOpenFiles->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_ListOpenFiles->setSelectionMode(QAbstractItemView::SingleSelection);
    // 设置模型后获取选择模型以确保使用代理模型索引
    QItemSelectionModel *selectionModel = m_ListOpenFiles->selectionModel();
    if (selectionModel) {
        connect(selectionModel, &QItemSelectionModel::selectionChanged, this, &MainWindow::handleFilesSelectionChanged);
    }
    layout->addWidget(m_ListOpenFiles);
    
    widget->setLayout(layout);
    dock->setObjectName("FilesDock");
    dock->setWidget(widget);
    return dock;
}

QToolBar *MainWindow::buildMainToolBar()
{
    auto toolbar = new QToolBar(tr("侧边栏"), this);
    toolbar->addAction(QIcon(":/icons/icons8/icons8-android-os-48.png"), tr("打开 APK"), this, &MainWindow::handleActionApk);
    toolbar->addAction(QIcon(":/icons/icons8/icons8-folder-48.png"), tr("打开文件夹"), this, &MainWindow::handleActionFolder);
    toolbar->addSeparator();
    toolbar->addAction(QIcon(":/icons/icons8/icons8-gear-48.png"), tr("设置"), this, &MainWindow::handleActionSettings);
    auto empty = new QWidget(this);
    empty->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    toolbar->addWidget(empty);
    m_ActionBuild2 = toolbar->addAction(QIcon(":/icons/icons8/icons8-hammer-48.png"), tr("项目构建"), this, &MainWindow::handleActionBuild);
    m_ActionBuild2->setEnabled(false);
    m_ActionInstall2 = toolbar->addAction(QIcon(":/icons/icons8/icons8-software-installer-48.png"), tr("项目安装"), this, &MainWindow::handleActionInstall);
    m_ActionInstall2->setEnabled(false);
    toolbar->setIconSize(QSize(48, 48));
    toolbar->setMovable(false);
    toolbar->setObjectName("Toolbar");
    return toolbar;
}

QMenuBar *MainWindow::buildMenuBar()
{
    auto menubar = new QMenuBar(this);
    auto file = menubar->addMenu(tr("文件"));
    auto open = file->addMenu(tr("打开"));
    open->addAction(tr("APK"), this, &MainWindow::handleActionApk, QKeySequence::New);
    open->addAction(tr("文件夹"), this, &MainWindow::handleActionFolder, QKeySequence::Open);
    open->addSeparator();
    open->addAction(tr("文件"), this, &MainWindow::handleActionFile);
    file->addSeparator();
    m_ActionClose = file->addAction(tr("关闭"), this, &MainWindow::handleActionClose, QKeySequence::Close);
    m_ActionClose->setEnabled(false);
    m_ActionCloseAll = file->addAction(tr("全部关闭"), this, &MainWindow::handleActionCloseAll);
    m_ActionCloseAll->setEnabled(false);
    file->addSeparator();
    m_ActionSave = file->addAction(tr("保存"), this, &MainWindow::handleActionSave, QKeySequence::Save);
    m_ActionSave->setEnabled(false);
    m_ActionSaveAll = file->addAction(tr("全部保存"), this, &MainWindow::handleActionSaveAll);
    m_ActionSaveAll->setEnabled(false);
    file->addSeparator();
    file->addAction(tr("退出"), this, &MainWindow::handleActionQuit, QKeySequence::Quit);
    auto edit = menubar->addMenu(tr("编辑"));
    m_ActionUndo = edit->addAction(tr("撤销"), this, &MainWindow::handleActionUndo, QKeySequence::Undo);
    m_ActionUndo->setEnabled(false);
    m_ActionRedo = edit->addAction(tr("重做"), this, &MainWindow::handleActionRedo, QKeySequence::Redo);
    m_ActionRedo->setEnabled(false);
    edit->addSeparator();
    m_ActionCut = edit->addAction(tr("剪切"), this, &MainWindow::handleActionCut, QKeySequence::Cut);
    m_ActionCut->setEnabled(false);
    m_ActionCopy = edit->addAction(tr("复制"), this, &MainWindow::handleActionCopy, QKeySequence::Copy);
    m_ActionCopy->setEnabled(false);
    m_ActionPaste = edit->addAction(tr("粘贴"), this, &MainWindow::handleActionPaste, QKeySequence::Paste);
    m_ActionPaste->setEnabled(false);
    edit->addSeparator();
    m_ActionFind = edit->addAction(tr("查找"), this, &MainWindow::handleActionFind, QKeySequence::Find);
    m_ActionFind->setEnabled(false);
    m_ActionFindInFiles = edit->addAction(tr("在文件中查找"), this, &MainWindow::handleActionFindInFiles);
    m_ActionReplace = edit->addAction(tr("替换"), this, &MainWindow::handleActionReplace, QKeySequence::Replace);
    m_ActionReplace->setEnabled(false);
    m_ActionGoto = edit->addAction(tr("转到行"), this, &MainWindow::handleActionGoto);
    m_ActionGoto->setEnabled(false);
    edit->addSeparator();
    edit->addAction(tr("设置"), this, &MainWindow::handleActionSettings, QKeySequence::Preferences);
    auto view = menubar->addMenu(tr("视图"));
    m_ActionViewProject = view->addAction(tr("项目"));
    m_ActionViewProject->setCheckable(true);
    connect(m_ActionViewProject, &QAction::toggled, m_DockProject, &QDockWidget::setVisible);
    connect(m_DockProject, &QDockWidget::visibilityChanged, [this](bool isVisible) {
        if (!(windowState() & Qt::WindowMinimized)) {
            m_ActionViewProject->setChecked(isVisible);
        }
    });
    m_ActionViewFiles = view->addAction(tr("文件"));
    m_ActionViewFiles->setCheckable(true);
    connect(m_ActionViewFiles, &QAction::toggled, m_DockFiles, &QDockWidget::setVisible);
    connect(m_DockFiles, &QDockWidget::visibilityChanged, [this](bool isVisible) {
        if (!(windowState() & Qt::WindowMinimized)) {
            m_ActionViewFiles->setChecked(isVisible);
        }
    });
    m_ActionViewConsole = view->addAction(tr("控制台"));
    m_ActionViewConsole->setCheckable(true);
    connect(m_ActionViewConsole, &QAction::toggled, m_DockConsole, &QDockWidget::setVisible);
    connect(m_DockConsole, &QDockWidget::visibilityChanged, [this](bool isVisible) {
        if (!(windowState() & Qt::WindowMinimized)) {
            m_ActionViewConsole->setChecked(isVisible);
        }
    });
    view->addSeparator();
    m_ActionViewToolBar = view->addAction(tr("侧边栏"));
    m_ActionViewToolBar->setCheckable(true);
    connect(m_ActionViewToolBar, &QAction::toggled, m_MainToolBar, &QToolBar::setVisible);
    connect(m_MainToolBar, &QToolBar::visibilityChanged, [this](bool isVisible) {
        if (!(windowState() & Qt::WindowMinimized)) {
            m_ActionViewToolBar->setChecked(isVisible);
        }
    });
    auto project = menubar->addMenu(tr("项目"));
    m_ActionBuild1 = project->addAction(tr("构建"), this, &MainWindow::handleActionBuild);
    m_ActionBuild1->setEnabled(false);
    project->addSeparator();
    m_ActionSign = project->addAction(tr("签名/导出"), this, &MainWindow::handleActionSign);
    m_ActionSign->setEnabled(false);
    m_ActionInstall1 = project->addAction(tr("安装"), this, &MainWindow::handleActionInstall);
    m_ActionInstall1->setEnabled(false);
    project->addSeparator();
    project->addAction(tr("安装框架"), this, &MainWindow::handleActionInstallFramework);
    auto help = menubar->addMenu(tr("帮助"));
    help->addAction(tr("关于"), this, &MainWindow::handleActionAbout);
    help->addAction(tr("文档"), this, &MainWindow::handleActionDocumentation);
    help->addSeparator();
    auto feedback = help->addMenu(tr("反馈"));
    feedback->addAction(tr("感谢"), this, &MainWindow::handleActionSayThanks);
    feedback->addAction(tr("报告问题"), this, &MainWindow::handleActionReportIssues);
    help->addAction(tr("贡献"), this, &MainWindow::handleActionContribute);
    return menubar;
}

QDockWidget *MainWindow::buildProjectsDock()
{
    auto dock = new QDockWidget(tr("项目"), this);
    auto widget = new QWidget(this);
    auto layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    
    m_SearchProjects = new QLineEdit(this);
    m_SearchProjects->setPlaceholderText(tr("在项目中搜索..."));
    m_SearchProjects->setClearButtonEnabled(true);
    connect(m_SearchProjects, &QLineEdit::textChanged, this, &MainWindow::handleProjectsSearchChanged);
    layout->addWidget(m_SearchProjects);
    
    m_ProjectsTree = new QTreeWidget(this);
    m_ProjectsTree->header()->hide();
    m_ProjectsTree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_ProjectsTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_ProjectsTree->setMinimumWidth(240);
    m_ProjectsTree->setSelectionBehavior(QAbstractItemView::SelectItems);
    m_ProjectsTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_ProjectsTree->setSortingEnabled(false);
    connect(m_ProjectsTree, &QTreeWidget::customContextMenuRequested, this, &MainWindow::handleTreeContextMenu);
    connect(m_ProjectsTree, &QTreeWidget::doubleClicked, this, &MainWindow::handleTreeDoubleClicked);
    connect(m_ProjectsTree->selectionModel(), &QItemSelectionModel::selectionChanged, this, &MainWindow::handleTreeSelectionChanged);
    layout->addWidget(m_ProjectsTree);
    
    widget->setLayout(layout);
    dock->setObjectName("ProjectsDock");
    dock->setWidget(widget);
    return dock;
}

QStatusBar *MainWindow::buildStatusBar(const QMap<QString, QString> &versions)
{
    auto buildSeparator = [=] {
        auto frame = new QFrame(this);
        frame->setFrameStyle(QFrame::VLine);
        frame->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Expanding);
        return frame;
    };
    auto statusbar = new QStatusBar(this);
    statusbar->addPermanentWidget(new QLabel(tr("Java").append(": ").append(versions["java"]), this));
    statusbar->addPermanentWidget(buildSeparator());
    statusbar->addPermanentWidget(new QLabel(tr("Apktool").append(": ").append(versions["apktool"]), this));
    statusbar->addPermanentWidget(buildSeparator());
    statusbar->addPermanentWidget(new QLabel(tr("Jadx").append(": ").append(versions["jadx"]), this));
    statusbar->addPermanentWidget(buildSeparator());
    statusbar->addPermanentWidget(new QLabel(tr("ADB").append(": ").append(versions["adb"]), this));
    statusbar->addPermanentWidget(buildSeparator());
    statusbar->addPermanentWidget(new QLabel(tr("Uber APK Signer").append(": ").append(versions["uas"]), this));
    statusbar->addPermanentWidget(new QWidget(this), 1);
    statusbar->addPermanentWidget(m_StatusCursor = new QLabel("0:0", this));
    statusbar->addPermanentWidget(buildSeparator());
    statusbar->addPermanentWidget(m_StatusMessage = new QLabel(tr("就绪！"), this));
    statusbar->setContentsMargins(4, 4, 4, 4);
    statusbar->setStyleSheet("QStatusBar::item { border: none; }");
    return statusbar;
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    // 当工具栏通过上下文菜单显示/隐藏时同步工具栏菜单操作
    if (obj == m_MainToolBar && m_ActionViewToolBar) {
        if (event->type() == QEvent::Show || event->type() == QEvent::Hide) {
            m_ActionViewToolBar->setChecked(m_MainToolBar->isVisible());
        }
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    Q_UNUSED(event)
    QSettings settings;
    bool maximized = isMaximized();
    settings.setValue("app_maximized", maximized);
    if (!maximized) {
        settings.setValue("app_size", size());
    }
    settings.setValue("dock_state", saveState());
    QStringList files;
    const int total = m_ModelOpenFiles->rowCount();
    for (int i = 0; i < total; ++i) {
        files << m_ModelOpenFiles->index(i, 0).data(Qt::UserRole + 1).toString();
    }
    settings.setValue("open_files", QVariant::fromValue(files));
    settings.sync();
}

int MainWindow::findTabIndex(const QString &path)
{
    int total = m_TabEditors->count();
    for (int i = 0; i < total; i++) {
        QString path2;
        auto widget = m_TabEditors->widget(i);
        auto edit = dynamic_cast<SourceCodeEdit *>(widget);
        auto hex = dynamic_cast<HexEdit *>(widget);
        auto viewer = dynamic_cast<ImageViewerWidget *>(widget);
        if (edit) {
            path2 = edit->filePath();
        } else if (hex) {
            path2 = hex->filePath();
        } else if (viewer) {
            path2 = viewer->filePath();
        }
        if (QString::compare(path, path2) == 0) {
            return i;
        }
    }
    return -1;
}

void MainWindow::handleActionAbout()
{
    QMessageBox box;
    box.setIconPixmap(QPixmap(":/images/icon.png").scaledToWidth(128));
    QFile about(":/about.html");
    if (about.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream stream(&about);
        box.setInformativeText(stream.readAll());
        about.close();
    }
    box.setText(QString("<strong>标签</strong>: %1<br><strong>提交</strong>: %2").arg(GIT_TAG).arg(GIT_COMMIT_FULL));
    box.setWindowTitle(tr("关于"));
    box.exec();
}

void MainWindow::handleActionApk()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      tr("浏览 APK"),
                                                      QString(),
                                                      tr("Android APK 文件 (*.apk)"));
#ifdef QT_DEBUG
    qDebug() << "用户选择打开" << path;
#endif
    if (!path.isEmpty()) {
        openApkFile(path);
    }
}

void MainWindow::openApkFile(const QString &apkPath)
{
    if (apkPath.isEmpty() || !QFile::exists(apkPath)) {
        return;
    }
    
    auto dialog = new ApkDecompileDialog(QDir::toNativeSeparators(apkPath), this);
        if (dialog->exec() == QDialog::Accepted) {
            auto thread = new QThread();
            auto worker = new ApkDecompileWorker(dialog->apk(), dialog->folder(), dialog->smali(), dialog->resources(), dialog->java(), dialog->frameworkTag(), dialog->extraArguments());
            worker->moveToThread(thread);
            connect(worker, &ApkDecompileWorker::decompileFailed, this, &MainWindow::handleDecompileFailed);
            connect(worker, &ApkDecompileWorker::decompileFinished, this, &MainWindow::handleDecompileFinished);
            connect(worker, &ApkDecompileWorker::decompileProgress, this, &MainWindow::handleDecompileProgress);
            connect(thread, &QThread::started, worker, &ApkDecompileWorker::decompile);
            connect(worker, &ApkDecompileWorker::finished, thread, &QThread::quit);
            connect(worker, &ApkDecompileWorker::finished, worker, &QObject::deleteLater);
            connect(thread, &QThread::finished, thread, &QObject::deleteLater);
            thread->start();
            m_ProgressDialog = new QProgressDialog(this);
            m_ProgressDialog->setCancelButton(nullptr);
            m_ProgressDialog->setLabelText(tr("正在运行 apktool..."));
            m_ProgressDialog->setRange(0, 100);
            m_ProgressDialog->setWindowFlags(m_ProgressDialog->windowFlags() & ~Qt::WindowCloseButtonHint);
            m_ProgressDialog->setWindowTitle(tr("反编译中"));
            m_ProgressDialog->exec();
        }
        dialog->deleteLater();
}

void MainWindow::handleActionBuild()
{
    auto active = m_ProjectsTree->currentItem();
    if (!active) {
        active = m_ProjectsTree->topLevelItem(0);
    }
    while (active->data(0, Qt::UserRole + 1).toInt() != Project) {
        active = active->parent();
    }
    QSettings settings;
    auto appt2 = settings.value("use_aapt2", true).toBool();
    
    // 询问额外参数（可选）
    bool ok;
    const QString extraArgs = QInputDialog::getText(this,
                                                    tr("额外参数"),
                                                    tr("输入附加的 apktool 参数（可选）：\n多个参数请用空格分隔。\n示例：--force-all --no-res"),
                                                    QLineEdit::Normal,
                                                    QString(),
                                                    &ok);
    QString extraArguments;
    if (ok && !extraArgs.trimmed().isEmpty()) {
        extraArguments = extraArgs.trimmed();
    }
    
    auto thread = new QThread();
    auto worker = new ApkRecompileWorker(active->data(0, Qt::UserRole + 2).toString(), appt2, extraArguments);
    worker->moveToThread(thread);
    connect(worker, &ApkRecompileWorker::recompileFailed, this, &MainWindow::handleRecompileFailed);
    connect(worker, &ApkRecompileWorker::recompileFinished, this, &MainWindow::handleRecompileFinished);
    connect(thread, &QThread::started, worker, &ApkRecompileWorker::recompile);
    connect(worker, &ApkRecompileWorker::finished, thread, &QThread::quit);
    connect(worker, &ApkRecompileWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    m_ProgressDialog = new QProgressDialog(this);
    m_ProgressDialog->setCancelButton(nullptr);
    m_ProgressDialog->setLabelText(tr("正在运行 apktool..."));
    m_ProgressDialog->setRange(0, 100);
    m_ProgressDialog->setValue(50);
    m_ProgressDialog->setWindowFlags(m_ProgressDialog->windowFlags() & ~Qt::WindowCloseButtonHint);
    m_ProgressDialog->setWindowTitle(tr("重新编译中"));
    m_ProgressDialog->exec();
}

void MainWindow::handleActionClose()
{
    int i = m_TabEditors->currentIndex();
    if (i >= 0) {
        handleTabCloseRequested(i);
    }
}

void MainWindow::handleActionCloseAll()
{
    int i = m_TabEditors->count();
    for (int j = --i; j >= 0; j--) {
        handleTabCloseRequested(j);
    }
}

void MainWindow::handleActionContribute()
{
    QDesktopServices::openUrl(QUrl(URL_CONTRIBUTE));
}

void MainWindow::handleActionCopy()
{
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    edit->copy();
}

void MainWindow::handleActionCut()
{
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    edit->cut();
}

void MainWindow::handleActionDocumentation()
{
    QDesktopServices::openUrl(QUrl(URL_DOCUMENTATION));
}

void MainWindow::handleActionFile()
{
    const QString path = QFileDialog::getOpenFileName(this,
                                                      tr("浏览文件"),
                                                      QString());
#ifdef QT_DEBUG
    qDebug() << "用户选择打开" << path;
#endif
    if (!path.isEmpty()) {
        openFile(path);
    }
}

void MainWindow::handleActionFind()
{
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    openFindReplaceDialog(edit, false);
}

void MainWindow::handleActionFindInFiles()
{
    QStringList roots = getProjectRoots();
    if (roots.isEmpty()) {
        QMessageBox::information(this, tr("在文件中查找"), tr("没有打开的项目文件夹。请先打开一个项目。"));
        return;
    }
    
    if (!m_FindInFilesDialog) {
        m_FindInFilesDialog = new FindInFilesDialog(this);
        connect(m_FindInFilesDialog, &QDialog::finished, [=] {
            m_FindInFilesDialog->deleteLater();
            m_FindInFilesDialog = nullptr;
        });
    }
    
    // 使用第一个（最近）的项目根目录
    m_FindInFilesDialog->setSearchRoot(roots.first());
    m_FindInFilesDialog->show();
    m_FindInFilesDialog->raise();
    m_FindInFilesDialog->activateWindow();
}

void MainWindow::handleActionFolder()
{
    QSettings settings;
    const QString project = settings.value("open_project").toString();
    const QString path = QFileDialog::getOpenFileName(this,
                                                      tr("浏览文件夹 (apktool.yml)"),
                                                      project,
                                                      tr("Apktool 项目文件 (apktool.yml)"));
#ifdef QT_DEBUG
    qDebug() << "用户选择打开" << path;
#endif
    if (!path.isEmpty()) {
        openProject(QFileInfo(path).dir().absolutePath());
    }
}

void MainWindow::handleActionGoto()
{
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    QTextCursor cursor = edit->textCursor();
    const int line = QInputDialog::getInt(this, tr("转到行"), tr("输入行号："), cursor.blockNumber() + 1, 1, edit->document()->lineCount());
    if (line > 0) {
        edit->gotoLine(line);
    }
}

void MainWindow::handleActionInstall()
{
    auto selected = m_ProjectsTree->selectedItems().first();
    const QString path = selected->data(0, Qt::UserRole + 2).toString();
#ifdef QT_DEBUG
    qDebug() << "用户希望安装" << path;
#endif
    
    // 显示设备选择对话框
    DeviceSelectionDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    
    QString deviceSerial = dialog.selectedDeviceSerial();
    if (deviceSerial.isEmpty()) {
        QMessageBox::warning(this, tr("错误"), tr("未选择设备。"));
        return;
    }
    
    auto thread = new QThread();
    auto worker = new AdbInstallWorker(path, deviceSerial);
    worker->moveToThread(thread);
    connect(worker, &AdbInstallWorker::installFailed, this, &MainWindow::handleInstallFailed);
    connect(worker, &AdbInstallWorker::installFinished, this, &MainWindow::handleInstallFinished);
    connect(thread, &QThread::started, worker, &AdbInstallWorker::install);
    connect(worker, &AdbInstallWorker::finished, thread, &QThread::quit);
    connect(worker, &AdbInstallWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    m_ProgressDialog = new QProgressDialog(this);
    m_ProgressDialog->setCancelButton(nullptr);
    m_ProgressDialog->setLabelText(tr("正在运行 adb install..."));
    m_ProgressDialog->setRange(0, 100);
    m_ProgressDialog->setValue(50);
    m_ProgressDialog->setWindowFlags(m_ProgressDialog->windowFlags() & ~Qt::WindowCloseButtonHint);
    m_ProgressDialog->setWindowTitle(tr("安装中..."));
    m_ProgressDialog->exec();
}

void MainWindow::handleActionInstallFramework()
{
    const QString frameworkPath = QFileDialog::getOpenFileName(this,
                                                                 tr("选择框架 APK"),
                                                                 QString(),
                                                                 tr("Android APK 文件 (*.apk)"));
    if (frameworkPath.isEmpty()) {
        return;
    }
    
    bool ok;
    const QString tag = QInputDialog::getText(this,
                                               tr("框架标签"),
                                               tr("输入可选的框架标签（留空使用默认）："),
                                               QLineEdit::Normal,
                                               QString(),
                                               &ok);
    if (!ok) {
        return;
    }
    
    const QString java = ProcessUtils::javaExe();
    const QString apktool = ProcessUtils::apktoolJar();
    if (java.isEmpty() || apktool.isEmpty()) {
        QMessageBox::warning(this, tr("错误"), tr("未找到 Java 或 Apktool。请在设置中配置它们。"));
        return;
    }
    
    QString heap("-Xmx%1m");
    heap = heap.arg(QString::number(ProcessUtils::javaHeapSize()));
    QStringList args;
    args << heap << "-jar" << apktool;
    args << "if" << QDir::toNativeSeparators(frameworkPath);
    if (!tag.trimmed().isEmpty()) {
        args << "-t" << tag.trimmed();
    }
    
    ProcessResult result = ProcessUtils::runCommand(java, args);
    
    if (result.code == 0) {
        QString message = tr("框架安装成功！");
        if (!result.output.isEmpty()) {
            message += "\n\n" + result.output.join("\n");
        }
        QMessageBox::information(this, tr("成功"), message);
    } else {
        QString error = tr("框架安装失败！");
        if (!result.error.isEmpty()) {
            error += "\n\n" + result.error.join("\n");
        } else if (!result.output.isEmpty()) {
            error += "\n\n" + result.output.join("\n");
        }
        QMessageBox::warning(this, tr("错误"), error);
    }
}

void MainWindow::handleActionPaste()
{
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    if (edit->canPaste()) {
        edit->paste();
    }
}

void MainWindow::handleActionQuit()
{
    close();
}

void MainWindow::handleActionRedo()
{
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    edit->redo();
}

void MainWindow::handleActionReplace()
{
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    openFindReplaceDialog(edit, false);
}

void MainWindow::handleActionReportIssues()
{
    QDesktopServices::openUrl(QUrl(URL_ISSUES));
}

void MainWindow::handleActionSave()
{
    auto i = m_TabEditors->currentIndex();
    if (i >= 0) {
        saveTab(i);
    }
}

void MainWindow::handleActionSaveAll()
{
    int i = m_TabEditors->count();
    for (int j = 0; j < i; j++) {
        saveTab(j);
    }
}

void MainWindow::handleActionSayThanks()
{
    QDesktopServices::openUrl(QUrl(URL_THANKS));
}

void MainWindow::handleActionSettings()
{
    (new SettingsDialog(0, this))->exec();
}

void MainWindow::handleActionSign()
{
    auto selected = m_ProjectsTree->selectedItems().first();
    const QString path = selected->data(0, Qt::UserRole + 2).toString();
#ifdef QT_DEBUG
    qDebug() << "用户希望签名" << path;
#endif
    auto dialog = new SigningConfigDialog(this);
    if (dialog->exec() == QDialog::Accepted) {
        QSettings settings;
        auto thread = new QThread();
        auto worker = new ApkSignWorker(
                    path,
                    settings.value("signing_keystore").toString(),
                    settings.value("signing_keystore_password").toString(),
                    settings.value("signing_alias").toString(),
                    settings.value("signing_alias_password").toString(),
                    settings.value("signing_zipalign", true).toBool());
        worker->moveToThread(thread);
        connect(worker, &ApkSignWorker::signFailed, this, &MainWindow::handleSignFailed);
        connect(worker, &ApkSignWorker::signFinished, this, &MainWindow::handleSignFinished);
        connect(thread, &QThread::started, worker, &ApkSignWorker::sign);
        connect(worker, &ApkSignWorker::finished, thread, &QThread::quit);
        connect(worker, &ApkSignWorker::finished, worker, &QObject::deleteLater);
        connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        thread->start();
        m_ProgressDialog = new QProgressDialog(this);
        m_ProgressDialog->setCancelButton(nullptr);
        m_ProgressDialog->setLabelText(tr("正在运行 uber-apk-signer..."));
        m_ProgressDialog->setRange(0, 100);
        m_ProgressDialog->setValue(50);
        m_ProgressDialog->setWindowFlags(m_ProgressDialog->windowFlags() & ~Qt::WindowCloseButtonHint);
        m_ProgressDialog->setWindowTitle(tr("签名中"));
        m_ProgressDialog->exec();
    }
}

void MainWindow::handleActionUndo()
{
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    edit->undo();
}

void MainWindow::handleClipboardDataChanged()
{
#ifdef QT_DEBUG
    qDebug() << "剪贴板内容已更改。";
#endif
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    if (edit) {
        m_ActionPaste->setEnabled(edit && edit->canPaste());
    }
}

void MainWindow::handleCommandFinished(const ProcessResult &result)
{
    if (!result.error.isEmpty()) {
        m_EditConsole->setTextColor(QColor(COLOR_ERROR));
        foreach (auto line, result.error) {
            m_EditConsole->append(line);
        }
    }
    if (!result.output.isEmpty()) {
        m_EditConsole->setTextColor(QColor(COLOR_OUTPUT));
        foreach (auto line, result.output) {
            m_EditConsole->append(line);
        }
    }
    m_EditConsole->setTextColor(QColor(COLOR_CODE));
    m_EditConsole->append(QString("进程退出，代码 %1。").arg(result.code));
    m_EditConsole->append(QString());
}

void MainWindow::handleCommandStarting(const QString &exe, const QStringList &args)
{
    QString line = "$ " + exe;
    foreach (auto arg, args) {
        QString argument(arg);
        if (arg.contains(' ')) {
            argument.prepend('"');
            argument.append('"');
        }
        line.append(' ' + arg);
    }
    m_EditConsole->setTextColor(QColor(COLOR_COMMAND));
    m_EditConsole->append(line.trimmed());
}

void MainWindow::handleCursorPositionChanged()
{
    auto edit = dynamic_cast<SourceCodeEdit *>(m_TabEditors->currentWidget());
    if (edit) {
        QTextCursor cursor = edit->textCursor();
        const QString position = QString("%1:%2").arg(cursor.blockNumber() + 1).arg(cursor.positionInBlock() + 1);
        m_StatusCursor->setText(position);
    } else {
        m_StatusCursor->setText("0:0");
    }
}

void MainWindow::handleDecompileFailed(const QString &apk)
{
    Q_UNUSED(apk)
    m_ProgressDialog->close();
    m_ProgressDialog->deleteLater();
    m_StatusMessage->setText(tr("反编译失败。"));
}

void MainWindow::handleDecompileFinished(const QString &apk, const QString &folder)
{
    Q_UNUSED(apk)
    m_ProgressDialog->close();
    m_ProgressDialog->deleteLater();
    m_StatusMessage->setText(tr("反编译完成。"));
    openProject(folder);
}

void MainWindow::handleDecompileProgress(const int percent, const QString &message)
{
    m_ProgressDialog->setLabelText(message);
    m_ProgressDialog->setValue(percent);
}

void MainWindow::handleFilesSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    Q_UNUSED(deselected)
    if (selected.isEmpty() || !m_FilesProxyModel || !m_ModelOpenFiles) {
        return;
    }
    
    auto proxyIndex = selected.indexes().first();
    if (!proxyIndex.isValid()) {
        return;
    }
    
    // 验证索引属于代理模型 - 这对防止断言至关重要
    const QAbstractItemModel *indexModel = proxyIndex.model();
#ifdef QT_DEBUG
    if (indexModel != m_FilesProxyModel) {
        qDebug() << "警告：选择索引来自错误的模型。期望：" << m_FilesProxyModel << "实际：" << indexModel;
        return;
    }
#else
    if (!indexModel || indexModel != m_FilesProxyModel) {
        return;
    }
#endif
    
    // 额外的安全检查：验证代理模型仍然有效
    if (!m_FilesProxyModel->sourceModel() || m_FilesProxyModel->sourceModel() != m_ModelOpenFiles) {
#ifdef QT_DEBUG
        qDebug() << "警告：代理模型源不匹配";
#endif
        return;
    }
    
    auto sourceIndex = m_FilesProxyModel->mapToSource(proxyIndex);
    if (sourceIndex.isValid() && sourceIndex.model() == m_ModelOpenFiles) {
        const QString path = m_ModelOpenFiles->data(sourceIndex, Qt::UserRole + 1).toString();
        if (!path.isEmpty()) {
        m_TabEditors->setCurrentIndex(findTabIndex(path));
        }
    }
}

void MainWindow::handleFilesSearchChanged(const QString &text)
{
    m_FilesProxyModel->setFilterFixedString(text);
}

void MainWindow::handleProjectsSearchChanged(const QString &text)
{
    for (int i = 0; i < m_ProjectsTree->topLevelItemCount(); ++i) {
        filterProjectTreeItems(m_ProjectsTree->topLevelItem(i), text);
    }
}

void MainWindow::filterProjectTreeItems(QTreeWidgetItem *item, const QString &filter)
{
    if (!item) {
        return;
    }
    
    bool visible = false;
    QString itemText = item->text(0);
    
    // 检查此项是否匹配过滤器
    if (filter.isEmpty() || itemText.contains(filter, Qt::CaseInsensitive)) {
        visible = true;
    }
    
    // 递归检查子项
    for (int i = 0; i < item->childCount(); ++i) {
        QTreeWidgetItem *child = item->child(i);
        filterProjectTreeItems(child, filter);
        // 如果有任何子项可见，则此项也应当可见
        if (!child->isHidden()) {
            visible = true;
        }
    }
    
    item->setHidden(!visible);
    
    // 如果此项可见，展开其父项
    if (visible && item->parent()) {
        item->parent()->setExpanded(true);
    }
}

void MainWindow::handleInstallFailed(const QString &apk)
{
    Q_UNUSED(apk)
    m_ProgressDialog->close();
    m_ProgressDialog->deleteLater();
    m_StatusMessage->setText(tr("安装失败。"));
}

void MainWindow::handleInstallFinished(const QString &apk)
{
    Q_UNUSED(apk)
    m_ProgressDialog->close();
    m_ProgressDialog->deleteLater();
    m_StatusMessage->setText(tr("安装完成。"));
}

void MainWindow::handleRecompileFailed(const QString &folder)
{
    Q_UNUSED(folder)
    m_ProgressDialog->close();
    m_ProgressDialog->deleteLater();
    m_StatusMessage->setText(tr("重新编译失败。"));
}

void MainWindow::handleRecompileFinished(const QString &folder)
{
    Q_UNUSED(folder)
    m_ProgressDialog->close();
    m_ProgressDialog->deleteLater();
    m_StatusMessage->setText(tr("重新编译完成。"));
    QTreeWidgetItem *focus = nullptr;
    for (int i = 0; i < m_ProjectsTree->topLevelItemCount(); i++) {
        auto parent = m_ProjectsTree->topLevelItem(i);
        if (folder != parent->data(0, Qt::UserRole + 2).toString()) {
            continue;
        }
#ifdef QT_DEBUG
        qDebug() << "找到项目" << folder;
#endif
        reloadChildren(parent);
        auto dist = m_ProjectsTree->findItems(".apk", Qt::MatchEndsWith | Qt::MatchRecursive, 0);
        foreach (auto child, dist) {
            if (child->data(0, Qt::UserRole + 1).toInt() == File) {
                const QString path = child->data(0, Qt::UserRole + 2).toString();
#ifdef QT_DEBUG
                qDebug() << "找到文件" << path;
#endif
                if (path.startsWith(folder)) {
                    focus = child;
                    break;
                }
            }
        }
    }
    if (focus) {
        auto parent = focus->parent();
        while (parent) {
            if (!parent->isExpanded()) {
                m_ProjectsTree->expandItem(parent);
            }
            parent = parent->parent();
        }
        m_ProjectsTree->scrollToItem(focus);
        m_ProjectsTree->selectionModel()->clearSelection();
        focus->setSelected(true);
    }
}

void MainWindow::handleSignFailed(const QString &apk)
{
    Q_UNUSED(apk)
    m_ProgressDialog->close();
    m_ProgressDialog->deleteLater();
    m_StatusMessage->setText(tr("签名失败。"));
}

void MainWindow::handleSignFinished(const QString &apk)
{
    Q_UNUSED(apk)
    m_ProgressDialog->close();
    m_ProgressDialog->deleteLater();
    m_StatusMessage->setText(tr("签名完成。"));
    auto selected = m_ProjectsTree->selectedItems().first();
    auto parent = selected->parent();
    reloadChildren(parent);
    selected = parent->child(0);
    m_ProjectsTree->scrollToItem(selected);
    m_ProjectsTree->selectionModel()->clearSelection();
    selected->setSelected(true);
}

void MainWindow::handleTabChanged(const int index)
{
#ifdef QT_DEBUG
    qDebug() << "用户更改了当前标签页" << index;
#endif
    QString path;
    auto widget = m_TabEditors->currentWidget();
    auto edit = dynamic_cast<SourceCodeEdit *>(widget);
    auto hex = dynamic_cast<HexEdit *>(widget);
    auto viewer = dynamic_cast<ImageViewerWidget *>(widget);
    if (edit) {
        path = edit->filePath();
    } else if (hex) {
        path = hex->filePath();
    } else if (viewer) {
        path = viewer->filePath();
    }
    // 阻塞信号以防止选择更改处理程序触发
    QSignalBlocker blocker(m_ListOpenFiles);
    if (m_ListOpenFiles->selectionModel()) {
        QSignalBlocker selectionBlocker(m_ListOpenFiles->selectionModel());
    const int total = m_ModelOpenFiles->rowCount();
    for (int i = 0; i < total; ++i) {
            const QModelIndex &sourceIndex = m_ModelOpenFiles->index(i, 0);
            if (QString::compare(sourceIndex.data(Qt::UserRole + 1).toString(), path) == 0) {
                // 在设置选择之前将源索引映射到代理索引
                if (m_FilesProxyModel) {
                    QModelIndex proxyIndex = m_FilesProxyModel->mapFromSource(sourceIndex);
                    if (proxyIndex.isValid()) {
                        m_ListOpenFiles->setCurrentIndex(proxyIndex);
                    }
                }
            break;
            }
        }
    }
    m_ActionClose->setEnabled(index >= 0);
    m_ActionCloseAll->setEnabled(index >= 0);
    m_ActionCopy->setEnabled(false);
    m_ActionCut->setEnabled(false);
    m_ActionPaste->setEnabled(false);
    m_ActionRedo->setEnabled(false);
    m_ActionUndo->setEnabled(false);
    m_ActionFind->setEnabled(edit);
    m_ActionReplace->setEnabled(edit);
    m_ActionSave->setEnabled(edit || hex);
    m_ActionSaveAll->setEnabled(edit || hex);
    m_ActionGoto->setEnabled(edit);
    for (auto conn: m_EditorConnections) {
        disconnect(conn);
    }
    m_EditorConnections.clear();
    if (edit) {
        m_EditorConnections << connect(edit, &QPlainTextEdit::copyAvailable, m_ActionCopy, &QAction::setEnabled);
        m_EditorConnections << connect(edit, &QPlainTextEdit::copyAvailable, m_ActionCut, &QAction::setEnabled);
        m_EditorConnections << connect(edit, &QPlainTextEdit::redoAvailable, m_ActionRedo, &QAction::setEnabled);
        m_EditorConnections << connect(edit, &QPlainTextEdit::undoAvailable, m_ActionUndo, &QAction::setEnabled);
        m_EditorConnections << connect(edit, &QPlainTextEdit::cursorPositionChanged, this, &MainWindow::handleCursorPositionChanged);
        bool selected = !edit->textCursor().selection().isEmpty();
        m_ActionCut->setEnabled(selected);
        m_ActionCopy->setEnabled(selected);
        m_ActionPaste->setEnabled(edit->canPaste());
        m_ActionRedo->setEnabled(edit->document()->isRedoAvailable());
        m_ActionUndo->setEnabled(edit->document()->isUndoAvailable());
        if (m_FindReplaceDialog) {
            m_FindReplaceDialog->setTextEdit(edit);
        }
    }
    handleCursorPositionChanged();
}

void MainWindow::handleTabCloseRequested(const int index)
{
#ifdef QT_DEBUG
    qDebug() << "用户请求关闭标签页" << index;
#endif
    QString path;
    auto widget = m_TabEditors->widget(index);
    auto edit = dynamic_cast<SourceCodeEdit *>(widget);
    auto hex = dynamic_cast<HexEdit *>(widget);
    auto viewer = dynamic_cast<ImageViewerWidget *>(widget);
    if (edit) {
        path = edit->filePath();
    } else if (hex) {
        path = hex->filePath();
    } else if (viewer) {
        path = viewer->filePath();
    }
    // 在模型更新期间阻塞信号以防止选择更改错误
    QSignalBlocker blocker(m_ListOpenFiles);
    if (m_ListOpenFiles->selectionModel()) {
        QSignalBlocker selectionBlocker(m_ListOpenFiles->selectionModel());
    const int total = m_ModelOpenFiles->rowCount();
    for (int i = 0; i < total; ++i) {
        const QModelIndex &mindex = m_ModelOpenFiles->index(i, 0);
        if (QString::compare(mindex.data(Qt::UserRole + 1).toString(), path) == 0) {
            m_ModelOpenFiles->removeRow(mindex.row());
            break;
            }
        }
    } else {
        const int total = m_ModelOpenFiles->rowCount();
        for (int i = 0; i < total; ++i) {
            const QModelIndex &mindex = m_ModelOpenFiles->index(i, 0);
            if (QString::compare(mindex.data(Qt::UserRole + 1).toString(), path) == 0) {
                m_ModelOpenFiles->removeRow(mindex.row());
                break;
            }
        }
    }
    m_ActionUndo->setEnabled(false);
    m_ActionRedo->setEnabled(false);
    m_ActionCut->setEnabled(false);
    m_ActionCopy->setEnabled(false);
    m_ActionPaste->setEnabled(false);
    m_TabEditors->removeTab(index);
    if (m_TabEditors->count() == 0) {
        m_CentralStack->setCurrentIndex(0);
        // 关闭所有标签页时清除搜索框
        if (m_SearchFiles) {
            m_SearchFiles->clear();
        }
        if (m_SearchProjects) {
            m_SearchProjects->clear();
        }
    }
}

void MainWindow::handleTreeContextMenu(const QPoint &point)
{
    QMenu menu(this);
    auto item = m_ProjectsTree->itemAt(point);
    if (item) {
        const int type = item->data(0, Qt::UserRole + 1).toInt();
        const QString path = item->data(0, Qt::UserRole + 2).toString();
#ifdef QT_DEBUG
        qDebug() << "为" << item->text(0) << "在" << point << "请求上下文菜单";
#endif
        if (type == File) {
            auto open = menu.addAction(tr("打开"));
            connect(open, &QAction::triggered, [=] {
                openFile(path);
            });
        }
#ifdef Q_OS_WIN
        auto openin = menu.addAction(tr"在资源管理器中打开"));
        connect(openin, &QAction::triggered, [=] {
            QStringList args;
            if (type == File) {
                args << QLatin1String("/select,");
            }
            args << QDir::toNativeSeparators(path);
            QProcess::startDetached("explorer.exe", args);
        });
#elif defined(Q_OS_MACOS)
        auto openin = menu.addAction(tr("在访达中打开"));
        connect(openin, &QAction::triggered, [=] {
            QStringList args;
            args << "-e" << QString("tell application \"Finder\" to reveal POSIX file \"%1\"").arg(path);
            QProcess::execute("/usr/bin/osascript", args);
            args.clear();
            args << "-e" << "tell application \"Finder\" to activate";
            QProcess::execute("/usr/bin/osascript", args);
        });
#else
        auto openin = menu.addAction(tr("在文件管理器中打开"));
        connect(openin, &QAction::triggered, [=] {
            QProcess::startDetached("xdg-open", QStringList() << path);
        });
#endif
        menu.addSeparator();
        auto build = menu.addAction(tr("构建"));
        connect(build, &QAction::triggered, this, &MainWindow::handleActionBuild);
        if (path.endsWith(".apk")) {
            menu.addSeparator();
            auto install = menu.addAction(tr("安装"));
            connect(install, &QAction::triggered, this, &MainWindow::handleActionInstall);
            auto sign = menu.addAction(tr("签名/导出"));
            connect(sign, &QAction::triggered, this, &MainWindow::handleActionSign);
        }
        menu.addSeparator();
        if (type != File) {
            auto refresh = menu.addAction(tr("刷新"));
            connect(refresh, &QAction::triggered, [=] {
                reloadChildren(item);
            });
        }
    } else {
        auto apk = menu.addAction(tr("打开 APK"));
        connect(apk, &QAction::triggered, this, &MainWindow::handleActionApk);
        auto folder = menu.addAction(tr("打开文件夹"));
        connect(folder, &QAction::triggered, this, &MainWindow::handleActionFolder);
        menu.addSeparator();
    }
    auto collapse = menu.addAction(tr("全部折叠"));
    if (m_ProjectsTree->topLevelItemCount() == 0) {
        collapse->setEnabled(false);
    } else {
        connect(collapse, &QAction::triggered, m_ProjectsTree, &QTreeWidget::collapseAll);
    }
    menu.exec(m_ProjectsTree->mapToGlobal(point));
}

void MainWindow::handleTreeDoubleClicked(const QModelIndex &index)
{
    const int type = index.data(Qt::UserRole + 1).toInt();
    const QString path = index.data(Qt::UserRole + 2).toString();
#ifdef QT_DEBUG
    qDebug() << "用户双击了" << path;
#endif
    switch (type) {
    case Project:
    case Folder:
        break;
    case File:
        openFile(path);
        break;
    }
}

void MainWindow::handleTreeSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    Q_UNUSED(deselected)
    bool apk = false;
    if (!selected.isEmpty()) {
        auto index = selected.indexes().first();
        const int type = index.data(Qt::UserRole + 1).toInt();
        const QString path = index.data(Qt::UserRole + 2).toString();
        apk = (type == File) && path.endsWith(".apk");
    }
    m_ActionInstall1->setEnabled(apk);
    m_ActionInstall2->setEnabled(apk);
    m_ActionSign->setEnabled(apk);
}

void MainWindow::openFile(const QString &path)
{
#ifdef QT_DEBUG
    qDebug() << "正在打开" << path;
#endif
    const int total = m_ModelOpenFiles->rowCount();
    for (int i = 0; i < total; ++i) {
        const QModelIndex &index = m_ModelOpenFiles->index(i, 0);
        if (QString::compare(index.data(Qt::UserRole + 1).toString(), path) == 0) {
            m_TabEditors->setCurrentIndex(findTabIndex(path));
            return;
        }
    }
    QFileInfo info(path);
    QWidget *widget;
    const QString extension = info.suffix();
    if (!extension.isEmpty() && QString(IMAGE_EXTENSIONS).contains(extension, Qt::CaseInsensitive)) {
        auto viewer = new ImageViewerWidget(this);
        viewer->open(path);
        viewer->zoomReset();
        widget = viewer;
    } else if (!extension.isEmpty() && QString(TEXT_EXTENSIONS).contains(extension, Qt::CaseInsensitive)) {
        auto editor = new SourceCodeEdit(this);
        editor->open(path);
        widget = editor;
    } else {
        auto hex = new HexEdit(this);
        hex->open(path);
        widget = hex;
    }
    const QIcon icon = m_FileIconProvider.icon(info);
    auto item = new QStandardItem(icon, info.fileName());
    item->setData(path, Qt::UserRole + 1);
    
    // 在模型更新期间阻塞信号以防止选择更改错误
    QSignalBlocker blocker(m_ListOpenFiles);
    if (m_ListOpenFiles->selectionModel()) {
        QSignalBlocker selectionBlocker(m_ListOpenFiles->selectionModel());
    m_ModelOpenFiles->appendRow(item);
    } else {
        m_ModelOpenFiles->appendRow(item);
    }
    const int i = m_TabEditors->addTab(widget, icon, info.fileName());
    m_TabEditors->setCurrentIndex(i);
    m_TabEditors->setTabToolTip(i, path);
    if (m_CentralStack->currentIndex() != 1) {
        m_CentralStack->setCurrentIndex(1);
    }
    m_ActionClose->setEnabled(true);
    m_ActionCloseAll->setEnabled(true);
}

void MainWindow::openFindReplaceDialog(QPlainTextEdit *edit, const bool replace)
{
    if (!m_FindReplaceDialog) {
        m_FindReplaceDialog = new FindReplaceDialog(replace, this);
        connect(m_FindReplaceDialog, &QDialog::finished, [=] {
            m_FindReplaceDialog->deleteLater();
            m_FindReplaceDialog = nullptr;
        });
        m_FindReplaceDialog->show();
    }
    m_FindReplaceDialog->setTextEdit(edit);
}

void MainWindow::openProject(const QString &folder, const bool last)
{
    // 打开新项目时清除搜索框
    if (m_SearchFiles) {
        m_SearchFiles->clear();
    }
    if (m_SearchProjects) {
        m_SearchProjects->clear();
    }
    
    QSettings settings;
    settings.setValue("open_project", folder);
    settings.sync();
    QFileInfo info(folder);
    QTreeWidgetItem *item = new QTreeWidgetItem(m_ProjectsTree);
    item->setData(0, Qt::UserRole + 1, Project);
    item->setData(0, Qt::UserRole + 2, folder);
    item->setIcon(0, m_FileIconProvider.icon(info));
    item->setText(0, info.fileName());
    reloadChildren(item);
    m_ProjectsTree->addTopLevelItem(item);
    m_ProjectsTree->expandItem(item);
    m_ActionBuild1->setEnabled(true);
    m_ActionBuild2->setEnabled(true);
    QDir dir(folder);
    if (!last) {
        const QString manifest = dir.filePath("AndroidManifest.xml");
        if (QFile::exists(manifest)) {
            openFile(manifest);
        }
    }
    updateWindowTitle();
}

void MainWindow::reloadChildren(QTreeWidgetItem *item)
{
    while (item->childCount()) {
        qDeleteAll(item->takeChildren());
    }
    QDir dir(item->data(0, Qt::UserRole + 2).toString());
    if (dir.exists()) {
        QFileInfoList files = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst);
        foreach (auto info, files) {
            QTreeWidgetItem *child = new QTreeWidgetItem(item);
            child->setData(0, Qt::UserRole + 1, info.isDir() ? Folder : File);
            child->setData(0, Qt::UserRole + 2, info.absoluteFilePath());
            child->setIcon(0, m_FileIconProvider.icon(info));
            child->setText(0, info.fileName());
            const QString tooltip = QString("%1 - %2")
                    .arg(QDir::toNativeSeparators(info.filePath()))
                    .arg(QLocale::system().formattedDataSize(info.size(), 2, QLocale::DataSizeTraditionalFormat));
            child->setToolTip(0, tooltip);
            item->addChild(child);
            if (info.isDir()) {
                reloadChildren(child);
            }
        }
    }
}

bool MainWindow::saveTab(int i)
{
    auto widget = m_TabEditors->widget(i);
    auto edit = dynamic_cast<SourceCodeEdit *>(widget);
    if (edit) {
        return edit->save();
    } else {
        auto hex = dynamic_cast<HexEdit *>(widget);
        if (hex) {
            return hex->save();
        }
    }
    return true;
}

void MainWindow::updateWindowTitle()
{
    QString title = tr("APK Studio by VPZ");
    
    // 从树中获取第一个（最近）项目
    if (m_ProjectsTree->topLevelItemCount() > 0) {
        QTreeWidgetItem *firstProject = m_ProjectsTree->topLevelItem(0);
        if (firstProject) {
            QString projectFolder = firstProject->data(0, Qt::UserRole + 2).toString();
            if (!projectFolder.isEmpty()) {
                QFileInfo info(projectFolder);
                title += tr(" - %1").arg(info.fileName());
            }
        }
    }
    
    setWindowTitle(title);
}

#ifdef Q_OS_LINUX
void MainWindow::checkAndInstallDesktopFile()
{
    QString applicationsDir = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
    if (applicationsDir.isEmpty()) {
        // 回退到 ~/.local/share/applications
        applicationsDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/.local/share/applications";
    }
    
    QString desktopFilePath = applicationsDir + "/apkstudio.desktop";
    QFileInfo desktopFileInfo(desktopFilePath);
    
    // 检查 .desktop 文件是否已存在
    if (desktopFileInfo.exists()) {
        return; // 已安装
    }
    
    // 询问用户确认
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(tr("安装桌面入口"));
    msgBox.setText(tr("是否要为 APK Studio 安装桌面入口？\n\n这将允许您从应用程序菜单启动 APK Studio。"));
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::Yes);
    msgBox.setIcon(QMessageBox::Question);
    
    if (msgBox.exec() != QMessageBox::Yes) {
        return; // 用户拒绝
    }
    
    // 获取可执行文件路径
    // 检查是否从 AppImage 运行 - 如果是，则使用 AppImage 文件路径
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString executablePath;
    QString appImagePath = env.value("APPIMAGE");
    
    if (!appImagePath.isEmpty() && QFile::exists(appImagePath)) {
        // 从 AppImage 运行 - 使用 AppImage 文件本身
        executablePath = appImagePath;
    } else {
        // 不从 AppImage 运行 - 使用常规可执行文件路径
        executablePath = QApplication::applicationFilePath();
        QFileInfo exeInfo(executablePath);
        if (!exeInfo.exists()) {
            // 如果可执行文件路径不存在（例如从构建目录运行），
            // 尝试使用 argv[0] 或通用命令
            executablePath = "apkstudio";
        } else {
            executablePath = exeInfo.absoluteFilePath();
        }
    }
    
    // 从捆绑资源中提取图标并保存到桌面文件可以使用的位置
    QString iconPath = "apkstudio"; // 回退到通用名称
    
    // 尝试从捆绑资源中提取图标
    QPixmap iconPixmap(":/images/icon.png");
    if (!iconPixmap.isNull()) {
        // 将图标保存到 ~/.local/share/icons/apkstudio.png
        QString iconsDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/.local/share/icons";
        QDir iconsDirObj;
        if (iconsDirObj.mkpath(iconsDir)) {
            QString iconFilePath = iconsDir + "/apkstudio.png";
            if (iconPixmap.save(iconFilePath, "PNG")) {
                iconPath = iconFilePath;
            } else {
                // 回退：尝试 ~/.local/share/apkstudio/icon.png
                QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
                if (appDataDir.isEmpty()) {
                    appDataDir = QStandardPaths::writableLocation(QStandardPaths::HomeLocation) + "/.apkstudio";
                }
                if (iconsDirObj.mkpath(appDataDir)) {
                    iconFilePath = appDataDir + "/icon.png";
                    if (iconPixmap.save(iconFilePath, "PNG")) {
                        iconPath = iconFilePath;
                    }
                }
            }
        }
    }
    
    // 如果图标提取失败，尝试在可执行文件相对路径中查找（适用于非 AppImage 安装）
    QFileInfo exeInfo(executablePath);
    if (iconPath == "apkstudio" && exeInfo.exists() && appImagePath.isEmpty()) {
        QString exeDir = exeInfo.absolutePath();
        QFileInfo iconInfo(exeDir + "/../share/pixmaps/apkstudio.png");
        if (iconInfo.exists()) {
            iconPath = iconInfo.absoluteFilePath();
        } else {
            // 尝试相对于可执行文件
            iconInfo.setFile(exeDir + "/../share/apkstudio/icon.png");
            if (iconInfo.exists()) {
                iconPath = iconInfo.absoluteFilePath();
            }
        }
    }
    
    // 创建应用程序目录（如果不存在）
    QDir dir;
    if (!dir.mkpath(applicationsDir)) {
        QMessageBox::warning(this, tr("错误"), 
                            tr("无法创建应用程序目录：\n%1").arg(applicationsDir));
        return;
    }
    
    // 创建 .desktop 文件内容
    QFile desktopFile(desktopFilePath);
    if (!desktopFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("错误"), 
                            tr("无法创建桌面文件：\n%1").arg(desktopFilePath));
        return;
    }
    
    QTextStream out(&desktopFile);
    out << "[Desktop Entry]\n";
    out << "Type=Application\n";
    out << "Name=APK Studio\n";
    out << "Name[en]=APK Studio\n";
    out << "Comment=Android APK reverse engineering tool\n";
    out << "Comment[en]=Android APK reverse engineering tool\n";
    // 如果可执行路径包含空格，进行转义
    QString escapedExecPath = executablePath;
    if (executablePath.contains(" ")) {
        escapedExecPath = "\"" + executablePath + "\"";
    }
    out << "Exec=" << escapedExecPath << " %F\n";
    out << "Icon=" << iconPath << "\n";
    out << "Terminal=false\n";
    out << "Categories=Development;Utility;\n";
    out << "StartupNotify=true\n";
    out << "StartupWMClass=apkstudio\n";
    desktopFile.close();
    
    // 设置适当的权限（用户、组和其他用户可读）
    desktopFile.setPermissions(QFile::ReadUser | QFile::WriteUser | 
                               QFile::ReadGroup | QFile::ReadOther);
    
    // 更新桌面数据库使其显示在 Ubuntu 启动器中（异步使用工作线程）
    QThread *thread = new QThread(this);
    DesktopDatabaseUpdateWorker *worker = new DesktopDatabaseUpdateWorker(applicationsDir);
    worker->moveToThread(thread);
    
    connect(thread, &QThread::started, worker, &DesktopDatabaseUpdateWorker::updateDatabase);
    connect(worker, &DesktopDatabaseUpdateWorker::finished, thread, &QThread::quit);
    connect(worker, &DesktopDatabaseUpdateWorker::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    // 注意：我们不显示数据库更新失败的错误消息，因为它们不重要
    thread->start();
}
#endif

QWidget* MainWindow::findTabWidget(const QString& path)
{
    int index = findTabIndex(path);
    if (index >= 0) {
        return m_TabEditors->widget(index);
    }
    return nullptr;
}

QStringList MainWindow::getProjectRoots()
{
    QStringList roots;
    int count = m_ProjectsTree->topLevelItemCount();
    for (int i = 0; i < count; i++) {
        QTreeWidgetItem *item = m_ProjectsTree->topLevelItem(i);
        if (item) {
            int type = item->data(0, Qt::UserRole + 1).toInt();
            if (type == Project) {
                QString path = item->data(0, Qt::UserRole + 2).toString();
                if (!path.isEmpty()) {
                    roots.append(path);
                }
            }
        }
    }
    return roots;
}

MainWindow::~MainWindow()
{
}

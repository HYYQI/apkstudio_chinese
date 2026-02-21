#include <QApplication>
#include <QBrush>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QListWidgetItem>
#include <QMap>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStringConverter>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include "findinfilesdialog.h"
#include "mainwindow.h"
#include "sourcecodeedit.h"

FindInFilesDialog::FindInFilesDialog(MainWindow *parent)
    : QDialog(parent), m_MainWindow(parent), m_SearchRoot()
{
    setWindowTitle(tr("在文件中查找"));
    setMinimumSize(512, 384);
    resize(640, 480);
    
#ifdef Q_OS_WIN
    setWindowIcon(QIcon(":/icons/fugue/binocular.png"));
#endif
    
    buildUI();
    
    // 连接搜索按钮和回车键
    connect(m_EditSearch, &QLineEdit::returnPressed, this, &FindInFilesDialog::handleSearch);
}

void FindInFilesDialog::buildUI()
{
    auto mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);
    
    // 搜索输入框和选项
    auto searchLayout = new QHBoxLayout;
    searchLayout->addWidget(new QLabel(tr("搜索："), this));
    m_EditSearch = new QLineEdit(this);
    m_EditSearch->setPlaceholderText(tr("请输入搜索关键词..."));
    searchLayout->addWidget(m_EditSearch);
    
    auto searchButton = new QPushButton(tr("搜索"), this);
    connect(searchButton, &QPushButton::clicked, this, &FindInFilesDialog::handleSearch);
    searchLayout->addWidget(searchButton);
    mainLayout->addLayout(searchLayout);
    
    // 选项组
    auto optionsGroup = new QGroupBox(tr("选项"), this);
    optionsGroup->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    auto optionsLayout = new QHBoxLayout(optionsGroup);
    m_CheckCase = new QCheckBox(tr("区分大小写"), this);
    m_CheckWhole = new QCheckBox(tr("全词匹配"), this);
    m_CheckRegexp = new QCheckBox(tr("使用正则表达式"), this);
    optionsLayout->addWidget(m_CheckCase);
    optionsLayout->addWidget(m_CheckWhole);
    optionsLayout->addWidget(m_CheckRegexp);
    optionsLayout->addStretch();
    mainLayout->addWidget(optionsGroup);
    
    // 分割结果显示和预览区
    m_Splitter = new QSplitter(Qt::Horizontal, this);
    
    // 结果列表
    m_ResultsList = new QListWidget(this);
    m_ResultsList->setAlternatingRowColors(true);
    connect(m_ResultsList, &QListWidget::itemClicked, this, &FindInFilesDialog::onResultClicked);
    connect(m_ResultsList, &QListWidget::itemSelectionChanged, this, &FindInFilesDialog::handleResultSelectionChanged);
    m_Splitter->addWidget(m_ResultsList);
    
    // 预览文本区
    m_PreviewText = new QTextEdit(this);
    m_PreviewText->setReadOnly(true);
    m_PreviewText->setFont(QFont("Consolas", 10));
    m_Splitter->addWidget(m_PreviewText);
    
    m_Splitter->setStretchFactor(0, 1);
    m_Splitter->setStretchFactor(1, 1);
    mainLayout->addWidget(m_Splitter, 1); // 添加分割器并设置拉伸因子 1 以填充剩余空间
    
    // 进度条
    m_ProgressBar = new QProgressBar(this);
    m_ProgressBar->setVisible(false);
    m_ProgressBar->setMinimum(0);
    m_ProgressBar->setMaximum(0); // 不确定进度
    m_ProgressBar->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    mainLayout->addWidget(m_ProgressBar);
    
    // 按钮
    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
    
    // 状态标签
    m_LabelStatus = new QLabel(tr("就绪"), this);
    m_LabelStatus->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
    mainLayout->addWidget(m_LabelStatus);
    
    setLayout(mainLayout);
}

void FindInFilesDialog::setSearchRoot(const QString &rootPath)
{
    m_SearchRoot = rootPath;
}

QStringList FindInFilesDialog::getTextFileExtensions()
{
    // 匹配 MainWindow 中使用的扩展名
    return QStringList() << "java" << "html" << "properties" << "smali" 
                        << "txt" << "xml" << "yaml" << "yml" << "cpp" 
                        << "h" << "c" << "hpp" << "cc" << "cxx" << "js" 
                        << "ts" << "json" << "css" << "md" << "sh" << "bat" 
                        << "cmake" << "py" << "gradle" << "kt" << "pro" 
                        << "pri" << "qrc" << "ui" << "qml";
}

bool FindInFilesDialog::isTextFile(const QString &filePath)
{
    QFileInfo info(filePath);
    QString extension = info.suffix().toLower();
    return getTextFileExtensions().contains(extension);
}

void FindInFilesDialog::scanDirectory(const QString &dirPath, const QString &searchTerm,
                                     bool caseSensitive, bool wholeWords, bool useRegexp)
{
    QDir dir(dirPath);
    if (!dir.exists()) {
        return;
    }
    
    QFileInfoList entries = dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, 
                                              QDir::Name | QDir::DirsFirst);
    
    for (const QFileInfo &info : entries) {
        if (info.isDir()) {
            // 递归扫描子目录
            scanDirectory(info.absoluteFilePath(), searchTerm, caseSensitive, wholeWords, useRegexp);
        } else if (info.isFile() && isTextFile(info.absoluteFilePath())) {
            // 在文本文件中搜索
            QFile file(info.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                continue;
            }
            
            QTextStream in(&file);
            in.setEncoding(QStringConverter::Utf8);
            int lineNumber = 1;
            
            QRegularExpression regex;
            if (useRegexp) {
                QRegularExpression::PatternOptions options = QRegularExpression::NoPatternOption;
                if (!caseSensitive) {
                    options |= QRegularExpression::CaseInsensitiveOption;
                }
                regex = QRegularExpression(searchTerm, options);
                if (!regex.isValid()) {
                    continue;
                }
            }
            
            while (!in.atEnd()) {
                QString line = in.readLine();
                bool found = false;
                int matchStart = -1;
                int matchLength = 0;
                
                if (useRegexp) {
                    QRegularExpressionMatch match = regex.match(line);
                    if (match.hasMatch()) {
                        found = true;
                        matchStart = match.capturedStart();
                        matchLength = match.capturedLength();
                    }
                } else {
                    QString searchText = searchTerm;
                    QString lineText = line;
                    
                    if (!caseSensitive) {
                        searchText = searchText.toLower();
                        lineText = lineText.toLower();
                    }
                    
                    int pos = lineText.indexOf(searchText);
                    if (pos >= 0) {
                        if (wholeWords) {
                            // 检查是否为全词匹配
                            bool isWholeWord = true;
                            if (pos > 0) {
                                QChar prev = line.at(pos - 1);
                                if (prev.isLetterOrNumber() || prev == '_') {
                                    isWholeWord = false;
                                }
                            }
                            if (pos + searchText.length() < line.length()) {
                                QChar next = line.at(pos + searchText.length());
                                if (next.isLetterOrNumber() || next == '_') {
                                    isWholeWord = false;
                                }
                            }
                            if (!isWholeWord) {
                                pos = -1;
                            }
                        }
                        
                        if (pos >= 0) {
                            found = true;
                            matchStart = pos;
                            matchLength = searchTerm.length();
                        }
                    }
                }
                
                if (found) {
                    SearchMatch match;
                    match.filePath = info.absoluteFilePath();
                    match.lineNumber = lineNumber;
                    match.lineText = line;
                    match.matchStart = matchStart;
                    match.matchLength = matchLength;
                    m_Matches.append(match);
                }
                
                lineNumber++;
            }
            
            file.close();
        }
    }
}

void FindInFilesDialog::performSearch()
{
    QString searchTerm = m_EditSearch->text().trimmed();
    if (searchTerm.isEmpty()) {
        m_LabelStatus->setText(tr("请输入搜索关键词。"));
        return;
    }
    
    if (m_SearchRoot.isEmpty()) {
        m_LabelStatus->setText(tr("未选择项目文件夹。"));
        return;
    }
    
    m_Matches.clear();
    m_ResultsList->clear();
    m_PreviewText->clear();
    
    m_ProgressBar->setVisible(true);
    m_LabelStatus->setText(tr("正在搜索..."));
    QApplication::processEvents();
    
    bool caseSensitive = m_CheckCase->isChecked();
    bool wholeWords = m_CheckWhole->isChecked();
    bool useRegexp = m_CheckRegexp->isChecked();
    
    if (useRegexp) {
        QRegularExpression testRegex(searchTerm);
        if (!testRegex.isValid()) {
            m_LabelStatus->setText(tr("无效的正则表达式：%1").arg(testRegex.errorString()));
            m_ProgressBar->setVisible(false);
            return;
        }
    }
    
    scanDirectory(m_SearchRoot, searchTerm, caseSensitive, wholeWords, useRegexp);
    
    m_ProgressBar->setVisible(false);
    updateResults();
}

void FindInFilesDialog::updateResults()
{
    m_ResultsList->clear();
    
    int matchCount = m_Matches.size();
    if (matchCount == 0) {
        m_LabelStatus->setText(tr("未找到匹配项。"));
        return;
    }
    
    // 按文件分组匹配结果
    QMap<QString, QList<SearchMatch>> fileMatches;
    for (const SearchMatch &match : m_Matches) {
        fileMatches[match.filePath].append(match);
    }
    
    // 将项目添加到列表
    for (auto it = fileMatches.begin(); it != fileMatches.end(); ++it) {
        QString filePath = it.key();
        QFileInfo fileInfo(filePath);
        QString fileName = fileInfo.fileName();
        QString relativePath = QDir(m_SearchRoot).relativeFilePath(filePath);
        
        // 文件头项目
        auto headerItem = new QListWidgetItem(QString("%1（%2 个匹配）").arg(relativePath).arg(it.value().size()));
        headerItem->setData(Qt::UserRole, QVariant::fromValue<QString>(QString())); // 空字符串表示这是标题
        QFont headerFont = headerItem->font();
        headerFont.setBold(true);
        headerItem->setFont(headerFont);
        // 使用适应当前主题的调色板颜色（浅色模式下为 AlternateBase，深色模式下为深灰色）
        headerItem->setBackground(QBrush(palette().color(QPalette::AlternateBase)));
        headerItem->setForeground(QBrush(palette().color(QPalette::Text)));
        m_ResultsList->addItem(headerItem);
        
        // 匹配项
        for (const SearchMatch &match : it.value()) {
            QString displayText = QString("  %1：%2").arg(match.lineNumber).arg(match.lineText.trimmed());
            if (displayText.length() > 100) {
                displayText = displayText.left(100) + "...";
            }
            auto item = new QListWidgetItem(displayText);
            item->setData(Qt::UserRole, QVariant::fromValue<SearchMatch>(match));
            // 使用中性样式 - Base 背景和 Text 前景色，正常字体粗细
            item->setBackground(QBrush(palette().color(QPalette::Base)));
            item->setForeground(QBrush(palette().color(QPalette::Text)));
            QFont itemFont = item->font();
            itemFont.setBold(false);
            item->setFont(itemFont);
            m_ResultsList->addItem(item);
        }
    }
    
    m_LabelStatus->setText(tr("找到 %1 个匹配项，分布在 %2 个文件中")
                          .arg(matchCount)
                          .arg(fileMatches.size()));
}

void FindInFilesDialog::handleSearch()
{
    performSearch();
}

void FindInFilesDialog::handleResultSelectionChanged()
{
    auto item = m_ResultsList->currentItem();
    if (!item) {
        m_PreviewText->clear();
        return;
    }
    
    QVariant data = item->data(Qt::UserRole);
    if (data.canConvert<SearchMatch>()) {
        SearchMatch match = data.value<SearchMatch>();
        
        // 加载文件内容进行预览
        QFile file(match.filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&file);
            in.setEncoding(QStringConverter::Utf8);
            QString content = in.readAll();
            file.close();
            
            // 显示匹配行周围的上下文（前后各 5 行）
            QStringList lines = content.split('\n');
            int startLine = qMax(0, match.lineNumber - 6);
            int endLine = qMin(lines.size(), match.lineNumber + 5);
            
            QString preview;
            for (int i = startLine; i < endLine; i++) {
                QString lineNum = QString::number(i + 1).rightJustified(4, ' ');
                QString line = lines[i];
                if (i == match.lineNumber - 1) {
                    preview += QString("%1：%2\n").arg(lineNum).arg(line);
                } else {
                    preview += QString("%1：%2\n").arg(lineNum).arg(line);
                }
            }
            
            m_PreviewText->setPlainText(preview);
            
            // 高亮匹配行
            QTextCursor cursor = m_PreviewText->textCursor();
            int targetLine = match.lineNumber - startLine - 1;
            cursor.movePosition(QTextCursor::Start);
            for (int i = 0; i < targetLine; i++) {
                cursor.movePosition(QTextCursor::Down);
            }
            cursor.movePosition(QTextCursor::EndOfLine);
            m_PreviewText->setTextCursor(cursor);
            m_PreviewText->ensureCursorVisible();
        }
    } else {
        m_PreviewText->clear();
    }
}

void FindInFilesDialog::onResultClicked(QListWidgetItem *item)
{
    if (!item) {
        return;
    }
    
    QVariant data = item->data(Qt::UserRole);
    if (!data.canConvert<SearchMatch>()) {
        return; // 标题项，不做任何操作
    }
    
    SearchMatch match = data.value<SearchMatch>();
    
    // 在主窗口中打开文件
    if (m_MainWindow) {
        // 先打开文件
        m_MainWindow->openFile(match.filePath);
        
        // 稍等片刻让文件打开，然后跳转到对应行
        QTimer::singleShot(100, [this, match]() {
            auto widget = m_MainWindow->findTabWidget(match.filePath);
            if (widget) {
                auto edit = dynamic_cast<SourceCodeEdit *>(widget);
                if (edit) {
                    edit->gotoLine(match.lineNumber);
                    
                    // 高亮匹配的文本
                    QTextCursor cursor = edit->textCursor();
                    QTextBlock block = edit->document()->findBlockByLineNumber(match.lineNumber - 1);
                    if (block.isValid()) {
                        cursor.setPosition(block.position() + match.matchStart);
                        cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, match.matchLength);
                        edit->setTextCursor(cursor);
                        edit->ensureCursorVisible();
                    }
                }
            }
        });
    }
}


#include <QFormLayout>
#include <QGroupBox>
#include <QIcon>
#include <QMainWindow>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>
#include "findreplacedialog.h"

FindReplaceDialog::FindReplaceDialog(const bool replace, QWidget *parent)
    : QDialog(parent), m_Edit(nullptr)
{
    auto layout = new QVBoxLayout(this);
    layout->addLayout(buildForm(replace));
    layout->addWidget(buildButtonBox(replace));
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    setMinimumSize(QSize(320, 192));
    setMaximumSize(QSize(480, 320));
#ifdef Q_OS_WIN
    setWindowIcon(QIcon(replace ? ":/icons/fugue/edit-replace.png" : ":/icons/fugue/binocular.png"));
#endif
    setWindowTitle(tr(replace ? "查找和替换" : "查找"));
    m_EditFind->setFocus();
    m_RadioDown->setChecked(true);
}

QWidget *FindReplaceDialog::buildButtonBox(const bool replace)
{
    m_Buttons = new QDialogButtonBox(this);
    auto find = m_Buttons->addButton(tr("查找"), QDialogButtonBox::AcceptRole);
    connect(find, &QPushButton::clicked, this, &FindReplaceDialog::handleFind);
    if (replace) {
        auto replaceb = m_Buttons->addButton(tr("替换"), QDialogButtonBox::ActionRole);
        auto replaceall = m_Buttons->addButton(tr("全部替换"), QDialogButtonBox::ActionRole);
        connect(replaceb, &QPushButton::clicked, this, &FindReplaceDialog::handleReplace);
        connect(replaceall, &QPushButton::clicked, this, &FindReplaceDialog::handleReplaceAll);
    }
    return m_Buttons;
}

QLayout *FindReplaceDialog::buildForm(const bool replace)
{
    auto directions = new QGroupBox(tr("方向"), this);
    auto flags = new QGroupBox(tr("选项"), this);
    auto directionsl = new QVBoxLayout(directions);
    auto flagsl = new QVBoxLayout(flags);
    auto form = new QFormLayout;
    directionsl->addWidget(new QRadioButton(tr("向上"), this));
    directionsl->addWidget(m_RadioDown = new QRadioButton(tr("向下"), this));
    flagsl->addWidget(m_CheckCase = new QCheckBox(tr("区分大小写"), this));
    flagsl->addWidget(m_CheckWhole = new QCheckBox(tr("全词匹配"), this));
    flagsl->addWidget(m_CheckRegexp = new QCheckBox(tr("使用正则表达式"), this));
    form->addRow(tr("查找内容"), m_EditFind = new QLineEdit(this));
    if (replace) {
        form->addRow(tr("替换为"), m_EditReplacement = new QLineEdit(this));
    }
    auto options = new QHBoxLayout;
    options->addWidget(directions);
    options->addWidget(flags);
    auto layout = new QVBoxLayout;
    layout->addLayout(form);
    layout->addLayout(options);
    layout->addWidget(m_LabelMessage = new QLabel(tr("就绪"), this));
    m_LabelMessage->setAlignment(Qt::AlignTop | Qt::AlignRight);
    m_LabelMessage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    return layout;
}

void FindReplaceDialog::findInTextEdit(const bool next)
{
    bool found = false;
    if (m_Edit) {
        m_LabelMessage->setText(QString());
        const QString term = m_EditFind->text();
        QTextDocument::FindFlags flags;
        if (!next) {
            flags |= QTextDocument::FindBackward;
        }
        if (m_CheckCase->isChecked()) {
            flags |= QTextDocument::FindCaseSensitively;
        }
        if (m_CheckWhole->isChecked()) {
            flags |= QTextDocument::FindWholeWords;
        }
        QTextCursor cursor;
        if (m_CheckRegexp->isChecked()) {
            QRegularExpression regex(term, (m_CheckCase->isChecked() ? QRegularExpression::NoPatternOption : QRegularExpression::CaseInsensitiveOption));
            cursor = m_Edit->document()->find(regex, cursor, flags);
            m_Edit->setTextCursor(cursor);
            found = !cursor.isNull();
        } else {
            found = m_Edit->find(term, flags);
        }
        if (!found) {
            cursor.setPosition(QTextCursor::Start);
            m_Edit->setTextCursor(cursor);
        }
    }
    if (!found) {
        m_LabelMessage->setText(tr("未找到搜索内容。"));
    }
}

void FindReplaceDialog::handleFind()
{
    findInTextEdit(m_RadioDown->isChecked());
}

void FindReplaceDialog::handleReplace()
{
    if (m_Edit && !m_Edit->isReadOnly()) {
        if (m_Edit->textCursor().hasSelection()) {
            m_Edit->textCursor().insertText(m_EditReplacement->text());
            findInTextEdit(m_RadioDown->isChecked());
        } else {
            findInTextEdit(m_RadioDown->isChecked());
        }
    }
}

void FindReplaceDialog::handleReplaceAll()
{
    if (m_Edit && !m_Edit->isReadOnly()) {
        int i = 0;
        while (m_Edit->textCursor().hasSelection()) {
            m_Edit->textCursor().insertText(m_EditReplacement->text());
            findInTextEdit(m_RadioDown->isChecked());
            i++;
        }
        m_LabelMessage->setText(tr("已替换 %1 处匹配项。").arg(i));
    }
}

void FindReplaceDialog::setTextEdit(QPlainTextEdit *edit)
{
    m_Edit = edit;
}

#include <QHBoxLayout>
#include "settingsdialog.h"

SettingsDialog::SettingsDialog(const int page, QWidget *parent)
    : QDialog(parent)
{
    auto layout = new QVBoxLayout(this);
    layout->addLayout(buildForm());
    layout->addWidget(buildButtonBox());
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumSize(320, 240);
#ifdef Q_OS_WIN
    setWindowIcon(QIcon(":/icons/fugue/gear.png"));
#endif
    setWindowTitle(tr("设置"));
    if (page != 0) {
        m_OptionsList->setCurrentRow(page);
    }
}

QWidget *SettingsDialog::buildButtonBox()
{
    m_ButtonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(m_ButtonBox, &QDialogButtonBox::accepted, m_AppearanceSettingsWidget, &AppearanceSettingsWidget::save);
    connect(m_ButtonBox, &QDialogButtonBox::accepted, m_BinarySettingsWidget, &BinarySettingsWidget::save);
    connect(m_ButtonBox, &QDialogButtonBox::accepted, m_SigningConfigWidget, &SigningConfigWidget::save);
    connect(m_ButtonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_ButtonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    return m_ButtonBox;
}

QLayout *SettingsDialog::buildForm()
{
    auto layout = new QHBoxLayout;
    layout->addWidget(m_OptionsList = new QListWidget(this), 1);
    m_OptionsList->addItem(new QListWidgetItem(QIcon(":/icons/fugue/color.png"), tr("外观")));
    m_OptionsList->addItem(new QListWidgetItem(QIcon(":/icons/fugue/application-terminal.png"), tr("二进制文件")));
    m_OptionsList->addItem(new QListWidgetItem(QIcon(":/icons/fugue/edit-signiture.png"), tr("签名")));
    m_OptionsList->setCurrentRow(0);
    layout->addWidget(m_WidgetStack = new QStackedWidget(this), 3);
    m_WidgetStack->addWidget(m_AppearanceSettingsWidget = new AppearanceSettingsWidget(this));
    m_WidgetStack->addWidget(m_BinarySettingsWidget = new BinarySettingsWidget(this));
    m_WidgetStack->addWidget(m_SigningConfigWidget = new SigningConfigWidget(this));
    connect(m_OptionsList, &QListWidget::currentRowChanged, m_WidgetStack, &QStackedWidget::setCurrentIndex);
    return layout;
}

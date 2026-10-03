#include "desktop/SubscriptionEditorDialog.h"
#include "domain/Theme.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

namespace campus {
namespace {
QString label(std::string_view value) {
    return QString::fromUtf8(value.data(), static_cast<int>(value.size()));
}
std::vector<std::string> words(const QString &value) {
    std::vector<std::string> result;
    for (const auto &word : value.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts))
        result.push_back(word.toStdString());
    return result;
}
QString joined(const std::vector<std::string> &values) {
    QStringList result;
    for (const auto &value : values)
        result << QString::fromStdString(value);
    return result.join(' ');
}
bool contains(const std::vector<std::string> &values, std::string_view key) {
    return std::find(values.begin(), values.end(), key) != values.end();
}
} // namespace
SubscriptionEditorDialog::SubscriptionEditorDialog(const SchoolPackage &school,
                                                   SubscriptionService &service,
                                                   Subscription initial, QWidget *parent)
    : QDialog(parent), service_(service), initial_(std::move(initial)) {
    setObjectName("subscriptionEditor");
    setWindowTitle(initial_.id.empty() ? "创建订阅" : "编辑订阅");
    resize(740, 710);
    initial_.schoolId = school.id.toStdString();
    initial_.query.schoolId = initial_.schoolId;
    auto *layout = new QVBoxLayout(this);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *formWidget = new QWidget;
    auto *content = new QVBoxLayout(formWidget);
    auto *explanation = new QLabel("不同条件同时满足；同组来源、主题、阶段任一匹配。"
                                   "关键词以空格分隔，排除优先。阶段为标题提示，请核对原文。");
    explanation->setWordWrap(true);
    content->addWidget(explanation);
    auto *form = new QFormLayout;
    name_ = new QLineEdit(QString::fromStdString(initial_.name));
    name_->setObjectName("subscriptionName");
    name_->setMaxLength(80);
    name_->setPlaceholderText("例如：今年重修缴费、奖学金申请");
    form->addRow("订阅名称", name_);
    year_ = new QComboBox;
    year_->setObjectName("subscriptionYearPolicy");
    year_->addItem("今年（随年份变化）", "current_year");
    year_->addItem("全部年份", "all_years");
    year_->addItem("固定年份", "fixed_year");
    year_->addItem("日期待核实", "unknown_date");
    year_->setCurrentIndex(
        year_->findData(QString::fromStdString(yearPolicyKey(initial_.query.yearPolicy))));
    fixedYear_ = new QSpinBox;
    fixedYear_->setObjectName("subscriptionFixedYear");
    fixedYear_->setRange(1, 9999);
    fixedYear_->setValue(initial_.query.fixedYear > 0 ? initial_.query.fixedYear
                                                      : QDate::currentDate().year());
    auto *yearLayout = new QHBoxLayout;
    yearLayout->addWidget(year_, 1);
    yearLayout->addWidget(fixedYear_);
    form->addRow("年份", yearLayout);
    all_ = new QLineEdit(joined(initial_.query.keywordAll));
    all_->setObjectName("subscriptionKeywordAll");
    any_ = new QLineEdit(joined(initial_.query.keywordAny));
    any_->setObjectName("subscriptionKeywordAny");
    exclude_ = new QLineEdit(joined(initial_.query.keywordExclude));
    exclude_->setObjectName("subscriptionKeywordExclude");
    all_->setPlaceholderText("每个词都包含，例如：重修 缴费");
    any_->setPlaceholderText("至少包含一个，例如：补考 重修");
    exclude_->setPlaceholderText("出现任一词就排除，例如：公示 结果");
    form->addRow("全部包含", all_);
    form->addRow("任一包含", any_);
    form->addRow("排除关键词", exclude_);
    content->addLayout(form);
    auto *sourceGroup = new QGroupBox("来源（不选表示全部）");
    auto *sourceLayout = new QVBoxLayout(sourceGroup);
    sources_ = new QListWidget;
    sources_->setObjectName("subscriptionSources");
    sources_->setMaximumHeight(145);
    std::vector<std::string> known;
    for (const auto &source : school.catalog) {
        known.push_back(source.id);
        auto *item = new QListWidgetItem(
            QString::fromStdString(source.name) +
                (source.ready && source.configuredEnabled ? "" : "（待接入，仅匹配已有缓存）"),
            sources_);
        item->setData(Qt::UserRole, QString::fromStdString(source.id));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(contains(initial_.query.sourceIds, source.id) ? Qt::Checked
                                                                          : Qt::Unchecked);
    }
    for (const auto &id : initial_.query.sourceIds)
        if (!contains(known, id)) {
            auto *item = new QListWidgetItem(
                QString::fromStdString(id) + "（目录已移除，保留引用）", sources_);
            item->setData(Qt::UserRole, QString::fromStdString(id));
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Checked);
        }
    sourceLayout->addWidget(sources_);
    content->addWidget(sourceGroup);
    const auto addChecks = [&](const auto &definitions, const auto &selected,
                               std::vector<QCheckBox *> &checks, const QString &title,
                               const QString &prefix) {
        auto *group = new QGroupBox(title);
        auto *grid = new QGridLayout(group);
        int index = 0;
        for (const auto &definition : definitions) {
            auto *check = new QCheckBox(label(definition.label));
            check->setObjectName(prefix + label(definition.key));
            check->setProperty("key", label(definition.key));
            check->setChecked(contains(selected, definition.key));
            checks.push_back(check);
            grid->addWidget(check, index / 4, index % 4);
            ++index;
        }
        content->addWidget(group);
    };
    addChecks(Themes, initial_.query.themeKeys, themes_, "主题（不选表示全部）",
              "subscriptionTheme_");
    addChecks(Stages, initial_.query.stageKeys, stages_, "阶段提示（不选表示全部）",
              "subscriptionStage_");
    content->addStretch();
    scroll->setWidget(formWidget);
    layout->addWidget(scroll, 1);
    preview_ = new QLabel;
    preview_->setObjectName("subscriptionPreview");
    preview_->setWordWrap(true);
    layout->addWidget(preview_);
    error_ = new QLabel;
    error_->setObjectName("subscriptionError");
    error_->setTextFormat(Qt::PlainText);
    error_->setWordWrap(true);
    layout->addWidget(error_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText("保存订阅");
    buttons->button(QDialogButtonBox::Save)->setObjectName("saveSubscriptionDialogButton");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SubscriptionEditorDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    for (auto *input : {all_, any_, exclude_})
        connect(input, &QLineEdit::textChanged, this, [this] { updatePreview(); });
    connect(year_, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    connect(fixedYear_, &QSpinBox::valueChanged, this, [this] { updatePreview(); });
    connect(sources_, &QListWidget::itemChanged, this, [this] { updatePreview(); });
    for (auto *check : themes_)
        connect(check, &QCheckBox::toggled, this, [this] { updatePreview(); });
    for (auto *check : stages_)
        connect(check, &QCheckBox::toggled, this, [this] { updatePreview(); });
    updatePreview();
}
NoticeQuery SubscriptionEditorDialog::readQuery() const {
    NoticeQuery query;
    query.schoolId = initial_.schoolId;
    query.yearPolicy = yearPolicyFromKey(year_->currentData().toString().toStdString());
    query.fixedYear = query.yearPolicy == YearPolicy::FixedYear ? fixedYear_->value() : 0;
    query.keywordAll = words(all_->text());
    query.keywordAny = words(any_->text());
    query.keywordExclude = words(exclude_->text());
    for (int row = 0; row < sources_->count(); ++row)
        if (sources_->item(row)->checkState() == Qt::Checked)
            query.sourceIds.push_back(
                sources_->item(row)->data(Qt::UserRole).toString().toStdString());
    for (auto *check : themes_)
        if (check->isChecked())
            query.themeKeys.push_back(check->property("key").toString().toStdString());
    for (auto *check : stages_)
        if (check->isChecked())
            query.stageKeys.push_back(check->property("key").toString().toStdString());
    return query;
}
void SubscriptionEditorDialog::updatePreview() {
    try {
        fixedYear_->setEnabled(year_->currentData().toString() == "fixed_year");
        const auto notices = service_.preview(readQuery(), QDate::currentDate().year());
        preview_->setText(
            QString("当前缓存匹配 %1 条通知 · 保存后可在“我的订阅”查看。").arg(notices.size()));
    } catch (const std::exception &error) {
        preview_->setText("预览失败：" + QString::fromUtf8(error.what()));
    }
}
void SubscriptionEditorDialog::save() {
    try {
        auto subscription = initial_;
        subscription.name = name_->text().trimmed().toStdString();
        subscription.query = readQuery();
        saved_ = service_.save(std::move(subscription));
        accept();
    } catch (const std::exception &error) {
        error_->setText(QString::fromUtf8(error.what()));
    }
}
} // namespace campus

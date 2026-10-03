#include "desktop/TaskEditorDialog.h"
#include "desktop/TaskDisplay.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTextEdit>
#include <QTimeEdit>
#include <QVBoxLayout>
#include <stdexcept>
namespace campus {
TaskEditorDialog::TaskEditorDialog(const SchoolPackage &school, TaskService &service,
                                   PersonalTask task, const QString &noticeTitle, QWidget *parent)
    : QDialog(parent), service_(service), initial_(std::move(task)),
      zone_(initial_.time.timeZone.empty() ? school.timeZone.toUtf8()
                                           : QByteArray::fromStdString(initial_.time.timeZone)) {
    if (!zone_.isValid())
        throw std::invalid_argument("待办学校时区无效");
    setObjectName("taskEditor");
    setWindowTitle(initial_.id.empty() ? "加入我的待办" : "编辑我的待办");
    resize(770, 820);
    auto *layout = new QVBoxLayout(this);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *form = new QFormLayout(content);
    auto *origin = new QLabel("关联通知：" + noticeTitle);
    origin->setTextFormat(Qt::PlainText);
    origin->setWordWrap(true);
    form->addRow(origin);
    auto *tip =
        new QLabel("为这条通知填写你要办理的事项。同一通知可以添加多个待办，日期由你核对并确认。");
    tip->setWordWrap(true);
    form->addRow(tip);
    title_ = new QLineEdit(QString::fromStdString(initial_.title));
    title_->setObjectName("taskTitle");
    title_->setMaxLength(160);
    title_->setPlaceholderText("例如：完成重修缴费、提交奖学金申请材料");
    form->addRow("办理事项", title_);
    action_ = new QComboBox;
    action_->setObjectName("taskAction");
    for (const auto &a : TaskActions)
        action_->addItem(taskActionText(std::string(a.key)),
                         QString::fromUtf8(a.key.data(), static_cast<int>(a.key.size())));
    action_->setCurrentIndex(action_->findData(QString::fromStdString(initial_.action)));
    form->addRow("操作类型", action_);
    status_ = new QComboBox;
    status_->setObjectName("taskStatus");
    for (auto s : {TaskStatus::NotStarted, TaskStatus::InProgress, TaskStatus::Completed,
                   TaskStatus::Cancelled})
        status_->addItem(taskStatusText(s), QString::fromStdString(taskStatusKey(s)));
    status_->setCurrentIndex(
        status_->findData(QString::fromStdString(taskStatusKey(initial_.status))));
    form->addRow("办理状态", status_);
    notes_ = new QTextEdit;
    notes_->setPlainText(QString::fromStdString(initial_.notes));
    notes_->setObjectName("taskNotes");
    notes_->setAcceptRichText(false);
    notes_->setMaximumHeight(90);
    form->addRow("个人备注", notes_);
    precision_ = new QComboBox;
    precision_->setObjectName("taskTimePrecision");
    precision_->addItem("日期待定", "unknown");
    precision_->addItem("仅日期", "date");
    precision_->addItem("准确时刻", "datetime");
    precision_->setCurrentIndex(
        precision_->findData(QString::fromStdString(timePrecisionKey(initial_.time.precision))));
    form->addRow("时间精度", precision_);
    const auto today = QDateTime::currentDateTimeUtc().toTimeZone(zone_);
    date_ = new QDateEdit;
    date_->setObjectName("taskDate");
    date_->setCalendarPopup(true);
    date_->setDisplayFormat("yyyy-MM-dd");
    date_->setDateRange(QDate(1, 1, 1), QDate(9999, 12, 31));
    date_->setDate(
        initial_.time.date.empty()
            ? today.date()
            : QDate::fromString(QString::fromStdString(initial_.time.date), Qt::ISODate));
    form->addRow("办理日期", date_);
    dateTime_ = new QDateTimeEdit;
    dateTime_->setObjectName("taskDateTime");
    dateTime_->setTimeZone(zone_);
    dateTime_->setCalendarPopup(true);
    dateTime_->setDisplayFormat("yyyy-MM-dd HH:mm:ss");
    dateTime_->setDateRange(QDate(1, 1, 1), QDate(9999, 12, 31));
    dateTime_->setDateTime(
        initial_.time.utcDateTime.empty()
            ? today
            : QDateTime::fromString(QString::fromStdString(initial_.time.utcDateTime), Qt::ISODate)
                  .toTimeZone(zone_));
    form->addRow("办理时刻", dateTime_);
    form->addRow("学校时区", new QLabel(QString::fromUtf8(zone_.id())));
    source_ = new QComboBox;
    source_->setObjectName("taskTimeSource");
    source_->addItem("个人计划时间", "personal");
    source_->addItem("核对原文后的时间", "original_text");
    source_->setCurrentIndex(initial_.time.confirmation == TimeConfirmation::OriginalText ? 1 : 0);
    form->addRow("时间来源", source_);
    evidence_ = new QTextEdit;
    evidence_->setPlainText(QString::fromStdString(initial_.time.evidence));
    evidence_->setObjectName("taskTimeEvidence");
    evidence_->setAcceptRichText(false);
    evidence_->setMaximumHeight(80);
    evidence_->setPlaceholderText("原文时间表述或个人设置理由；摘自原文时请填写原文说明");
    form->addRow("时间说明", evidence_);
    confirmed_ = new QCheckBox("已核对并确认此日期或时刻");
    confirmed_->setObjectName("taskTimeConfirmed");
    confirmed_->setChecked(initial_.time.precision != TimePrecision::Unknown);
    form->addRow(confirmed_);
    remind_ = new QCheckBox("启用本地提醒（应用运行时有效）");
    remind_->setObjectName("taskReminderEnabled");
    remind_->setChecked(initial_.reminder.enabled);
    form->addRow(remind_);
    minutes_ = new QSpinBox;
    minutes_->setObjectName("taskReminderMinutes");
    minutes_->setRange(0, 525600);
    minutes_->setSuffix(" 分钟");
    minutes_->setValue(initial_.reminder.minutesBefore);
    form->addRow("准确时刻提前", minutes_);
    days_ = new QSpinBox;
    days_->setObjectName("taskReminderDays");
    days_->setRange(0, 365);
    days_->setSuffix(" 天");
    days_->setValue(initial_.reminder.daysBefore);
    form->addRow("仅日期提前", days_);
    reminderTime_ = new QTimeEdit;
    reminderTime_->setObjectName("taskReminderTime");
    reminderTime_->setDisplayFormat("HH:mm");
    reminderTime_->setTime(
        QTime::fromString(QString::fromStdString(initial_.reminder.dateOnlyAt), "HH:mm"));
    form->addRow("仅日期提醒钟点", reminderTime_);
    auto *boundary = new QLabel("仅日期保持日期精度；提醒钟点单独保存。日期待定的事项可以保存。");
    boundary->setWordWrap(true);
    form->addRow(boundary);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    error_ = new QLabel;
    error_->setObjectName("taskError");
    error_->setTextFormat(Qt::PlainText);
    error_->setWordWrap(true);
    layout->addWidget(error_);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText("保存待办");
    buttons->button(QDialogButtonBox::Save)->setObjectName("saveTaskButton");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &TaskEditorDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(precision_, &QComboBox::currentIndexChanged, this, [this] {
        timeChanged();
        updateTimeControls();
    });
    connect(date_, &QDateEdit::dateChanged, this, [this] { timeChanged(); });
    connect(dateTime_, &QDateTimeEdit::dateTimeChanged, this, [this] { timeChanged(); });
    connect(source_, &QComboBox::currentIndexChanged, this, [this] { timeChanged(); });
    connect(evidence_, &QTextEdit::textChanged, this, [this] { timeChanged(); });
    connect(remind_, &QCheckBox::toggled, this, [this] { updateTimeControls(); });
    updateTimeControls();
}
void TaskEditorDialog::timeChanged() {
    confirmed_->setChecked(false);
}
void TaskEditorDialog::updateTimeControls() {
    const auto key = precision_->currentData().toString();
    const bool known = key != "unknown";
    date_->setEnabled(key == "date");
    dateTime_->setEnabled(key == "datetime");
    source_->setEnabled(known);
    confirmed_->setEnabled(known);
    remind_->setEnabled(known);
    if (!known) {
        confirmed_->setChecked(false);
        remind_->setChecked(false);
    }
    minutes_->setEnabled(key == "datetime" && remind_->isChecked());
    days_->setEnabled(key == "date" && remind_->isChecked());
    reminderTime_->setEnabled(key == "date" && remind_->isChecked());
}
void TaskEditorDialog::save() {
    try {
        auto task = initial_;
        task.title = title_->text().trimmed().toStdString();
        task.notes = notes_->toPlainText().toStdString();
        task.action = action_->currentData().toString().toStdString();
        task.status = taskStatusFromKey(status_->currentData().toString().toStdString());
        task.time.precision =
            timePrecisionFromKey(precision_->currentData().toString().toStdString());
        task.time.timeZone = zone_.id().toStdString();
        task.time.evidence = evidence_->toPlainText().toStdString();
        task.time.confirmation =
            task.time.precision == TimePrecision::Unknown
                ? TimeConfirmation::None
                : confirmationFromKey(source_->currentData().toString().toStdString());
        if (task.time.precision == TimePrecision::DateOnly)
            task.time.date = date_->date().toString(Qt::ISODate).toStdString();
        if (task.time.precision == TimePrecision::DateTime) {
            const QDateTime chosen(dateTime_->date(), dateTime_->time(), zone_,
                                   QDateTime::TransitionResolution::Reject);
            if (!chosen.isValid())
                throw std::invalid_argument("此时刻在学校时区不存在或不唯一，请选择其他时刻");
            task.time.utcDateTime = chosen.toUTC().toString(Qt::ISODate).toStdString();
        }
        task.reminder.enabled = remind_->isChecked();
        task.reminder.minutesBefore = minutes_->value();
        task.reminder.daysBefore = days_->value();
        task.reminder.dateOnlyAt = reminderTime_->time().toString("HH:mm").toStdString();
        saved_ = service_.save(std::move(task), confirmed_->isChecked());
        accept();
    } catch (const std::exception &e) {
        error_->setText(QString::fromUtf8(e.what()));
    }
}
} // namespace campus

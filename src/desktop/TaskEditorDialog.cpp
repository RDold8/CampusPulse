#include "desktop/TaskEditorDialog.h"
#include "desktop/TaskDisplay.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTimeEdit>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTextEdit>
#include <QTimeEdit>
#include <QToolButton>
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
    resize(690, 350);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    form_ = new QFormLayout(content);
    form_->setContentsMargins(0, 0, 0, 0);
    form_->setVerticalSpacing(10);
    form_->setHorizontalSpacing(12);
    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    auto *origin = new QLabel("关联通知：" + noticeTitle);
    origin->setTextFormat(Qt::PlainText);
    origin->setWordWrap(true);
    form_->addRow(origin);
    title_ = new QLineEdit(QString::fromStdString(initial_.title));
    title_->setObjectName("taskTitle");
    title_->setMaxLength(160);
    title_->setPlaceholderText("例如：完成重修缴费、提交奖学金申请材料");
    form_->addRow("办理事项", title_);
    scheduled_ = new QCheckBox("安排时间（取消勾选可暂不安排）");
    scheduled_->setObjectName("taskTimeScheduled");
    form_->addRow(scheduled_);
    const auto today = QDateTime::currentDateTimeUtc().toTimeZone(zone_);
    auto proposed = today.addSecs(3600);
    proposed.setTime(QTime(proposed.time().hour(), proposed.time().minute(), 0));
    date_ = new QDateEdit;
    date_->setObjectName("taskDate");
    date_->setCalendarPopup(true);
    date_->setDisplayFormat("yyyy年MM月dd日");
    date_->setDateRange(QDate(1, 1, 1), QDate(9999, 12, 31));
    date_->setDate(initial_.time.date.empty()
                       ? today.date()
                       : QDate::fromString(QString::fromStdString(initial_.time.date), Qt::ISODate));
    date_->installEventFilter(this);
    form_->addRow("办理日期", date_);
    dateTime_ = new QDateTimeEdit;
    dateTime_->setObjectName("taskDateTime");
    dateTime_->setTimeZone(zone_);
    dateTime_->setCalendarPopup(true);
    dateTime_->setDisplayFormat("yyyy年MM月dd日 HH:mm");
    dateTime_->setDateRange(QDate(1, 1, 1), QDate(9999, 12, 31));
    dateTime_->setDateTime(initial_.time.utcDateTime.empty()
                               ? proposed
                               : QDateTime::fromString(QString::fromStdString(initial_.time.utcDateTime),
                                                       Qt::ISODate)
                                     .toTimeZone(zone_));
    dateTime_->installEventFilter(this);
    form_->addRow("办理时间", dateTime_);
    reminderPreset_ = new QComboBox;
    reminderPreset_->setObjectName("taskReminderPreset");
    form_->addRow("提醒我", reminderPreset_);
    // Retain the previous control names for automation and the existing storage contract.
    // This checkbox is an internal mirror of the single visible reminder selector.
    remind_ = new QCheckBox(content);
    remind_->setObjectName("taskReminderEnabled");
    remind_->setChecked(initial_.reminder.enabled);
    remind_->hide();
    minutes_ = new QSpinBox;
    minutes_->setObjectName("taskReminderMinutes");
    minutes_->setRange(0, 525600);
    minutes_->setSuffix(" 分钟");
    minutes_->setValue(initial_.reminder.minutesBefore);
    form_->addRow("提前多久", minutes_);
    days_ = new QSpinBox;
    days_->setObjectName("taskReminderDays");
    days_->setRange(0, 365);
    days_->setSuffix(" 天");
    days_->setValue(initial_.reminder.daysBefore);
    form_->addRow("提前几天", days_);
    reminderTime_ = new QTimeEdit;
    reminderTime_->setObjectName("taskReminderTime");
    reminderTime_->setDisplayFormat("HH:mm");
    reminderTime_->setTime(QTime::fromString(QString::fromStdString(initial_.reminder.dateOnlyAt),
                                           "HH:mm"));
    form_->addRow("提醒钟点", reminderTime_);
    preview_ = new QLabel;
    preview_->setObjectName("taskReminderPreview");
    preview_->setTextFormat(Qt::PlainText);
    preview_->setWordWrap(true);
    form_->addRow(preview_);
    more_ = new QToolButton;
    more_->setObjectName("taskMoreOptions");
    more_->setText("更多设置");
    more_->setCheckable(true);
    more_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    more_->setArrowType(Qt::RightArrow);
    form_->addRow(more_);
    advanced_ = new QWidget;
    advanced_->setObjectName("taskAdvancedOptions");
    advancedForm_ = new QFormLayout(advanced_);
    advancedForm_->setContentsMargins(0, 0, 0, 0);
    advancedForm_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    action_ = new QComboBox;
    action_->setObjectName("taskAction");
    for (const auto &a : TaskActions)
        action_->addItem(taskActionText(std::string(a.key)),
                         QString::fromUtf8(a.key.data(), static_cast<int>(a.key.size())));
    action_->setCurrentIndex(action_->findData(QString::fromStdString(initial_.action)));
    advancedForm_->addRow("操作类型", action_);
    status_ = new QComboBox;
    status_->setObjectName("taskStatus");
    for (auto s : {TaskStatus::NotStarted, TaskStatus::InProgress, TaskStatus::Completed,
                   TaskStatus::Cancelled})
        status_->addItem(taskStatusText(s), QString::fromStdString(taskStatusKey(s)));
    status_->setCurrentIndex(status_->findData(QString::fromStdString(taskStatusKey(initial_.status))));
    advancedForm_->addRow("办理状态", status_);
    notes_ = new QTextEdit;
    notes_->setPlainText(QString::fromStdString(initial_.notes));
    notes_->setObjectName("taskNotes");
    notes_->setAcceptRichText(false);
    notes_->setMaximumHeight(80);
    advancedForm_->addRow("个人备注", notes_);
    precision_ = new QComboBox;
    precision_->setObjectName("taskTimePrecision");
    precision_->addItem("暂不安排", "unknown");
    precision_->addItem("仅日期", "date");
    precision_->addItem("日期和时刻", "datetime");
    const bool fresh = initial_.id.empty();
    precision_->setCurrentIndex(precision_->findData(
        fresh && initial_.time.precision == TimePrecision::Unknown
            ? QString("datetime")
            : QString::fromStdString(timePrecisionKey(initial_.time.precision))));
    advancedForm_->addRow("时间格式", precision_);
    advancedForm_->addRow("学校时区", new QLabel(QString::fromUtf8(zone_.id())));
    source_ = new QComboBox;
    source_->setObjectName("taskTimeSource");
    source_->addItem("个人计划时间", "personal");
    source_->addItem("核对原文后的时间", "original_text");
    source_->setCurrentIndex(initial_.time.confirmation == TimeConfirmation::OriginalText ? 1 : 0);
    advancedForm_->addRow("时间来源", source_);
    evidence_ = new QTextEdit;
    evidence_->setPlainText(QString::fromStdString(initial_.time.evidence));
    evidence_->setObjectName("taskTimeEvidence");
    evidence_->setAcceptRichText(false);
    evidence_->setMaximumHeight(80);
    evidence_->setPlaceholderText("摘自原文时填写原文的时间表述；通知发布日期不等于办理期限");
    advancedForm_->addRow("原文时间说明", evidence_);
    confirmed_ = new QCheckBox("我已核对原文，确认上述办理时间");
    confirmed_->setObjectName("taskTimeConfirmed");
    confirmed_->setChecked(initial_.time.precision != TimePrecision::Unknown);
    advancedForm_->addRow(confirmed_);
    form_->addRow(advanced_);
    advanced_->hide();
    if (fresh && initial_.time.precision == TimePrecision::Unknown) {
        minutes_->setValue(0);
        days_->setValue(0);
        remind_->setChecked(true);
    }
    scheduled_->setChecked(precision_->currentData().toString() != "unknown");
    rebuildReminderPresets();
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    error_ = new QLabel;
    error_->setObjectName("taskError");
    error_->setTextFormat(Qt::PlainText);
    error_->setWordWrap(true);
    layout->addWidget(error_);
    error_->hide();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Save)->setText("保存待办");
    buttons->button(QDialogButtonBox::Save)->setObjectName("saveTaskButton");
    buttons->button(QDialogButtonBox::Cancel)->setText("取消");
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &TaskEditorDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(more_, &QToolButton::toggled, this, [this](bool expanded) {
        advanced_->setVisible(expanded);
        more_->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        resize(width(), expanded ? 610 : 350);
    });
    connect(scheduled_, &QCheckBox::toggled, this, [this](bool scheduled) {
        if (!scheduled)
            precision_->setCurrentIndex(precision_->findData("unknown"));
        else if (precision_->currentData().toString() == "unknown")
            precision_->setCurrentIndex(precision_->findData("datetime"));
    });
    connect(precision_, &QComboBox::currentIndexChanged, this, [this] {
        timeChanged();
        if (initial_.id.empty() && !reminderChosen_ && precision_->currentData().toString() != "unknown") {
            QSignalBlocker block(remind_);
            remind_->setChecked(true);
            minutes_->setValue(0);
            days_->setValue(0);
            reminderTime_->setTime(QTime(9, 0));
        }
        rebuildReminderPresets();
        updateTimeControls();
    });
    connect(date_, &QDateEdit::dateChanged, this, [this] { timeChanged(); });
    connect(dateTime_, &QDateTimeEdit::dateTimeChanged, this, [this] { timeChanged(); });
    connect(source_, &QComboBox::currentIndexChanged, this, [this] {
        timeChanged();
        updateTimeControls();
    });
    connect(evidence_, &QTextEdit::textChanged, this, [this] { timeChanged(); });
    connect(reminderPreset_, &QComboBox::currentIndexChanged, this, [this] {
        reminderChosen_ = true;
        applyReminderPreset();
    });
    connect(remind_, &QCheckBox::toggled, this, [this] {
        rebuildReminderPresets();
        updateTimeControls();
    });
    connect(minutes_, &QSpinBox::valueChanged, this, [this] { updatePreview(); });
    connect(days_, &QSpinBox::valueChanged, this, [this] { updatePreview(); });
    connect(reminderTime_, &QTimeEdit::timeChanged, this, [this] { updatePreview(); });
    connect(status_, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    updateTimeControls();
}
bool TaskEditorDialog::eventFilter(QObject *watched, QEvent *event) {
    // A scroll through the dialog must not silently move the selected year or date.
    if ((watched == date_ || watched == dateTime_) && event->type() == QEvent::Wheel)
        return true;
    return QDialog::eventFilter(watched, event);
}
void TaskEditorDialog::timeChanged() {
    confirmed_->setChecked(false);
    updatePreview();
}
void TaskEditorDialog::rebuildReminderPresets() {
    const QSignalBlocker block(reminderPreset_);
    reminderPreset_->clear();
    reminderPreset_->addItem("不提醒", "off");
    const bool dateOnly = precision_->currentData().toString() == "date";
    if (dateOnly) {
        reminderPreset_->addItem("当天 09:00", "0");
        reminderPreset_->addItem("提前一天 09:00", "1");
    } else {
        reminderPreset_->addItem("到时间提醒", "0");
        reminderPreset_->addItem("提前 10 分钟", "10");
        reminderPreset_->addItem("提前 30 分钟", "30");
        reminderPreset_->addItem("提前 1 小时", "60");
        reminderPreset_->addItem("提前 1 天", "1440");
    }
    reminderPreset_->addItem("自定义…", "custom");
    QString chosen = "off";
    if (remind_->isChecked()) {
        chosen = QString::number(dateOnly ? days_->value() : minutes_->value());
        if ((dateOnly && reminderTime_->time() != QTime(9, 0)) ||
            reminderPreset_->findData(chosen) < 0)
            chosen = "custom";
    }
    reminderPreset_->setCurrentIndex(reminderPreset_->findData(chosen));
}
void TaskEditorDialog::applyReminderPreset() {
    const auto chosen = reminderPreset_->currentData().toString();
    const QSignalBlocker block(remind_);
    remind_->setChecked(chosen != "off");
    if (chosen != "off" && chosen != "custom") {
        if (precision_->currentData().toString() == "date") {
            days_->setValue(chosen.toInt());
            reminderTime_->setTime(QTime(9, 0));
        } else
            minutes_->setValue(chosen.toInt());
    }
    updateTimeControls();
}
void TaskEditorDialog::updateTimeControls() {
    const auto key = precision_->currentData().toString();
    const bool known = key != "unknown";
    const QSignalBlocker scheduledBlock(scheduled_);
    scheduled_->setChecked(known);
    form_->setRowVisible(date_, key == "date");
    form_->setRowVisible(dateTime_, key == "datetime");
    source_->setEnabled(known);
    confirmed_->setEnabled(known);
    remind_->setEnabled(known);
    reminderPreset_->setEnabled(known);
    const bool original = known && source_->currentData().toString() == "original_text";
    advancedForm_->setRowVisible(evidence_, original);
    advancedForm_->setRowVisible(confirmed_, original);
    if (!known) {
        confirmed_->setChecked(false);
        const QSignalBlocker block(remind_);
        remind_->setChecked(false);
        rebuildReminderPresets();
    }
    const bool custom = known && remind_->isChecked() &&
                        reminderPreset_->currentData().toString() == "custom";
    form_->setRowVisible(minutes_, key == "datetime" && custom);
    form_->setRowVisible(days_, key == "date" && custom);
    form_->setRowVisible(reminderTime_, key == "date" && custom);
    updatePreview();
}
void TaskEditorDialog::updatePreview() {
    if (precision_->currentData().toString() == "unknown") {
        preview_->setText("暂不安排时间，保存后可随时补充。不会根据通知发布日期推断办理期限。");
        return;
    }
    const auto personal = source_->currentData().toString() == "personal";
    const QString origin = personal ? "这是你设置的个人计划时间。" : "办理时间来自原文，请在更多设置中核对并确认。";
    if (!remind_->isChecked()) {
        preview_->setText("已安排时间，当前不提醒。" + origin);
        return;
    }
    QDateTime fire;
    if (precision_->currentData().toString() == "datetime")
        fire = QDateTime(dateTime_->date(), dateTime_->time(), zone_,
                         QDateTime::TransitionResolution::Reject)
                   .addSecs(-qint64(minutes_->value()) * 60);
    else
        fire = QDateTime(date_->date().addDays(-days_->value()), reminderTime_->time(), zone_,
                         QDateTime::TransitionResolution::Reject);
    if (!fire.isValid()) {
        preview_->setText("所选提醒时刻在学校时区无效，请调整。" + origin);
        return;
    }
    auto message = QString("提醒时间：%1（%2）\n%3应用运行时有效。")
                       .arg(fire.toString("yyyy年MM月dd日 HH:mm"), QString::fromUtf8(zone_.id()), origin);
    if (!TaskService::active(taskStatusFromKey(status_->currentData().toString().toStdString())))
        message += "\n此待办已完成或取消，不会触发提醒。";
    if (fire <= QDateTime::currentDateTimeUtc())
        message += "\n提醒时刻已过去；保存历史事项不会保证补发提醒，请改为未来时刻。";
    preview_->setText(message);
}
void TaskEditorDialog::save() {
    try {
        auto task = initial_;
        task.title = title_->text().trimmed().toStdString();
        task.notes = notes_->toPlainText().toStdString();
        task.action = action_->currentData().toString().toStdString();
        task.status = taskStatusFromKey(status_->currentData().toString().toStdString());
        task.time.precision = timePrecisionFromKey(precision_->currentData().toString().toStdString());
        task.time.timeZone = zone_.id().toStdString();
        task.time.evidence = evidence_->toPlainText().toStdString();
        task.time.confirmation = task.time.precision == TimePrecision::Unknown
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
        const bool confirm = task.time.confirmation == TimeConfirmation::Personal || confirmed_->isChecked();
        saved_ = service_.save(std::move(task), confirm);
        accept();
    } catch (const std::exception &e) {
        error_->setText(QString::fromUtf8(e.what()));
        error_->show();
        if (source_->currentData().toString() == "original_text")
            more_->setChecked(true);
    }
}
} // namespace campus

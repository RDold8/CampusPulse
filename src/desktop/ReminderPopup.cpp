#include "desktop/ReminderPopup.h"
#include "desktop/BrandTheme.h"
#include "adapters/ReminderScheduler.h"
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QTimeZone>
#include <QVBoxLayout>

namespace campus {
ReminderPopup::ReminderPopup(QWidget *parent) : QDialog(parent) {
    setObjectName("reminderPopup");
    setWindowTitle("CampusPulse · 待办提醒");
    setWindowFlag(Qt::WindowStaysOnTopHint);
    setModal(false);
    resize(520, 330);
    auto *layout = new QVBoxLayout(this);
    heading_ = new QLabel("到时间了", this);
    heading_->setObjectName("reminderPopupHeading");
    heading_->setStyleSheet("font-size: 24px; font-weight: bold;");
    layout->addWidget(heading_);
    auto *hint = new QLabel("提醒会保留到你关闭。查看待办后，可以标记完成或修改时间。", this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    items_ = new QListWidget(this);
    items_->setObjectName("reminderPopupItems");
    items_->setWordWrap(true);
    items_->setSpacing(8);
    layout->addWidget(items_);
    auto *buttons = new QDialogButtonBox(this);
    auto *open = buttons->addButton("查看待办", QDialogButtonBox::ActionRole);
    open->setObjectName("openReminderTaskButton");
    auto *dismiss = buttons->addButton("知道了", QDialogButtonBox::AcceptRole);
    dismiss->setObjectName("dismissReminderButton");
    layout->addWidget(buttons);
    connect(open, &QPushButton::clicked, this, [this] {
        const auto *item = items_->currentItem();
        if (item && !item->data(Qt::UserRole).toString().isEmpty())
            emit taskRequested(item->data(Qt::UserRole + 1).toString(),
                               item->data(Qt::UserRole).toString());
    });
    connect(dismiss, &QPushButton::clicked, this, [this] {
        const int row = items_->currentRow();
        delete items_->takeItem(row < 0 ? 0 : row);
        if (items_->count() == 0) hide();
        else items_->setCurrentRow(0);
    });
    BrandTheme::applyWindow(*this);
}
void ReminderPopup::showTask(const PersonalTask &task, const QString &schoolName) {
    const auto trigger = ReminderScheduler::trigger(task);
    const auto key = QString::fromStdString(task.id) + ":" + trigger.toString(Qt::ISODate);
    if (displayed_.contains(key)) return;
    displayed_.insert(key);
    heading_->setText("到时间了");
    const QTimeZone zone(QByteArray::fromStdString(task.time.timeZone));
    const auto time = trigger.toTimeZone(zone).toString("yyyy-MM-dd HH:mm");
    const auto label = schoolName.isEmpty() ? QString::fromStdString(task.schoolId) : schoolName;
    auto *item = new QListWidgetItem(QString::fromStdString(task.title) + "\n" +
                                    label + " · 提醒时间 " + time, items_);
    item->setData(Qt::UserRole, QString::fromStdString(task.id));
    item->setData(Qt::UserRole + 1, QString::fromStdString(task.schoolId));
    items_->setCurrentItem(item);
    present();
}
void ReminderPopup::showTest() {
    heading_->setText("测试提醒已弹出");
    auto *item = new QListWidgetItem("这是一条测试提醒。真实待办到点也会在这里显示。\n"
                                    "测试不会创建待办或修改你的数据。", items_);
    items_->setCurrentItem(item);
    present();
}
void ReminderPopup::showFailure(const QString &reason) {
    if (failures_.contains(reason)) return;
    failures_.insert(reason);
    heading_->setText("提醒遇到问题");
    auto *item = new QListWidgetItem("请检查待办与本机存储：\n" + reason, items_);
    items_->setCurrentItem(item);
    present();
}
void ReminderPopup::present() {
    show();
    raise();
    activateWindow();
    QApplication::alert(this, 0);
    QApplication::beep();
}
int ReminderPopup::reminderCount() const { return items_->count(); }
void ReminderPopup::closeEvent(QCloseEvent *event) {
    items_->clear();
    QDialog::closeEvent(event);
}
} // namespace campus

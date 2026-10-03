#pragma once
#include "domain/Reminder.h"
#include "domain/PersonalTask.h"
#include <QObject>
#include <QDateTime>
#include <QTimer>
#include <functional>
namespace campus {
class ReminderScheduler final : public QObject {
    Q_OBJECT
  public:
    using Delivery = std::function<void(const PersonalTask &)>;
    ReminderScheduler(TaskRepository &, ReminderRepository &, Delivery, QObject *parent = nullptr);
    void start();
    void poll(const QDateTime &now);
    static QDateTime trigger(const PersonalTask &);
  signals:
    void delivered(QString title, QString taskId);
    void failed(QString error);

  private:
    TaskRepository &tasks_;
    ReminderRepository &records_;
    Delivery delivery_;
    QTimer timer_;
    QDateTime previous_;
};
} // namespace campus

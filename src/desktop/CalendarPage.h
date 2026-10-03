#pragma once
#include "adapters/SchoolPackage.h"
#include "application/CalendarService.h"
#include <QWidget>

class QCalendarWidget;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QStandardItemModel;
class QTableView;
class QTextBrowser;

namespace campus {
class CalendarPage final : public QWidget {
    Q_OBJECT
  public:
    CalendarPage(const SchoolPackage &school, TaskService &tasks, QWidget *parent = nullptr);
    void reload(const QString &selectId = {});
    bool exportToFile(const QString &path, bool wholeMonth, bool includeAlarm);

  signals:
    void tasksChanged();
    void noticeRequested(QString id);
    void showTasksRequested();

  private:
    SchoolPackage school_;
    TaskService &tasks_;
    CalendarService calendarService_;
    std::vector<CalendarItem> monthItems_;
    QCalendarWidget *calendar_;
    QComboBox *visibility_;
    QCheckBox *alarms_;
    QStandardItemModel *model_;
    QTableView *table_;
    QLabel *summary_, *status_, *day_;
    QTextBrowser *detail_;
    QPushButton *edit_, *open_, *exportSelected_, *exportMonth_;
    QString selectedId() const;
    const CalendarItem *selected() const;
    void selectionChanged();
    void edit();
    void chooseExport(bool wholeMonth);
};
} // namespace campus

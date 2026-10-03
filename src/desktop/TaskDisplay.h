#pragma once
#include "domain/PersonalTask.h"
#include <QString>
namespace campus {
QString taskStatusText(TaskStatus status);
QString taskActionText(const std::string &action);
QString taskTimeText(const TaskTime &time);
} // namespace campus

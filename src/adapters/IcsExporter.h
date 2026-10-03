#pragma once
#include "application/CalendarService.h"
#include <QByteArray>
#include <QString>

namespace campus {
struct IcsExportOptions {
    bool includeAlarms = false;
};
class IcsExporter {
  public:
    static QByteArray render(const std::vector<CalendarItem> &items, IcsExportOptions options = {});
    static void write(const QString &path, const std::vector<CalendarItem> &items,
                      IcsExportOptions options = {});
};
} // namespace campus

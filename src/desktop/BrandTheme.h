#pragma once
#include <QColor>
#include <QIcon>
#include <QString>

class QApplication;
class QWidget;

namespace campus {
// Shared native desktop presentation. Business widgets keep their object names and signals.
class BrandTheme final {
  public:
    enum class StatusTone { Neutral, Success, Warning, Error };
    static void installApplication(QApplication &application);
    static void applyWindow(QWidget &window);
    static QIcon applicationIcon();
    static QIcon navigationIcon(const QString &page);
    static QColor statusColor(StatusTone tone);
    static QString styleSheet();
};
} // namespace campus

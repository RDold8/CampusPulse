#pragma once
#include <QString>
class QWidget;
namespace campus {
// Moves only this application's window. Does not switch the user's active desktop.
void placeWindowOnDesktop(QWidget &window, const QString &desktopId);
} // namespace campus

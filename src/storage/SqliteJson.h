#pragma once
#include <QString>
#include <string>
#include <vector>
namespace campus {
QString stringsJson(const std::vector<std::string> &values);
std::vector<std::string> stringsFromJson(const QString &json);
} // namespace campus

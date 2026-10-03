#include "storage/SqliteJson.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <stdexcept>
namespace campus {
QString stringsJson(const std::vector<std::string> &values) {
    QJsonArray array;
    for (const auto &value : values)
        array.append(QString::fromStdString(value));
    return QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}
std::vector<std::string> stringsFromJson(const QString &json) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray())
        throw std::runtime_error("保存的筛选或标签数据无效");
    std::vector<std::string> result;
    for (const auto value : document.array()) {
        if (!value.isString())
            throw std::runtime_error("保存的筛选项必须是字符串");
        result.push_back(value.toString().toStdString());
    }
    return result;
}
} // namespace campus

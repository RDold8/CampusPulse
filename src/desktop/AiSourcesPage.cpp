#include "desktop/AiSourcesPage.h"
#include "adapters/SchoolOnboarding.h"
#include "adapters/UniversityRegistry.h"
#include "adapters/RefreshCoordinator.h"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QListWidget>
#include <QSettings>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QTimer>
#include <stdexcept>
namespace campus {
AiSourcesPage::AiSourcesPage(const SchoolPackage &school, const UniversityRegistry &registry,
                             RefreshCoordinator &refresh, QWidget *parent)
    : QWidget(parent), school_(school), search_(this) {
    setObjectName("aiSourcesPage");
    for (const auto &entry : registry.list())
        if (entry.id == school.id)
            root_ = entry.homepage.host();
    if (root_.startsWith("www."))
        root_.remove(0, 4);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    auto *heading = new QLabel("AI 补充官网栏目 · DeepSeek");
    layout->addWidget(heading);
    auto *intro = new QLabel(
        "规则爬虫先采集，AI "
        "再寻找公开栏目候选，不保证找到全部信息。搜索结果必须属于本校官网，并通过列表和正文校验才能接入。"
        "不能据此判断学校购买权限、账号可用性或资源全文是否可读。开启AI会消耗API "
        "token；普通采集不调用模型。仅发送学校名称和官网域名，不发送个人待办或数据库。");
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *form = new QFormLayout;
    key_ = new QLineEdit(DeepSeekSearch::sessionKey());
    key_->setEchoMode(QLineEdit::Password);
    key_->setObjectName("deepseekApiKey");
    key_->setPlaceholderText("本次进程有效；也可使用DEEPSEEK_API_KEY");
    model_ = new QLineEdit(QSettings().value("ai/model", "deepseek-flash").toString());
    model_->setObjectName("deepseekModel");
    form->addRow("API Key", key_);
    form->addRow("检索模型", model_);
    layout->addLayout(form);
    automatic_ = new QCheckBox("规则更新完成后自动补充（每校至少间隔1小时）");
    automatic_->setObjectName("aiAutoSupplement");
    automatic_->setChecked(QSettings().value("ai/enabled", false).toBool());
    connect(automatic_, &QCheckBox::toggled, this,
            [](bool checked) { QSettings().setValue("ai/enabled", checked); });
    layout->addWidget(automatic_);
    run_ = new QPushButton("用 DS 寻找遗漏栏目并校验接入");
    run_->setObjectName("aiSearchButton");
    layout->addWidget(run_);
    connect(run_, &QPushButton::clicked, this, &AiSourcesPage::run);
    status_ = new QLabel("AI补充默认关闭。Key仅保留在内存，不写入学校包、日志或设置文件。");
    status_->setObjectName("aiStatus");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    results_ = new QListWidget;
    results_->setObjectName("aiCandidates");
    layout->addWidget(results_, 1);
    connect(&search_, &DeepSeekSearch::failed, this, [this](const QString &reason) {
        busy_ = false;
        run_->setEnabled(!refreshing_);
        status_->setText(reason);
    });
    connect(&search_, &DeepSeekSearch::finished, this, &AiSourcesPage::validate);
    connect(&refresh, &RefreshCoordinator::started, this, [this] {
        refreshing_ = true;
        run_->setEnabled(false);
    });
    connect(&refresh, &RefreshCoordinator::finished, this, [this](int good, int) {
        refreshing_ = false;
        run_->setEnabled(!busy_);
        const auto last = QSettings().value("ai/lastAttempt/" + school_.id).toLongLong();
        if (automatic_->isChecked() && good > 0 && !key_->text().trimmed().isEmpty() &&
            QDateTime::currentSecsSinceEpoch() - last >= 3600)
            QTimer::singleShot(0, this, &AiSourcesPage::run);
    });
}
void AiSourcesPage::run() {
    if (busy_ || refreshing_)
        return;
    if (key_->text().trimmed().isEmpty()) {
        status_->setText("请填写DeepSeek API Key；尚未发起请求，不会消耗token。");
        return;
    }
    busy_ = true;
    run_->setEnabled(false);
    results_->clear();
    QSettings settings;
    requestedModel_ = model_->text().trimmed();
    settings.setValue("ai/model", model_->text().trimmed());
    settings.setValue("ai/lastAttempt/" + school_.id, QDateTime::currentSecsSinceEpoch());
    QSet<QString> known;
    for (const auto &source : school_.catalog) {
        QUrl url(QString::fromStdString(source.entryUrl));
        if (url.scheme() == "http")
            url.setScheme("https");
        known.insert(url.toString(QUrl::FullyEncoded));
    }
    status_->setText("正在调用DeepSeek原生检索：最多2次搜索、2048输出token；不会自动重试。");
    search_.search(key_->text(), requestedModel_, school_.name, root_, known);
}
void AiSourcesPage::validate(QJsonArray candidates, QJsonObject usage) {
    const auto auditFolder =
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/ai-schools";
    const QJsonObject audit{{"contract_version", "search-v1"},
                            {"status", "candidate_only"},
                            {"school_id", school_.id},
                            {"model", requestedModel_},
                            {"model_calls", 1},
                            {"usage", usage},
                            {"candidates", candidates},
                            {"searched_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    QSaveFile auditFile(auditFolder + "/" + school_.id + ".search.json");
    const auto auditBytes = QJsonDocument(audit).toJson();
    if (!QDir().mkpath(auditFolder) || !auditFile.open(QIODevice::WriteOnly) ||
        auditFile.write(auditBytes) != auditBytes.size() || !auditFile.commit()) {
        busy_ = false;
        run_->setEnabled(!refreshing_);
        status_->setText("DeepSeek已返回，但检索记录保存失败；未添加来源。");
        return;
    }
    const auto use = QString("输入token %1 · 输出token %2")
                         .arg(usage.value("input_tokens").toVariant().toString(),
                              usage.value("output_tokens").toVariant().toString());
    if (candidates.empty()) {
        busy_ = false;
        run_->setEnabled(!refreshing_);
        status_->setText("检索完成，没有新的本校栏目候选。" + use);
        return;
    }
    QStringList urls;
    for (const auto &value : candidates) {
        const auto hit = value.toObject();
        urls.append(hit.value("url").toString());
        results_->addItem(hit.value("title").toString() + "\n" + urls.back());
    }
    try {
        QFile current(school_.configFile);
        if (!current.open(QIODevice::ReadOnly))
            throw std::runtime_error("无法读取当前学校配置");
        auto seed = QJsonDocument::fromJson(current.readAll()).object();
        current.close();
        seed["auto_discovery"] = QJsonObject{{"department_urls", QJsonArray{}}, {"max_pages", 24}};
        const auto folder =
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/ai-schools";
        if (!QDir().mkpath(folder))
            throw std::runtime_error("无法创建AI补充缓存目录");
        const auto seedFile = folder + "/" + school_.id + ".seed.json";
        QSaveFile file(seedFile);
        const auto bytes = QJsonDocument(seed).toJson();
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
            throw std::runtime_error("无法保存AI补充种子");
        auto *scan = new SchoolOnboarding(seedFile, folder, this, 3000, urls);
        connect(scan, &SchoolOnboarding::progress, this,
                [this, use](const QString &message) { status_->setText(use + "\n" + message); });
        connect(scan, &SchoolOnboarding::failed, this, [this, scan, use](const QString &error) {
            busy_ = false;
            run_->setEnabled(!refreshing_);
            status_->setText(use + "\n" + error);
            scan->deleteLater();
        });
        connect(scan, &SchoolOnboarding::finished, this,
                [this, scan, use](const QString &config, int count, int) {
                    busy_ = false;
                    status_->setText(QString("新增%1个已校验来源。%2").arg(count).arg(use));
                    scan->deleteLater();
                    emit configReady(config);
                });
        scan->start();
    } catch (const std::exception &e) {
        busy_ = false;
        run_->setEnabled(!refreshing_);
        status_->setText(use + "\n" + QString::fromUtf8(e.what()));
    }
}
} // namespace campus

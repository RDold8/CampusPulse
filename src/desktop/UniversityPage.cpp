#include "desktop/UniversityPage.h"
#include "adapters/UniversityRegistry.h"
#include "adapters/UnknownUniversityDiscovery.h"
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QListWidget>
#include <QProgressBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <stdexcept>

namespace campus {
UniversityPage::UniversityPage(const UniversityRegistry &registry, const QString &currentSchoolId,
                               QWidget *parent)
    : QWidget(parent), registry_(registry) {
    setObjectName("universityPage");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(14);
    auto *heading = new QLabel("添加 / 切换大学");
    heading->setObjectName("heading");
    layout->addWidget(heading);
    auto *subtitle = new QLabel("输入大学官网首页，接入公开通知与学校资源");
    subtitle->setObjectName("subtitle");
    layout->addWidget(subtitle);
    auto *scope =
        new QLabel("输入学校官网首页地址或域名。已有社区配置优先加载；陌生大学可从公开的 .edu.cn "
                   "官网识别学校、发现栏目并生成本地草案。自动识别结果待社区核验，公开栏目覆盖会逐步完善。");
    scope->setObjectName("scope");
    scope->setWordWrap(true);
    scope->setTextFormat(Qt::PlainText);
    layout->addWidget(scope);

    auto *controls = new QHBoxLayout;
    auto *label = new QLabel("大学官网");
    controls->addWidget(label);
    url_ = new QLineEdit;
    url_->setObjectName("universityUrlInput");
    url_->setPlaceholderText("例如 https://www.neepu.edu.cn/ 或 www.neepu.edu.cn");
    url_->setClearButtonEnabled(true);
    url_->setMaxLength(2048);
    label->setBuddy(url_);
    controls->addWidget(url_, 1);
    load_ = new QPushButton("接入 / 切换大学");
    load_->setObjectName("loadUniversityButton");
    controls->addWidget(load_);
    layout->addLayout(controls);

    feedback_ = new QLabel("选择已有大学，或输入新大学的官网首页开始发现。");
    feedback_->setObjectName("universityFeedback");
    feedback_->setTextFormat(Qt::PlainText);
    feedback_->setWordWrap(true);
    feedback_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    feedback_->setAccessibleName("学校接入状态");
    feedback_->setAccessibleDescription(feedback_->text());
    layout->addWidget(feedback_);
    progress_ = new QProgressBar;
    progress_->setObjectName("universityProgress");
    progress_->setAccessibleName("学校接入进度");
    progress_->setRange(0, 100);
    progress_->setValue(0);
    progress_->setTextVisible(false);
    progress_->setFixedHeight(16);
    progress_->hide();
    layout->addWidget(progress_);
    auto *directoryLabel = new QLabel("社区配置与本地识别的大学");
    layout->addWidget(directoryLabel);
    directory_ = new QListWidget;
    directory_->setObjectName("universityDirectory");
    for (const auto &university : registry_.list()) {
        const QString homepage = university.homepage.toString();
        auto *item = new QListWidgetItem(university.name +
                                             (university.id == currentSchoolId ? "（当前）" : "") +
                                             (university.automaticallyIdentified ? " · 自动识别，待核验" : " · 社区配置") +
                                             "\n" + homepage,
                                         directory_);
        item->setData(Qt::UserRole, homepage);
        item->setToolTip(university.id);
        if (university.id == currentSchoolId) {
            url_->setText(homepage);
            directory_->setCurrentItem(item);
        }
    }
    layout->addWidget(directory_, 1);
    auto *note = new QLabel("选择目录中的条目会填入官网地址；点击“接入 / 切换大学”继续。"
                            "当前学校的通知缓存和本机来源偏好会保留。");
    note->setWordWrap(true);
    note->setTextFormat(Qt::PlainText);
    layout->addWidget(note);

    connect(directory_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) { url_->setText(item->data(Qt::UserRole).toString()); });
    connect(load_, &QPushButton::clicked, this, &UniversityPage::loadUniversity);
    connect(url_, &QLineEdit::returnPressed, this, &UniversityPage::loadUniversity);
}

void UniversityPage::setBusy(bool busy) {
    onboardingBusy_ = busy;
    applyBusy();
}
void UniversityPage::setRefreshing(bool refreshing) {
    refreshing_ = refreshing;
    applyBusy();
}
void UniversityPage::applyBusy() {
    const bool busy = onboardingBusy_ || refreshing_;
    const bool changed = busy_ != busy;
    busy_ = busy;
    url_->setEnabled(!busy);
    load_->setEnabled(!busy);
    directory_->setEnabled(!busy);
    load_->setText(busy ? "正在处理……" : "接入 / 切换大学");
    if (busy) {
        progress_->setRange(0, 0);
        progress_->show();
        if (changed)
            setFeedback("正在检查学校公开信息，请稍候……");
    } else {
        // Leave indeterminate mode as well as hiding the widget so the native
        // style's animation cannot continue after a run finishes or fails.
        progress_->setRange(0, 100);
        progress_->setValue(0);
        progress_->hide();
        progress_->setAccessibleDescription({});
        if (changed)
            setFeedback("可输入大学官网首页，或从当前社区目录选择大学。");
    }
}
void UniversityPage::setFeedback(const QString &message) {
    feedback_->setText(message);
    feedback_->setAccessibleDescription(message);
    if (busy_)
        progress_->setAccessibleDescription(message);
}

void UniversityPage::loadUniversity() {
    if (!load_->isEnabled())
        return;
    try {
        const auto university = registry_.resolve(url_->text());
        setFeedback("已匹配：" + university.name + "。正在加载该校配置……");
        emit universitySelected(university.configFile);
    } catch (const std::exception &error) {
        try {
            const auto home = UnknownUniversityDiscovery::normalizedHomepage(url_->text());
            setFeedback("正在检查新大学的公开官网与学校身份……");
            emit homepageDiscoveryRequested(home.toString());
        } catch (const std::exception &unknownError) {
            setFeedback(QString::fromUtf8(error.what()) + "\n" +
                        QString::fromUtf8(unknownError.what()));
        }
    }
}
} // namespace campus

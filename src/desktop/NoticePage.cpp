#include "desktop/NoticePage.h"
#include <QLineEdit>
#include <QComboBox>
#include <QTableView>
#include <QTextBrowser>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QResizeEvent>
#include <QScrollArea>
#include <QFontMetrics>
#include <QSplitter>
#include <QHeaderView>
#include <QDesktopServices>
#include <QDateTime>
#include <QSignalBlocker>
#include <QProgressBar>
#include <QShortcut>
#include <map>
#include <QDate>
#include "domain/Theme.h"

namespace campus {
NoticePage::NoticePage(const SchoolPackage &school, NoticeService &service,
                       RefreshCoordinator &coordinator, QWidget *parent)
    : QWidget(parent), service_(service), coordinator_(coordinator), model_(this), filter_(this) {
    setObjectName("noticePage");
    NoticeQuery rule;
    rule.schoolId = school.id.toStdString();
    filter_.setRule(rule);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setObjectName("noticeScroll");
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget;
    scroll->setWidget(content);
    outer->addWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(14);
    auto *heading = new QLabel("CampusPulse");
    heading->setObjectName("heading");
    auto *subtitle = new QLabel(school.name + " · 校园通知与办理事项");
    subtitle->setObjectName("subtitle");
    subtitle->setTextFormat(Qt::PlainText);
    subtitle->setWordWrap(true);
    layout->addWidget(heading);
    layout->addWidget(subtitle);
    QStringList sourceNames;
    for (const auto &source : school.sources)
        sourceNames << source.name;
    auto *scope = new QLabel((sourceNames.isEmpty() ? QString("当前学校尚无已接入来源。")
                                                    : "已接入：" + sourceNames.join("、") + "。") +
                             "年份按发布日期划分；列表页按各来源配置的页数上限读取。"
                             "办理期限请查看原文，来源更新和暂停状态可在“来源”页查看。");
    scope->setWordWrap(true);
    scope->setObjectName("scope");
    layout->addWidget(scope);
    controls_ = new QGridLayout;
    auto *controls = controls_;
    year_ = new QComboBox;
    year_->setObjectName("yearSelector");
    year_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    year_->setMinimumContentsLength(10);
    year_->setAccessibleName("通知发布年份");
    auto *search = new QLineEdit;
    search->setObjectName("keywordSearch");
    search->setPlaceholderText("搜索标题与主来源：重修、缴费、补考……（多个词用空格分隔）");
    search->setClearButtonEnabled(true);
    auto *category = new QComboBox;
    category->setObjectName("themeSelector");
    category->addItem("全部主题", "");
    for (const auto &theme : Themes)
        category->addItem(
            QString::fromUtf8(theme.label.data(), static_cast<int>(theme.label.size())),
            QString::fromUtf8(theme.key.data(), static_cast<int>(theme.key.size())));
    auto *reset = new QPushButton("清除筛选");
    reset->setObjectName("resetNoticeFiltersButton");
    reset->setToolTip("恢复今年、全部来源与主题，并清空关键词");
    refresh_ = new QPushButton("更新官网");
    refresh_->setObjectName("primary");
    search->setMinimumWidth(0);
    search->setToolTip(search->placeholderText());
    controls->addWidget(search, 0, 0, 1, 4);
    controls->addWidget(year_, 1, 0);
    controls->addWidget(category, 1, 1);
    controls->addWidget(reset, 1, 2);
    controls->addWidget(refresh_, 1, 3);
    controls->setColumnStretch(0, 1);
    layout->addLayout(controls);
    sourceControls_ = new QBoxLayout(QBoxLayout::LeftToRight);
    auto *sourceControls = sourceControls_;
    auto *sourceChoice = new QComboBox;
    sourceChoice->setObjectName("noticeSourceSelector");
    sourceChoice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    sourceChoice->setMinimumContentsLength(12);
    sourceChoice->addItem("全部来源", "");
    for (const auto &source : school.catalog)
        sourceChoice->addItem(QString::fromStdString(source.name),
                              QString::fromStdString(source.id));
    sourceControls->addWidget(new QLabel("信息来源"));
    sourceControls->addWidget(sourceChoice);
    auto *sourceHelp = new QLabel("跨站汇总 · 来源目录与更新状态见“来源”页");
    sourceHelp->setWordWrap(true);
    sourceControls->addWidget(sourceHelp);
    sourceControls->addStretch();
    auto *save = new QPushButton("保存为订阅");
    save->setObjectName("saveSubscriptionButton");
    sourceControls->addWidget(save);
    connect(save, &QPushButton::clicked, this,
            [this] { emit saveSubscriptionRequested(filter_.query()); });
    layout->addLayout(sourceControls);
    connect(sourceChoice, &QComboBox::currentIndexChanged, this, [this, sourceChoice](int) {
        filter_.setSource(sourceChoice->currentData().toString());
        updateCount();
    });
    auto *searchShortcut = new QShortcut(QKeySequence::Find, this);
    searchShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(searchShortcut, &QShortcut::activated, search, [search] {
        search->setFocus();
        search->selectAll();
    });
    emptyState_ = new QWidget;
    emptyState_->setObjectName("noticeEmptyState");
    auto *emptyLayout = new QHBoxLayout(emptyState_);
    emptyLayout->setContentsMargins(16, 12, 16, 12);
    emptyMessage_ = new QLabel;
    emptyMessage_->setObjectName("noticeEmptyMessage");
    emptyMessage_->setWordWrap(true);
    emptyLayout->addWidget(emptyMessage_, 1);
    allYears_ = new QPushButton("查看全部年份");
    allYears_->setObjectName("showAllNoticeYearsButton");
    emptyLayout->addWidget(allYears_);
    connect(allYears_, &QPushButton::clicked, this, [this] {
        year_->setCurrentIndex(year_->findData(0));
    });
    layout->addWidget(emptyState_);
    progress_ = new QProgressBar;
    progress_->setObjectName("noticeUpdateProgress");
    progress_->setAccessibleName("官网通知更新进度");
    progress_->setRange(0, 0);
    progress_->setTextVisible(false);
    progress_->setMaximumHeight(5);
    progress_->hide();
    layout->addWidget(progress_);
    splitter_ = new QSplitter;
    splitter_->setObjectName("noticeSplitter");
    splitter_->setChildrenCollapsible(false);
    auto *splitter = splitter_;
    table_ = new QTableView;
    table_->setObjectName("noticeTable");
    filter_.setSourceModel(&model_);
    table_->setModel(&filter_);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->hide();
    table_->setAlternatingRowColors(true);
    table_->setWordWrap(false);
    table_->setTextElideMode(Qt::ElideRight);
    table_->horizontalHeader()->setMinimumSectionSize(60);
    table_->setSortingEnabled(true);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->setColumnWidth(0, 108);
    table_->setColumnWidth(3, 128);
    table_->setColumnWidth(4, 115);
    table_->sortByColumn(0, Qt::DescendingOrder);
    auto *detail = new QWidget;
    auto *dl = new QVBoxLayout(detail);
    dl->setContentsMargins(18, 8, 0, 0);
    dl->setSpacing(12);
    title_ = new QLabel("选择一条通知查看详情");
    title_->setTextFormat(Qt::PlainText);
    title_->setWordWrap(true);
    title_->setObjectName("detailTitle");
    dl->addWidget(title_);
    openOriginal_ = new QPushButton("在浏览器打开官方原文");
    openOriginal_->setObjectName("openNoticeOriginalButton");
    auto *open = openOriginal_;
    dl->addWidget(open);
    detailActions_ = new QBoxLayout(QBoxLayout::LeftToRight);
    auto *detailActions = detailActions_;
    addTask_ = new QPushButton("加入我的待办");
    addTask_->setObjectName("addTaskButton");
    refreshOriginal_ = new QPushButton("刷新原文");
    refreshOriginal_->setObjectName("refreshOriginalButton");
    detailActions->addWidget(addTask_);
    detailActions->addWidget(refreshOriginal_);
    dl->addLayout(detailActions);
    connect(addTask_, &QPushButton::clicked, this, [this] {
        if (!selectedId_.isEmpty())
            emit addTaskRequested(selectedId_);
    });
    connect(refreshOriginal_, &QPushButton::clicked, this, [this] {
        if (selectedId_.isEmpty())
            return;
        try {
            status_->setText("正在刷新官方原文，保留已有正文与个人待办……");
            coordinator_.loadDetail(selected_);
        } catch (const std::exception &e) {
            status_->setText("原文刷新失败：" + QString::fromUtf8(e.what()));
        }
    });
    body_ = new QTextBrowser;
    body_->setObjectName("noticeBody");
    body_->setPlainText(
        "左侧为官网通知列表。正文按需读取并缓存在本地；附件保留官方链接，不下载或解析名单。");
    dl->addWidget(body_, 1);
    attachments_ = new QTextBrowser;
    attachments_->setObjectName("noticeAttachments");
    attachments_->setMaximumHeight(135);
    attachments_->setOpenExternalLinks(false);
    dl->addWidget(attachments_);
    splitter->addWidget(table_);
    splitter->addWidget(detail);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    splitter->setSizes({780, 490});
    layout->addWidget(splitter, 1);
    count_ = new QLabel;
    count_->setObjectName("noticeCount");
    count_->setWordWrap(true);
    status_ = new QLabel("本地数据就绪；点击更新官网获取最新列表");
    status_->setObjectName("noticeStatus");
    status_->setWordWrap(true);
    layout->addWidget(count_);
    layout->addWidget(status_);
    connect(year_, &QComboBox::currentIndexChanged, this, [this](int) {
        const int selected = year_->currentData().toInt();
        if (selected == QDate::currentDate().year())
            filter_.setCurrentYear();
        else
            filter_.setYear(selected);
        updateCount();
    });
    connect(search, &QLineEdit::textChanged, this, [this](const auto &q) {
        filter_.setQuery(q);
        updateCount();
    });
    connect(category, &QComboBox::currentIndexChanged, this, [this, category](int) {
        filter_.setThemeKey(category->currentData().toString());
        updateCount();
    });
    connect(reset, &QPushButton::clicked, this, [this, search, category, sourceChoice] {
        search->clear();
        category->setCurrentIndex(0);
        sourceChoice->setCurrentIndex(0);
        year_->setCurrentIndex(year_->findData(QDate::currentDate().year()));
    });
    connect(refresh_, &QPushButton::clicked, this, [this] {
        try {
            coordinator_.refresh();
        } catch (const std::exception &error) {
            refresh_->setEnabled(!coordinator_.busy());
            status_->setText("官网更新失败：" + QString::fromUtf8(error.what()));
        }
    });
    connect(&coordinator_, &RefreshCoordinator::started, this, [this] {
        progress_->show();
        refresh_->setEnabled(false);
        updateDetailActions();
        status_->setText("正在读取官网，每次请求间隔至少3秒……");
    });
    connect(&coordinator_, &RefreshCoordinator::changed, this, &NoticePage::reload);
    connect(&coordinator_, &RefreshCoordinator::message, status_, &QLabel::setText);
    connect(&coordinator_, &RefreshCoordinator::finished, this, [this](int good, int bad) {
        progress_->hide();
        refresh_->setEnabled(true);
        updateDetailActions();
        status_->setText(
            good == 0 && bad == 0
                ? "当前没有可更新的来源；可在“来源”页恢复已暂停来源，已有缓存仍可查看。"
                : QString("本轮完成：%1 个来源完整成功、%2 个来源未完整成功 · %3 · "
                          "各来源进度与错误请查看“来源”页")
                      .arg(good)
                      .arg(bad)
                      .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss")));
    });
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this] { selectedChanged(); });
    connect(&coordinator_, &RefreshCoordinator::detailFinished, this, [this](const QString &id) {
        if (id == selectedId_) {
            detailPending_ = false;
            updateDetailActions();
        }
        if (id != selectedId_)
            return;
        for (const auto &n : service_.list())
            if (QString::fromStdString(n.id) == id) {
                selected_ = n;
                renderDetail(n);
                break;
            }
    });
    connect(&coordinator_, &RefreshCoordinator::detailStarted, this, [this](const QString &id) {
        if (id == selectedId_) {
            detailPending_ = true;
            updateDetailActions();
        }
    });
    connect(&coordinator_, &RefreshCoordinator::detailFailed, this,
            [this](const QString &id, const QString &) {
                if (id == selectedId_) {
                    detailPending_ = false;
                    updateDetailActions();
                }
            });
    connect(open, &QPushButton::clicked, this, [this] {
        if (!selected_.url.empty())
            QDesktopServices::openUrl(QUrl(QString::fromStdString(selected_.url)));
    });
    connect(attachments_, &QTextBrowser::anchorClicked, this, [](const QUrl &url) {
        if (url.scheme() == "https" || url.scheme() == "http")
            QDesktopServices::openUrl(url);
    });
    connect(table_, &QTableView::activated, body_, [this](const QModelIndex &) {
        body_->setFocus(Qt::ShortcutFocusReason);
    });
    QWidget::setTabOrder(search, year_);
    QWidget::setTabOrder(year_, category);
    QWidget::setTabOrder(category, reset);
    QWidget::setTabOrder(reset, refresh_);
    QWidget::setTabOrder(refresh_, sourceChoice);
    QWidget::setTabOrder(sourceChoice, save);
    QWidget::setTabOrder(save, table_);
    QWidget::setTabOrder(table_, openOriginal_);
    QWidget::setTabOrder(openOriginal_, addTask_);
    QWidget::setTabOrder(addTask_, refreshOriginal_);
    QWidget::setTabOrder(refreshOriginal_, body_);
    QWidget::setTabOrder(body_, attachments_);
    updateLayout();
    updateDetailActions();
    reload();
}

void NoticePage::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateLayout();
}

void NoticePage::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::FontChange)
        updateLayout();
}

void NoticePage::updateLayout() {
    const int breakpoint = qMax(800, fontMetrics().horizontalAdvance(QString(72, QLatin1Char('M'))));
    const bool narrow = width() < breakpoint;
    // Move existing widgets, keeping their connections and keyboard traversal order.
    const char *names[] = {"keywordSearch", "yearSelector", "themeSelector",
                           "resetNoticeFiltersButton", "primary"};
    QWidget *widgets[5];
    for (int i = 0; i < 5; ++i) {
        widgets[i] = findChild<QWidget *>(QString::fromLatin1(names[i]));
        controls_->removeWidget(widgets[i]);
    }
    controls_->addWidget(widgets[0], 0, 0, 1, narrow ? 2 : 4);
    controls_->addWidget(widgets[1], 1, 0);
    controls_->addWidget(widgets[2], 1, 1);
    controls_->addWidget(widgets[3], narrow ? 2 : 1, narrow ? 0 : 2);
    controls_->addWidget(widgets[4], narrow ? 2 : 1, narrow ? 1 : 3);
    sourceControls_->setDirection(narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    detailActions_->setDirection(narrow ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    const auto orientation = narrow ? Qt::Vertical : Qt::Horizontal;
    if (splitter_->orientation() != orientation) {
        splitter_->setOrientation(orientation);
        splitter_->setSizes({3, 2});
    }
    table_->verticalHeader()->setDefaultSectionSize(fontMetrics().lineSpacing() + 18);
    table_->setMinimumHeight(fontMetrics().lineSpacing() * 6);
    body_->setMinimumHeight(fontMetrics().lineSpacing() * 8);
}

void NoticePage::reload() {
    try {
        const QString selectedId = selectedId_;
        const QSignalBlocker selectionBlock(table_->selectionModel());
        model_.setNotices(service_.list());
        const QSignalBlocker block(year_);
        const int selectedYear = filter_.year();
        std::map<int, int> counts;
        counts[QDate::currentDate().year()] = 0;
        int unknown = 0;
        for (int row = 0; row < model_.rowCount(); ++row) {
            const auto date =
                QDate::fromString(model_.index(row, 0).data().toString(), Qt::ISODate);
            if (date.isValid())
                ++counts[date.year()];
            else
                ++unknown;
        }
        year_->clear();
        for (auto it = counts.crbegin(); it != counts.crend(); ++it) {
            year_->addItem(QString("%1年%2 · %3条")
                               .arg(it->first)
                               .arg(it->first == QDate::currentDate().year() ? "（今年）" : "")
                               .arg(it->second),
                           it->first);
        }
        year_->addItem(QString("全部年份 · %1条").arg(model_.rowCount()), 0);
        year_->addItem(QString("日期待核实 · %1条").arg(unknown), -1);
        const int yearIndex = year_->findData(selectedYear);
        year_->setCurrentIndex(yearIndex < 0 ? year_->findData(QDate::currentDate().year())
                                             : yearIndex);
        const int selected = year_->currentData().toInt();
        if (selected == QDate::currentDate().year())
            filter_.setCurrentYear();
        else
            filter_.setYear(selected);
        updateCount();
        bool restored = false;
        for (int row = 0; row < model_.rowCount(); ++row) {
            const auto &notice = model_.notice(row);
            if (QString::fromStdString(notice.id) != selectedId)
                continue;
            const auto noticeIndex = filter_.mapFromSource(model_.index(row, 1));
            if (!noticeIndex.isValid())
                break;
            table_->setCurrentIndex(noticeIndex);
            selected_ = notice;
            selectedId_ = selectedId;
            renderDetail(selected_);
            restored = true;
            break;
        }
        if (!restored) {
            selected_ = {};
            selectedId_.clear();
            title_->setText("选择一条通知查看详情");
            body_->clear();
            attachments_->clear();
        }
        updateDetailActions();
    } catch (const std::exception &e) {
        status_->setText("本地读取失败：" + QString::fromUtf8(e.what()));
    }
}
void NoticePage::updateCount() {
    const bool empty = filter_.rowCount() == 0;
    emptyState_->setVisible(empty);
    allYears_->setVisible(model_.rowCount() > 0 && filter_.query().yearPolicy != YearPolicy::AllYears);
    emptyMessage_->setText(model_.rowCount() == 0
        ? "还没有保存通知。点击“更新官网”读取已接入栏目；来源未接入或访问失败时，请在“来源”页查看原因。"
        : "当前条件下没有匹配通知。可以查看全部年份，或使用“清除筛选”恢复今年与全部来源、主题。");
    count_->setText(
        QString("显示 %1 条 / 本地共 %2 条 · %3 · 关键词分类仅供筛选%4")
            .arg(filter_.rowCount())
            .arg(model_.rowCount())
            .arg(year_->currentText())
            .arg(filter_.rowCount() == 0 ? " · 此筛选下暂无已采集通知，可切换年份或清除筛选" : ""));
}
void NoticePage::selectedChanged() {
    const auto index = filter_.mapToSource(table_->currentIndex());
    if (!index.isValid()) {
        selected_ = {};
        selectedId_.clear();
        title_->setText("选择一条通知查看详情");
        body_->clear();
        attachments_->clear();
        detailPending_ = false;
        updateDetailActions();
        return;
    }
    selected_ = model_.notice(index.row());
    selectedId_ = QString::fromStdString(selected_.id);
    detailPending_ = false;
    updateDetailActions();
    renderDetail(selected_);
    if (!coordinator_.busy() && selected_.body.empty()) {
        status_->setText("读取官方正文……");
        coordinator_.loadDetail(selected_);
    }
}
void NoticePage::updateDetailActions() {
    openOriginal_->setEnabled(!selectedId_.isEmpty() && !selected_.url.empty());
    addTask_->setEnabled(!selectedId_.isEmpty());
    refreshOriginal_->setEnabled(!selectedId_.isEmpty() && !coordinator_.busy() && !detailPending_);
}
void NoticePage::renderDetail(const Notice &n) {
    title_->setText(QString::fromStdString(n.title));
    QStringList tags, stages;
    for (const auto &tag : n.tags)
        tags << categoryLabel(tag);
    for (const auto &stage : n.stages) {
        const auto label = stageLabel(stage);
        stages << QString::fromUtf8(label.data(), static_cast<int>(label.size()));
    }
    body_->setPlainText(
        "发布日期：" + QString::fromStdString(n.publishedDate) + "\n来源：" +
        QString::fromStdString(n.sourceName) + "\n主题提示：" + tags.join("、") + "\n阶段提示：" +
        stages.join("、") + "（标题规则，请核对原文）" + "\n发布日期不是办理截止时间。\n\n" +
        (n.body.empty() ? QString("正文尚未缓存。可打开官方原文；未暂停来源将在选中后按需读取。")
                        : QString::fromStdString(n.body)));
    QString links = "<b>官方附件</b><br>";
    for (const auto &a : n.attachments)
        links += "<p><a href=\"" + QString::fromStdString(a.url).toHtmlEscaped() + "\">" +
                 QString::fromStdString(a.name).toHtmlEscaped() + "</a></p>";
    if (n.attachments.empty())
        links += "未读取到附件链接";
    attachments_->setHtml(links);
}
bool NoticePage::selectContaining(const QString &keyword) {
    for (int row = 0; row < filter_.rowCount(); ++row) {
        if (filter_.index(row, 1).data().toString().contains(keyword)) {
            table_->setCurrentIndex(filter_.index(row, 1));
            table_->scrollTo(filter_.index(row, 1));
            return true;
        }
    }
    return false;
}
bool NoticePage::showNotice(const QString &id) {
    findChild<QLineEdit *>("keywordSearch")->clear();
    findChild<QComboBox *>("themeSelector")->setCurrentIndex(0);
    findChild<QComboBox *>("noticeSourceSelector")->setCurrentIndex(0);
    year_->setCurrentIndex(year_->findData(0));
    for (int row = 0; row < filter_.rowCount(); ++row) {
        const auto original = filter_.mapToSource(filter_.index(row, 1));
        if (QString::fromStdString(model_.notice(original.row()).id) == id) {
            table_->setCurrentIndex(filter_.index(row, 1));
            table_->scrollTo(filter_.index(row, 1));
            return true;
        }
    }
    return false;
}
} // namespace campus

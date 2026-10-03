#include "desktop/BrandTheme.h"
#include <QApplication>
#include <QCalendarWidget>
#include <QColor>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLabel>
#include <QLayout>
#include <QPalette>
#include <QResource>
#include <QStyleHints>
#include <QTextCharFormat>
#include <QWidget>
#include <algorithm>
#include <utility>

// The resource lives in a static library. This reference keeps its object linked in both
// the desktop executable and the Qt widget tests.
static void initializeCampusPulseResources() {
    static const bool initialized = [] {
        Q_INIT_RESOURCE(campuspulse);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace campus {
namespace {
struct Colors {
    QString canvas, surface, subtle, foreground, muted, border, accent, accentHover, selection,
        selectedText, informational, error, disabled;
};

bool darkAppearance() {
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown)
        return scheme == Qt::ColorScheme::Dark;
    return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

Colors colors() {
    if (darkAppearance())
        return {"#16202d", "#1d2a3a", "#253448", "#edf3fa", "#b6c7da", "#475a72", "#9dc9ff",
                "#bbdaff", "#354f70", "#ffffff", "#243a52", "#ffb7b2", "#a8b7c9"};
    return {"#f4f7fa", "#ffffff", "#eef3f8", "#1c2d40", "#536579", "#bfcbd8", "#225d9c",
            "#174875", "#dceafb", "#173d68", "#e8f0f8", "#a62735", "#617286"};
}

void setName(QWidget &window, const char *objectName, const QString &name) {
    if (auto *control = window.findChild<QWidget *>(QString::fromLatin1(objectName));
        control && control->accessibleName().isEmpty())
        control->setAccessibleName(name);
}

void applyApplicationColors(QApplication &application) {
    const auto c = colors();
    auto palette = application.palette();
    palette.setColor(QPalette::Window, QColor(c.canvas));
    palette.setColor(QPalette::WindowText, QColor(c.foreground));
    palette.setColor(QPalette::Base, QColor(c.surface));
    palette.setColor(QPalette::AlternateBase, QColor(c.subtle));
    palette.setColor(QPalette::Text, QColor(c.foreground));
    palette.setColor(QPalette::PlaceholderText, QColor(c.muted));
    palette.setColor(QPalette::Button, QColor(c.surface));
    palette.setColor(QPalette::ButtonText, QColor(c.foreground));
    palette.setColor(QPalette::Highlight, QColor(c.selection));
    palette.setColor(QPalette::HighlightedText, QColor(c.selectedText));
    palette.setColor(QPalette::Link, QColor(c.accent));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(c.disabled));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(c.disabled));
    application.setPalette(palette);
    application.setStyleSheet(BrandTheme::styleSheet());
}
} // namespace

QIcon BrandTheme::applicationIcon() {
    initializeCampusPulseResources();
    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256})
        icon.addFile(QString(":/campuspulse/icons/campuspulse-%1.png").arg(size),
                     QSize(size, size));
    return icon;
}

QIcon BrandTheme::navigationIcon(const QString &page) {
    initializeCampusPulseResources();
    QIcon icon;
    icon.addFile(":/campuspulse/navigation/" + page + "-24.png", QSize(24, 24));
    icon.addFile(":/campuspulse/navigation/" + page + "-48.png", QSize(48, 48));
    return icon;
}

QColor BrandTheme::statusColor(StatusTone tone) {
    const bool dark = darkAppearance();
    switch (tone) {
    case StatusTone::Success:
        return QColor(dark ? "#88dcb6" : "#207346");
    case StatusTone::Warning:
        return QColor(dark ? "#f4ce83" : "#8a5711");
    case StatusTone::Error:
        return QColor(colors().error);
    case StatusTone::Neutral:
        return QColor(colors().foreground);
    }
    return QColor(colors().foreground);
}

void BrandTheme::installApplication(QApplication &application) {
    application.setWindowIcon(applicationIcon());
    auto font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
    if (font.pointSizeF() > 0)
        font.setPointSizeF(std::max(font.pointSizeF(), 10.5));
    application.setFont(font);
    applyApplicationColors(application);
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, &application,
                     [&application] {
                         applyApplicationColors(application);
                         for (auto *window : QApplication::topLevelWidgets())
                             BrandTheme::applyWindow(*window);
                     });
}

QString BrandTheme::styleSheet() {
    const auto c = colors();
    const qreal base =
        QApplication::font().pointSizeF() > 0 ? QApplication::font().pointSizeF() : 10.5;
    QString sheet = QString::fromUtf8(R"QSS(
QMainWindow, QDialog { background: @canvas; }
QWidget { color: @foreground; }
QLabel#heading, QLabel[brandRole="heading"] {
    font-size: @headingpt; font-weight: 700; color: @foreground; padding-bottom: 2px;
}
QLabel#subtitle { font-size: @subtitlept; color: @muted; }
QLabel#detailTitle { font-size: @detailpt; font-weight: 600; }
QWidget#sourceLoginPanel { background: @informational; border: 1px solid @border; border-radius: 6px; }
QLabel[brandRole="accessHeading"] { font-size: @subtitlept; font-weight: 600; }
QLabel#scope { background: @informational; padding: 10px 12px; border-radius: 6px; color: @foreground; }
QLabel#noticeCount, QLabel#sourceSummary, QLabel#taskSummary, QLabel#subscriptionSummary {
    font-weight: 600; color: @muted;
}
QLabel#subscriptionError, QLabel#taskError { color: @error; }
QLineEdit, QComboBox, QAbstractSpinBox {
    background: @surface; border: 1px solid @border; border-radius: 5px; padding: 7px 9px;
    selection-background-color: @selection; selection-color: @selectedText;
}
QLineEdit:hover, QComboBox:hover, QAbstractSpinBox:hover { border-color: @muted; }
QLineEdit:focus, QComboBox:focus, QAbstractSpinBox:focus { border: 2px solid @accent; padding: 6px 8px; }
QLineEdit:disabled, QComboBox:disabled, QAbstractSpinBox:disabled {
    background: @subtle; color: @disabled;
}
QComboBox QAbstractItemView {
    background: @surface; border: 1px solid @border; selection-background-color: @selection;
    selection-color: @selectedText; padding: 3px;
}
QPushButton {
    background: @surface; color: @accent; border: 1px solid @border; border-radius: 5px;
    padding: 7px 12px;
}
QPushButton:hover { background: @subtle; border-color: @accent; }
QPushButton:pressed { background: @selection; }
QPushButton:focus { border: 2px solid @accent; padding: 6px 11px; }
QPushButton#primary, QPushButton#loadUniversityButton, QPushButton#saveTaskButton,
QPushButton#saveSubscriptionDialogButton, QPushButton#newSubscriptionButton,
QPushButton#exportCalendarButton {
    background: @accent; color: @surface; border-color: @accent; font-weight: 600;
}
QPushButton#primary:hover, QPushButton#loadUniversityButton:hover, QPushButton#saveTaskButton:hover,
QPushButton#saveSubscriptionDialogButton:hover, QPushButton#newSubscriptionButton:hover,
QPushButton#exportCalendarButton:hover { background: @accentHover; border-color: @accentHover; }
QPushButton:disabled {
    background: @subtle; color: @disabled; border: 1px solid @border; font-weight: 400;
}
QPushButton#primary:disabled, QPushButton#loadUniversityButton:disabled,
QPushButton#saveTaskButton:disabled, QPushButton#saveSubscriptionDialogButton:disabled,
QPushButton#newSubscriptionButton:disabled, QPushButton#exportCalendarButton:disabled {
    background: @subtle; color: @disabled; border-color: @border;
}
QTableView, QListView, QTextBrowser, QTextEdit {
    background: @surface; alternate-background-color: @subtle; border: 1px solid @border;
    border-radius: 5px; selection-background-color: @selection; selection-color: @selectedText;
}
QTextBrowser, QTextEdit { padding: 8px; }
QTableView { gridline-color: @border; }
QTableView::item { padding: 5px 7px; }
QTableView::item:selected, QListView::item:selected { background: @selection; color: @selectedText; }
QTableView:focus, QListView:focus, QTextBrowser:focus, QTextEdit:focus { border: 2px solid @accent; }
QListView::item { padding: 8px 10px; }
QListView::item:hover { background: @subtle; }
QListView::item:selected:hover { background: @selection; }
QHeaderView::section {
    background: @subtle; color: @foreground; padding: 8px 7px; border: 0;
    border-bottom: 1px solid @border; font-weight: 600;
}
QTabWidget::pane { border: 0; background: @canvas; }
QTabBar::tab {
    background: @subtle; color: @muted; border: 1px solid transparent;
    padding: 9px 15px; margin: 3px 2px 0 2px;
}
QTabBar::tab:selected { background: @surface; color: @accent; border-bottom: 2px solid @accent; font-weight: 600; }
QTabBar::tab:hover:!selected { background: @informational; color: @foreground; }
QTabBar::tab:focus { border: 1px solid @accent; }
QCheckBox { spacing: 7px; padding: 4px 0; }
QCheckBox:disabled { color: @disabled; }
QSplitter::handle { background: @canvas; }
QSplitter::handle:hover { background: @border; }
QScrollArea { border: 0; background: @canvas; }
QCalendarWidget QWidget { background: @surface; }
QCalendarWidget QAbstractItemView {
    background: @surface; color: @foreground; selection-background-color: @selection;
    selection-color: @selectedText; border: 1px solid @border;
}
QCalendarWidget QToolButton {
    color: @accent; background: @subtle; border: 1px solid transparent; padding: 5px 8px;
}
QCalendarWidget QToolButton:hover, QCalendarWidget QToolButton:focus { border-color: @accent; }
QLabel#resourceSummary { font-weight: 600; color: @muted; }
QToolTip { background: @surface; color: @foreground; border: 1px solid @border; padding: 6px; }
)QSS");
    const std::pair<const char *, QString> replacements[] = {
        {"@canvas", c.canvas},
        {"@surface", c.surface},
        {"@subtle", c.subtle},
        {"@foreground", c.foreground},
        {"@muted", c.muted},
        {"@border", c.border},
        {"@accentHover", c.accentHover},
        {"@accent", c.accent},
        {"@selection", c.selection},
        {"@selectedText", c.selectedText},
        {"@informational", c.informational},
        {"@error", c.error},
        {"@disabled", c.disabled},
        {"@headingpt", QString::number(base * 1.9, 'f', 1) + "pt"},
        {"@subtitlept", QString::number(base * 1.05, 'f', 1) + "pt"},
        {"@detailpt", QString::number(base * 1.25, 'f', 1) + "pt"}};
    for (const auto &[key, value] : replacements)
        sheet.replace(QString::fromLatin1(key), value);
    return sheet;
}

void BrandTheme::applyWindow(QWidget &window) {
    window.setWindowIcon(applicationIcon());
    const auto c = colors();
    for (auto *calendar : window.findChildren<QCalendarWidget *>())
        for (const auto day : {Qt::Saturday, Qt::Sunday}) {
            auto format = calendar->weekdayTextFormat(day);
            format.setForeground(QColor(c.error));
            calendar->setWeekdayTextFormat(day, format);
        }
    for (const auto *id : {"taskError", "subscriptionError"})
        if (auto *error = window.findChild<QLabel *>(QString::fromLatin1(id)))
            error->setStyleSheet("color:" + c.error + ";");
    if (auto *ai = window.findChild<QWidget *>("aiSourcesPage"); ai && ai->layout())
        if (auto *label = qobject_cast<QLabel *>(ai->layout()->itemAt(0)->widget()))
            label->setProperty("brandRole", "heading");
    setName(window, "keywordSearch", "搜索通知标题与来源");
    setName(window, "yearSelector", "通知发布年份");
    setName(window, "themeSelector", "通知主题");
    setName(window, "noticeSourceSelector", "通知信息来源");
    setName(window, "taskSearch", "搜索我的待办");
    setName(window, "taskStatusFilter", "待办办理状态");
    setName(window, "taskDateFilter", "待办日期范围");
    setName(window, "universityUrlInput", "大学官方网站地址");
    setName(window, "deepseekApiKey", "DeepSeek API Key，仅本次进程有效");
    setName(window, "deepseekModel", "DeepSeek 检索模型");
    setName(window, "noticeTable", "官方通知列表");
    setName(window, "sourceTable", "官网采集来源与更新状态");
    setName(window, "taskTable", "我的办理事项列表");
    setName(window, "calendarWidget", "办理事项日历");
    setName(window, "universityDirectory", "已验证的大学社区目录");
    setName(window, "resourceCategoryFilter", "学校资源类别");
    setName(window, "resourceStageFilter", "学校资源适用学习阶段");
    setName(window, "resourceSearch", "搜索学校资源名称、用途与提供方");
    setName(window, "resourceFavoritesOnly", "仅显示已收藏的学校资源");
    setName(window, "resourceTable", "学校资源列表");
    setName(window, "resourceDetail", "所选资源用途、访问要求与官方发现出处");
    setName(window, "discoverResourcesButton", "在后台发现学校官网中的实用资源");
    setName(window, "cancelResourceDiscoveryButton", "取消当前资源发现并保留已缓存结果");
    setName(window, "openResourceButton", "在系统浏览器打开所选学校资源");
    setName(window, "favoriteResourceButton", "收藏或取消收藏所选学校资源");
    window.setStyleSheet(styleSheet());
}
} // namespace campus

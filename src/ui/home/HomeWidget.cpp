// HomeWidget — Home workspace start screen implementation

#include "HomeWidget.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QDateTime>
#include <QFrame>
#include <QScrollArea>

HomeWidget::HomeWidget(ApplicationContext* ctx, QWidget* parent)
    : QWidget(parent), m_ctx(ctx) {
    setupUi();
    refreshRecent();

    connect(&m_ctx->projects(), &ProjectManager::recentChanged,
            this, &HomeWidget::onRecentChanged);
    connect(&m_ctx->projects(), &ProjectManager::projectOpened,
            this, [this](const Project&) { refreshRecent(); });
}

void HomeWidget::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(40, 30, 40, 30);
    mainLayout->setSpacing(20);

    // Welcome header
    m_welcomeLabel = new QLabel("OpenGeoStudio");
    m_welcomeLabel->setStyleSheet("font-size: 28px; font-weight: bold; color: #d0d0d0;");
    mainLayout->addWidget(m_welcomeLabel);

    auto* subtitle = new QLabel("Native C++/Qt 6 desktop application for terrain and road design");
    subtitle->setStyleSheet("font-size: 14px; color: #888;");
    mainLayout->addWidget(subtitle);

    // Template cards
    auto* templatesGroup = new QGroupBox("Create New Project");
    templatesGroup->setStyleSheet("QGroupBox { font-size: 16px; font-weight: bold; color: #d0d0d0; border: 1px solid #444; border-radius: 6px; margin-top: 12px; padding-top: 16px; }");
    auto* tmplLayout = new QHBoxLayout(templatesGroup);
    tmplLayout->setSpacing(16);

    // Terrain template
    auto* terrainCard = new QPushButton("Terrain\n\nMap area selection\nDEM/heightmaps\nSatellite imagery");
    terrainCard->setFixedSize(220, 140);
    terrainCard->setStyleSheet(
        "QPushButton { text-align: left; padding: 16px; font-size: 13px; "
        "background-color: #2d5a3d; color: white; border: 1px solid #3a7a52; border-radius: 8px; }"
        "QPushButton:hover { background-color: #3a7a52; }");
    connect(terrainCard, &QPushButton::clicked, this, &HomeWidget::onCreateTerrain);
    tmplLayout->addWidget(terrainCard);

    // Road Studio template
    auto* roadCard = new QPushButton("Road Studio\n\nRoad network design\nBezier pen tool\nC++ geometry engine");
    roadCard->setFixedSize(220, 140);
    roadCard->setStyleSheet(
        "QPushButton { text-align: left; padding: 16px; font-size: 13px; "
        "background-color: #2d4a5a; color: white; border: 1px solid #3a6a82; border-radius: 8px; }"
        "QPushButton:hover { background-color: #3a6a82; }");
    connect(roadCard, &QPushButton::clicked, this, &HomeWidget::onCreateRoadStudio);
    tmplLayout->addWidget(roadCard);

    tmplLayout->addStretch();
    mainLayout->addWidget(templatesGroup);

    // Quick actions
    auto* actionsLayout = new QHBoxLayout();
    auto* openBtn = new QPushButton("Open Project...");
    openBtn->setStyleSheet("QPushButton { padding: 8px 20px; font-size: 13px; }");
    connect(openBtn, &QPushButton::clicked, this, &HomeWidget::onOpenFile);
    actionsLayout->addWidget(openBtn);
    actionsLayout->addStretch();
    mainLayout->addLayout(actionsLayout);

    // Recent projects
    auto* recentGroup = new QGroupBox("Recent Projects");
    recentGroup->setStyleSheet("QGroupBox { font-size: 16px; font-weight: bold; color: #d0d0d0; border: 1px solid #444; border-radius: 6px; margin-top: 12px; padding-top: 16px; }");
    auto* recentLayout = new QVBoxLayout(recentGroup);

    m_search = new QLineEdit();
    m_search->setPlaceholderText("Search recent projects...");
    m_search->setStyleSheet("QLineEdit { padding: 6px; }");
    connect(m_search, &QLineEdit::textChanged, this, &HomeWidget::onSearchChanged);
    recentLayout->addWidget(m_search);

    m_recentList = new QListWidget();
    m_recentList->setStyleSheet(
        "QListWidget { background-color: #232323; border: 1px solid #444; border-radius: 4px; }"
        "QListWidget::item { padding: 8px; }"
        "QListWidget::item:hover { background-color: #2a2a2a; }"
        "QListWidget::item:selected { background-color: #2a82da; }");
    connect(m_recentList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        onRecentItemClicked(m_recentList->currentRow());
    });
    recentLayout->addWidget(m_recentList);

    mainLayout->addWidget(recentGroup, 1);
}

void HomeWidget::refreshRecent(const QString& filter) {
    m_recentList->clear();

    const auto& recent = m_ctx->projects().recent();
    for (const auto& entry : recent) {
        if (!filter.isEmpty() && !entry.name.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }

        QString displayText = entry.name;
        if (entry.pinned) displayText = "★ " + displayText;
        displayText += "\n  " + entry.filePath;

        if (!entry.modifiedAt.isEmpty()) {
            QDateTime dt = QDateTime::fromString(entry.modifiedAt, Qt::ISODate);
            if (dt.isValid()) {
                displayText += "  |  " + dt.toString("yyyy-MM-dd hh:mm");
            }
        }

        auto* item = new QListWidgetItem(displayText, m_recentList);
        item->setData(Qt::UserRole, entry.filePath);
        m_recentList->addItem(item);
    }

    if (m_recentList->count() == 0) {
        auto* item = new QListWidgetItem("No recent projects. Create a new one above.");
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
        item->setForeground(QColor("#666"));
        m_recentList->addItem(item);
    }
}

void HomeWidget::onCreateTerrain() {
    emit newProjectRequested("terrain");
}

void HomeWidget::onCreateRoadStudio() {
    emit newProjectRequested("road-studio");
}

void HomeWidget::onOpenFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open Project", {}, "OpenGeoStudio Projects (*.ogproj)");
    if (!path.isEmpty()) {
        emit openProjectRequested(path);
    }
}

void HomeWidget::onRecentItemClicked(int row) {
    if (row < 0 || row >= m_recentList->count()) return;
    QListWidgetItem* item = m_recentList->item(row);
    const QString filePath = item->data(Qt::UserRole).toString();
    if (!filePath.isEmpty()) {
        emit openProjectRequested(filePath);
    }
}

void HomeWidget::onSearchChanged(const QString& text) {
    refreshRecent(text);
}

void HomeWidget::onRecentChanged() {
    refreshRecent(m_search->text());
}

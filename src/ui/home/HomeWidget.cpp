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
#include <QToolButton>
#include <QListWidgetItem>
#include <QMessageBox>

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
    m_welcomeLabel->setStyleSheet("font-size: 28px; font-weight: bold; color: #e6edf3;");
    mainLayout->addWidget(m_welcomeLabel);

    auto* subtitle = new QLabel("Native C++/Qt 6 desktop application for terrain and road design");
    subtitle->setStyleSheet("font-size: 14px; color: #7d8590;");
    mainLayout->addWidget(subtitle);

    // Template cards
    auto* templatesGroup = new QGroupBox("Create New Project");
    auto* tmplLayout = new QHBoxLayout(templatesGroup);
    tmplLayout->setSpacing(16);

    // Terrain template — green accent like reference
    auto* terrainCard = new QPushButton("Terrain\n\nMap area selection\nDEM / heightmaps\nSatellite imagery");
    terrainCard->setFixedSize(220, 140);
    terrainCard->setStyleSheet(
        "QPushButton { text-align: left; padding: 16px; font-size: 13px; "
        "background-color: #161b22; color: #e6edf3; border: 2px solid #30363d; border-radius: 8px; }"
        "QPushButton:hover { background-color: #1c2128; border: 2px solid #3fb950; }");
    connect(terrainCard, &QPushButton::clicked, this, &HomeWidget::onCreateTerrain);
    tmplLayout->addWidget(terrainCard);

    // Road Studio template — cyan accent like reference
    auto* roadCard = new QPushButton("Road Studio\n\nRoad network design\nLaneMaker integration\nC++ geometry engine");
    roadCard->setFixedSize(220, 140);
    roadCard->setStyleSheet(
        "QPushButton { text-align: left; padding: 16px; font-size: 13px; "
        "background-color: #161b22; color: #e6edf3; border: 2px solid #30363d; border-radius: 8px; }"
        "QPushButton:hover { background-color: #1c2128; border: 2px solid #06b6d4; }");
    connect(roadCard, &QPushButton::clicked, this, &HomeWidget::onCreateRoadStudio);
    tmplLayout->addWidget(roadCard);

    // Train Studio template — cyan accent
    auto* trainCard = new QPushButton("Train Studio\n\nRailway design\nOSM import\nTrack editing");
    trainCard->setFixedSize(220, 140);
    trainCard->setStyleSheet(
        "QPushButton { text-align: left; padding: 16px; font-size: 13px; "
        "background-color: #161b22; color: #e6edf3; border: 2px solid #30363d; border-radius: 8px; }"
        "QPushButton:hover { background-color: #1c2128; border: 2px solid #06b6d4; }");
    connect(trainCard, &QPushButton::clicked, this, [this]() { emit newProjectRequested("train-studio"); });
    tmplLayout->addWidget(trainCard);

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
    auto* recentLayout = new QVBoxLayout(recentGroup);

    m_search = new QLineEdit();
    m_search->setPlaceholderText("Search recent projects...");
    connect(m_search, &QLineEdit::textChanged, this, &HomeWidget::onSearchChanged);
    recentLayout->addWidget(m_search);

    m_recentList = new QListWidget();
    connect(m_recentList, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        onRecentItemClicked(m_recentList->currentRow());
    });
    recentLayout->addWidget(m_recentList);

    mainLayout->addWidget(recentGroup, 1);

    // System status bar (matching reference app)
    auto* statusLayout = new QHBoxLayout();
    auto* wsCount = new QLabel("Workspaces: 4");
    wsCount->setStyleSheet("color: #7d8590; font-size: 12px;");
    statusLayout->addWidget(wsCount);

    auto* sep1 = new QLabel("·");
    sep1->setStyleSheet("color: #484f58;");
    statusLayout->addWidget(sep1);

    auto* engineLabel = new QLabel("C++ Engine: Native");
    engineLabel->setStyleSheet("color: #3fb950; font-size: 12px;");
    statusLayout->addWidget(engineLabel);

    auto* sep2 = new QLabel("·");
    sep2->setStyleSheet("color: #484f58;");
    statusLayout->addWidget(sep2);

    auto* versionLabel = new QLabel("v0.1.0");
    versionLabel->setStyleSheet("color: #7d8590; font-size: 12px;");
    statusLayout->addWidget(versionLabel);

    statusLayout->addStretch();
    mainLayout->addLayout(statusLayout);
}

void HomeWidget::refreshRecent(const QString& filter) {
    m_recentList->clear();

    const auto& recent = m_ctx->projects().recent();

    // Sort: pinned first, then by modified date
    auto sortedRecent = recent;
    std::sort(sortedRecent.begin(), sortedRecent.end(),
        [](const auto& a, const auto& b) {
            if (a.pinned != b.pinned) return a.pinned;
            return a.modifiedAt > b.modifiedAt;
        });

    for (const auto& entry : sortedRecent) {
        if (!filter.isEmpty() && !entry.name.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }

        // Create a custom widget for each row with pin button
        auto* rowWidget = new QWidget();
        auto* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(8, 6, 8, 6);
        rowLayout->setSpacing(8);

        // Pin/unpin button
        auto* pinBtn = new QToolButton(rowWidget);
        pinBtn->setText(entry.pinned ? "★" : "☆");
        pinBtn->setFixedSize(24, 24);
        pinBtn->setStyleSheet(
            "QToolButton { border: none; font-size: 16px; color: #d29922; }"
            "QToolButton:hover { color: #06b6d4; }");
        connect(pinBtn, &QToolButton::clicked, this, [this, filePath = entry.filePath]() {
            m_ctx->projects().togglePin(filePath);
        });
        rowLayout->addWidget(pinBtn);

        // Project info
        QString infoText = entry.name;
        infoText += "\n  " + entry.filePath;
        if (!entry.modifiedAt.isEmpty()) {
            QDateTime dt = QDateTime::fromString(entry.modifiedAt, Qt::ISODate);
            if (dt.isValid()) {
                infoText += "  |  " + dt.toString("yyyy-MM-dd hh:mm");
            }
        }
        auto* infoLabel = new QLabel(infoText, rowWidget);
        infoLabel->setStyleSheet("color: #e6edf3; font-size: 13px;");
        rowLayout->addWidget(infoLabel, 1);

        // Remove button (delete project from disk + recent list)
        auto* removeBtn = new QToolButton(rowWidget);
        removeBtn->setText("✕");
        removeBtn->setFixedSize(24, 24);
        removeBtn->setToolTip("Remove this project (deletes from disk)");
        removeBtn->setStyleSheet(
            "QToolButton { border: none; font-size: 14px; color: #f85149; }"
            "QToolButton:hover { color: #ff6b6b; background: #21262d; border-radius: 4px; }");
        connect(removeBtn, &QToolButton::clicked, this, [this, filePath = entry.filePath, name = entry.name]() {
            auto ret = QMessageBox::question(this, "Delete Project",
                QString("Delete project \"%1\"?\n\n%2\n\nThis will permanently delete the project folder and all its contents.")
                    .arg(name, filePath),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ret == QMessageBox::Yes) {
                m_ctx->projects().deleteProject(filePath, true);
            }
        });
        rowLayout->addWidget(removeBtn);

        auto* item = new QListWidgetItem(m_recentList);
        item->setData(Qt::UserRole, entry.filePath);
        item->setSizeHint(rowWidget->sizeHint());
        m_recentList->addItem(item);
        m_recentList->setItemWidget(item, rowWidget);
    }

    if (m_recentList->count() == 0) {
        auto* item = new QListWidgetItem("No recent projects. Create a new one above.");
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
        item->setForeground(QColor("#484f58"));
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

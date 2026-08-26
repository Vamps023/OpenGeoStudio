// HomeWidget — Home workspace start screen implementation

#include "HomeWidget.hpp"

#include "../../theme/Theme.hpp"

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
    namespace th = ogs::theme;
    auto* heroRow = new QHBoxLayout();
    m_welcomeLabel = new QLabel("OpenGeoStudio");
    m_welcomeLabel->setStyleSheet(
        QStringLiteral("font-size: 32px; font-weight: 600; color: %1;").arg(th::c::Text));
    heroRow->addWidget(m_welcomeLabel);

    // Version pill badge
    auto* versionBadge = new QLabel("v" OGS_VERSION);
    versionBadge->setStyleSheet(
        QStringLiteral("background: %1; color: %2; border-radius: 9px;"
                       " padding: 3px 10px; font-size: 11px; font-weight: bold;")
            .arg(th::c::BgOverlay, th::c::Accent));
    heroRow->addWidget(versionBadge);
    heroRow->addStretch();
    mainLayout->addLayout(heroRow);

    auto* subtitle = new QLabel("Native C++/Qt 6 desktop application for terrain and road design");
    subtitle->setStyleSheet(
        QStringLiteral("font-size: 14px; color: %1; margin-top: -12px;").arg(th::c::TextMuted));
    mainLayout->addWidget(subtitle);

    // Template cards
    auto* templatesGroup = new QGroupBox("Create New Project");
    auto* tmplLayout = new QHBoxLayout(templatesGroup);
    tmplLayout->setSpacing(16);

    // Template cards
    const QString cardStyle = QStringLiteral(
        "QPushButton { text-align: left; padding: 18px; font-size: 13px; "
        "background-color: %1; color: %2; border: 1px solid %3; border-radius: 10px; }"
        "QPushButton:hover { background-color: %4; border: 1px solid %5; color: %2; }"
        "QPushButton:pressed { background-color: %1; }")
            .arg(th::c::BgSurface, th::c::Text, th::c::Border,
                 th::c::BgActive, th::c::Accent);

    // Terrain template
    auto* terrainCard = new QPushButton(
        "🗺  Terrain\n\nMap area selection\nDEM / heightmaps\nSatellite imagery");
    terrainCard->setFixedSize(240, 160);
    terrainCard->setStyleSheet(cardStyle);
    connect(terrainCard, &QPushButton::clicked, this, &HomeWidget::onCreateTerrain);
    tmplLayout->addWidget(terrainCard);

    // Road Studio template
    auto* roadCard = new QPushButton(
        "🛣  Road Studio\n\nRoad network design\nLaneMaker integration\nC++ geometry engine");
    roadCard->setFixedSize(240, 160);
    roadCard->setStyleSheet(cardStyle);
    connect(roadCard, &QPushButton::clicked, this, &HomeWidget::onCreateRoadStudio);
    tmplLayout->addWidget(roadCard);

    // Train Studio template
    auto* trainCard = new QPushButton(
        "🚆  Train Studio\n\nRailway design\nOSM import\nTrack editing");
    trainCard->setFixedSize(240, 160);
    trainCard->setStyleSheet(cardStyle);
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
    wsCount->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(th::c::TextMuted));
    statusLayout->addWidget(wsCount);

    auto* sep1 = new QLabel("·");
    sep1->setStyleSheet(QStringLiteral("color: %1;").arg(th::c::TextFaint));
    statusLayout->addWidget(sep1);

    auto* engineLabel = new QLabel("C++ Engine: Native");
    engineLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(th::c::Success));
    statusLayout->addWidget(engineLabel);

    auto* sep2 = new QLabel("·");
    sep2->setStyleSheet(QStringLiteral("color: %1;").arg(th::c::TextFaint));
    statusLayout->addWidget(sep2);

    auto* versionLabel = new QLabel("v" OGS_VERSION);
    versionLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(th::c::TextMuted));
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
            QStringLiteral("QToolButton { border: none; font-size: 16px; color: %1; }"
            "QToolButton:hover { color: %2; }")
                .arg(ogs::theme::c::Warning, ogs::theme::c::Accent));

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
        infoLabel->setStyleSheet(
            QStringLiteral("color: %1; font-size: 13px;").arg(ogs::theme::c::Text));
        rowLayout->addWidget(infoLabel, 1);

        // Remove button (delete project from disk + recent list)
        auto* removeBtn = new QToolButton(rowWidget);
        removeBtn->setText("✕");
        removeBtn->setFixedSize(24, 24);
        removeBtn->setToolTip("Remove this project (deletes from disk)");
        removeBtn->setStyleSheet(
            QStringLiteral("QToolButton { border: none; font-size: 14px; color: %1; }"
            "QToolButton:hover { color: %1; background: %2; border-radius: 4px; }")
                .arg(ogs::theme::c::Danger, ogs::theme::c::BgOverlay));

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
        item->setForeground(ogs::theme::c::qWarn());
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

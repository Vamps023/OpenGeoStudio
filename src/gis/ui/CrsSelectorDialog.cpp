// ============================================================
// CrsSelectorDialog.cpp — PROJ-backed CRS selector dialog
// ============================================================

#include "CrsSelectorDialog.hpp"
#include "CRSManager.hpp"
#include "CRSSearch.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>

namespace gis {

CrsSelectorDialog::CrsSelectorDialog(QWidget* parent)
    : QDialog(parent) {
    setupUI();
    populateAuthorityCombo();
    populateKindCombo();
    updateFavoritesList();
    updateRecentList();
}

CrsSelectorDialog::~CrsSelectorDialog() = default;

// ============================================================
// UI setup
// ============================================================
void CrsSelectorDialog::setupUI() {
    setWindowTitle("Coordinate Reference System Selector");
    setMinimumSize(800, 600);

    auto* mainLayout = new QVBoxLayout(this);

    // Tab widget
    m_tabWidget = new QTabWidget(this);
    mainLayout->addWidget(m_tabWidget);

    // ---- Search Tab ----
    auto* searchTab = new QWidget;
    auto* searchLayout = new QVBoxLayout(searchTab);

    // Search bar
    auto* searchRow = new QHBoxLayout;
    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText("Search CRS: EPSG code, name, or authority:code (e.g. 32643, UTM 43N, EPSG:4326)");
    m_searchButton = new QPushButton("Search");
    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(m_searchButton);
    searchLayout->addLayout(searchRow);

    // Filters
    auto* filterRow = new QHBoxLayout;
    filterRow->addWidget(new QLabel("Authority:"));
    m_authorityCombo = new QComboBox;
    m_authorityCombo->addItem("All", "");
    filterRow->addWidget(m_authorityCombo);

    filterRow->addWidget(new QLabel("Type:"));
    m_kindCombo = new QComboBox;
    m_kindCombo->addItem("All", static_cast<int>(CRSKind::Unknown));
    m_kindCombo->addItem("Geographic", static_cast<int>(CRSKind::Geographic));
    m_kindCombo->addItem("Projected", static_cast<int>(CRSKind::Projected));
    m_kindCombo->addItem("Geocentric", static_cast<int>(CRSKind::Geocentric));
    m_kindCombo->addItem("Vertical", static_cast<int>(CRSKind::Vertical));
    m_kindCombo->addItem("Compound", static_cast<int>(CRSKind::Compound));
    m_kindCombo->addItem("Engineering", static_cast<int>(CRSKind::Engineering));
    filterRow->addWidget(m_kindCombo);

    filterRow->addWidget(new QLabel("Area:"));
    m_areaEdit = new QLineEdit;
    m_areaEdit->setPlaceholderText("e.g. India, Europe, World");
    filterRow->addWidget(m_areaEdit);
    searchLayout->addLayout(filterRow);

    // Splitter: results | details
    auto* splitter = new QSplitter(Qt::Horizontal);

    // Results list
    m_resultsList = new QListWidget;
    m_resultsList->setMinimumWidth(300);
    splitter->addWidget(m_resultsList);

    // Details panel
    auto* detailsWidget = new QWidget;
    auto* detailsLayout = new QVBoxLayout(detailsWidget);

    m_nameLabel = new QLabel("—");
    m_nameLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    detailsLayout->addWidget(m_nameLabel);

    m_authIdLabel = new QLabel("—");
    detailsLayout->addWidget(m_authIdLabel);

    m_kindLabel = new QLabel("—");
    detailsLayout->addWidget(m_kindLabel);

    m_unitLabel = new QLabel("—");
    detailsLayout->addWidget(m_unitLabel);

    m_areaLabel = new QLabel("—");
    m_areaLabel->setWordWrap(true);
    detailsLayout->addWidget(m_areaLabel);

    m_datumLabel = new QLabel("—");
    detailsLayout->addWidget(m_datumLabel);

    // WKT / PROJJSON tabs
    auto* detailsTabs = new QTabWidget;
    m_wktEdit = new QTextEdit;
    m_wktEdit->setReadOnly(true);
    m_wktEdit->setFont(QFont("Consolas", 9));
    detailsTabs->addTab(m_wktEdit, "WKT2");

    m_projJsonEdit = new QTextEdit;
    m_projJsonEdit->setReadOnly(true);
    m_projJsonEdit->setFont(QFont("Consolas", 9));
    detailsTabs->addTab(m_projJsonEdit, "PROJJSON");

    detailsLayout->addWidget(detailsTabs, 1);
    splitter->addWidget(detailsWidget);

    splitter->setSizes({350, 450});
    searchLayout->addWidget(splitter, 1);

    m_tabWidget->addTab(searchTab, "Search");

    // ---- Favorites Tab ----
    auto* favTab = new QWidget;
    auto* favLayout = new QVBoxLayout(favTab);
    m_favoritesList = new QListWidget;
    favLayout->addWidget(m_favoritesList, 1);

    auto* favButtonRow = new QHBoxLayout;
    m_addFavoriteButton = new QPushButton("Add Current to Favorites");
    m_removeFavoriteButton = new QPushButton("Remove Selected");
    favButtonRow->addWidget(m_addFavoriteButton);
    favButtonRow->addWidget(m_removeFavoriteButton);
    favButtonRow->addStretch();
    favLayout->addLayout(favButtonRow);

    m_tabWidget->addTab(favTab, "Favorites");

    // ---- Recent Tab ----
    auto* recentTab = new QWidget;
    auto* recentLayout = new QVBoxLayout(recentTab);
    m_recentList = new QListWidget;
    recentLayout->addWidget(m_recentList, 1);
    m_tabWidget->addTab(recentTab, "Recent");

    // ---- Custom CRS Tab ----
    auto* customTab = new QWidget;
    auto* customLayout = new QVBoxLayout(customTab);

    customLayout->addWidget(new QLabel(
        "Enter a WKT2 or PROJJSON string to define a custom CRS:"));

    m_customEdit = new QTextEdit;
    m_customEdit->setFont(QFont("Consolas", 9));
    m_customEdit->setPlaceholderText("Paste WKT2 or PROJJSON here...");
    customLayout->addWidget(m_customEdit, 1);

    auto* customButtonRow = new QHBoxLayout;
    m_applyCustomButton = new QPushButton("Validate & Preview");
    customButtonRow->addWidget(m_applyCustomButton);
    customButtonRow->addStretch();
    customLayout->addLayout(customButtonRow);

    m_customErrorLabel = new QLabel;
    m_customErrorLabel->setWordWrap(true);
    customLayout->addWidget(m_customErrorLabel);

    m_tabWidget->addTab(customTab, "Custom CRS");

    // ---- Dialog buttons ----
    auto* buttonRow = new QHBoxLayout;
    buttonRow->addStretch();
    m_okButton = new QPushButton("Select CRS");
    m_cancelButton = new QPushButton("Cancel");
    buttonRow->addWidget(m_okButton);
    buttonRow->addWidget(m_cancelButton);
    mainLayout->addLayout(buttonRow);

    // ---- Connections ----
    connect(m_searchButton, &QPushButton::clicked, this, &CrsSelectorDialog::onSearch);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &CrsSelectorDialog::onSearch);
    connect(m_resultsList, &QListWidget::currentRowChanged,
            this, &CrsSelectorDialog::onResultSelected);
    connect(m_resultsList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) { onAccept(); });
    connect(m_authorityCombo, &QComboBox::currentIndexChanged,
            this, &CrsSelectorDialog::onAuthorityFilterChanged);
    connect(m_kindCombo, &QComboBox::currentIndexChanged,
            this, &CrsSelectorDialog::onKindFilterChanged);
    connect(m_tabWidget, &QTabWidget::currentChanged,
            this, &CrsSelectorDialog::onTabChanged);
    connect(m_applyCustomButton, &QPushButton::clicked,
            this, &CrsSelectorDialog::onApplyCustom);
    connect(m_addFavoriteButton, &QPushButton::clicked,
            this, &CrsSelectorDialog::onAddFavorite);
    connect(m_removeFavoriteButton, &QPushButton::clicked,
            this, &CrsSelectorDialog::onRemoveFavorite);
    connect(m_favoritesList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) { onFavoriteSelected(); });
    connect(m_favoritesList, &QListWidget::itemSelectionChanged,
            this, &CrsSelectorDialog::onFavoriteSelected);
    connect(m_recentList, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) { onRecentSelected(); });
    connect(m_recentList, &QListWidget::itemSelectionChanged,
            this, &CrsSelectorDialog::onRecentSelected);
    connect(m_okButton, &QPushButton::clicked, this, &CrsSelectorDialog::onAccept);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);

    // Debounced search
    auto* searchTimer = new QTimer(this);
    searchTimer->setSingleShot(true);
    connect(m_searchEdit, &QLineEdit::textEdited,
            [searchTimer]() { searchTimer->start(300); });
    connect(searchTimer, &QTimer::timeout, this, &CrsSelectorDialog::onSearch);
}

// ============================================================
// Populate filter combos
// ============================================================
void CrsSelectorDialog::populateAuthorityCombo() {
    auto authorities = CRSManager::instance().knownAuthorities();
    for (const auto& a : authorities) {
        m_authorityCombo->addItem(QString::fromStdString(a),
                                   QString::fromStdString(a));
    }
}

void CrsSelectorDialog::populateKindCombo() {
    // Already populated in setupUI
}

// ============================================================
// Search
// ============================================================
void CrsSelectorDialog::onSearch() {
    performSearch();
}

void CrsSelectorDialog::performSearch() {
    m_resultsList->clear();

    std::string query = m_searchEdit->text().trimmed().toStdString();
    std::string authority = m_authorityCombo->currentData().toString().toStdString();
    CRSKind kind = static_cast<CRSKind>(m_kindCombo->currentData().toInt());
    std::string area = m_areaEdit->text().trimmed().toStdString();

    auto results = CRSSearch::search(query, authority, kind, area, 100);

    for (const auto& r : results) {
        const auto& crs = r.crs;
        QString displayText = QString("%1 — %2\n  %3 • %4")
            .arg(QString::fromStdString(crs.authId()))
            .arg(QString::fromStdString(crs.name))
            .arg(QString::fromStdString(CRSDefinition::kindToString(crs.kind)))
            .arg(QString::fromStdString(CRSDefinition::unitToString(crs.unit)));

        if (!crs.areaOfUseName.empty()) {
            displayText += QString(" • %1")
                .arg(QString::fromStdString(crs.areaOfUseName));
        }

        auto* item = new QListWidgetItem(displayText);
        item->setData(Qt::UserRole, QVariant::fromValue(
            QString::fromStdString(crs.authId())));
        m_resultsList->addItem(item);
    }

    if (results.empty() && !query.empty()) {
        m_resultsList->addItem("No results found");
    }
}

void CrsSelectorDialog::onResultSelected(int row) {
    if (row < 0 || row >= m_resultsList->count()) return;

    auto* item = m_resultsList->item(row);
    QString authId = item->data(Qt::UserRole).toString();
    if (authId.isEmpty()) return;

    auto crs = CRSSearch::findByAuthId(authId.toStdString());
    if (crs) {
        m_selected = *crs;
        updateDetailsView(*crs);
    }
}

void CrsSelectorDialog::onResultDoubleClicked(int row) {
    onResultSelected(row);
    onAccept();
}

void CrsSelectorDialog::onAuthorityFilterChanged() {
    performSearch();
}

void CrsSelectorDialog::onKindFilterChanged() {
    performSearch();
}

void CrsSelectorDialog::onTabChanged(int index) {
    if (index == 1) updateFavoritesList();
    if (index == 2) updateRecentList();
}

// ============================================================
// Details view
// ============================================================
void CrsSelectorDialog::updateDetailsView(const CRSDefinition& crs) {
    m_nameLabel->setText(QString::fromStdString(crs.name));
    m_authIdLabel->setText(QString::fromStdString(crs.authId()));
    m_kindLabel->setText(QString("Type: %1")
        .arg(QString::fromStdString(CRSDefinition::kindToString(crs.kind))));
    m_unitLabel->setText(QString("Units: %1")
        .arg(QString::fromStdString(CRSDefinition::unitToString(crs.unit))));

    if (!crs.areaOfUseName.empty()) {
        m_areaLabel->setText(QString("Area: %1 (%2° to %3°, %4° to %5°)")
            .arg(QString::fromStdString(crs.areaOfUseName))
            .arg(crs.areaOfUse.west, 0, 'f', 2)
            .arg(crs.areaOfUse.east, 0, 'f', 2)
            .arg(crs.areaOfUse.south, 0, 'f', 2)
            .arg(crs.areaOfUse.north, 0, 'f', 2));
    } else {
        m_areaLabel->setText("Area: —");
    }

    if (!crs.datum.empty()) {
        m_datumLabel->setText(QString("Datum: %1 / %2")
            .arg(QString::fromStdString(crs.datum))
            .arg(QString::fromStdString(crs.ellipsoid)));
    } else {
        m_datumLabel->setText("Datum: —");
    }

    m_wktEdit->setPlainText(QString::fromStdString(crs.wkt2));

    // Format PROJJSON nicely
    QString projJson = QString::fromStdString(crs.projJson);
    if (!projJson.isEmpty()) {
        QJsonDocument doc = QJsonDocument::fromJson(projJson.toUtf8());
        if (!doc.isNull()) {
            m_projJsonEdit->setPlainText(QString::fromUtf8(
                doc.toJson(QJsonDocument::Indented)));
        } else {
            m_projJsonEdit->setPlainText(projJson);
        }
    } else {
        m_projJsonEdit->setPlainText("");
    }
}

// ============================================================
// Custom CRS
// ============================================================
void CrsSelectorDialog::onCustomTextEdited() {
    m_customErrorLabel->clear();
}

void CrsSelectorDialog::onApplyCustom() {
    QString text = m_customEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        m_customErrorLabel->setText("Please enter WKT2 or PROJJSON text.");
        m_customErrorLabel->setStyleSheet("color: red;");
        return;
    }

    auto crs = CRSManager::instance().fromAny(text.toStdString());
    if (crs) {
        m_selected = *crs;
        updateDetailsView(*crs);
        m_customErrorLabel->setText(QString("Valid CRS: %1 (%2)")
            .arg(QString::fromStdString(crs->name))
            .arg(QString::fromStdString(crs->authId())));
        m_customErrorLabel->setStyleSheet("color: green;");
    } else {
        m_customErrorLabel->setText(QString::fromStdString(
            CRSManager::instance().lastError()));
        m_customErrorLabel->setStyleSheet("color: red;");
    }
}

// ============================================================
// Favorites
// ============================================================
void CrsSelectorDialog::onAddFavorite() {
    if (!m_selected.isValid()) return;
    CRSSearch::addFavorite(m_selected);
    updateFavoritesList();
}

void CrsSelectorDialog::onRemoveFavorite() {
    auto* item = m_favoritesList->currentItem();
    if (!item) return;
    QString authId = item->data(Qt::UserRole).toString();
    CRSSearch::removeFavorite(authId.toStdString());
    updateFavoritesList();
}

void CrsSelectorDialog::onFavoriteSelected() {
    auto* item = m_favoritesList->currentItem();
    if (!item) return;
    QString authId = item->data(Qt::UserRole).toString();
    auto crs = CRSSearch::findByAuthId(authId.toStdString());
    if (crs) {
        m_selected = *crs;
        updateDetailsView(*crs);
    }
}

void CrsSelectorDialog::updateFavoritesList() {
    m_favoritesList->clear();
    auto favorites = CRSSearch::favoriteCRS();
    for (const auto& crs : favorites) {
        QString text = QString("%1 — %2")
            .arg(QString::fromStdString(crs.authId()))
            .arg(QString::fromStdString(crs.name));
        auto* item = new QListWidgetItem(text);
        item->setData(Qt::UserRole,
            QVariant::fromValue(QString::fromStdString(crs.authId())));
        m_favoritesList->addItem(item);
    }
}

// ============================================================
// Recent
// ============================================================
void CrsSelectorDialog::onRecentSelected() {
    auto* item = m_recentList->currentItem();
    if (!item) return;
    QString authId = item->data(Qt::UserRole).toString();
    auto crs = CRSSearch::findByAuthId(authId.toStdString());
    if (crs) {
        m_selected = *crs;
        updateDetailsView(*crs);
    }
}

void CrsSelectorDialog::updateRecentList() {
    m_recentList->clear();
    auto recent = CRSSearch::recentCRS();
    for (const auto& crs : recent) {
        QString text = QString("%1 — %2")
            .arg(QString::fromStdString(crs.authId()))
            .arg(QString::fromStdString(crs.name));
        auto* item = new QListWidgetItem(text);
        item->setData(Qt::UserRole,
            QVariant::fromValue(QString::fromStdString(crs.authId())));
        m_recentList->addItem(item);
    }
}

// ============================================================
// Accept
// ============================================================
void CrsSelectorDialog::onAccept() {
    if (m_selected.isValid()) {
        CRSSearch::addRecent(m_selected);
        emit crsSelected(m_selected);
        accept();
    }
}

// ============================================================
// Public accessors
// ============================================================
CRSDefinition CrsSelectorDialog::selectedCRS() const {
    return m_selected;
}

void CrsSelectorDialog::setSelectedCRS(const CRSDefinition& crs) {
    m_selected = crs;
    if (crs.isValid()) {
        updateDetailsView(crs);
    }
}

void CrsSelectorDialog::setAreaOfInterest(const BoundingBox& bbox) {
    m_areaOfInterest = bbox;
    m_hasAreaOfInterest = true;
}

} // namespace gis

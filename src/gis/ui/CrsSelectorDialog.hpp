#pragma once

// ============================================================
// CrsSelectorDialog.hpp — PROJ-backed CRS selector dialog
//
// A professional GIS-style CRS selector that uses PROJ's
// proj.db database for searching, filtering, and selecting
// coordinate reference systems.
//
// Features:
//   - Search by EPSG code, name, or authority:code
//   - Filter by authority (EPSG, ESRI, IGNF, ...)
//   - Filter by kind (geographic, projected, compound, ...)
//   - Filter by area of use
//   - WKT2 / PROJJSON display
//   - Recent CRS list
//   - Favorites
//   - Custom CRS input (WKT2 / PROJJSON)
// ============================================================

#include "CRS.hpp"

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QComboBox>
#include <QTextEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QTabWidget>

namespace gis {

class CrsSelectorDialog : public QDialog {
    Q_OBJECT

public:
    explicit CrsSelectorDialog(QWidget* parent = nullptr);
    ~CrsSelectorDialog() override;

    // Get the selected CRS (invalid if none selected)
    CRSDefinition selectedCRS() const;

    // Set the currently selected CRS
    void setSelectedCRS(const CRSDefinition& crs);

    // Set area of interest for filtering
    void setAreaOfInterest(const BoundingBox& bbox);

signals:
    void crsSelected(const CRSDefinition& crs);

private slots:
    void onSearch();
    void onResultSelected(int row);
    void onResultDoubleClicked(int row);
    void onAuthorityFilterChanged();
    void onKindFilterChanged();
    void onTabChanged(int index);
    void onCustomTextEdited();
    void onApplyCustom();
    void onAddFavorite();
    void onRemoveFavorite();
    void onFavoriteSelected();
    void onRecentSelected();
    void onAccept();

private:
    void setupUI();
    void populateAuthorityCombo();
    void populateKindCombo();
    void performSearch();
    void updateDetailsView(const CRSDefinition& crs);
    void updateFavoritesList();
    void updateRecentList();

    // Search tab
    QLineEdit* m_searchEdit = nullptr;
    QComboBox* m_authorityCombo = nullptr;
    QComboBox* m_kindCombo = nullptr;
    QLineEdit* m_areaEdit = nullptr;
    QListWidget* m_resultsList = nullptr;
    QPushButton* m_searchButton = nullptr;

    // Details
    QLabel* m_nameLabel = nullptr;
    QLabel* m_authIdLabel = nullptr;
    QLabel* m_kindLabel = nullptr;
    QLabel* m_unitLabel = nullptr;
    QLabel* m_areaLabel = nullptr;
    QLabel* m_datumLabel = nullptr;
    QTextEdit* m_wktEdit = nullptr;
    QTextEdit* m_projJsonEdit = nullptr;

    // Favorites tab
    QListWidget* m_favoritesList = nullptr;
    QPushButton* m_addFavoriteButton = nullptr;
    QPushButton* m_removeFavoriteButton = nullptr;

    // Recent tab
    QListWidget* m_recentList = nullptr;

    // Custom CRS tab
    QTextEdit* m_customEdit = nullptr;
    QPushButton* m_applyCustomButton = nullptr;
    QLabel* m_customErrorLabel = nullptr;

    // Dialog buttons
    QPushButton* m_okButton = nullptr;
    QPushButton* m_cancelButton = nullptr;

    // State
    CRSDefinition m_selected;
    BoundingBox m_areaOfInterest;
    bool m_hasAreaOfInterest = false;

    // Tab widget
    QTabWidget* m_tabWidget = nullptr;
};

} // namespace gis

#pragma once

// ============================================================
// SettingsDialog — API keys and project settings
// ============================================================
// Styling comes from the global theme stylesheet (ogs::theme).

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QSettings>
#include <QPushButton>
#include <QVBoxLayout>

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("Settings");
        setMinimumWidth(450);

        auto* layout = new QVBoxLayout(this);
        layout->setSpacing(12);
        layout->setContentsMargins(16, 16, 16, 16);

        // API Keys section
        auto* keysGroup = new QGroupBox("API Keys");
        auto* keysForm = new QFormLayout(keysGroup);

        m_openTopoKey = new QLineEdit();
        m_openTopoKey->setPlaceholderText("OpenTopography API key");
        m_openTopoKey->setEchoMode(QLineEdit::Password);
        keysForm->addRow("OpenTopography:", m_openTopoKey);

        m_mapboxKey = new QLineEdit();
        m_mapboxKey->setPlaceholderText("Mapbox access token");
        m_mapboxKey->setEchoMode(QLineEdit::Password);
        keysForm->addRow("Mapbox:", m_mapboxKey);

        m_maptilerKey = new QLineEdit();
        m_maptilerKey->setPlaceholderText("MapTiler API key");
        m_maptilerKey->setEchoMode(QLineEdit::Password);
        keysForm->addRow("MapTiler:", m_maptilerKey);

        layout->addWidget(keysGroup);

        // Project defaults section
        auto* defaultsGroup = new QGroupBox("Project Defaults");
        auto* defaultsForm = new QFormLayout(defaultsGroup);

        m_defaultWorkspace = new QComboBox();
        m_defaultWorkspace->addItems({"Home", "Terrain", "Road Studio", "Train Studio"});
        defaultsForm->addRow("Default workspace:", m_defaultWorkspace);

        m_defaultRoadWidth = new QLineEdit("8.0");
        defaultsForm->addRow("Default road width (m):", m_defaultRoadWidth);

        m_defaultLanes = new QLineEdit("2");
        defaultsForm->addRow("Default lane count:", m_defaultLanes);

        layout->addWidget(defaultsGroup);

        // Buttons
        auto* btnLayout = new QHBoxLayout();
        btnLayout->addStretch();

        auto* saveBtn = new QPushButton("Save");
        connect(saveBtn, &QPushButton::clicked, this, &SettingsDialog::saveAndAccept);
        btnLayout->addWidget(saveBtn);

        auto* cancelBtn = new QPushButton("Cancel");
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        btnLayout->addWidget(cancelBtn);

        layout->addLayout(btnLayout);

        loadSettings();
    }

private slots:
    void saveAndAccept() {
        QSettings s;
        s.setValue("api/opentopo_key", m_openTopoKey->text());
        s.setValue("api/mapbox_token", m_mapboxKey->text());
        s.setValue("api/maptiler_key", m_maptilerKey->text());
        s.setValue("defaults/workspace", m_defaultWorkspace->currentIndex());
        s.setValue("defaults/road_width", m_defaultRoadWidth->text());
        s.setValue("defaults/lanes", m_defaultLanes->text());
        accept();
    }

private:
    QLineEdit* m_openTopoKey = nullptr;
    QLineEdit* m_mapboxKey = nullptr;
    QLineEdit* m_maptilerKey = nullptr;
    QComboBox* m_defaultWorkspace = nullptr;
    QLineEdit* m_defaultRoadWidth = nullptr;
    QLineEdit* m_defaultLanes = nullptr;

    void loadSettings() {
        QSettings s;
        m_openTopoKey->setText(s.value("api/opentopo_key").toString());
        m_mapboxKey->setText(s.value("api/mapbox_token").toString());
        m_maptilerKey->setText(s.value("api/maptiler_key").toString());
        m_defaultWorkspace->setCurrentIndex(s.value("defaults/workspace", 0).toInt());
        m_defaultRoadWidth->setText(s.value("defaults/road_width", "8.0").toString());
        m_defaultLanes->setText(s.value("defaults/lanes", "2").toString());
    }
};

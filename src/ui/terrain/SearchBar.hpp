#pragma once

// ============================================================
// SearchBar — Location search with autocomplete
// ============================================================
//
// Replaces modules/terrain/client/SearchBar/SearchBar.tsx.
// Uses Nominatim (OpenStreetMap) geocoding API.
// 300ms debounce + 1000ms rate limiting.
// Keyboard navigation (ArrowUp/Down/Enter/Escape).
// Shows max 5 results.
//

#include <QWidget>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <QUrlQuery>
#include <QKeyEvent>
#include <algorithm>

#include "../../core/logger/Logger.hpp"

struct GeocodingResult {
    QString displayName;
    double latitude = 0;
    double longitude = 0;
    QString type;
};

class SearchBar : public QWidget {
    Q_OBJECT

public:
    explicit SearchBar(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);

        m_input = new QLineEdit();
        m_input->setPlaceholderText("Search for a location...");
        m_input->setStyleSheet(
            "QLineEdit { background: #161b22; border: 1px solid #30363d; border-radius: 6px;"
            "padding: 6px 10px 6px 28px; color: #e6edf3; font-size: 12px; }"
            "QLineEdit:focus { border-color: #1f6feb; }"
            "QLineEdit::placeholder { color: #484f58; }");
        m_input->setClearButtonEnabled(true);

        // Add search icon via action
        // (Using text prefix since icon resources may not be available)
        layout->addWidget(m_input);

        m_dropdown = new QListWidget();
        m_dropdown->setMaximumHeight(150);
        m_dropdown->setStyleSheet(
            "QListWidget { background: #161b22; border: 1px solid #30363d; border-radius: 6px;"
            "color: #e6edf3; font-size: 12px; outline: none; }"
            "QListWidget::item { padding: 6px 10px; border-bottom: 1px solid #21262d; }"
            "QListWidget::item:selected { background: #1f6feb; }"
            "QListWidget::item:hover { background: #21262d; }");
        m_dropdown->hide();
        layout->addWidget(m_dropdown);

        m_network = new QNetworkAccessManager(this);
        m_debounceTimer = new QTimer(this);
        m_debounceTimer->setSingleShot(true);
        m_rateTimer = new QTimer(this);
        m_rateTimer->setSingleShot(true);

        connect(m_input, &QLineEdit::textChanged, this, &SearchBar::onTextChanged);
        connect(m_input, &QLineEdit::returnPressed, this, &SearchBar::onReturnPressed);
        connect(m_debounceTimer, &QTimer::timeout, this, &SearchBar::performSearch);
        connect(m_dropdown, &QListWidget::itemClicked, this, &SearchBar::onResultSelected);
        connect(m_dropdown, &QListWidget::itemSelectionChanged, this, [this]() {
            auto* item = m_dropdown->currentItem();
            if (item) {
                int idx = m_dropdown->row(item);
                m_highlightedIndex = idx;
            }
        });
    }

signals:
    void locationSelected(double lat, double lon, int zoom);

private:
    void onTextChanged(const QString& text) {
        m_dropdown->hide();
        m_results.clear();

        if (text.trimmed().length() < 3) {
            m_debounceTimer->stop();
            return;
        }

        m_debounceTimer->start(300);  // 300ms debounce
    }

    void performSearch() {
        QString query = m_input->text().trimmed();
        if (query.length() < 3) return;

        // Rate limiting: 1000ms between requests
        if (m_rateTimer->isActive()) {
            // Wait for rate timer, then retry
            QTimer::singleShot(1000 - m_rateTimer->remainingTime(), this, [this]() {
                if (!m_rateTimer->isActive()) performSearch();
            });
            return;
        }

        m_rateTimer->start(1000);

        QUrl url("https://nominatim.openstreetmap.org/search");
        QUrlQuery q;
        q.addQueryItem("q", query);
        q.addQueryItem("format", "json");
        q.addQueryItem("limit", "5");
        q.addQueryItem("addressdetails", "0");
        url.setQuery(q);

        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader, "OpenGeoStudio-Qt/1.0");
        request.setRawHeader("Accept", "application/json");

        QNetworkReply* reply = m_network->get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                m_log.warn("Geocoding search failed: " + reply->errorString());
                showDropdown();
                return;
            }

            m_results.clear();
            auto doc = QJsonDocument::fromJson(reply->readAll());
            auto arr = doc.array();
            for (const auto& val : arr) {
                auto obj = val.toObject();
                GeocodingResult result;
                result.displayName = obj.value("display_name").toString();
                result.latitude = obj.value("lat").toString().toDouble();
                result.longitude = obj.value("lon").toString().toDouble();
                result.type = obj.value("type").toString();
                m_results.append(result);
            }

            showDropdown();
        });
    }

    void showDropdown() {
        m_dropdown->clear();
        if (m_results.isEmpty()) {
            auto* item = new QListWidgetItem("No results found");
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable & ~Qt::ItemIsEnabled);
            item->setForeground(QColor("#7d8590"));
            m_dropdown->addItem(item);
        } else {
            for (const auto& r : m_results) {
                m_dropdown->addItem(r.displayName);
            }
        }
        m_dropdown->show();
        m_dropdown->setFocus();
        m_highlightedIndex = -1;
    }

    void onReturnPressed() {
        if (m_highlightedIndex >= 0 && m_highlightedIndex < m_results.size()) {
            selectResult(m_results[m_highlightedIndex]);
        } else if (!m_results.isEmpty()) {
            selectResult(m_results.first());
        }
    }

    void onResultSelected(QListWidgetItem* item) {
        int idx = m_dropdown->row(item);
        if (idx >= 0 && idx < m_results.size()) {
            selectResult(m_results[idx]);
        }
    }

    void selectResult(const GeocodingResult& result) {
        m_input->setText(result.displayName);
        m_dropdown->hide();

        int zoom = getZoomForType(result.type);
        emit locationSelected(result.latitude, result.longitude, zoom);
    }

    int getZoomForType(const QString& type) const {
        if (type == "city" || type == "town") return 11;
        if (type == "village" || type == "hamlet") return 13;
        if (type == "suburb" || type == "neighbourhood") return 14;
        if (type == "country" || type == "state" || type == "region") return 6;
        if (type == "peak" || type == "mountain") return 12;
        return 13;  // Default
    }

    void keyPressEvent(QKeyEvent* e) override {
        if (m_dropdown->isHidden()) {
            QWidget::keyPressEvent(e);
            return;
        }

        switch (e->key()) {
        case Qt::Key_Down:
            m_highlightedIndex = std::min(m_highlightedIndex + 1, static_cast<int>(m_results.size()) - 1);
            m_dropdown->setCurrentRow(m_highlightedIndex);
            break;
        case Qt::Key_Up:
            m_highlightedIndex = std::max(m_highlightedIndex - 1, 0);
            m_dropdown->setCurrentRow(m_highlightedIndex);
            break;
        case Qt::Key_Escape:
            m_dropdown->hide();
            break;
        default:
            QWidget::keyPressEvent(e);
        }
    }

    QLineEdit* m_input = nullptr;
    QListWidget* m_dropdown = nullptr;
    QNetworkAccessManager* m_network = nullptr;
    QTimer* m_debounceTimer = nullptr;
    QTimer* m_rateTimer = nullptr;
    QList<GeocodingResult> m_results;
    int m_highlightedIndex = -1;
    Logger m_log{"SearchBar"};
};

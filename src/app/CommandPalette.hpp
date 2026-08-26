#pragma once

// ============================================================
// CommandPalette — Ctrl+Shift+P quick command execution
// ============================================================

#include "../../theme/Theme.hpp"

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QStringList>
#include <QVBoxLayout>

class CommandPalette : public QDialog {
    Q_OBJECT
public:
    explicit CommandPalette(QWidget* parent = nullptr) : QDialog(parent) {
        setWindowTitle("Command Palette");
        setMinimumWidth(500);
        setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(4);

        namespace th = ogs::theme;
        m_input = new QLineEdit(this);
        m_input->setPlaceholderText("Type a command...");
        m_input->setStyleSheet(
            QStringLiteral("QLineEdit { background: %1; border: 1px solid %2; border-radius: 6px;"
            "padding: 10px 14px; color: %3; font-size: 14px; }")
                .arg(th::c::BgOverlay, th::c::Accent, th::c::Text));
        layout->addWidget(m_input);

        m_list = new QListWidget(this);
        m_list->setStyleSheet(
            QStringLiteral("QListWidget { background: %1; border: 1px solid %2; border-radius: 6px; }"
            "QListWidget::item { padding: 8px 12px; color: %3; }"
            "QListWidget::item:hover { background: %4; }"
            "QListWidget::item:selected { background: %5; color: %6; }")
                .arg(th::c::BgBase, th::c::Border, th::c::Text,
                     th::c::BgSurface, th::c::AccentSoft, th::c::Accent));
        layout->addWidget(m_list);

        // Populate commands
        addCommand("File: New Project", "file.new");
        addCommand("File: Open Project", "file.open");
        addCommand("File: Save Project", "file.save");
        addCommand("View: Home", "ws.home");
        addCommand("View: Terrain", "ws.terrain");
        addCommand("View: Road Studio", "ws.road");
        addCommand("View: Train Studio", "ws.train");
        addCommand("Settings: Open Settings", "settings.open");
        addCommand("Help: About", "help.about");

        m_input->setFocus();

        connect(m_input, &QLineEdit::textChanged, this, &CommandPalette::filterCommands);
        connect(m_input, &QLineEdit::returnPressed, this, &CommandPalette::executeSelected);
        connect(m_list, &QListWidget::itemDoubleClicked, this, &CommandPalette::executeSelected);
    }

    QString selectedCommand() const { return m_selectedCommand; }

private:
    QLineEdit* m_input = nullptr;
    QListWidget* m_list = nullptr;
    QString m_selectedCommand;
    QStringList m_allCommands;

    void addCommand(const QString& label, const QString& id) {
        auto* item = new QListWidgetItem(label, m_list);
        item->setData(Qt::UserRole, id);
        m_allCommands.append(label + "|" + id);
    }

    void filterCommands(const QString& text) {
        m_list->clear();
        for (const auto& cmd : m_allCommands) {
            auto parts = cmd.split('|');
            if (parts.size() != 2) continue;
            if (text.isEmpty() || parts[0].contains(text, Qt::CaseInsensitive)) {
                auto* item = new QListWidgetItem(parts[0], m_list);
                item->setData(Qt::UserRole, parts[1]);
            }
        }
        if (m_list->count() > 0) m_list->setCurrentRow(0);
    }

    void executeSelected() {
        auto* item = m_list->currentItem();
        if (item) {
            m_selectedCommand = item->data(Qt::UserRole).toString();
            accept();
        }
    }
};

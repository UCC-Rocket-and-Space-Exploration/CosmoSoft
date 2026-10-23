#include "gui/widgets/PreflightChecklistWidget.h"
#include "gui/SettingsKeys.h"
#include "gui/Theme.h"
#include "gui/ThemeManager.h"
#include "services/persistence/PreflightChecklist.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <memory>

namespace {
QJsonArray storedProfiles() {
  const auto profiles =
      QJsonDocument::fromJson(QSettings(kSettingsOrg, kSettingsApp)
                                  .value(kSettingsChecklistProfiles)
                                  .toByteArray())
          .array();
  return cosmo::mission::validChecklists(profiles)
             ? profiles
             : cosmo::mission::defaultChecklists();
}
} // namespace
PreflightChecklistWidget::PreflightChecklistWidget(QWidget *parent)
    : QFrame(parent) {
  setObjectName("preflightChecklist");
  auto layout = std::make_unique<QVBoxLayout>(this);
  layout->setContentsMargins(12, 8, 12, 8);
  layout->setSpacing(4);
  auto header = std::make_unique<QHBoxLayout>();
  m_toggle = std::make_unique<QToolButton>(this).release();
  m_toggle->setText(tr("Preflight"));
  m_toggle->setCheckable(true);
  m_toggle->setArrowType(Qt::RightArrow);
  m_toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  m_summary = std::make_unique<QLabel>(this).release();
  m_summary->setObjectName("checklistSummary");
  header->addWidget(m_toggle);
  header->addStretch();
  header->addWidget(m_summary);
  layout->addLayout(header.release());
  m_details = std::make_unique<QWidget>(this).release();
  m_details->hide();
  auto details = std::make_unique<QVBoxLayout>(m_details);
  details->setContentsMargins(0, 0, 0, 0);
  m_profile = std::make_unique<QComboBox>(m_details).release();
  m_profile->setAccessibleName(tr("Preflight profile"));
  details->addWidget(m_profile);
  m_items = std::make_unique<QListWidget>(m_details).release();
  m_items->setMaximumHeight(120);
  details->addWidget(m_items);
  details.release();
  layout->addWidget(m_details);
  layout.release();
  connect(m_toggle, &QToolButton::toggled, this, [this](bool open) {
    m_details->setVisible(open);
    m_toggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
  });
  connect(m_profile, &QComboBox::currentIndexChanged, this,
          [this] { resetChecks(); });
  connect(m_items, &QListWidget::itemChanged, this, [this] { rebuild(); });
  connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
          this, &PreflightChecklistWidget::refreshStyleSheet);
  reloadProfiles();
  refreshStyleSheet();
}
void PreflightChecklistWidget::refreshStyleSheet() {
  setStyleSheet(
      QStringLiteral(
          "QFrame {background:%1; color:%2; border-radius:%4px;} QLabel "
          "{color:%3; font-size:11px;} QListWidget {color:%2; background:%1;}")
          .arg(Theme::kBgPanel(), Theme::kTextPrimary(), Theme::kTextMid())
          .arg(Theme::kRadiusMd));
}
void PreflightChecklistWidget::reloadProfiles() {
  const auto profiles = storedProfiles();
  if (profiles == m_profiles)
    return;
  m_profiles = profiles;
  const QSignalBlocker blocked(m_profile);
  m_profile->clear();
  for (const auto &value : profiles)
    m_profile->addItem(value.toObject().value("name").toString());
  m_profile->setCurrentIndex(0);
  resetChecks();
}
void PreflightChecklistWidget::resetChecks() {
  m_items->clear();
  rebuild();
}
void PreflightChecklistWidget::setMeasurements(std::optional<double> age,
                                               std::optional<double> battery,
                                               bool position) {
  m_age = age;
  m_battery = battery;
  m_position = position;
  rebuild();
}
void PreflightChecklistWidget::rebuild() {
  const int index = m_profile->currentIndex();
  if (index < 0 || index >= m_profiles.size())
    return;
  const auto items = m_profiles.at(index).toObject().value("items").toArray();
  const QSignalBlocker blocked(m_items);
  while (m_items->count() < items.size())
    m_items->addItem(std::make_unique<QListWidgetItem>().release());
  int required = 0, complete = 0;
  for (int row = 0; row < items.size(); ++row) {
    const auto definition = items.at(row).toObject();
    auto *item = m_items->item(row);
    const bool manual = definition.value("kind").toString() == "manual";
    if (definition.value("required").toBool())
      ++required;
    item->setFlags(manual ? Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                                Qt::ItemIsUserCheckable
                          : Qt::ItemIsEnabled);
    if (manual) {
      if (!item->data(Qt::UserRole).isValid())
        item->setCheckState(Qt::Unchecked);
      item->setData(Qt::UserRole, true);
      item->setText(definition.value("name").toString());
      if (definition.value("required").toBool() &&
          item->checkState() == Qt::Checked)
        ++complete;
    } else {
      const auto result = cosmo::mission::measuredCheck(
          definition.value("kind").toString(),
          definition.value("limit").toDouble(), m_age, m_battery, m_position);
      item->setText(definition.value("name").toString() +
                    (!result   ? tr(" · Unknown")
                     : *result ? tr(" · Passed")
                               : tr(" · Pending")));
      if (definition.value("required").toBool() && result && *result)
        ++complete;
    }
  }
  m_summary->setText(required && complete == required
                         ? tr("Checks complete")
                         : tr("%1/%2 checks").arg(complete).arg(required));
  m_summary->setToolTip(
      tr("A checklist records checks; it does not certify launch safety."));
}

PreflightProfileEditor::PreflightProfileEditor(QWidget *parent)
    : QFrame(parent), m_profiles(storedProfiles()) {
  setObjectName("preflightProfileEditor");
  auto layout = std::make_unique<QVBoxLayout>(this);
  layout->setContentsMargins(0, 0, 0, 0);
  auto title =
      std::make_unique<QLabel>(tr("Preflight checklist profiles"), this);
  layout->addWidget(title.release());
  m_selector = std::make_unique<QComboBox>(this).release();
  m_selector->setAccessibleName(tr("Checklist profile"));
  layout->addWidget(m_selector);
  m_name = std::make_unique<QLineEdit>(this).release();
  m_name->setObjectName("preflightProfileName");
  m_name->setMaxLength(128);
  m_name->setAccessibleName(tr("Profile name"));
  layout->addWidget(m_name);
  m_table = std::make_unique<QTableWidget>(0, 4, this).release();
  m_table->setObjectName("preflightItemsEditor");
  m_table->setHorizontalHeaderLabels(
      {tr("Item"), tr("Check"), tr("Required"), tr("Limit")});
  m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  m_table->horizontalHeader()->setSectionResizeMode(
      1, QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(
      2, QHeaderView::ResizeToContents);
  m_table->setColumnWidth(3, 90);
  m_table->setMinimumHeight(160);
  layout->addWidget(m_table);
  auto items = std::make_unique<QHBoxLayout>();
  for (const auto &text :
       {tr("Add item"), tr("Delete item"), tr("Up"), tr("Down")}) {
    auto button = std::make_unique<QPushButton>(text, this);
    connect(button.get(), &QPushButton::clicked, this, [this, text] {
      capture();
      auto profile = m_profiles.at(m_index).toObject();
      auto definitions = profile.value("items").toArray();
      int row = m_table->currentRow();
      if (text == tr("Add item") && definitions.size() < 64)
        definitions.append(QJsonObject{{"name", tr("New check")},
                                       {"kind", "manual"},
                                       {"required", true},
                                       {"limit", 0}});
      else if (text == tr("Delete item") && row >= 0)
        definitions.removeAt(row);
      else if (row >= 0) {
        const int next = row + (text == tr("Up") ? -1 : 1);
        if (next >= 0 && next < definitions.size()) {
          const auto entry = definitions.at(row);
          definitions.removeAt(row);
          definitions.insert(next, entry);
          row = next;
        }
      }
      profile["items"] = definitions;
      m_profiles[m_index] = profile;
      display(m_index);
      if (row >= 0 && row < m_table->rowCount())
        m_table->setCurrentCell(row, 0);
    });
    items->addWidget(button.release());
  }
  layout->addLayout(items.release());
  auto profiles = std::make_unique<QHBoxLayout>();
  for (const auto &text : {tr("Duplicate"), tr("Delete profile"),
                           tr("Reset defaults"), tr("Save profiles")}) {
    auto button = std::make_unique<QPushButton>(text, this);
    if (text == tr("Save profiles"))
      button->setObjectName("savePreflightProfiles");
    connect(button.get(), &QPushButton::clicked, this, [this, text] {
      capture();
      int next = m_index;
      if (text == tr("Duplicate") && m_profiles.size() < 32) {
        auto copy = m_profiles.at(m_index).toObject();
        QString name = copy.value("name").toString() + tr(" copy");
        QStringList names;
        for (const auto &value : m_profiles)
          names << value.toObject().value("name").toString();
        while (names.contains(name))
          name += tr(" copy");
        copy["name"] = name;
        m_profiles.append(copy);
        next = static_cast<int>(m_profiles.size()) - 1;
      } else if (text == tr("Delete profile") && m_profiles.size() > 1) {
        m_profiles.removeAt(m_index);
        next = 0;
      } else if (text == tr("Reset defaults")) {
        m_profiles = cosmo::mission::defaultChecklists();
        next = 0;
      } else if (text == tr("Save profiles")) {
        if (!cosmo::mission::validChecklists(m_profiles)) {
          QMessageBox::warning(this, tr("Invalid profiles"),
                               tr("Use unique names, nonempty items, and "
                                  "nonnegative finite limits."));
          return;
        }
        QSettings settings(kSettingsOrg, kSettingsApp);
        settings.setValue(
            kSettingsChecklistProfiles,
            QJsonDocument(m_profiles).toJson(QJsonDocument::Compact));
        settings.sync();
        if (settings.status() != QSettings::NoError) {
          QMessageBox::warning(this, tr("Could not save"),
                               tr("Checklist profiles could not be stored."));
          return;
        }
        if (profilesSaved)
          profilesSaved();
      }
      refreshProfiles(next);
    });
    profiles->addWidget(button.release());
  }
  layout->addLayout(profiles.release());
  auto hint = std::make_unique<QLabel>(
      tr("Packet-age limits use seconds; battery limits use volts. Manual "
         "confirmations reset for each recording."),
      this);
  hint->setWordWrap(true);
  layout->addWidget(hint.release());
  layout.release();
  connect(m_selector, &QComboBox::currentIndexChanged, this, [this](int next) {
    if (m_loading)
      return;
    capture();
    display(next);
  });
  connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
          this, &PreflightProfileEditor::refreshStyleSheet);
  refreshProfiles(0);
  refreshStyleSheet();
}
void PreflightProfileEditor::refreshStyleSheet() {
  setStyleSheet(
      QStringLiteral("QFrame#preflightProfileEditor {background:%1;color:%2;} "
                     "QTableWidget {background:%1;color:%2;gridline-color:%3;}")
          .arg(Theme::kBgPanel(), Theme::kTextPrimary(),
               Theme::kBorderSubtle()));
}
void PreflightProfileEditor::capture() {
  if (m_loading || m_index < 0 || m_index >= m_profiles.size())
    return;
  QJsonArray definitions;
  for (int row = 0; row < m_table->rowCount(); ++row)
    definitions.append(QJsonObject{
        {"name", m_table->item(row, 0) ? m_table->item(row, 0)->text().trimmed()
                                       : QString()},
        {"kind", static_cast<QComboBox *>(m_table->cellWidget(row, 1))
                     ->currentData()
                     .toString()},
        {"required",
         static_cast<QCheckBox *>(m_table->cellWidget(row, 2))->isChecked()},
        {"limit",
         static_cast<QDoubleSpinBox *>(m_table->cellWidget(row, 3))->value()}});
  m_profiles[m_index] =
      QJsonObject{{"name", m_name->text().trimmed()}, {"items", definitions}};
}
void PreflightProfileEditor::refreshProfiles(int selected) {
  const QSignalBlocker blocked(m_selector);
  m_loading = true;
  m_selector->clear();
  for (const auto &value : m_profiles)
    m_selector->addItem(value.toObject().value("name").toString());
  m_selector->setCurrentIndex(selected);
  m_loading = false;
  display(selected);
}
void PreflightProfileEditor::display(int index) {
  if (index < 0 || index >= m_profiles.size())
    return;
  m_loading = true;
  m_index = index;
  const auto profile = m_profiles.at(index).toObject();
  m_name->setText(profile.value("name").toString());
  const auto items = profile.value("items").toArray();
  m_table->setRowCount(0);
  m_table->setRowCount(static_cast<int>(items.size()));
  for (int row = 0; row < items.size(); ++row) {
    const auto item = items.at(row).toObject();
    m_table->setItem(
        row, 0,
        std::make_unique<QTableWidgetItem>(item.value("name").toString())
            .release());
    auto kind = std::make_unique<QComboBox>();
    kind->addItem(tr("Manual"), "manual");
    kind->addItem(tr("Packet age ≤"), "packetAge");
    kind->addItem(tr("Position received"), "gps");
    kind->addItem(tr("Battery ≥"), "battery");
    kind->setCurrentIndex(kind->findData(item.value("kind").toString()));
    m_table->setCellWidget(row, 1, kind.release());
    auto required = std::make_unique<QCheckBox>();
    required->setChecked(item.value("required").toBool());
    m_table->setCellWidget(row, 2, required.release());
    auto limit = std::make_unique<QDoubleSpinBox>();
    limit->setRange(0, 1e6);
    limit->setValue(item.value("limit").toDouble());
    m_table->setCellWidget(row, 3, limit.release());
  }
  m_loading = false;
}

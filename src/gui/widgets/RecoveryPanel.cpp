#include "gui/widgets/RecoveryPanel.h"
#include "gui/Theme.h"
#include "gui/ThemeManager.h"
#include "services/persistence/RecoveryState.h"
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QShowEvent>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <memory>

RecoveryPanel::RecoveryPanel(QWidget *parent) : QFrame(parent) {
  setObjectName("recoveryPanel");
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
  auto layout = std::make_unique<QVBoxLayout>(this);
  layout->setContentsMargins(6, 2, 6, 2);
  layout->setSpacing(4);
  m_mode = std::make_unique<QAction>(this).release();
  m_mode->setText(tr("Recovery"));
  m_mode->setObjectName("recoveryMode");
  m_mode->setCheckable(true);
  m_mode->setToolTip(tr("Show last known position and recovery navigation"));
  m_details = std::make_unique<QWidget>(this).release();
  m_details->hide();
  auto details = std::make_unique<QVBoxLayout>(m_details);
  details->setContentsMargins(0, 0, 0, 0);
  details->setSpacing(4);
  m_position = std::make_unique<QLabel>(m_details).release();
  m_position->setObjectName("lastKnownPosition");
  m_position->setTextFormat(Qt::PlainText);
  m_position->setWordWrap(true);
  m_position->setTextInteractionFlags(Qt::TextSelectableByMouse |
                                      Qt::TextSelectableByKeyboard);
  details->addWidget(m_position);
  auto buttons = std::make_unique<QHBoxLayout>();
  for (const auto &label :
       {tr("Copy coordinates"), tr("Open map"), tr("Reference…")}) {
    auto button = std::make_unique<QPushButton>(label, m_details);
    connect(button.get(), &QPushButton::clicked, this, [this, label] {
      if (label == tr("Reference…")) {
        chooseReference();
        return;
      }
      if (m_record.isEmpty())
        return;
      const auto coordinate =
          QStringLiteral("%1,%2")
              .arg(m_record.value("lat").toDouble(), 0, 'f', 6)
              .arg(m_record.value("lon").toDouble(), 0, 'f', 6);
      if (label == tr("Copy coordinates"))
        QApplication::clipboard()->setText(coordinate);
      else {
        QUrl url("https://www.openstreetmap.org/");
        QUrlQuery query;
        query.addQueryItem(
            "mlat", QString::number(m_record.value("lat").toDouble(), 'f', 6));
        query.addQueryItem(
            "mlon", QString::number(m_record.value("lon").toDouble(), 'f', 6));
        url.setQuery(query);
        QDesktopServices::openUrl(url);
      }
    });
    buttons->addWidget(button.release());
  }
  details->addLayout(buttons.release());
  layout->addWidget(m_details);
  auto timer = std::make_unique<QTimer>(this);
  m_timer = timer.get();
  timer->setInterval(1000);
  connect(timer.release(), &QTimer::timeout, this, &RecoveryPanel::refresh);
  connect(m_mode, &QAction::toggled, this, [this](bool active) {
    m_details->setVisible(active);
    setVisible(active);
    if (active && isVisible())
      m_timer->start();
    else
      m_timer->stop();
    refresh();
    if (modeChanged)
      modeChanged(active, m_record);
  });
  connect(&cosmo::ThemeManager::instance(), &cosmo::ThemeManager::themeChanged,
          this, &RecoveryPanel::refreshStyleSheet);
  details.release();
  layout.release();
  refreshStyleSheet();
  refresh();
  hide();
}
void RecoveryPanel::setRecord(const QJsonObject &record) {
  const auto lat = record.value("lat"), lon = record.value("lon");
  if (!record.isEmpty() &&
      (!lat.isDouble() || !lon.isDouble() ||
       !cosmo::mission::usablePosition(lat.toDouble(), lon.toDouble())))
    return;
  if (record.value("receivedUtcMs") != m_record.value("receivedUtcMs") ||
      record.value("session") != m_record.value("session")) {
    const auto received = record.value("receivedUtcMs").toDouble();
    m_initialAgeMs =
        std::max(0.0, static_cast<double>(QDateTime::currentMSecsSinceEpoch()) -
                          received);
    m_receiptAge.start();
  }
  m_record = record;
  if (record.value("reference").isObject())
    m_reference = record.value("reference").toObject();
  refresh();
  if (modeChanged)
    modeChanged(m_mode->isChecked(), m_record);
}
void RecoveryPanel::setReference(const QJsonObject &reference) {
  m_reference = reference;
  refresh();
}
void RecoveryPanel::showEvent(QShowEvent *event) {
  QFrame::showEvent(event);
  refresh();
  if (m_mode->isChecked())
    m_timer->start();
}
void RecoveryPanel::hideEvent(QHideEvent *event) {
  m_timer->stop();
  QFrame::hideEvent(event);
}
void RecoveryPanel::refreshStyleSheet() {
  setStyleSheet(QStringLiteral(
                    "QFrame#recoveryPanel { background:%1; color:%2; } "
                    "QFrame#recoveryPanel QLabel { color:%2; font-size:11px; }")
                    .arg(Theme::kBgPanel(), Theme::kTextPrimary()));
}
void RecoveryPanel::refresh() {
  if (m_record.isEmpty()) {
    m_position->setText(tr("No received position"));
    return;
  }
  const auto stamp = QDateTime::fromMSecsSinceEpoch(
      static_cast<qint64>(m_record.value("receivedUtcMs").toDouble()),
      QTimeZone::utc());
  const auto age =
      (m_initialAgeMs + (m_receiptAge.isValid() ? m_receiptAge.elapsed() : 0)) /
      1000;
  QString text = tr("Last known position: %1, %2")
                     .arg(m_record.value("lat").toDouble(), 0, 'f', 6)
                     .arg(m_record.value("lon").toDouble(), 0, 'f', 6);
  if (m_record.value("replay").toBool())
    text += tr(" · Replay sample");
  else
    text +=
        tr(" · %1 s old%2")
            .arg(age)
            .arg(m_record.value("earlier").toBool() ? tr(" · Earlier session")
                                                    : QString());
  if (stamp.isValid())
    m_position->setToolTip(tr("Received %1 UTC · %2 · Session %3")
                               .arg(stamp.toString(Qt::ISODate),
                                    m_record.value("source").toString(),
                                    m_record.value("session").toString()));
  if (!m_reference.isEmpty()) {
    const auto [distance, bearing] = cosmo::mission::rangeBearing(
        m_reference.value("lat").toDouble(),
        m_reference.value("lon").toDouble(), m_record.value("lat").toDouble(),
        m_record.value("lon").toDouble());
    text += tr("\nFrom %1: %2 m · %3°")
                .arg(m_reference.value("name").toString(tr("ground reference")))
                .arg(distance, 0, 'f', 0)
                .arg(bearing, 0, 'f', 0);
  }
  m_position->setText(text);
}
void RecoveryPanel::chooseReference() {
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Ground reference"));
  auto form = std::make_unique<QFormLayout>(&dialog);
  QDoubleSpinBox latitude, longitude;
  latitude.setDecimals(6);
  longitude.setDecimals(6);
  latitude.setRange(-90, 90);
  longitude.setRange(-180, 180);
  latitude.setValue(
      m_reference.value("lat").toDouble(m_record.value("lat").toDouble()));
  longitude.setValue(
      m_reference.value("lon").toDouble(m_record.value("lon").toDouble()));
  form->addRow(tr("Latitude"), &latitude);
  form->addRow(tr("Longitude"), &longitude);
  QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  form->addRow(&buttons);
  if (dialog.exec() != QDialog::Accepted)
    return;
  m_reference = {{"lat", latitude.value()},
                 {"lon", longitude.value()},
                 {"name", tr("ground reference")}};
  refresh();
  if (referenceChanged)
    referenceChanged();
}

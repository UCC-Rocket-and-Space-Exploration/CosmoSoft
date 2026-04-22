#include "gui/AboutDialog.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(u"About CosmoSoft"_s);
    setWindowIcon(QApplication::windowIcon());
    setFixedSize(420, 280);
    setModal(true);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(32, 28, 32, 20);
    root->setSpacing(12);

    auto *titleLabel = new QLabel(u"CosmoSoft"_s, this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(22);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignLeft);

    auto *versionLabel = new QLabel(u"Version 0.1.0 — development build"_s, this);
    versionLabel->setAlignment(Qt::AlignLeft);

    auto *descLabel = new QLabel(
        u"Open-source ground-station software for rocket telemetry.\n"
        u"Written in C++20 with Qt 6."_s,
        this);
    descLabel->setWordWrap(true);
    descLabel->setAlignment(Qt::AlignLeft);

    auto *licenseLabel = new QLabel(u"Licensed under the Apache License 2.0."_s, this);
    licenseLabel->setAlignment(Qt::AlignLeft);

    auto *repoLabel = new QLabel(
        u"<a href=\"https://github.com/UCC-Rocket-and-Space-Exploration/CosmoSoft\">"
        u"github.com/UCC-Rocket-and-Space-Exploration/CosmoSoft</a>"_s,
        this);
    repoLabel->setOpenExternalLinks(true);
    repoLabel->setTextFormat(Qt::RichText);
    repoLabel->setAlignment(Qt::AlignLeft);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    root->addWidget(titleLabel);
    root->addWidget(versionLabel);
    root->addSpacing(4);
    root->addWidget(descLabel);
    root->addWidget(licenseLabel);
    root->addWidget(repoLabel);
    root->addStretch(1);
    root->addWidget(buttons);

    setStyleSheet(uR"(
        QDialog {
            background-color: #1f1f1f;
            color: #f0f0f0;
        }
        QLabel {
            color: #f0f0f0;
            font-family: "Red Hat Mono", "Courier New", monospace;
        }
        QLabel a {
            color: #6ab0de;
        }
        QPushButton {
            border: 1px solid #6a6a6a;
            border-radius: 4px;
            padding: 4px 16px;
            min-height: 28px;
            background-color: #3d3f47;
            color: #f0f0f0;
            font-family: "Red Hat Mono", "Courier New", monospace;
        }
        QPushButton:hover  { background-color: #4d4f57; }
        QPushButton:pressed { background-color: #2d2f37; }
    )"_s);
}

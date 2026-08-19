#include "gui/AboutDialog.h"

#include "gui/Theme.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(u"About CosmoSoft"_s);
    setWindowIcon(QApplication::windowIcon());
    setMinimumSize(360, 260);
    resize(480, 320);
    setSizeGripEnabled(true);
    setModal(true);

    auto *root = new QVBoxLayout(this);
    root->setSizeConstraint(QLayout::SetMinimumSize);
    root->setContentsMargins(32, 28, 32, 20);
    root->setSpacing(12);

    auto *titleLabel = new QLabel(u"CosmoSoft"_s, this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() * 1.75);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    titleLabel->setAlignment(Qt::AlignLeft);

    auto *versionLabel = new QLabel(u"Version 0.1.0 — development build"_s, this);
    versionLabel->setWordWrap(true);
    versionLabel->setAlignment(Qt::AlignLeft);

    auto *descLabel = new QLabel(
        u"Open-source ground-station software for rocket telemetry.\n"
        u"Written in C++20 with Qt 6."_s,
        this);
    descLabel->setWordWrap(true);
    descLabel->setAlignment(Qt::AlignLeft);

    auto *licenseLabel = new QLabel(u"Licensed under the Apache License 2.0."_s, this);
    licenseLabel->setWordWrap(true);
    licenseLabel->setAlignment(Qt::AlignLeft);

    auto *repoLabel = new QLabel(
        QString(
            u"<a style=\"color:%1\" "
            u"href=\"https://github.com/UCC-Rocket-and-Space-Exploration/CosmoSoft\">"
            u"CosmoSoft project on GitHub</a>"_s)
            .arg(Theme::kAccentLink()),
        this);
    repoLabel->setObjectName(u"aboutRepositoryLink"_s);
    repoLabel->setOpenExternalLinks(true);
    repoLabel->setTextFormat(Qt::RichText);
    repoLabel->setTextInteractionFlags(
        Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    repoLabel->setFocusPolicy(Qt::StrongFocus);
    repoLabel->setAccessibleName(u"Open the CosmoSoft project repository on GitHub"_s);
    repoLabel->setAccessibleDescription(
        u"Opens the CosmoSoft source-code repository in the default web browser"_s);
    repoLabel->setWordWrap(true);
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

    setStyleSheet(
        QString(uR"(
        QDialog {
            background-color: %1;
            color: %2;
        }
        QLabel {
            color: %2;
            font-family: %3;
        }
        QLabel a {
            color: %4;
        }
        QLabel#aboutRepositoryLink:focus {
            border: 2px solid %5;
            border-radius: %6px;
            padding: 2px;
        }
    )"_s)
            .arg(Theme::kBgBase())
            .arg(Theme::kTextPrimary())
            .arg(Theme::kFontMono)
            .arg(Theme::kAccentLink())
            .arg(Theme::kFocusRing())
            .arg(Theme::kRadiusSm));
}

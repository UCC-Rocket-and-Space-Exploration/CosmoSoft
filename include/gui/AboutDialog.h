/**
 * @file AboutDialog.h
 * @brief Non-resizable dialog displaying application metadata.
 *
 * Shows the application name, version, license (Apache 2.0), and a clickable
 * link to the project's GitHub repository.  Triggered from Help → About in
 * the menu bar.
 */

#ifndef COSMO_SOFT_ABOUTDIALOG_H
#define COSMO_SOFT_ABOUTDIALOG_H

#include <QDialog>

/**
 * @class AboutDialog
 * @brief Displays CosmoSoft version, license, and project repository information.
 */
class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
    ~AboutDialog() override = default;
};

#endif // COSMO_SOFT_ABOUTDIALOG_H

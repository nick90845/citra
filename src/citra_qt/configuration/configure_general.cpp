// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QDesktopServices>
#include <QMessageBox>
#include <QUrl>
#include "citra_qt/configuration/configure_general.h"
#include "citra_qt/uisettings.h"
#include "core/core.h"
#include "core/settings.h"
#include "ui_configure_general.h"

ConfigureGeneral::ConfigureGeneral(QWidget* parent)
    : QWidget(parent), ui(new Ui::ConfigureGeneral) {

    ui->setupUi(this);
    SetConfiguration();

    connect(ui->toggle_frame_limit, &QCheckBox::toggled, ui->frame_limit, &QSpinBox::setVisible);
    connect(ui->button_reset_defaults, &QPushButton::clicked, this,
            &ConfigureGeneral::ResetDefaults);

    connect(ui->button_open_configuration_location, &QPushButton::clicked, this, [] {
        QString path = QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir));
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });
}

ConfigureGeneral::~ConfigureGeneral() = default;

void ConfigureGeneral::SetConfiguration() {
    // The first item is "auto-select" with actual value -1, so plus one here will do the trick
    ui->region_combobox->setCurrentIndex(Settings::values.region_value + 1);

    ui->toggle_frame_limit->setChecked(Settings::values.use_frame_limit);
    ui->frame_limit->setVisible(ui->toggle_frame_limit->isChecked());
    ui->frame_limit->setValue(Settings::values.frame_limit);
    ui->toggle_check_exit->setChecked(UISettings::values.confirm_before_closing);
    ui->toggle_background_pause->setChecked(UISettings::values.pause_when_in_background);
}

void ConfigureGeneral::ResetDefaults() {
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, "Citra", "Are you sure you want to <b>reset your settings</b> and close Citra?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (answer == QMessageBox::No) {
        return;
    }

    FileUtil::Delete(FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir) + "qt-config.ini");
    std::exit(0);
}

void ConfigureGeneral::ApplyConfiguration() {
    Settings::values.use_frame_limit = ui->toggle_frame_limit->isChecked();
    Settings::values.frame_limit = ui->frame_limit->value();
    UISettings::values.confirm_before_closing = ui->toggle_check_exit->isChecked();
    UISettings::values.pause_when_in_background = ui->toggle_background_pause->isChecked();
    Settings::values.region_value = ui->region_combobox->currentIndex() - 1;
    Settings::values.use_frame_limit = ui->toggle_frame_limit->isChecked();
    Settings::values.frame_limit = ui->frame_limit->value();
}

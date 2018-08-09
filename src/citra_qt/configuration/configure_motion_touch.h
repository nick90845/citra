// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <memory>
#include <QDialog>
#include "common/param_package.h"

namespace Ui {
class ConfigureMotionTouch;
}

class ConfigureMotionTouch : public QDialog {
    Q_OBJECT

public:
    explicit ConfigureMotionTouch(QWidget* parent = nullptr);
    ~ConfigureMotionTouch();

public slots:
    void applyConfiguration();

private slots:
    void OnCemuhookUDPTest();
    void OnConfigureTouchCalibration();

private:
    void closeEvent(QCloseEvent* event) override;
    Q_INVOKABLE void ShowUDPTestResult(bool result);
    Q_INVOKABLE void ShowCalibrationConfigureResult(bool result);
    void setConfiguration();
    void updateUiDisplay();
    void connectEvents();
    bool CanCloseDialog();

    std::unique_ptr<Ui::ConfigureMotionTouch> ui;

    // Coordinate system of the CemuhookUDP touch provider
    int min_x, min_y, max_x, max_y;

    bool udp_test_in_progress{}, calibration_config_in_progress{};
};

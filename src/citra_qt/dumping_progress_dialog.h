// Copyright 2019 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <functional>
#include <QDialog>

class QVBoxLayout;
class QLabel;
class QPushButton;

class DumpingProgressDialog final : public QDialog {
    Q_OBJECT

public:
    DumpingProgressDialog(QWidget* parent);

private:
    QVBoxLayout* layout;
    QLabel* label;
    QPushButton* cancel_button;
};

// Copyright 2019 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include "citra_qt/dumping_progress_dialog.h"

DumpingProgressDialog::DumpingProgressDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Dump Video"));
    setWindowFlags(Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint |
                   Qt::WindowSystemMenuHint | Qt::WindowCloseButtonHint);
    setWindowModality(Qt::WindowModal);

    layout = new QVBoxLayout(this);
    label = new QLabel(tr("Saving the dumped video...\n\nThis can take up to a "
                          "few minutes,\ndepending on your computer capabilities."));
    cancel_button = new QPushButton(tr("Cancel"));
    layout->addWidget(label);
    layout->addWidget(cancel_button);

    connect(cancel_button, &QPushButton::clicked, this, &QDialog::reject);
}

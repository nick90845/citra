// Copyright 2019 Citra Valentin Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QSettings>
#include "citra_qt/configuration/config.h"
#include "core/frontend/mic.h"

void Config::ReadAudioValues() {
    qt_config->beginGroup("Audio");
    Settings::values.enable_dsp_lle = ReadSetting(QStringLiteral("enable_dsp_lle"), false).toBool();
    Settings::values.enable_dsp_lle_multithread =
        ReadSetting(QStringLiteral("enable_dsp_lle_multithread"), false).toBool();
    Settings::values.sink_id = ReadSetting(QStringLiteral("output_engine"), QStringLiteral("auto"))
                                   .toString()
                                   .toStdString();
    Settings::values.enable_audio_stretching =
        ReadSetting(QStringLiteral("enable_audio_stretching"), true).toBool();
    Settings::values.audio_device_id =
        ReadSetting(QStringLiteral("output_device"), QStringLiteral("auto"))
            .toString()
            .toStdString();
    Settings::values.volume = ReadSetting(QStringLiteral("volume"), 1).toFloat();
    Settings::values.mic_input_type = static_cast<Settings::MicInputType>(
        ReadSetting(QStringLiteral("mic_input_type"), 0).toInt());
    Settings::values.mic_input_device =
        ReadSetting(QStringLiteral("mic_input_device"), Frontend::Mic::default_device_name)
            .toString()
            .toStdString();
    qt_config->endGroup();
}

void Config::SaveAudioValues() {
    qt_config->beginGroup(QStringLiteral("Audio"));
    WriteSetting(QStringLiteral("enable_dsp_lle"), Settings::values.enable_dsp_lle, false);
    WriteSetting(QStringLiteral("enable_dsp_lle_multithread"),
                 Settings::values.enable_dsp_lle_multithread, false);
    WriteSetting(QStringLiteral("output_engine"), QString::fromStdString(Settings::values.sink_id),
                 QStringLiteral("auto"));
    WriteSetting(QStringLiteral("enable_audio_stretching"),
                 Settings::values.enable_audio_stretching, true);
    WriteSetting(QStringLiteral("output_device"),
                 QString::fromStdString(Settings::values.audio_device_id), QStringLiteral("auto"));
    WriteSetting(QStringLiteral("volume"), Settings::values.volume, 1.0f);
    WriteSetting(QStringLiteral("mic_input_device"),
                 QString::fromStdString(Settings::values.mic_input_device),
                 Frontend::Mic::default_device_name);
    WriteSetting(QStringLiteral("mic_input_type"),
                 static_cast<int>(Settings::values.mic_input_type), 0);
    qt_config->endGroup();
}

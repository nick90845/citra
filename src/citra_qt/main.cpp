// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <clocale>
#include <memory>
#include <thread>
#include <QDesktopWidget>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QMessageBox>
#include <QOpenGLFunctions_3_3_Core>
#include <QSysInfo>
#include <QtConcurrent/QtConcurrentRun>
#include <QtGui>
#include <QtWidgets>
#include <fmt/format.h>
#ifdef _WIN32
#include <windows.h>
#endif
#include "citra_qt/aboutdialog.h"
#include "citra_qt/applets/mii_selector.h"
#include "citra_qt/applets/swkbd.h"
#include "citra_qt/bootmanager.h"
#ifdef _WIN32
#include "citra_qt/camera/dshow_camera.h"
#endif
#include "citra_qt/camera/qt_multimedia_camera.h"
#include "citra_qt/camera/still_image_camera.h"
#include "citra_qt/cheats.h"
#include "citra_qt/configuration/config.h"
#include "citra_qt/configuration/configure_dialog.h"
#include "citra_qt/debugger/console.h"
#include "citra_qt/debugger/graphics/graphics.h"
#include "citra_qt/debugger/graphics/graphics_breakpoints.h"
#include "citra_qt/debugger/graphics/graphics_cmdlists.h"
#include "citra_qt/debugger/graphics/graphics_surface.h"
#include "citra_qt/debugger/graphics/graphics_tracing.h"
#include "citra_qt/debugger/graphics/graphics_vertex_shader.h"
#include "citra_qt/debugger/ipc/recorder.h"
#include "citra_qt/debugger/lle_service_modules.h"
#include "citra_qt/debugger/profiler.h"
#include "citra_qt/debugger/registers.h"
#include "citra_qt/debugger/wait_tree.h"
#include "citra_qt/game_list.h"
#include "citra_qt/hotkeys.h"
#include "citra_qt/main.h"
#include "citra_qt/multiplayer/state.h"
#include "citra_qt/qt_image_interface.h"
#include "citra_qt/uisettings.h"
#include "citra_qt/util/clickable_label.h"
#include "common/common_paths.h"
#include "common/detached_tasks.h"
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/logging/filter.h"
#include "common/logging/log.h"
#include "common/logging/text_formatter.h"
#include "common/microprofile.h"
#include "common/scope_exit.h"
#include "common/version.h"
#ifdef ARCHITECTURE_x86_64
#include "common/x64/cpu_detect.h"
#endif
#include "citra_qt/game_list_p.h"
#include "core/core.h"
#include "core/dumping/backend.h"
#include "core/file_sys/archive_extsavedata.h"
#include "core/file_sys/archive_source_sd_savedata.h"
#include "core/frontend/applets/default_applets.h"
#include "core/frontend/scope_acquire_context.h"
#include "core/gdbstub/gdbstub.h"
#include "core/hle/service/fs/archive.h"
#include "core/hle/service/nfc/nfc.h"
#include "core/loader/loader.h"
#include "core/movie.h"
#include "core/settings.h"
#include "video_core/renderer_base.h"
#include "video_core/video_core.h"

#ifdef QT_STATICPLUGIN
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin);
#endif

#ifdef _WIN32
extern "C" {
// tells Nvidia drivers to use the dedicated GPU by default on laptops with switchable graphics
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
}
#endif

const int GMainWindow::max_recent_files_item;

static void InitializeLogging() {
    Log::Filter log_filter;
    log_filter.ParseFilterString(Settings::values.log_filter);
    Log::SetGlobalFilter(log_filter);

    const std::string& log_dir = FileUtil::GetUserPath(FileUtil::UserPath::LogDir);
    FileUtil::CreateFullPath(log_dir);
    Log::AddBackend(std::make_unique<Log::FileBackend>(log_dir + "citra-valentin-qt.log"));
#ifdef _WIN32
    Log::AddBackend(std::make_unique<Log::DebuggerBackend>());
#endif
}

GMainWindow::GMainWindow()
    : config(new Config()), emu_thread(nullptr)
#ifdef CITRA_ENABLE_DISCORD_RP
      ,
      discord_rp(Core::System::GetInstance())
#endif
{
    InitializeLogging();
    Debugger::ToggleConsole();
    Settings::LogSettings();

    // register types to use in slots and signals
    qRegisterMetaType<std::size_t>("std::size_t");
    qRegisterMetaType<Service::AM::InstallStatus>("Service::AM::InstallStatus");

    Pica::g_debug_context = Pica::DebugContext::Construct();
    setAcceptDrops(true);
    ui.setupUi(this);
    statusBar()->hide();

    default_theme_paths = QIcon::themeSearchPaths();
    UpdateUITheme();

    Network::Init();

    InitializeWidgets();
    InitializeDebugWidgets();
    InitializeRecentFileMenuActions();
    InitializeHotkeys();

    SetDefaultUIGeometry();
    RestoreUIState();

    ConnectWidgetEvents();
    ConnectMenuEvents();

    LOG_INFO(Frontend, "Version: {}.{}.{}", Version::major, Version::minor, Version::patch);
    LOG_INFO(Frontend, "Network Version: {}", Version::network);
    LOG_INFO(Frontend, "Movie Version: {}", Version::movie);
    LOG_INFO(Frontend, "Shader Cache Version: {}", Version::shader_cache);
#ifdef ARCHITECTURE_x86_64
    LOG_INFO(Frontend, "Host CPU: {}", Common::GetCPUCaps().cpu_string);
#endif
    LOG_INFO(Frontend, "Host OS: {}", QSysInfo::prettyProductName().toStdString());
    UpdateWindowTitle();

    show();

    game_list->PopulateAsync(UISettings::values.game_dirs);

    QStringList args = QApplication::arguments();
    if (args.length() >= 2) {
        BootGame(args[1]);
    }
}

GMainWindow::~GMainWindow() {
    // will get automatically deleted otherwise
    if (render_window->parent() == nullptr)
        delete render_window;

    Pica::g_debug_context.reset();
    Network::Shutdown();
}

void GMainWindow::InitializeWidgets() {
    render_window = new GRenderWindow(this, emu_thread.get());
    render_window->hide();

    game_list = new GameList(this);
    ui.horizontalLayout->addWidget(game_list);

    game_list_placeholder = new GameListPlaceholder(this);
    ui.horizontalLayout->addWidget(game_list_placeholder);
    game_list_placeholder->setVisible(false);

    multiplayer_state = new MultiplayerState(this, game_list->GetModel(), ui.action_Leave_Room,
                                             ui.action_Show_Room);
    multiplayer_state->setVisible(false);
#ifdef CITRA_ENABLE_DISCORD_RP
    connect(multiplayer_state, &MultiplayerState::RoomInformationChanged, this,
            [this] { discord_rp.Update(); });
#endif

    // Create status bar
    message_label = new QLabel();
    // Configured separately for left alignment
    message_label->setVisible(false);
    message_label->setFrameStyle(QFrame::NoFrame);
    message_label->setContentsMargins(4, 0, 4, 0);
    message_label->setAlignment(Qt::AlignLeft);
    statusBar()->addPermanentWidget(message_label, 1);

    progress_bar = new QProgressBar();
    progress_bar->hide();
    statusBar()->addPermanentWidget(progress_bar);

    emu_speed_label = new QLabel();
    emu_speed_label->setToolTip(
        "Frames per second and current emulation speed. Speed values higher or lower than 100% "
        "indicate emulation is running faster or slower than a 3DS.");

    emu_frametime_label = new QLabel();
    emu_frametime_label->setToolTip(
        "Time taken to emulate a 3DS frame, not counting framelimiting or v-sync.");

    for (auto& label : {emu_speed_label, emu_frametime_label}) {
        label->setVisible(false);
        label->setFrameStyle(QFrame::NoFrame);
        label->setContentsMargins(4, 0, 4, 0);
        statusBar()->addPermanentWidget(label, 0);
    }
    statusBar()->addPermanentWidget(multiplayer_state->GetStatusText(), 0);
    statusBar()->addPermanentWidget(multiplayer_state->GetStatusIcon(), 0);
    statusBar()->setVisible(true);

    // Removes an ugly inner border from the status bar widgets under Linux
    setStyleSheet("QStatusBar::item{border: none;}");

    QActionGroup* actionGroup_ScreenLayouts = new QActionGroup(this);
    actionGroup_ScreenLayouts->addAction(ui.action_Screen_Layout_Default);
    actionGroup_ScreenLayouts->addAction(ui.action_Screen_Layout_Single_Screen);
    actionGroup_ScreenLayouts->addAction(ui.action_Screen_Layout_Medium_Screen);
    actionGroup_ScreenLayouts->addAction(ui.action_Screen_Layout_Large_Screen);
    actionGroup_ScreenLayouts->addAction(ui.action_Screen_Layout_Side_by_Side);
}

void GMainWindow::InitializeDebugWidgets() {
    connect(ui.action_Create_Pica_Surface_Viewer, &QAction::triggered, this,
            &GMainWindow::OnCreateGraphicsSurfaceViewer);

    QMenu* debug_menu = ui.menu_View_Debugging;

#if MICROPROFILE_ENABLED
    microprofile_dialog = new MicroprofileDialog(this);
    microprofile_dialog->hide();
    debug_menu->addAction(microprofile_dialog->toggleViewAction());
#endif

    registers_widget = new RegistersWidget(this);
    addDockWidget(Qt::RightDockWidgetArea, registers_widget);
    registers_widget->hide();
    debug_menu->addAction(registers_widget->toggleViewAction());
    connect(this, &GMainWindow::EmulationStarting, registers_widget,
            &RegistersWidget::OnEmulationStarting);
    connect(this, &GMainWindow::EmulationStopping, registers_widget,
            &RegistersWidget::OnEmulationStopping);

    gpu_command_stream_widget = new GpuCommandStreamWidget(this);
    addDockWidget(Qt::RightDockWidgetArea, gpu_command_stream_widget);
    gpu_command_stream_widget->hide();
    debug_menu->addAction(gpu_command_stream_widget->toggleViewAction());

    gpu_command_list_widget = new GpuCommandListWidget(this);
    addDockWidget(Qt::RightDockWidgetArea, gpu_command_list_widget);
    gpu_command_list_widget->hide();
    debug_menu->addAction(gpu_command_list_widget->toggleViewAction());

    graphics_breakpoints_widget = new GraphicsBreakpointsWidget(Pica::g_debug_context, this);
    addDockWidget(Qt::RightDockWidgetArea, graphics_breakpoints_widget);
    graphics_breakpoints_widget->hide();
    debug_menu->addAction(graphics_breakpoints_widget->toggleViewAction());

    graphics_vertex_shader_widget = new GraphicsVertexShaderWidget(Pica::g_debug_context, this);
    addDockWidget(Qt::RightDockWidgetArea, graphics_vertex_shader_widget);
    graphics_vertex_shader_widget->hide();
    debug_menu->addAction(graphics_vertex_shader_widget->toggleViewAction());

    graphics_tracing_widget = new GraphicsTracingWidget(Pica::g_debug_context, this);
    addDockWidget(Qt::RightDockWidgetArea, graphics_tracing_widget);
    graphics_tracing_widget->hide();
    debug_menu->addAction(graphics_tracing_widget->toggleViewAction());
    connect(this, &GMainWindow::EmulationStarting, graphics_tracing_widget,
            &GraphicsTracingWidget::OnEmulationStarting);
    connect(this, &GMainWindow::EmulationStopping, graphics_tracing_widget,
            &GraphicsTracingWidget::OnEmulationStopping);

    wait_tree_widget = new WaitTreeWidget(this);
    addDockWidget(Qt::LeftDockWidgetArea, wait_tree_widget);
    wait_tree_widget->hide();
    debug_menu->addAction(wait_tree_widget->toggleViewAction());
    connect(this, &GMainWindow::EmulationStarting, wait_tree_widget,
            &WaitTreeWidget::OnEmulationStarting);
    connect(this, &GMainWindow::EmulationStopping, wait_tree_widget,
            &WaitTreeWidget::OnEmulationStopping);

    lle_service_modules_widget = new LleServiceModulesWidget(this);
    addDockWidget(Qt::RightDockWidgetArea, lle_service_modules_widget);
    lle_service_modules_widget->hide();
    debug_menu->addAction(lle_service_modules_widget->toggleViewAction());
    connect(this, &GMainWindow::EmulationStarting,
            [this] { lle_service_modules_widget->setDisabled(true); });
    connect(this, &GMainWindow::EmulationStopping, lle_service_modules_widget,
            [this] { lle_service_modules_widget->setDisabled(false); });

    ipc_recorder_widget = new IpcRecorderWidget(this);
    addDockWidget(Qt::RightDockWidgetArea, ipc_recorder_widget);
    ipc_recorder_widget->hide();
    debug_menu->addAction(ipc_recorder_widget->toggleViewAction());
    connect(this, &GMainWindow::EmulationStarting, ipc_recorder_widget,
            &IpcRecorderWidget::OnEmulationStarting);
}

void GMainWindow::InitializeRecentFileMenuActions() {
    for (int i = 0; i < max_recent_files_item; ++i) {
        actions_recent_files[i] = new QAction(this);
        actions_recent_files[i]->setVisible(false);
        connect(actions_recent_files[i], &QAction::triggered, this, &GMainWindow::OnMenuRecentFile);

        ui.menu_recent_files->addAction(actions_recent_files[i]);
    }
    ui.menu_recent_files->addSeparator();
    QAction* action_clear_recent_files = new QAction(this);
    action_clear_recent_files->setText(QStringLiteral("Clear Recent Files"));
    connect(action_clear_recent_files, &QAction::triggered, this, [this] {
        UISettings::values.recent_files.clear();
        UpdateRecentFiles();
    });
    ui.menu_recent_files->addAction(action_clear_recent_files);

    UpdateRecentFiles();
}

void GMainWindow::InitializeHotkeys() {
    hotkey_registry.LoadHotkeys();

    ui.action_Show_Filter_Bar->setShortcut(
        hotkey_registry.GetKeySequence("Main Window", "Toggle Filter Bar"));
    ui.action_Show_Filter_Bar->setShortcutContext(
        hotkey_registry.GetShortcutContext("Main Window", "Toggle Filter Bar"));

    ui.action_Show_Status_Bar->setShortcut(
        hotkey_registry.GetKeySequence("Main Window", "Toggle Status Bar"));
    ui.action_Show_Status_Bar->setShortcutContext(
        hotkey_registry.GetShortcutContext("Main Window", "Toggle Status Bar"));

    connect(hotkey_registry.GetHotkey("Main Window", "Load File", this), &QShortcut::activated,
            ui.action_Load_File, &QAction::trigger);
    connect(hotkey_registry.GetHotkey("Main Window", "Stop Emulation", this), &QShortcut::activated,
            ui.action_Stop, &QAction::trigger);
    connect(hotkey_registry.GetHotkey("Main Window", "Exit Citra", this), &QShortcut::activated,
            ui.action_Exit, &QAction::trigger);
    connect(hotkey_registry.GetHotkey("Main Window", "Continue/Pause Emulation", this),
            &QShortcut::activated, this, [&] {
                if (emulation_running) {
                    if (emu_thread->IsRunning()) {
                        OnPauseGame();
                    } else {
                        OnStartGame();
                    }
                }
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Restart Emulation", this),
            &QShortcut::activated, this, [this] {
                if (!Core::System::GetInstance().IsPoweredOn())
                    return;
                BootGame(QString(game_path));
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Swap Screens", render_window),
            &QShortcut::activated, ui.action_Screen_Layout_Swap_Screens, &QAction::trigger);
    connect(hotkey_registry.GetHotkey("Main Window", "Toggle Screen Layout", render_window),
            &QShortcut::activated, this, &GMainWindow::ToggleScreenLayout);
    connect(hotkey_registry.GetHotkey("Main Window", "Fullscreen", render_window),
            &QShortcut::activated, ui.action_Fullscreen, &QAction::trigger);
    connect(hotkey_registry.GetHotkey("Main Window", "Fullscreen", render_window),
            &QShortcut::activatedAmbiguously, ui.action_Fullscreen, &QAction::trigger);
    connect(hotkey_registry.GetHotkey("Main Window", "Exit Fullscreen", this),
            &QShortcut::activated, this, [&] {
                if (emulation_running) {
                    ui.action_Fullscreen->setChecked(false);
                    ToggleFullscreen();
                }
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Toggle Speed Limit", this),
            &QShortcut::activated, this, [&] {
                Settings::values.use_frame_limit = !Settings::values.use_frame_limit;
                UpdateStatusBar();
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Toggle Texture Dumping", this),
            &QShortcut::activated, this,
            [&] { Settings::values.dump_textures = !Settings::values.dump_textures; });
    // We use "static" here in order to avoid capturing by lambda due to a MSVC bug, which makes
    // the variable hold a garbage value after this function exits
    static constexpr u16 SPEED_LIMIT_STEP = 5;
    connect(hotkey_registry.GetHotkey("Main Window", "Increase Speed Limit", this),
            &QShortcut::activated, this, [&] {
                if (Settings::values.frame_limit < 9999 - SPEED_LIMIT_STEP) {
                    Settings::values.frame_limit += SPEED_LIMIT_STEP;
                    UpdateStatusBar();
                }
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Decrease Speed Limit", this),
            &QShortcut::activated, this, [&] {
                if (Settings::values.frame_limit > SPEED_LIMIT_STEP) {
                    Settings::values.frame_limit -= SPEED_LIMIT_STEP;
                    UpdateStatusBar();
                }
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Toggle Frame Advancing", this),
            &QShortcut::activated, ui.action_Enable_Frame_Advancing, &QAction::trigger);
    connect(hotkey_registry.GetHotkey("Main Window", "Advance Frame", this), &QShortcut::activated,
            ui.action_Advance_Frame, &QAction::trigger);
    connect(hotkey_registry.GetHotkey("Main Window", "Load Amiibo", this), &QShortcut::activated,
            this, [&] {
                if (ui.action_Load_Amiibo->isEnabled()) {
                    OnLoadAmiibo();
                }
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Remove Amiibo", this), &QShortcut::activated,
            this, [&] {
                if (ui.action_Remove_Amiibo->isEnabled()) {
                    OnRemoveAmiibo();
                }
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Capture Screenshot", this),
            &QShortcut::activated, this, [&] {
                if (emu_thread->IsRunning()) {
                    OnCaptureScreenshot();
                }
            });
    connect(hotkey_registry.GetHotkey("Main Window", "Toggle Custom Ticks", this),
            &QShortcut::activated, this, [this] {
                Settings::values.custom_ticks = !Settings::values.custom_ticks;
                statusBar()->showMessage(Settings::values.custom_ticks
                                             ? QStringLiteral("Custom Ticks: On")
                                             : QStringLiteral("Custom Ticks: Off"));
            });

    connect(hotkey_registry.GetHotkey("Main Window", "Auto Internal Resolution", this),
            &QShortcut::activated, this, [this] {
                Settings::values.resolution_factor = 0;
                statusBar()->showMessage(QStringLiteral("Internal Resolution: Auto"));
            });

    connect(hotkey_registry.GetHotkey("Main Window", "Native Internal Resolution", this),
            &QShortcut::activated, this, [this] {
                Settings::values.resolution_factor = 1;
                statusBar()->showMessage(QStringLiteral("Internal Resolution: Native"));
            });

    connect(hotkey_registry.GetHotkey("Main Window", "2x Native Internal Resolution", this),
            &QShortcut::activated, this, [this] {
                Settings::values.resolution_factor = 2;
                statusBar()->showMessage(QStringLiteral("Internal Resolution: 2x Native"));
            });

    connect(hotkey_registry.GetHotkey("Main Window", "3x Native Internal Resolution", this),
            &QShortcut::activated, this, [this] {
                Settings::values.resolution_factor = 3;
                statusBar()->showMessage(QStringLiteral("Internal Resolution: 3x Native"));
            });

    connect(hotkey_registry.GetHotkey("Main Window", "4x Native Internal Resolution", this),
            &QShortcut::activated, this, [this] {
                Settings::values.resolution_factor = 4;
                statusBar()->showMessage(QStringLiteral("Internal Resolution: 4x Native"));
            });

    connect(hotkey_registry.GetHotkey("Main Window", "5x Native Internal Resolution", this),
            &QShortcut::activated, this, [this] {
                Settings::values.resolution_factor = 5;
                statusBar()->showMessage(QStringLiteral("Internal Resolution: 5x Native"));
            });

    connect(hotkey_registry.GetHotkey("Main Window", "6x Native Internal Resolution", this),
            &QShortcut::activated, this, [this] {
                Settings::values.resolution_factor = 6;
                statusBar()->showMessage(QStringLiteral("Internal Resolution: 6x Native"));
            });

    connect(hotkey_registry.GetHotkey("Main Window", "7x Native Internal Resolution", this),
            &QShortcut::activated, this, [this] {
                Settings::values.resolution_factor = 7;
                statusBar()->showMessage(QStringLiteral("Internal Resolution: 7x Native"));
            });

    connect(hotkey_registry.GetHotkey("Main Window", "8x Native Internal Resolution", this),
            &QShortcut::activated, this, [this] {
                Settings::values.resolution_factor = 8;
                statusBar()->showMessage(QStringLiteral("Internal Resolution: 8x Native"));
            });

    connect(hotkey_registry.GetHotkey("Main Window", "9x Native Internal Resolution", this),
            &QShortcut::activated, this, [this] {
                Settings::values.resolution_factor = 9;
                statusBar()->showMessage(QStringLiteral("Internal Resolution: 9x Native"));
            });

    connect(hotkey_registry.GetHotkey("Main Window", "10x Native Internal Resolution", this),
            &QShortcut::activated, this, [this] {
                Settings::values.resolution_factor = 10;
                statusBar()->showMessage(QStringLiteral("Internal Resolution: 10x Native"));
            });
}

void GMainWindow::SetDefaultUIGeometry() {
    // geometry: 55% of the window contents are in the upper screen half, 45% in the lower half
    const QRect screenRect = QApplication::desktop()->screenGeometry(this);

    const int w = screenRect.width() * 2 / 3;
    const int h = screenRect.height() / 2;
    const int x = (screenRect.x() + screenRect.width()) / 2 - w / 2;
    const int y = (screenRect.y() + screenRect.height()) / 2 - h * 55 / 100;

    setGeometry(x, y, w, h);
}

void GMainWindow::RestoreUIState() {
    restoreGeometry(UISettings::values.geometry);
    restoreState(UISettings::values.state);
    render_window->restoreGeometry(UISettings::values.renderwindow_geometry);
#if MICROPROFILE_ENABLED
    microprofile_dialog->restoreGeometry(UISettings::values.microprofile_geometry);
    microprofile_dialog->setVisible(UISettings::values.microprofile_visible);
#endif
    ui.action_Cheats->setEnabled(false);

    game_list->LoadInterfaceLayout();

    ui.action_Single_Window_Mode->setChecked(UISettings::values.single_window_mode);
    ToggleWindowMode();

    ui.action_Fullscreen->setChecked(UISettings::values.fullscreen);
    SyncMenuUISettings();

    ui.action_Display_Dock_Widget_Headers->setChecked(UISettings::values.display_titlebar);
    OnDisplayTitleBars(ui.action_Display_Dock_Widget_Headers->isChecked());

    ui.action_Show_Filter_Bar->setChecked(UISettings::values.show_filter_bar);
    game_list->setFilterVisible(ui.action_Show_Filter_Bar->isChecked());

    ui.action_Show_Status_Bar->setChecked(UISettings::values.show_status_bar);
    statusBar()->setVisible(ui.action_Show_Status_Bar->isChecked());
}

void GMainWindow::OnAppFocusStateChanged(Qt::ApplicationState state) {
    if (!UISettings::values.pause_when_in_background) {
        return;
    }
    if (state != Qt::ApplicationHidden && state != Qt::ApplicationInactive &&
        state != Qt::ApplicationActive) {
        LOG_DEBUG(Frontend, "ApplicationState unusual flag: {} ", state);
    }
    if (ui.action_Pause->isEnabled() &&
        (state & (Qt::ApplicationHidden | Qt::ApplicationInactive))) {
        auto_paused = true;
        OnPauseGame();
    } else if (ui.action_Start->isEnabled() && auto_paused && state == Qt::ApplicationActive) {
        auto_paused = false;
        OnStartGame();
    }
}

void GMainWindow::ConnectWidgetEvents() {
    connect(game_list, &GameList::GameChosen, this, &GMainWindow::OnGameListLoadFile);
    connect(game_list, &GameList::OpenDirectory, this, &GMainWindow::OnGameListOpenDirectory);
    connect(game_list, &GameList::OpenFolderRequested, this, &GMainWindow::OnGameListOpenFolder);
    connect(game_list, &GameList::AddDirectory, this, &GMainWindow::OnGameListAddDirectory);
    connect(game_list_placeholder, &GameListPlaceholder::AddDirectory, this,
            &GMainWindow::OnGameListAddDirectory);
    connect(game_list, &GameList::ShowList, this, &GMainWindow::OnGameListShowList);
    connect(game_list, &GameList::PopulatingCompleted,
            [this] { multiplayer_state->UpdateGameList(game_list->GetModel()); });

    connect(this, &GMainWindow::EmulationStarting, render_window,
            &GRenderWindow::OnEmulationStarting);
    connect(this, &GMainWindow::EmulationStopping, render_window,
            &GRenderWindow::OnEmulationStopping);

    connect(&status_bar_update_timer, &QTimer::timeout, this, &GMainWindow::UpdateStatusBar);

    connect(this, &GMainWindow::UpdateProgress, this, &GMainWindow::OnUpdateProgress);
    connect(this, &GMainWindow::CIAInstallReport, this, &GMainWindow::OnCIAInstallReport);
    connect(this, &GMainWindow::CIAInstallFinished, this, &GMainWindow::OnCIAInstallFinished);
    connect(this, &GMainWindow::UpdateThemedIcons, multiplayer_state,
            &MultiplayerState::UpdateThemedIcons);
}

void GMainWindow::ConnectMenuEvents() {
    // File
    connect(ui.action_Load_File, &QAction::triggered, this, &GMainWindow::OnMenuLoadFile);
    connect(ui.action_Install_CIA, &QAction::triggered, this, &GMainWindow::OnMenuInstallCIA);
    connect(ui.action_Exit, &QAction::triggered, this, &QMainWindow::close);
    connect(ui.action_Load_Amiibo, &QAction::triggered, this, &GMainWindow::OnLoadAmiibo);
    connect(ui.action_Remove_Amiibo, &QAction::triggered, this, &GMainWindow::OnRemoveAmiibo);

    // Emulation
    connect(ui.action_Start, &QAction::triggered, this, &GMainWindow::OnStartGame);
    connect(ui.action_Pause, &QAction::triggered, this, &GMainWindow::OnPauseGame);
    connect(ui.action_Stop, &QAction::triggered, this, &GMainWindow::OnStopGame);
    connect(ui.action_Restart, &QAction::triggered, this, [this] { BootGame(QString(game_path)); });
    connect(ui.action_Configure, &QAction::triggered, this, &GMainWindow::OnConfigure);
    connect(ui.action_Cheats, &QAction::triggered, this, &GMainWindow::OnCheats);

    // View
    connect(ui.action_Single_Window_Mode, &QAction::triggered, this,
            &GMainWindow::ToggleWindowMode);
    connect(ui.action_Display_Dock_Widget_Headers, &QAction::triggered, this,
            &GMainWindow::OnDisplayTitleBars);
    connect(ui.action_Show_Filter_Bar, &QAction::triggered, this, &GMainWindow::OnToggleFilterBar);
    connect(ui.action_Show_Status_Bar, &QAction::triggered, statusBar(), &QStatusBar::setVisible);

    // Multiplayer
    connect(ui.action_View_Lobby, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnViewLobby);
    connect(ui.action_Start_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnCreateRoom);
    connect(ui.action_Leave_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnCloseRoom);
    connect(ui.action_Connect_To_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnDirectConnectToRoom);
    connect(ui.action_Show_Room, &QAction::triggered, multiplayer_state,
            &MultiplayerState::OnOpenNetworkRoom);

    ui.action_Fullscreen->setShortcut(
        hotkey_registry.GetHotkey("Main Window", "Fullscreen", this)->key());
    ui.action_Screen_Layout_Swap_Screens->setShortcut(
        hotkey_registry.GetHotkey("Main Window", "Swap Screens", this)->key());
    ui.action_Screen_Layout_Swap_Screens->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(ui.action_Fullscreen, &QAction::triggered, this, &GMainWindow::ToggleFullscreen);
    connect(ui.action_Screen_Layout_Default, &QAction::triggered, this,
            &GMainWindow::ChangeScreenLayout);
    connect(ui.action_Screen_Layout_Single_Screen, &QAction::triggered, this,
            &GMainWindow::ChangeScreenLayout);
    connect(ui.action_Screen_Layout_Medium_Screen, &QAction::triggered, this,
            &GMainWindow::ChangeScreenLayout);
    connect(ui.action_Screen_Layout_Large_Screen, &QAction::triggered, this,
            &GMainWindow::ChangeScreenLayout);
    connect(ui.action_Screen_Layout_Side_by_Side, &QAction::triggered, this,
            &GMainWindow::ChangeScreenLayout);
    connect(ui.action_Screen_Layout_Swap_Screens, &QAction::triggered, this,
            &GMainWindow::OnSwapScreens);

    // Movie
    connect(ui.action_Record_Movie, &QAction::triggered, this, &GMainWindow::OnRecordMovie);
    connect(ui.action_Play_Movie, &QAction::triggered, this, [this] { OnPlayMovie(""); });
    connect(ui.action_Stop_Recording_Playback, &QAction::triggered, this,
            &GMainWindow::OnStopRecordingPlayback);
    connect(ui.action_Enable_Frame_Advancing, &QAction::triggered, this, [this] {
        if (emulation_running) {
            Core::System::GetInstance().frame_limiter.SetFrameAdvancing(
                ui.action_Enable_Frame_Advancing->isChecked());
            ui.action_Advance_Frame->setEnabled(ui.action_Enable_Frame_Advancing->isChecked());
        }
    });
    connect(ui.action_Advance_Frame, &QAction::triggered, this, [this] {
        if (emulation_running) {
            ui.action_Enable_Frame_Advancing->setChecked(true);
            ui.action_Advance_Frame->setEnabled(true);
            Core::System::GetInstance().frame_limiter.AdvanceFrame();
        }
    });
    connect(ui.action_Capture_Screenshot, &QAction::triggered, this,
            &GMainWindow::OnCaptureScreenshot);

#ifndef ENABLE_FFMPEG_VIDEO_DUMPER
    ui.action_Dump_Video->setEnabled(false);
#endif
    connect(ui.action_Dump_Video, &QAction::triggered, [this] {
        if (ui.action_Dump_Video->isChecked()) {
            OnStartVideoDumping();
        } else {
            OnStopVideoDumping();
        }
    });

    // Help
    connect(ui.action_Open_Citra_Folder, &QAction::triggered, this,
            &GMainWindow::OnOpenCitraFolder);
#ifdef _WIN32
    connect(ui.action_Open_EXE_Location, &QAction::triggered, this, [] {
        QDesktopServices::openUrl(
            QUrl::fromLocalFile(QString::fromStdString(FileUtil::GetExeDirectory())));
    });
#else
    ui.action_Open_EXE_Location->setVisible(false);
#endif
    connect(ui.action_About, &QAction::triggered, this, &GMainWindow::OnMenuAboutCitraValentin);
}

void GMainWindow::OnDisplayTitleBars(bool show) {
    QList<QDockWidget*> widgets = findChildren<QDockWidget*>();

    if (show) {
        for (QDockWidget* widget : widgets) {
            QWidget* old = widget->titleBarWidget();
            widget->setTitleBarWidget(nullptr);
            if (old != nullptr) {
                delete old;
            }
        }
    } else {
        for (QDockWidget* widget : widgets) {
            QWidget* old = widget->titleBarWidget();
            widget->setTitleBarWidget(new QWidget());
            if (old != nullptr) {
                delete old;
            }
        }
    }
}

void GMainWindow::PreventOSSleep() {
#ifdef _WIN32
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
#endif
}

void GMainWindow::AllowOSSleep() {
#ifdef _WIN32
    SetThreadExecutionState(ES_CONTINUOUS);
#endif
}

bool GMainWindow::LoadROM(const QString& filename) {
    // Shutdown previous session if the emu thread is still active...
    if (emu_thread != nullptr) {
        ShutdownGame();
    }

    render_window->InitRenderTarget();

    Frontend::ScopeAcquireContext scope(*render_window);

    const QString below_gl33_title = QStringLiteral("OpenGL 3.3 Unsupported");
    const QString below_gl33_message =
        QStringLiteral("Your GPU may not support OpenGL 3.3, or you do not "
                       "have the latest graphics driver.");

    if (!QOpenGLContext::globalShareContext()->versionFunctions<QOpenGLFunctions_3_3_Core>()) {
        QMessageBox::critical(this, below_gl33_title, below_gl33_message);
        return false;
    }

    Core::System& system{Core::System::GetInstance()};

    const Core::System::ResultStatus result = system.Load(*render_window, filename.toStdString());

    if (result != Core::System::ResultStatus::Success) {
        switch (result) {
        case Core::System::ResultStatus::ErrorGetLoader:
            LOG_CRITICAL(Frontend, "Failed to obtain loader for {}!", filename.toStdString());
            QMessageBox::critical(
                this, QStringLiteral("Invalid ROM Format"),
                QStringLiteral(
                    "Your ROM format is not supported.<br/>Please follow the guides to redump your "
                    "<a href='https://citra-emu.org/wiki/dumping-game-cartridges/'>game "
                    "cartridges</a> or "
                    "<a href='https://citra-emu.org/wiki/dumping-installed-titles/'>installed "
                    "titles</a>."));
            break;

        case Core::System::ResultStatus::ErrorSystemMode:
            LOG_CRITICAL(Frontend, "Failed to load ROM!");
            QMessageBox::critical(
                this, QStringLiteral("ROM Corrupted"),
                QStringLiteral(
                    "Your ROM is corrupted. <br/>Please follow the guides to redump your "
                    "<a href='https://citra-emu.org/wiki/dumping-game-cartridges/'>game "
                    "cartridges</a> or "
                    "<a href='https://citra-emu.org/wiki/dumping-installed-titles/'>installed "
                    "titles</a>."));
            break;

        case Core::System::ResultStatus::ErrorLoader_ErrorEncrypted: {
            QMessageBox::critical(
                this, QStringLiteral("ROM Encrypted"),
                QStringLiteral(
                    "Your ROM is encrypted. <br/>Please follow the guides to redump your "
                    "<a href='https://citra-emu.org/wiki/dumping-game-cartridges/'>game "
                    "cartridges</a> or "
                    "<a href='https://citra-emu.org/wiki/dumping-installed-titles/'>installed "
                    "titles</a>."));
            break;
        }
        case Core::System::ResultStatus::ErrorLoader_ErrorInvalidFormat:
            QMessageBox::critical(
                this, QStringLiteral("Invalid ROM Format"),
                QStringLiteral(
                    "Your ROM format is not supported.<br/>Please follow the guides to redump your "
                    "<a href='https://citra-emu.org/wiki/dumping-game-cartridges/'>game "
                    "cartridges</a> or "
                    "<a href='https://citra-emu.org/wiki/dumping-installed-titles/'>installed "
                    "titles</a>."));
            break;

        case Core::System::ResultStatus::ErrorVideoCore:
            QMessageBox::critical(
                this, QStringLiteral("Video Core Error"),
                QStringLiteral(
                    "An error has occured. Please <a "
                    "href='https://community.citra-emu.org/t/how-to-upload-the-log-file/296'>see "
                    "the "
                    "log</a> for more details. "
                    "Ensure that you have the latest graphics drivers for your GPU."));
            break;

        case Core::System::ResultStatus::ErrorVideoCore_ErrorGenericDrivers:
            QMessageBox::critical(
                this, QStringLiteral("Video Core Error"),
                QStringLiteral(
                    "You are running default Windows drivers "
                    "for your GPU. You need to install the "
                    "proper drivers for your graphics card from the manufacturer's website."));
            break;

        case Core::System::ResultStatus::ErrorVideoCore_ErrorBelowGL33:
            QMessageBox::critical(this, below_gl33_title, below_gl33_message);
            break;

        default:
            QMessageBox::critical(
                this, QStringLiteral("Error while loading ROM!"),
                QStringLiteral("An unknown error occured. Please see the log for more details."));
            break;
        }
        return false;
    }

    std::string title;
    system.GetAppLoader().ReadTitle(title);
    game_title = QString::fromStdString(title);
    UpdateWindowTitle();

    game_path = filename;

    return true;
}

void GMainWindow::BootGame(const QString& filename) {
    if (filename.endsWith(".cia")) {
        const QMessageBox::StandardButton answer = QMessageBox::question(
            this, QStringLiteral("CIA must be installed before usage"),
            QStringLiteral(
                "Before using this CIA, you must install it. Do you want to install it now?"),
            QMessageBox::Yes | QMessageBox::No);

        if (answer == QMessageBox::Yes) {
            InstallCIA(QStringList(filename));
        }

        return;
    }

    LOG_INFO(Frontend, "Citra starting...");
    StoreRecentFile(filename); // Put the filename on top of the list

    if (movie_record_on_start) {
        Core::Movie::GetInstance().PrepareForRecording();
    }

    if (!LoadROM(filename)) {
        return;
    }

    // Create and start the emulation thread
    emu_thread = std::make_unique<EmuThread>(*render_window);
    emit EmulationStarting(emu_thread.get());
    emu_thread->start();

    connect(render_window, &GRenderWindow::Closed, this, &GMainWindow::OnStopGame);
    connect(render_window, &GRenderWindow::MiddleClick, ui.action_Fullscreen, &QAction::trigger);

    // BlockingQueuedConnection is important here, it makes sure we've finished refreshing our views
    // before the CPU continues
    connect(emu_thread.get(), &EmuThread::DebugModeEntered, registers_widget,
            &RegistersWidget::OnDebugModeEntered, Qt::BlockingQueuedConnection);
    connect(emu_thread.get(), &EmuThread::DebugModeEntered, wait_tree_widget,
            &WaitTreeWidget::OnDebugModeEntered, Qt::BlockingQueuedConnection);
    connect(emu_thread.get(), &EmuThread::DebugModeLeft, registers_widget,
            &RegistersWidget::OnDebugModeLeft, Qt::BlockingQueuedConnection);
    connect(emu_thread.get(), &EmuThread::DebugModeLeft, wait_tree_widget,
            &WaitTreeWidget::OnDebugModeLeft, Qt::BlockingQueuedConnection);

    // Update the GUI
    registers_widget->OnDebugModeEntered();
    if (ui.action_Single_Window_Mode->isChecked()) {
        game_list->hide();
        game_list_placeholder->hide();
    }
    status_bar_update_timer.start(2000);

    render_window->show();
    render_window->setFocus();

    emulation_running = true;
    if (ui.action_Fullscreen->isChecked()) {
        ShowFullscreen();
    }

    if (video_dumping_on_start) {
        Layout::FramebufferLayout layout{
            Layout::FrameLayoutFromResolutionScale(VideoCore::GetResolutionScaleFactor())};
        Core::System::GetInstance().VideoDumper().StartDumping(video_dumping_path.toStdString(),
                                                               "webm", layout);
        video_dumping_on_start = false;
        video_dumping_path.clear();
    }
    OnStartGame();

    qRegisterMetaType<Core::System::ResultStatus>("Core::System::ResultStatus");
    qRegisterMetaType<std::string>("std::string");
    connect(emu_thread.get(), &EmuThread::ErrorThrown, this, &GMainWindow::OnCoreError);

#ifdef CITRA_ENABLE_DISCORD_RP
    discord_rp.Update();
#endif
}

void GMainWindow::ShutdownGame() {
    if (!emulation_running) {
        return;
    }

    if (ui.action_Fullscreen->isChecked()) {
        HideFullscreen();
    }

    if (Core::System::GetInstance().VideoDumper().IsDumping()) {
        game_shutdown_delayed = true;
        OnStopVideoDumping();
        return;
    }

    AllowOSSleep();

    OnStopRecordingPlayback();
    emu_thread->RequestStop();

    // Release emu threads from any breakpoints
    // This belongs after RequestStop() and before wait() because if emulation stops on a GPU
    // breakpoint after (or before) RequestStop() is called, the emulation would never be able
    // to continue out to the main loop and terminate. Thus wait() would hang forever.
    // TODO(bunnei): This function is not thread safe, but it's being used as if it were
    Pica::g_debug_context->ClearBreakpoints();

    // Frame advancing must be cancelled in order to release the emu thread from waiting
    Core::System::GetInstance().frame_limiter.SetFrameAdvancing(false);

    emit EmulationStopping();

    // Wait for emulation thread to complete and delete it
    emu_thread->wait();
    emu_thread = nullptr;

    Camera::QtMultimediaCameraHandler::ReleaseHandlers();

    // The emulation is stopped, so closing the window or not does not matter anymore
    disconnect(render_window, &GRenderWindow::Closed, this, &GMainWindow::OnStopGame);

    disconnect(render_window, &GRenderWindow::MiddleClick, ui.action_Fullscreen, &QAction::trigger);

    // Update the GUI
    ui.action_Start->setEnabled(false);
    ui.action_Start->setText(QStringLiteral("Start"));
    ui.action_Pause->setEnabled(false);
    ui.action_Stop->setEnabled(false);
    ui.action_Restart->setEnabled(false);
    ui.action_Cheats->setEnabled(false);
    ui.action_Load_Amiibo->setEnabled(false);
    ui.action_Remove_Amiibo->setEnabled(false);
    ui.action_Enable_Frame_Advancing->setEnabled(false);
    ui.action_Enable_Frame_Advancing->setChecked(false);
    ui.action_Advance_Frame->setEnabled(false);
    ui.action_Capture_Screenshot->setEnabled(false);
    render_window->hide();
    if (game_list->isEmpty()) {
        game_list_placeholder->show();
    } else {
        game_list->show();
    }
    game_list->setFilterFocus();

    // Disable status bar updates
    status_bar_update_timer.stop();
    message_label->setVisible(false);
    emu_speed_label->setVisible(false);
    emu_frametime_label->setVisible(false);

    emulation_running = false;

    game_title.clear();
    UpdateWindowTitle();

    game_path.clear();

#ifdef CITRA_ENABLE_DISCORD_RP
    discord_rp.Update();
#endif
}

void GMainWindow::StoreRecentFile(const QString& filename) {
    UISettings::values.recent_files.prepend(filename);
    UISettings::values.recent_files.removeDuplicates();
    while (UISettings::values.recent_files.size() > max_recent_files_item) {
        UISettings::values.recent_files.removeLast();
    }

    UpdateRecentFiles();
}

void GMainWindow::UpdateRecentFiles() {
    const int num_recent_files =
        std::min(UISettings::values.recent_files.size(), max_recent_files_item);

    for (int i = 0; i < num_recent_files; i++) {
        const QString text = QString("&%1. %2").arg(i + 1).arg(
            QFileInfo(UISettings::values.recent_files[i]).fileName());
        actions_recent_files[i]->setText(text);
        actions_recent_files[i]->setData(UISettings::values.recent_files[i]);
        actions_recent_files[i]->setToolTip(UISettings::values.recent_files[i]);
        actions_recent_files[i]->setVisible(true);
    }

    for (int j = num_recent_files; j < max_recent_files_item; ++j) {
        actions_recent_files[j]->setVisible(false);
    }

    // Enable the recent files menu if the list isn't empty
    ui.menu_recent_files->setEnabled(num_recent_files != 0);
}

void GMainWindow::OnGameListLoadFile(QString game_path) {
    BootGame(game_path);
}

void GMainWindow::OnGameListOpenFolder(u64 data_id, GameListOpenTarget target) {
    std::string path;
    std::string open_target;

    switch (target) {
    case GameListOpenTarget::SAVE_DATA: {
        open_target = "Save Data";
        const std::string sdmc_dir = FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir);
        path = FileSys::ArchiveSource_SDSaveData::GetSaveDataPathFor(sdmc_dir, data_id);
        break;
    }
    case GameListOpenTarget::EXT_DATA: {
        open_target = "Extra Data";
        const std::string sdmc_dir = FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir);
        path = FileSys::GetExtDataPathFromId(sdmc_dir, data_id);
        break;
    }
    case GameListOpenTarget::APPLICATION: {
        open_target = "Application";
        const Service::FS::MediaType media_type = Service::AM::GetTitleMediaType(data_id);
        path = Service::AM::GetTitlePath(media_type, data_id) + "content/";
        break;
    }
    case GameListOpenTarget::UPDATE_DATA:
        open_target = "Update Data";
        path = Service::AM::GetTitlePath(Service::FS::MediaType::SDMC, data_id + 0xe00000000) +
               "content/";
        break;
    case GameListOpenTarget::TEXTURE_DUMP:
        open_target = "Dumped Textures";
        path = fmt::format("{}textures/{:016X}/",
                           FileUtil::GetUserPath(FileUtil::UserPath::DumpDir), data_id);
        break;
    case GameListOpenTarget::TEXTURE_LOAD:
        open_target = "Custom Textures";
        path = fmt::format("{}textures/{:016X}/",
                           FileUtil::GetUserPath(FileUtil::UserPath::LoadDir), data_id);
        break;
    default:
        LOG_ERROR(Frontend, "Unexpected target {}", static_cast<int>(target));
        return;
    }

    QString qpath = QString::fromStdString(path);

    QDir dir(qpath);
    if (!dir.exists()) {
        QMessageBox::critical(
            this,
            QStringLiteral("Error Opening %1 Folder").arg(QString::fromStdString(open_target)),
            QStringLiteral("Folder does not exist!"));
        return;
    }

    LOG_INFO(Frontend, "Opening {} path for data_id={:016x}", open_target, data_id);

    QDesktopServices::openUrl(QUrl::fromLocalFile(qpath));
}

void GMainWindow::OnGameListOpenDirectory(const QString& directory) {
    QString path;
    if (directory == "INSTALLED") {
        path = QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir) +
                                      "Nintendo "
                                      "3DS/00000000000000000000000000000000/"
                                      "00000000000000000000000000000000/title/00040000");
    } else if (directory == "SYSTEM") {
        path = QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::NANDDir) +
                                      "00000000000000000000000000000000/title/00040010");
    } else {
        path = directory;
    }
    if (!QFileInfo::exists(path)) {
        QMessageBox::critical(this, QStringLiteral("Error Opening %1").arg(path),
                              QStringLiteral("Folder does not exist!"));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void GMainWindow::OnGameListAddDirectory() {
    const QString dir_path =
        QFileDialog::getExistingDirectory(this, QStringLiteral("Select Directory"));
    if (dir_path.isEmpty()) {
        return;
    }
    UISettings::GameDir game_dir{dir_path, false, true};
    if (!UISettings::values.game_dirs.contains(game_dir)) {
        UISettings::values.game_dirs.append(game_dir);
        game_list->PopulateAsync(UISettings::values.game_dirs);
    } else {
        LOG_WARNING(Frontend, "Selected directory is already in the game list");
    }
}

void GMainWindow::OnGameListShowList(bool show) {
    if (emulation_running && ui.action_Single_Window_Mode->isChecked()) {
        return;
    }
    game_list->setVisible(show);
    game_list_placeholder->setVisible(!show);
};

void GMainWindow::OnMenuLoadFile() {
    const QString extensions =
        QString("*.").append(GameList::supported_file_extensions.join(" *."));
    const QString file_filter =
        QStringLiteral("3DS Executable (%1);;All Files (*.*)").arg(extensions);
    const QString filename =
        QFileDialog::getOpenFileName(this, "Load File", UISettings::values.roms_path, file_filter);
    if (filename.isEmpty()) {
        return;
    }
    UISettings::values.roms_path = QFileInfo(filename).path();
    BootGame(filename);
}

void GMainWindow::OnMenuInstallCIA() {
    const QStringList filepaths =
        QFileDialog::getOpenFileNames(this, "Load Files", UISettings::values.roms_path,
                                      "CTR Importable Archive (*.cia*);;All Files (*.*)");
    if (filepaths.isEmpty()) {
        return;
    }
    InstallCIA(filepaths);
}

void GMainWindow::InstallCIA(QStringList filepaths) {
    ui.action_Install_CIA->setEnabled(false);
    game_list->setDirectoryWatcherEnabled(false);
    progress_bar->show();
    progress_bar->setMaximum(INT_MAX);

    QtConcurrent::run([&, filepaths] {
        QString current_path;
        Service::AM::InstallStatus status;
        const auto cia_progress = [&](std::size_t written, std::size_t total) {
            emit UpdateProgress(written, total);
        };
        for (const QString current_path : filepaths) {
            status = Service::AM::InstallCIA(current_path.toStdString(), cia_progress);
            emit CIAInstallReport(status, current_path);
        }
        emit CIAInstallFinished();
    });
}

void GMainWindow::OnUpdateProgress(std::size_t written, std::size_t total) {
    progress_bar->setValue(
        static_cast<int>(INT_MAX * (static_cast<double>(written) / static_cast<double>(total))));
}

void GMainWindow::OnCIAInstallReport(Service::AM::InstallStatus status, QString filepath) {
    const QString filename = QFileInfo(filepath).fileName();

    switch (status) {
    case Service::AM::InstallStatus::Success:
        statusBar()->showMessage(
            QStringLiteral("%1 has been installed successfully.").arg(filename));
        break;
    case Service::AM::InstallStatus::ErrorFailedToOpenFile:
        QMessageBox::critical(this, "Unable to open File",
                              QStringLiteral("Could not open %1").arg(filename));
        break;
    case Service::AM::InstallStatus::ErrorAborted:
        QMessageBox::critical(
            this, "Installation aborted",
            QStringLiteral(
                "The installation of %1 was aborted. Please see the log for more details")
                .arg(filename));
        break;
    case Service::AM::InstallStatus::ErrorInvalid:
        QMessageBox::critical(this, "Invalid File",
                              QStringLiteral("%1 is not a valid CIA").arg(filename));
        break;
    case Service::AM::InstallStatus::ErrorEncrypted:
        QMessageBox::critical(
            this, "Encrypted File",
            QStringLiteral("%1 must be decrypted "
                           "before being used with Citra. A real 3DS is required.")
                .arg(filename));
        break;
    }
}

void GMainWindow::OnCIAInstallFinished() {
    progress_bar->hide();
    progress_bar->setValue(0);
    game_list->setDirectoryWatcherEnabled(true);
    ui.action_Install_CIA->setEnabled(true);
    game_list->PopulateAsync(UISettings::values.game_dirs);
}

void GMainWindow::OnMenuRecentFile() {
    const QAction* action = qobject_cast<QAction*>(sender());
    ASSERT(action);

    const QString filename = action->data().toString();
    if (QFileInfo::exists(filename)) {
        BootGame(filename);
    } else {
        // Display an error message and remove the file from the list.
        QMessageBox::information(this, QStringLiteral("File not found"),
                                 QStringLiteral("File \"%1\" not found").arg(filename));

        UISettings::values.recent_files.removeOne(filename);
        UpdateRecentFiles();
    }
}

void GMainWindow::OnStartGame() {
    Camera::QtMultimediaCameraHandler::ResumeCameras();

    if (movie_record_on_start) {
        Core::Movie::GetInstance().StartRecording(movie_record_path.toStdString());
        movie_record_on_start = false;
        movie_record_path.clear();
    }

    PreventOSSleep();

    emu_thread->SetRunning(true);

    ui.action_Start->setEnabled(false);
    ui.action_Start->setText(QStringLiteral("Continue"));

    ui.action_Pause->setEnabled(true);
    ui.action_Stop->setEnabled(true);
    ui.action_Restart->setEnabled(true);
    ui.action_Cheats->setEnabled(true);
    ui.action_Load_Amiibo->setEnabled(true);
    ui.action_Enable_Frame_Advancing->setEnabled(true);
    ui.action_Capture_Screenshot->setEnabled(true);
}

void GMainWindow::OnPauseGame() {
    emu_thread->SetRunning(false);
    Camera::QtMultimediaCameraHandler::StopCameras();
    ui.action_Start->setEnabled(true);
    ui.action_Pause->setEnabled(false);
    ui.action_Stop->setEnabled(true);
    ui.action_Capture_Screenshot->setEnabled(false);

    AllowOSSleep();
}

void GMainWindow::OnStopGame() {
    ShutdownGame();
}

void GMainWindow::ToggleFullscreen() {
    if (!emulation_running) {
        return;
    }

    if (ui.action_Fullscreen->isChecked()) {
        ShowFullscreen();
    } else {
        HideFullscreen();
    }
}

void GMainWindow::ShowFullscreen() {
    if (ui.action_Single_Window_Mode->isChecked()) {
        UISettings::values.geometry = saveGeometry();
        ui.menubar->hide();
        statusBar()->hide();
        showFullScreen();
    } else {
        UISettings::values.renderwindow_geometry = render_window->saveGeometry();
        render_window->showFullScreen();
    }
}

void GMainWindow::HideFullscreen() {
    if (ui.action_Single_Window_Mode->isChecked()) {
        statusBar()->setVisible(ui.action_Show_Status_Bar->isChecked());
        ui.menubar->show();
        showNormal();
        restoreGeometry(UISettings::values.geometry);
    } else {
        render_window->showNormal();
        render_window->restoreGeometry(UISettings::values.renderwindow_geometry);
    }
}

void GMainWindow::ToggleWindowMode() {
    if (ui.action_Single_Window_Mode->isChecked()) {
        // Render in the main window...
        render_window->BackupGeometry();
        ui.horizontalLayout->addWidget(render_window);
        render_window->setFocusPolicy(Qt::ClickFocus);
        if (emulation_running) {
            render_window->setVisible(true);
            render_window->setFocus();
            game_list->hide();
        }
    } else {
        // Render in a separate window...
        ui.horizontalLayout->removeWidget(render_window);
        render_window->setParent(nullptr);
        render_window->setFocusPolicy(Qt::NoFocus);
        if (emulation_running) {
            render_window->setVisible(true);
            render_window->RestoreGeometry();
            game_list->show();
        }
    }
}

void GMainWindow::ChangeScreenLayout() {
    Settings::LayoutOption new_layout = Settings::LayoutOption::Default;

    if (ui.action_Screen_Layout_Default->isChecked()) {
        new_layout = Settings::LayoutOption::Default;
    } else if (ui.action_Screen_Layout_Single_Screen->isChecked()) {
        new_layout = Settings::LayoutOption::SingleScreen;
    } else if (ui.action_Screen_Layout_Medium_Screen->isChecked()) {
        new_layout = Settings::LayoutOption::MediumScreen;
    } else if (ui.action_Screen_Layout_Large_Screen->isChecked()) {
        new_layout = Settings::LayoutOption::LargeScreen;
    } else if (ui.action_Screen_Layout_Side_by_Side->isChecked()) {
        new_layout = Settings::LayoutOption::SideScreen;
    }

    Settings::values.layout_option = new_layout;
    Settings::Apply();
}

void GMainWindow::ToggleScreenLayout() {
    Settings::LayoutOption new_layout = Settings::LayoutOption::Default;

    switch (Settings::values.layout_option) {
    case Settings::LayoutOption::Default:
        new_layout = Settings::LayoutOption::SingleScreen;
        break;
    case Settings::LayoutOption::SingleScreen:
        new_layout = Settings::LayoutOption::MediumScreen;
        break;
    case Settings::LayoutOption::MediumScreen:
        new_layout = Settings::LayoutOption::LargeScreen;
        break;
    case Settings::LayoutOption::LargeScreen:
        new_layout = Settings::LayoutOption::SideScreen;
        break;
    case Settings::LayoutOption::SideScreen:
        new_layout = Settings::LayoutOption::Default;
        break;
    }

    Settings::values.layout_option = new_layout;
    SyncMenuUISettings();
    Settings::Apply();
}

void GMainWindow::OnSwapScreens() {
    Settings::values.swap_screen = ui.action_Screen_Layout_Swap_Screens->isChecked();
    Settings::Apply();
}

void GMainWindow::OnCheats() {
    CheatDialog cheat_dialog(this);
    cheat_dialog.exec();
}

void GMainWindow::OnConfigure() {
    ConfigureDialog configureDialog(this, hotkey_registry,
                                    !multiplayer_state->IsHostingPublicRoom());
    const QString old_theme = UISettings::values.theme;
    const int old_input_profile_index = Settings::values.current_input_profile_index;
    const std::vector<Settings::InputProfile> old_input_profiles = Settings::values.input_profiles;
#ifdef CITRA_ENABLE_DISCORD_RP
    const bool enable_discord_rp = UISettings::values.enable_discord_rp;
#endif
    const int result = configureDialog.exec();
    if (result == QDialog::Accepted) {
        configureDialog.ApplyConfiguration();
        InitializeHotkeys();
        if (UISettings::values.theme != old_theme) {
            UpdateUITheme();
        }
        if (!multiplayer_state->IsHostingPublicRoom()) {
            multiplayer_state->UpdateCredentials();
        }
#ifdef CITRA_ENABLE_DISCORD_RP
        if (UISettings::values.enable_discord_rp != enable_discord_rp) {
            discord_rp.Update();
        }
#endif
        emit UpdateThemedIcons();
        SyncMenuUISettings();
        game_list->RefreshGameDirectory();
        config->Save();
    } else {
        Settings::values.input_profiles = old_input_profiles;
        Settings::LoadProfile(old_input_profile_index);
    }
}

void GMainWindow::OnLoadAmiibo() {
    const QString filename =
        QFileDialog::getOpenFileName(this, QStringLiteral("Load Amiibo"), QString(),
                                     QStringLiteral("Amiibo File (*.bin);; All Files (*.*)"));
    if (filename.isEmpty()) {
        return;
    }
    LoadAmiibo(filename);
}

void GMainWindow::LoadAmiibo(const QString& filename) {
    Service::SM::ServiceManager& sm = Core::System::GetInstance().ServiceManager();
    std::shared_ptr<Service::NFC::Module::Interface> nfc =
        sm.GetService<Service::NFC::Module::Interface>("nfc:u");
    if (nfc == nullptr) {
        return;
    }

    QFile nfc_file(filename);
    if (!nfc_file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(
            this, QStringLiteral("Error opening Amiibo data file"),
            QStringLiteral("Unable to open Amiibo file \"%1\" for reading.").arg(filename));
        return;
    }

    Service::NFC::AmiiboData amiibo_data{};
    const u64 read_size =
        nfc_file.read(reinterpret_cast<char*>(&amiibo_data), sizeof(Service::NFC::AmiiboData));
    if (read_size != sizeof(Service::NFC::AmiiboData)) {
        QMessageBox::warning(
            this, QStringLiteral("Error reading Amiibo data file"),
            QStringLiteral("Unable to fully read Amiibo data. Expected to read %1 bytes, but "
                           "was only able to read %2 bytes.")
                .arg(sizeof(Service::NFC::AmiiboData))
                .arg(read_size));
        return;
    }

    nfc->LoadAmiibo(amiibo_data);
    ui.action_Remove_Amiibo->setEnabled(true);
}

void GMainWindow::OnRemoveAmiibo() {
    Service::SM::ServiceManager& sm = Core::System::GetInstance().ServiceManager();
    std::shared_ptr<Service::NFC::Module::Interface> nfc =
        sm.GetService<Service::NFC::Module::Interface>("nfc:u");
    if (nfc == nullptr) {
        return;
    }
    nfc->RemoveAmiibo();

    ui.action_Remove_Amiibo->setEnabled(false);
}

void GMainWindow::OnOpenCitraFolder() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(
        QString::fromStdString(FileUtil::GetUserPath(FileUtil::UserPath::UserDir))));
}

void GMainWindow::OnToggleFilterBar() {
    game_list->setFilterVisible(ui.action_Show_Filter_Bar->isChecked());
    if (ui.action_Show_Filter_Bar->isChecked()) {
        game_list->setFilterFocus();
    } else {
        game_list->clearFilter();
    }
}

void GMainWindow::OnCreateGraphicsSurfaceViewer() {
    GraphicsSurfaceWidget* graphics_surface_viewer_widget =
        new GraphicsSurfaceWidget(Pica::g_debug_context, this);
    addDockWidget(Qt::RightDockWidgetArea, graphics_surface_viewer_widget);
    // TODO: Maybe graphics_surface_viewer_widget->setFloating(true);
    graphics_surface_viewer_widget->show();
}

void GMainWindow::OnRecordMovie() {
    if (emulation_running) {
        const QMessageBox::StandardButton answer = QMessageBox::warning(
            this, QStringLiteral("Record Movie"),
            QStringLiteral(
                "To keep consistency with the RNG, it is recommended to record the movie from game "
                "start.<br>Are you sure you still want to record movies now?"),
            QMessageBox::Yes | QMessageBox::No);
        if (answer == QMessageBox::No) {
            return;
        }
    }
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Record Movie"), UISettings::values.movie_record_path,
        QStringLiteral("Citra Valentin Movie (*.cvm)"));
    if (path.isEmpty()) {
        return;
    }
    UISettings::values.movie_record_path = QFileInfo(path).path();
    if (emulation_running) {
        Core::Movie::GetInstance().StartRecording(path.toStdString());
    } else {
        movie_record_on_start = true;
        movie_record_path = path;
        QMessageBox::information(this, QStringLiteral("Record Movie"),
                                 QStringLiteral("Recording will start once you boot a game."));
    }
    ui.action_Record_Movie->setEnabled(false);
    ui.action_Play_Movie->setEnabled(false);
    ui.action_Stop_Recording_Playback->setEnabled(true);
}

bool GMainWindow::ValidateMovie(const QString& path, u64 program_id) {
    const Core::Movie::ValidationResult result =
        Core::Movie::GetInstance().ValidateMovie(path.toStdString(), program_id);
    switch (result) {
    case Core::Movie::ValidationResult::GameDismatch: {
        const int answer = QMessageBox::question(
            this, QStringLiteral("Game Dismatch"),
            QStringLiteral(
                "The movie file you are trying to load was recorded with a different game."
                "<br/>The playback may not work as expected, and it may cause unexpected results."
                "<br/><br/>Are you sure you still want to load the movie file?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            return false;
        }
        break;
    }
    case Core::Movie::ValidationResult::Invalid: {
        QMessageBox::critical(
            this, QStringLiteral("Invalid Movie File"),
            QStringLiteral("The movie file you are trying to load is invalid."
                           "<br/>Either the file is corrupted, or the movie was created using a "
                           "official Citra build or a different Citra Valentin version."));
        return false;
    }
    default: { break; }
    }
    return true;
}

void GMainWindow::OnPlayMovie(const QString& filename) {
    if (emulation_running) {
        const QMessageBox::StandardButton answer = QMessageBox::warning(
            this, QStringLiteral("Play Movie"),
            QStringLiteral(
                "To keep consistency with the RNG, it is recommended to play the movie from game "
                "start.<br>Are you sure you still want to play movies now?"),
            QMessageBox::Yes | QMessageBox::No);
        if (answer == QMessageBox::No) {
            return;
        }
    }

    const QString path =
        filename.isEmpty()
            ? QFileDialog::getOpenFileName(this, QStringLiteral("Play Movie"),
                                           UISettings::values.movie_playback_path,
                                           QStringLiteral("Citra Valentin Movie (*.cvm)"))
            : filename;
    if (path.isEmpty()) {
        return;
    }
    UISettings::values.movie_playback_path = QFileInfo(path).path();

    if (emulation_running) {
        if (!ValidateMovie(path)) {
            return;
        }
    } else {
        const u64 program_id = Core::Movie::GetInstance().GetMovieProgramID(path.toStdString());
        if (program_id == 0) {
            QMessageBox::critical(this, QStringLiteral("Invalid Movie File"),
                                  QStringLiteral("The program ID is zero"));
            return;
        }
        const QString game_path = game_list->FindGameByProgramID(program_id);
        if (game_path.isEmpty()) {
            QMessageBox::warning(
                this, QStringLiteral("Game Not Found"),
                QStringLiteral("The movie you are trying to play is from a game that is not "
                               "in the game list. If you own the game, please add the game "
                               "folder to the game list and try to play the movie again."));
            return;
        }
        if (!ValidateMovie(path, program_id)) {
            return;
        }
        Core::Movie::GetInstance().PrepareForPlayback(path.toStdString());
        BootGame(game_path);
    }
    Core::Movie::GetInstance().StartPlayback(path.toStdString(), [this] {
        QMetaObject::invokeMethod(this, "OnMoviePlaybackCompleted");
    });
    ui.action_Record_Movie->setEnabled(false);
    ui.action_Play_Movie->setEnabled(false);
    ui.action_Stop_Recording_Playback->setEnabled(true);
}

void GMainWindow::OnStopRecordingPlayback() {
    if (movie_record_on_start) {
        QMessageBox::information(this, QStringLiteral("Record Movie"),
                                 QStringLiteral("Movie recording cancelled."));
        movie_record_on_start = false;
        movie_record_path.clear();
    } else {
        const bool was_recording = Core::Movie::GetInstance().IsRecordingInput();
        Core::Movie::GetInstance().Shutdown();
        if (was_recording) {
            QMessageBox::information(this, QStringLiteral("Movie Saved"),
                                     QStringLiteral("The movie is successfully saved."));
        }
    }
    ui.action_Record_Movie->setEnabled(true);
    ui.action_Play_Movie->setEnabled(true);
    ui.action_Stop_Recording_Playback->setEnabled(false);
}

void GMainWindow::OnCaptureScreenshot() {
    OnPauseGame();
    QFileDialog png_dialog(this, QStringLiteral("Capture Screenshot"),
                           UISettings::values.screenshot_path, QStringLiteral("PNG Image (*.png)"));
    png_dialog.setAcceptMode(QFileDialog::AcceptSave);
    png_dialog.setDefaultSuffix("png");
    if (png_dialog.exec()) {
        const QString path = png_dialog.selectedFiles().first();
        if (!path.isEmpty()) {
            UISettings::values.screenshot_path = QFileInfo(path).path();
            render_window->CaptureScreenshot(UISettings::values.screenshot_resolution_factor, path);
        }
    }
    OnStartGame();
}

void GMainWindow::OnStartVideoDumping() {
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Video"),
                                                      UISettings::values.video_dumping_path,
                                                      QStringLiteral("WebM Videos (*.webm)"));
    if (path.isEmpty()) {
        ui.action_Dump_Video->setChecked(false);
        return;
    }
    UISettings::values.video_dumping_path = QFileInfo(path).path();
    if (emulation_running) {
        Layout::FramebufferLayout layout{
            Layout::FrameLayoutFromResolutionScale(VideoCore::GetResolutionScaleFactor())};
        Core::System::GetInstance().VideoDumper().StartDumping(path.toStdString(), "webm", layout);
    } else {
        video_dumping_on_start = true;
        video_dumping_path = path;
    }
}

void GMainWindow::OnStopVideoDumping() {
    ui.action_Dump_Video->setChecked(false);

    if (video_dumping_on_start) {
        video_dumping_on_start = false;
        video_dumping_path.clear();
    } else {
        const bool was_dumping = Core::System::GetInstance().VideoDumper().IsDumping();
        if (!was_dumping)
            return;
        OnPauseGame();

        auto future =
            QtConcurrent::run([] { Core::System::GetInstance().VideoDumper().StopDumping(); });
        auto* future_watcher = new QFutureWatcher<void>(this);
        connect(future_watcher, &QFutureWatcher<void>::finished, this, [this] {
            if (game_shutdown_delayed) {
                game_shutdown_delayed = false;
                ShutdownGame();
            } else {
                OnStartGame();
            }
        });
        future_watcher->setFuture(future);
    }
}

void GMainWindow::UpdateStatusBar() {
    if (emu_thread == nullptr) {
        status_bar_update_timer.stop();
        return;
    }

    auto results = Core::System::GetInstance().GetAndResetPerfStats();

    if (Settings::values.use_frame_limit) {
        emu_speed_label->setText(QStringLiteral("%1 FPS (%2% / %3%)")
                                     .arg(results.game_fps, 0, 'f', 0)
                                     .arg(results.emulation_speed * 100.0, 0, 'f', 0)
                                     .arg(Settings::values.frame_limit));
    } else {
        emu_speed_label->setText(QStringLiteral("%1 FPS (%2%)")
                                     .arg(results.game_fps, 0, 'f', 0)
                                     .arg(results.emulation_speed * 100.0, 0, 'f', 0));
    }
    emu_frametime_label->setText(
        QStringLiteral("Frame: %1 ms").arg(results.frametime * 1000.0, 0, 'f', 2));

    emu_speed_label->setVisible(true);
    emu_frametime_label->setVisible(true);
}

void GMainWindow::OnCoreError(Core::System::ResultStatus result, std::string details) {
    QString status_message;

    QString title, message;
    if (result == Core::System::ResultStatus::ErrorSystemFiles) {
        const QString common_message = QStringLiteral(
            "%1 is missing. Please <a "
            "href='https://citra-emu.org/wiki/"
            "dumping-system-archives-and-the-shared-fonts-from-a-3ds-console/'>dump your "
            "system archives</a>.<br/>Continuing emulation may result in crashes and bugs.");

        if (!details.empty()) {
            message = common_message.arg(QString::fromStdString(details));
        } else {
            message = common_message.arg("A system archive");
        }

        title = QStringLiteral("System Archive Not Found");
        status_message = "System Archive Missing";
    } else {
        title = QStringLiteral("Fatal Error");
        message = QStringLiteral(
            "A fatal error occured. "
            "<a href='https://community.citra-emu.org/t/how-to-upload-the-log-file/296'>Check "
            "the log</a> for details."
            "<br/>Continuing emulation may result in crashes and bugs.");
        status_message = "Fatal Error encountered";
    }

    QMessageBox message_box;
    message_box.setWindowTitle(title);
    message_box.setText(message);
    message_box.setIcon(QMessageBox::Icon::Critical);
    message_box.addButton(QStringLiteral("Continue"), QMessageBox::RejectRole);
    QPushButton* abort_button =
        message_box.addButton(QStringLiteral("Abort"), QMessageBox::AcceptRole);
    if (result != Core::System::ResultStatus::ShutdownRequested)
        message_box.exec();

    if (result == Core::System::ResultStatus::ShutdownRequested ||
        message_box.clickedButton() == abort_button) {
        if (emu_thread) {
            ShutdownGame();
        }
    } else {
        // Only show the message if the game is still running.
        if (emu_thread) {
            emu_thread->SetRunning(true);
            message_label->setText(status_message);
            message_label->setVisible(true);
        }
    }
}

void GMainWindow::OnMenuAboutCitraValentin() {
    AboutDialog about{this};
    about.exec();
}

bool GMainWindow::ConfirmClose() {
    if (emu_thread == nullptr || !UISettings::values.confirm_before_closing) {
        return true;
    }

    return QMessageBox::question(this, QStringLiteral("Citra Valentin"),
                                 QStringLiteral("Would you like to exit now?"),
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No) != QMessageBox::No;
}

void GMainWindow::closeEvent(QCloseEvent* event) {
    if (!ConfirmClose()) {
        event->ignore();
        return;
    }

    if (!ui.action_Fullscreen->isChecked()) {
        UISettings::values.geometry = saveGeometry();
        UISettings::values.renderwindow_geometry = render_window->saveGeometry();
    }
    UISettings::values.state = saveState();
#if MICROPROFILE_ENABLED
    UISettings::values.microprofile_geometry = microprofile_dialog->saveGeometry();
    UISettings::values.microprofile_visible = microprofile_dialog->isVisible();
#endif
    UISettings::values.single_window_mode = ui.action_Single_Window_Mode->isChecked();
    UISettings::values.fullscreen = ui.action_Fullscreen->isChecked();
    UISettings::values.display_titlebar = ui.action_Display_Dock_Widget_Headers->isChecked();
    UISettings::values.show_filter_bar = ui.action_Show_Filter_Bar->isChecked();
    UISettings::values.show_status_bar = ui.action_Show_Status_Bar->isChecked();

    game_list->SaveInterfaceLayout();
    hotkey_registry.SaveHotkeys();

    // Shutdown session if the emu thread is active...
    if (emu_thread != nullptr) {
        ShutdownGame();
    }

    render_window->close();
    multiplayer_state->Close();
    QWidget::closeEvent(event);
}

static bool IsSingleFileDropEvent(const QMimeData* mime) {
    return mime->hasUrls() && mime->urls().length() == 1;
}

static const std::array<std::string, 9> ACCEPTED_EXTENSIONS = {"cci",  "cvm", "3ds", "cxi", "bin",
                                                               "3dsx", "app", "elf", "axf"};

static bool IsCorrectFileExtension(const QMimeData* mime) {
    const QString& filename = mime->urls().at(0).toLocalFile();
    return std::find(ACCEPTED_EXTENSIONS.begin(), ACCEPTED_EXTENSIONS.end(),
                     QFileInfo(filename).suffix().toStdString()) != ACCEPTED_EXTENSIONS.end();
}

static bool IsAcceptableDropEvent(QDropEvent* event) {
    return IsSingleFileDropEvent(event->mimeData()) && IsCorrectFileExtension(event->mimeData());
}

void GMainWindow::AcceptDropEvent(QDropEvent* event) {
    if (IsAcceptableDropEvent(event)) {
        event->setDropAction(Qt::DropAction::LinkAction);
        event->accept();
    }
}

bool GMainWindow::DropAction(QDropEvent* event) {
    if (!IsAcceptableDropEvent(event)) {
        return false;
    }

    const QMimeData* mime_data = event->mimeData();
    const QString& filename = mime_data->urls().at(0).toLocalFile();

    if (emulation_running && QFileInfo(filename).suffix() == "bin") {
        // Amiibo
        LoadAmiibo(filename);
    } else if (QFileInfo(filename).suffix() == "cvm") {
        // Movie
        OnPlayMovie(filename);
    } else {
        // Game
        if (ConfirmChangeGame()) {
            BootGame(filename);
        }
    }
    return true;
}

void GMainWindow::dropEvent(QDropEvent* event) {
    DropAction(event);
}

void GMainWindow::dragEnterEvent(QDragEnterEvent* event) {
    AcceptDropEvent(event);
}

void GMainWindow::dragMoveEvent(QDragMoveEvent* event) {
    AcceptDropEvent(event);
}

bool GMainWindow::ConfirmChangeGame() {
    if (emu_thread == nullptr) {
        return true;
    }

    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, "Citra", "The game is still running. Would you like to stop emulation?",
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return answer != QMessageBox::No;
}

void GMainWindow::filterBarSetChecked(bool state) {
    ui.action_Show_Filter_Bar->setChecked(state);
    emit(OnToggleFilterBar());
}

void GMainWindow::UpdateUITheme() {
    QStringList theme_paths(default_theme_paths);
    if (UISettings::values.theme != UISettings::themes[0].second &&
        !UISettings::values.theme.isEmpty()) {
        const QString theme_uri(":" + UISettings::values.theme + "/style.qss");
        QFile f(theme_uri);
        if (f.open(QFile::ReadOnly | QFile::Text)) {
            QTextStream ts(&f);
            qApp->setStyleSheet(ts.readAll());
            GMainWindow::setStyleSheet(ts.readAll());
        } else {
            LOG_ERROR(Frontend, "Unable to set style, stylesheet file not found");
        }
        theme_paths.append(QStringList{":/icons/default", ":/icons/" + UISettings::values.theme});
        QIcon::setThemeName(":/icons/" + UISettings::values.theme);
    } else {
        qApp->setStyleSheet("");
        GMainWindow::setStyleSheet("");
        theme_paths.append(QStringList{":/icons/default"});
        QIcon::setThemeName(":/icons/default");
    }
    QIcon::setThemeSearchPaths(theme_paths);
}

void GMainWindow::OnMoviePlaybackCompleted() {
    QMessageBox::information(this, "Playback Completed", "Movie playback completed.");
    ui.action_Record_Movie->setEnabled(true);
    ui.action_Play_Movie->setEnabled(true);
    ui.action_Stop_Recording_Playback->setEnabled(false);
}

void GMainWindow::UpdateWindowTitle() {
    if (game_title.isEmpty()) {
        setWindowTitle(QStringLiteral("Citra Valentin %1.%2.%3")
                           .arg(QString::number(Version::major), QString::number(Version::minor),
                                QString::number(Version::patch)));
    } else {
        setWindowTitle(QStringLiteral("Citra Valentin %1.%2.%3 | %4")
                           .arg(QString::number(Version::major), QString::number(Version::minor),
                                QString::number(Version::patch), game_title));
    }
}

void GMainWindow::SyncMenuUISettings() {
    ui.action_Screen_Layout_Default->setChecked(Settings::values.layout_option ==
                                                Settings::LayoutOption::Default);
    ui.action_Screen_Layout_Single_Screen->setChecked(Settings::values.layout_option ==
                                                      Settings::LayoutOption::SingleScreen);
    ui.action_Screen_Layout_Medium_Screen->setChecked(Settings::values.layout_option ==
                                                      Settings::LayoutOption::MediumScreen);
    ui.action_Screen_Layout_Large_Screen->setChecked(Settings::values.layout_option ==
                                                     Settings::LayoutOption::LargeScreen);
    ui.action_Screen_Layout_Side_by_Side->setChecked(Settings::values.layout_option ==
                                                     Settings::LayoutOption::SideScreen);
    ui.action_Screen_Layout_Swap_Screens->setChecked(Settings::values.swap_screen);
}

#ifdef main
#undef main
#endif

int main(int argc, char* argv[]) {
    Common::DetachedTasks detached_tasks;
    MicroProfileOnThreadCreate("Frontend");
    SCOPE_EXIT({ MicroProfileShutdown(); });

    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSwapInterval(1);
    // TODO: expose a setting for buffer value (ie default/single/double/triple)
    format.setSwapBehavior(QSurfaceFormat::DefaultSwapBehavior);
    QSurfaceFormat::setDefaultFormat(format);

    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_DontCheckOpenGLContextThreadAffinity);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);

    // Qt changes the locale and causes issues in float conversion using std::to_string() when
    // generating shaders
    setlocale(LC_ALL, "C");

    GMainWindow main_window;

    // Register CameraFactory
    Camera::RegisterFactory("image", std::make_unique<Camera::StillImageCameraFactory>());
    Camera::RegisterFactory("qt", std::make_unique<Camera::QtMultimediaCameraFactory>());
#ifdef _WIN32
    Camera::RegisterFactory("dshow", std::make_unique<Camera::DirectShowCameraFactory>());
#endif
    Camera::QtMultimediaCameraHandler::Init();

    // Register frontend applets
    Frontend::RegisterDefaultApplets();
    Core::System::GetInstance().RegisterMiiSelector(std::make_shared<QtMiiSelector>(main_window));
    Core::System::GetInstance().RegisterSoftwareKeyboard(std::make_shared<QtKeyboard>(main_window));

    // Register Qt image interface
    Core::System::GetInstance().RegisterImageInterface(std::make_shared<QtImageInterface>());

    main_window.show();

    QObject::connect(&app, &QGuiApplication::applicationStateChanged, &main_window,
                     &GMainWindow::OnAppFocusStateChanged);

    const int result = app.exec();
    detached_tasks.WaitForAllTasks();
    return result;
}

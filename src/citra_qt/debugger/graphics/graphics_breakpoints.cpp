// Copyright 2014 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <QLabel>
#include <QMetaType>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>
#include "citra_qt/debugger/graphics/graphics_breakpoints.h"
#include "citra_qt/debugger/graphics/graphics_breakpoints_p.h"
#include "common/assert.h"

BreakPointModel::BreakPointModel(std::shared_ptr<Pica::DebugContext> debug_context, QObject* parent)
    : QAbstractListModel(parent), context_weak(debug_context),
      at_breakpoint(debug_context->at_breakpoint),
      active_breakpoint(debug_context->active_breakpoint) {}

int BreakPointModel::columnCount(const QModelIndex& parent) const {
    return 1;
}

int BreakPointModel::rowCount(const QModelIndex& parent) const {
    return static_cast<int>(Pica::DebugContext::Event::NumEvents);
}

QVariant BreakPointModel::data(const QModelIndex& index, int role) const {
    const auto event = static_cast<Pica::DebugContext::Event>(index.row());

    switch (role) {
    case Qt::DisplayRole: {
        if (index.column() == 0) {
            return DebugContextEventToString(event);
        }
        break;
    }

    case Qt::CheckStateRole: {
        if (index.column() == 0)
            return data(index, Role_IsEnabled).toBool() ? Qt::Checked : Qt::Unchecked;
        break;
    }

    case Qt::BackgroundRole: {
        if (at_breakpoint && index.row() == static_cast<int>(active_breakpoint)) {
            return QBrush(QColor(0xE0, 0xE0, 0x10));
        }
        break;
    }

    case Role_IsEnabled: {
        auto context = context_weak.lock();
        return context && context->breakpoints[(int)event].enabled;
    }

    default:
        break;
    }
    return QVariant();
}

Qt::ItemFlags BreakPointModel::flags(const QModelIndex& index) const {
    if (!index.isValid())
        return 0;

    Qt::ItemFlags flags = Qt::ItemIsEnabled;
    if (index.column() == 0)
        flags |= Qt::ItemIsUserCheckable;
    return flags;
}

bool BreakPointModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    const auto event = static_cast<Pica::DebugContext::Event>(index.row());

    switch (role) {
    case Qt::CheckStateRole: {
        if (index.column() != 0)
            return false;

        auto context = context_weak.lock();
        if (!context)
            return false;

        context->breakpoints[(int)event].enabled = value == Qt::Checked;
        QModelIndex changed_index = createIndex(index.row(), 0);
        emit dataChanged(changed_index, changed_index);
        return true;
    }
    }

    return false;
}

void BreakPointModel::OnBreakPointHit(Pica::DebugContext::Event event) {
    auto context = context_weak.lock();
    if (!context)
        return;

    active_breakpoint = context->active_breakpoint;
    at_breakpoint = context->at_breakpoint;
    emit dataChanged(createIndex(static_cast<int>(event), 0),
                     createIndex(static_cast<int>(event), 0));
}

void BreakPointModel::OnResumed() {
    auto context = context_weak.lock();
    if (!context)
        return;

    at_breakpoint = context->at_breakpoint;
    emit dataChanged(createIndex(static_cast<int>(active_breakpoint), 0),
                     createIndex(static_cast<int>(active_breakpoint), 0));
    active_breakpoint = context->active_breakpoint;
}

QString BreakPointModel::DebugContextEventToString(Pica::DebugContext::Event event) {
    switch (event) {
    case Pica::DebugContext::Event::PicaCommandLoaded:
        return QStringLiteral("Pica command loaded");
    case Pica::DebugContext::Event::PicaCommandProcessed:
        return QStringLiteral("Pica command processed");
    case Pica::DebugContext::Event::IncomingPrimitiveBatch:
        return QStringLiteral("Incoming primitive batch");
    case Pica::DebugContext::Event::FinishedPrimitiveBatch:
        return QStringLiteral("Finished primitive batch");
    case Pica::DebugContext::Event::VertexShaderInvocation:
        return QStringLiteral("Vertex shader invocation");
    case Pica::DebugContext::Event::IncomingDisplayTransfer:
        return QStringLiteral("Incoming display transfer");
    case Pica::DebugContext::Event::GSPCommandProcessed:
        return QStringLiteral("GSP command processed");
    case Pica::DebugContext::Event::BufferSwapped:
        return QStringLiteral("Buffers swapped");
    case Pica::DebugContext::Event::NumEvents:
        break;
    }
    return QStringLiteral("Unknown debug context event");
}

GraphicsBreakpointsWidget::GraphicsBreakpointsWidget(
    std::shared_ptr<Pica::DebugContext> debug_context, QWidget* parent)
    : QDockWidget(QStringLiteral("Pica Breakpoints"), parent),
      Pica::DebugContext::BreakPointObserver(debug_context) {
    setObjectName(QStringLiteral("PicaBreakPointsWidget"));

    status_text = new QLabel(QStringLiteral("Emulation running"));
    resume_button = new QPushButton(QStringLiteral("Resume"));
    resume_button->setEnabled(false);

    breakpoint_model = new BreakPointModel(debug_context, this);
    breakpoint_list = new QTreeView;
    breakpoint_list->setRootIsDecorated(false);
    breakpoint_list->setHeaderHidden(true);
    breakpoint_list->setModel(breakpoint_model);

    qRegisterMetaType<Pica::DebugContext::Event>("Pica::DebugContext::Event");

    connect(breakpoint_list, &QTreeView::doubleClicked, this,
            &GraphicsBreakpointsWidget::OnItemDoubleClicked);

    connect(resume_button, &QPushButton::clicked, this,
            &GraphicsBreakpointsWidget::OnResumeRequested);

    connect(this, &GraphicsBreakpointsWidget::BreakPointHit, this,
            &GraphicsBreakpointsWidget::OnBreakPointHit, Qt::BlockingQueuedConnection);
    connect(this, &GraphicsBreakpointsWidget::Resumed, this, &GraphicsBreakpointsWidget::OnResumed);

    connect(this, &GraphicsBreakpointsWidget::BreakPointHit, breakpoint_model,
            &BreakPointModel::OnBreakPointHit, Qt::BlockingQueuedConnection);
    connect(this, &GraphicsBreakpointsWidget::Resumed, breakpoint_model,
            &BreakPointModel::OnResumed);

    connect(this, &GraphicsBreakpointsWidget::BreakPointsChanged,
            [this](const QModelIndex& top_left, const QModelIndex& bottom_right) {
                breakpoint_model->dataChanged(top_left, bottom_right);
            });

    QWidget* main_widget = new QWidget;
    auto main_layout = new QVBoxLayout;
    {
        auto sub_layout = new QHBoxLayout;
        sub_layout->addWidget(status_text);
        sub_layout->addWidget(resume_button);
        main_layout->addLayout(sub_layout);
    }
    main_layout->addWidget(breakpoint_list);
    main_widget->setLayout(main_layout);

    setWidget(main_widget);
}

void GraphicsBreakpointsWidget::OnPicaBreakPointHit(Event event, void* data) {
    // Process in GUI thread
    emit BreakPointHit(event, data);
}

void GraphicsBreakpointsWidget::OnBreakPointHit(Pica::DebugContext::Event event, void* data) {
    status_text->setText(QStringLiteral("Emulation halted at breakpoint"));
    resume_button->setEnabled(true);
}

void GraphicsBreakpointsWidget::OnPicaResume() {
    // Process in GUI thread
    emit Resumed();
}

void GraphicsBreakpointsWidget::OnResumed() {
    status_text->setText(QStringLiteral("Emulation running"));
    resume_button->setEnabled(false);
}

void GraphicsBreakpointsWidget::OnResumeRequested() {
    if (auto context = context_weak.lock())
        context->Resume();
}

void GraphicsBreakpointsWidget::OnItemDoubleClicked(const QModelIndex& index) {
    if (!index.isValid())
        return;

    QModelIndex check_index = breakpoint_list->model()->index(index.row(), 0);
    QVariant enabled = breakpoint_list->model()->data(check_index, Qt::CheckStateRole);
    QVariant new_state = Qt::Unchecked;
    if (enabled == Qt::Unchecked)
        new_state = Qt::Checked;
    breakpoint_list->model()->setData(check_index, new_state, Qt::CheckStateRole);
}

#include <QComboBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QModelIndex>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QToolTip>
#include <QVBoxLayout>

#include <QDebug>
#include <qpa/qplatformnativeinterface.h>

#include "widget.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    context = new Context;
    init_form();

    struct wl_display *display = NULL;

    QPlatformNativeInterface *native = QGuiApplication::platformNativeInterface();
    if (!native) {
        return;
    }

    display = reinterpret_cast<wl_display *>(
        native->nativeResourceForIntegration(QByteArrayLiteral("wl_display")));
    if (!display) {
        qWarning("Failed to get Wayland display.");
        return;
    }

    context->init(display, Context::Capability::Output | Context::Capability::Toplevel |
                               Context::Capability::Workspace);
}

MainWindow::~MainWindow()
{
    context->clean();
}

void MainWindow::init_workspace_widget(QWidget *widget)
{
    QGridLayout *layout = new QGridLayout(widget);

    tableWidget_0 = new QTableWidget();

    QStringList col_list_lable; // 列标题

    col_list_lable << QString("uuid");
    col_list_lable << QString("name");
    col_list_lable << QString("position");
    col_list_lable << QString("activated");
    int col_list_lable_cnt = col_list_lable.count();
    tableWidget_0->setColumnCount(col_list_lable_cnt);
    tableWidget_0->setHorizontalHeaderLabels(col_list_lable);

    tableWidget_0->setSelectionBehavior(QAbstractItemView::SelectRows); // 是否可选中
    tableWidget_0->setStyleSheet("selection-background-color:pink");
    tableWidget_0->setShowGrid(true);

    tableWidget_0->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

    QPushButton *add_btn = new QPushButton("add new");
    QPushButton *activited_btn = new QPushButton("set activited");
    QPushButton *move_up_btn = new QPushButton("move up");
    QPushButton *move_down_btn = new QPushButton("move down");
    QPushButton *delete_btn = new QPushButton("delete");

    add_btn->setFixedSize(100, 30);
    activited_btn->setFixedSize(100, 30);
    move_up_btn->setFixedSize(100, 30);
    move_down_btn->setFixedSize(100, 30);
    delete_btn->setFixedSize(100, 30);

    layout->addWidget(tableWidget_0, 0, 0, 5, 1);
    layout->addWidget(add_btn, 0, 1);
    layout->addWidget(activited_btn, 1, 1);
    layout->addWidget(move_up_btn, 2, 1);
    layout->addWidget(move_down_btn, 3, 1);
    layout->addWidget(delete_btn, 4, 1);

    layout->setColumnStretch(0, 8);
    layout->setColumnStretch(1, 2);

    widget->setLayout(layout);

    tableWidget_0->setMouseTracking(true);
    connect(tableWidget_0, SIGNAL(entered(QModelIndex)), this, SLOT(ShowTooltip(QModelIndex)));
    connect(add_btn, SIGNAL(clicked()), this, SLOT(add_btn_clicked()));
    connect(move_up_btn, SIGNAL(clicked()), this, SLOT(move_up_btn_clicked()));
    connect(move_down_btn, SIGNAL(clicked()), this, SLOT(move_down_btn_clicked()));
    connect(delete_btn, SIGNAL(clicked()), this, SLOT(delete_btn_clicked()));
    connect(activited_btn, SIGNAL(clicked()), this, SLOT(activited_btn_clicked()));
    workspace_count = 0;

    connect(context, &Context::workespaceIsAdded, this, &MainWindow::add_workspace_item);
}

void MainWindow::init_output_widget(QWidget *widget)
{
    QGridLayout *layout = new QGridLayout(widget);

    tableWidget_1 = new QTableWidget(widget);

    QStringList col_list_lable; // 列标题

    QLabel *label = new QLabel("Primary output :", widget);
    label->setFont(QFont("Helvetica"));

    pri_label = new QLabel(widget);
    pri_label->setStyleSheet("QLabel{background-color:rgb(192, 192, 192);}");
    pri_label->setAutoFillBackground(true);
    pri_label->setAlignment(Qt::AlignCenter);
    pri_label->setGeometry(102, 4, 100, 25);

    tableWidget_1->setGeometry(0, 30, 100, 100);

    col_list_lable << QString("uuid");
    col_list_lable << QString("name");
    col_list_lable << QString("make");
    col_list_lable << QString("model");
    col_list_lable << QString("serial");
    col_list_lable << QString("description");
    col_list_lable << QString("phisical size");
    col_list_lable << QString("capabilities");
    col_list_lable << QString("modes");
    col_list_lable << QString("current modes");
    col_list_lable << QString("position");
    col_list_lable << QString("transform");
    col_list_lable << QString("scale");
    col_list_lable << QString("enabled");
    col_list_lable << QString("power");
    col_list_lable << QString("brightness");
    col_list_lable << QString("color_temp");
    int col_list_lable_cnt = col_list_lable.count();
    tableWidget_1->setColumnCount(col_list_lable_cnt);
    tableWidget_1->setHorizontalHeaderLabels(col_list_lable);

    tableWidget_1->resize(940, 240);
    tableWidget_1->horizontalScrollBar()->setEnabled(true);
    tableWidget_1->setEditTriggers(QAbstractItemView::NoEditTriggers);  // 是否可编辑
    tableWidget_1->setSelectionBehavior(QAbstractItemView::SelectRows); // 是否可选中
    tableWidget_1->setStyleSheet("selection-background-color:pink");
    tableWidget_1->setShowGrid(true);

    tableWidget_1->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);

    layout->addWidget(label, 0, 0);
    layout->addWidget(pri_label, 0, 1);
    layout->addWidget(tableWidget_1, 1, 0, 10, 10);
    widget->setLayout(layout);
    outputs_count = 0;

    tableWidget_1->setMouseTracking(true);
    connect(tableWidget_1, SIGNAL(entered(QModelIndex)), this, SLOT(ShowTooltip(QModelIndex)));

    connect(context, &Context::outputIsAdded, this, &MainWindow::add_output_item);
}

void MainWindow::init_toplevel_widget(QWidget *widget)
{
    QVBoxLayout *layout = new QVBoxLayout();
    widget->setLayout(layout);

    tableWidget_2 = new QTableWidget;

    layout->addWidget(tableWidget_2);
    tableWidget_2->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tableWidget_2, SIGNAL(customContextMenuRequested(QPoint)), this,
            SLOT(show_menu(QPoint)));

    QStringList col_list_lable; // 列标题

    col_list_lable << QString("uuid");
    col_list_lable << QString("title");
    col_list_lable << QString("app_id");
    col_list_lable << QString("icon");
    col_list_lable << QString("capabilities");
    col_list_lable << QString("parent");
    col_list_lable << QString("primary output");
    col_list_lable << QString("activited");
    col_list_lable << QString("minimizad");
    col_list_lable << QString("maximized");
    col_list_lable << QString("fullscreen");
    col_list_lable << QString("workespaces");

    int col_list_lable_cnt = col_list_lable.count();
    tableWidget_2->setColumnCount(col_list_lable_cnt);
    tableWidget_2->setHorizontalHeaderLabels(col_list_lable);

    tableWidget_2->horizontalScrollBar()->setEnabled(true);
    tableWidget_2->setEditTriggers(QAbstractItemView::NoEditTriggers);  // 是否可编辑
    tableWidget_2->setSelectionBehavior(QAbstractItemView::SelectRows); // 是否可选中
    tableWidget_2->setStyleSheet("selection-background-color:pink");
    tableWidget_2->setShowGrid(true);

    tableWidget_2->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tableWidget_2->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    tableWidget_2->setMinimumSize(QSize(700, 500));
    toplevel_count = 0;

    tableWidget_2->setMouseTracking(true);
    connect(tableWidget_2, SIGNAL(entered(QModelIndex)), this, SLOT(ShowTooltip(QModelIndex)));
    connect(context, &Context::toplevelIsAdded, this, &MainWindow::add_toplevel_item);
}

void MainWindow::ShowTooltip(QModelIndex index)
{
    QToolTip::showText(QCursor::pos(), index.data().toString());
    return;
}

void MainWindow::init_form()
{
    QTabWidget *tabWidget = new QTabWidget(this);
    this->setCentralWidget(tabWidget);

    QWidget *pageWidget_0 = new QWidget;
    QWidget *pageWidget_1 = new QWidget;
    QWidget *pageWidget_2 = new QWidget;

    tabWidget->addTab(pageWidget_0, "workspace");
    tabWidget->addTab(pageWidget_1, "output");
    tabWidget->addTab(pageWidget_2, "toplevel");

    tabWidget->resize(940, 300);

    init_workspace_widget(pageWidget_0);
    init_output_widget(pageWidget_1);
    init_toplevel_widget(pageWidget_2);
}

static int get_cur_rows(QTableWidget *tableWidget, QString uuid)
{
    for (int i = 0; i < tableWidget->rowCount(); i++) {
        if (tableWidget->item(i, 0)->text() == uuid)
            return i;
    }
    return -1;
}

void MainWindow::update_output_item(Output::Masks mask)
{
    Output *output = qobject_cast<Output *>(sender());
    int cur_row = get_cur_rows(tableWidget_1, output->uuid());
    if (cur_row < 0)
        return;

    if (mask & Output::Mask::Enabled) {
        if (output->isEnable())
            tableWidget_1->item(cur_row, 13)->setText("yes");
        else
            tableWidget_1->item(cur_row, 13)->setText("no");
    }

    if (mask & Output::Mask::Mode) {
        Output::Mode cur_mode = output->curMode();
        QString cur_m = QString("%1 x %2 px,  %3 Hz")
                            .arg(cur_mode.size.width())
                            .arg(cur_mode.size.height())
                            .arg(cur_mode.refresh);
        if (cur_mode.preferred)
            cur_m += " preferred";
        tableWidget_1->item(cur_row, 9)->setText(cur_m);
    }

    if (mask & Output::Mask::Position) {
        QString position = QString("%1 , %2").arg(output->point().x()).arg(output->point().y());
        tableWidget_1->item(cur_row, 10)->setText(position);
    }

    if (mask & Output::Mask::Transform) {
        QString transform = QString("%1").arg(output->transform()) + "°";
        tableWidget_1->item(cur_row, 11)->setText(transform);
    }

    if (mask & Output::Mask::Scale)
        tableWidget_1->item(cur_row, 12)->setText(QString::number(output->scale(), 'f', 2));

    if (mask & Output::Mask::Power) {
        if (output->isPower())
            tableWidget_1->item(cur_row, 14)->setText("on");
        else
            tableWidget_1->item(cur_row, 14)->setText("off");
    }

    if (mask & Output::Mask::Primary) {
        if (output->isPrimary())
            pri_label->setText(output->name());
    }

    if (mask & Output::Mask::Brightness)
        tableWidget_1->item(cur_row, 15)->setText(QString("%1").arg(output->brightness()));

    if (mask & Output::Mask::ColorTemp)
        tableWidget_1->item(cur_row, 16)->setText(QString("%1").arg(output->colorTemp()));
}

void MainWindow::delete_output_item()
{
    Output *output = qobject_cast<Output *>(sender());
    int cur_row = get_cur_rows(tableWidget_1, output->uuid());
    if (cur_row < 0)
        return;

    tableWidget_1->removeRow(cur_row); // 删除
    delete output;
    outputs_count--;
}

void MainWindow::add_output_item(Output *output)
{
    int rows = tableWidget_1->rowCount();
    int columns = tableWidget_1->columnCount();
    if (outputs_count > (rows - 1))
        tableWidget_1->insertRow(outputs_count); // 插入

    for (int i = 0; i < columns; i++) {
        tableWidget_1->setItem(outputs_count, i, new QTableWidgetItem);
        tableWidget_1->setRowHeight(outputs_count, 30);
        tableWidget_1->item(outputs_count, i)->setTextAlignment(Qt::AlignCenter); // 居中显示
        tableWidget_1->item(outputs_count, i)->setFont(QFont("Helvetica")); // 设置字体为黑体
    }

    tableWidget_1->item(outputs_count, 0)->setText(output->uuid());
    tableWidget_1->item(outputs_count, 1)->setText(output->name());
    tableWidget_1->item(outputs_count, 2)->setText(output->make());
    tableWidget_1->item(outputs_count, 3)->setText(output->model());
    tableWidget_1->item(outputs_count, 4)->setText(output->serial());
    tableWidget_1->item(outputs_count, 5)->setText(output->description());

    QString phisical_size = QString("%1 x %2 mm")
                                .arg(output->physicalSize().width())
                                .arg(output->physicalSize().height());
    tableWidget_1->item(outputs_count, 6)->setText(phisical_size);

    QString caps;
    if (output->capabilities() & Output::Capability::Power)
        caps += "power ";
    if (output->capabilities() & Output::Capability::Brightness)
        caps += "brightness ";
    if (output->capabilities() & Output::Capability::ColorTemp)
        caps += "color_temp";
    tableWidget_1->item(outputs_count, 7)->setText(caps);

    QComboBox *comBox_mode = new QComboBox();
    QList<Output::Mode> modes = output->modes();
    for (int i = 0; i < modes.size(); i++) {
        QString mode = QString("%1 x %2 px,  %3 Hz")
                           .arg(modes[i].size.width())
                           .arg(modes[i].size.height())
                           .arg(modes[i].refresh);
        if (modes[i].preferred)
            mode += " preferred";
        comBox_mode->addItem(mode);
    }

    tableWidget_1->setCellWidget(outputs_count, 8, comBox_mode);
    if (output->isEnable())
        tableWidget_1->item(outputs_count, 13)->setText("yes");
    else
        tableWidget_1->item(outputs_count, 13)->setText("no");

    Output::Mode cur_mode = output->curMode();
    QString cur_m = QString("%1 x %2 px,  %3 Hz")
                        .arg(cur_mode.size.width())
                        .arg(cur_mode.size.height())
                        .arg(cur_mode.refresh);
    if (cur_mode.preferred)
        cur_m += " preferred";

    tableWidget_1->item(outputs_count, 9)->setText(cur_m);
    QString position = QString("%1 , %2").arg(output->point().x()).arg(output->point().y());
    tableWidget_1->item(outputs_count, 10)->setText(position);
    QString transform = QString("%1").arg(output->transform()) + "°";
    tableWidget_1->item(outputs_count, 11)->setText(transform);
    tableWidget_1->item(outputs_count, 12)->setText(QString::number(output->scale(), 'f', 2));
    if (output->isPower())
        tableWidget_1->item(outputs_count, 14)->setText("on");
    else
        tableWidget_1->item(outputs_count, 14)->setText("off");

    if (output->isPrimary())
        pri_label->setText(output->name());

    tableWidget_1->item(outputs_count, 15)->setText(QString("%1").arg(output->brightness()));
    tableWidget_1->item(outputs_count, 16)->setText(QString("%1").arg(output->colorTemp()));

    connect(output, &Output::stateUpdate, this, &MainWindow::update_output_item);
    connect(output, &Output::isDeleted, this, &MainWindow::delete_output_item);
    tableWidget_1->show();

    outputs_count++;
}

void MainWindow::delete_toplevel_item()
{
    Toplevel *toplevel = qobject_cast<Toplevel *>(sender());

    int cur_row = get_cur_rows(tableWidget_2, toplevel->uuid());
    if (cur_row < 0)
        return;
    tableWidget_2->removeRow(cur_row); // 删除
    delete toplevel;
    toplevel_count--;
}

void MainWindow::update_toplevel_item(Toplevel::Masks mask)
{
    Toplevel *toplevel = qobject_cast<Toplevel *>(sender());

    int cur_row = get_cur_rows(tableWidget_2, toplevel->uuid());
    if (cur_row < 0)
        return;

    if (mask & Toplevel::Mask::AppId)
        tableWidget_2->item(cur_row, 2)->setText(toplevel->appId());

    if (mask & Toplevel::Mask::Title)
        tableWidget_2->item(cur_row, 1)->setText(toplevel->title());

    if (mask & Toplevel::Mask::Activated) {
        if (toplevel->isActivated())
            tableWidget_2->item(cur_row, 7)->setText("true");
        else
            tableWidget_2->item(cur_row, 7)->setText("false");
    }

    if (mask & Toplevel::Mask::Minimized) {
        if (toplevel->isMinimized())
            tableWidget_2->item(cur_row, 8)->setText("true");
        else
            tableWidget_2->item(cur_row, 8)->setText("false");
    }

    if (mask & Toplevel::Mask::Maximized) {
        if (toplevel->isMaximized())
            tableWidget_2->item(cur_row, 9)->setText("true");
        else
            tableWidget_2->item(cur_row, 9)->setText("false");
    }

    if (mask & Toplevel::Mask::Fullscreen) {
        if (toplevel->isFullscreen())
            tableWidget_2->item(cur_row, 10)->setText("true");
        else
            tableWidget_2->item(cur_row, 10)->setText("false");
    }

    if (mask & Toplevel::Mask::PrimaryOutput)
        tableWidget_2->item(cur_row, 6)->setText(toplevel->primaryOutput());

    if (mask & Toplevel::Mask::Workspace) {
        QStringList workspaces = toplevel->workspaces();
        QComboBox *comBox_mode = new QComboBox();
        if (!workspaces.isEmpty()) {
            for (int i = 0; i < workspaces.size(); i++) {
                comBox_mode->addItem(workspaces.at(i));
            }
        }
        tableWidget_2->setCellWidget(cur_row, 11, comBox_mode);
    }

    if (mask & Toplevel::Mask::Parent) {
        Toplevel *parent = toplevel->parent();
        if (parent)
            tableWidget_2->item(cur_row, 5)->setText(parent->uuid());
        else
            tableWidget_2->item(cur_row, 5)->setText("NULL");
    }

    if (mask & Toplevel::Mask::Icon)
        tableWidget_2->item(cur_row, 3)->setText(toplevel->icon());
}

void MainWindow::add_toplevel_item(Toplevel *toplevel)
{
    int rows = tableWidget_2->rowCount();
    int columns = tableWidget_2->columnCount();
    if (toplevel_count > (rows - 1))
        tableWidget_2->insertRow(toplevel_count); // 插入

    for (int i = 0; i < columns; i++) {
        tableWidget_2->setItem(toplevel_count, i, new QTableWidgetItem);
        tableWidget_2->setRowHeight(toplevel_count, 30);
        tableWidget_2->item(toplevel_count, i)->setTextAlignment(Qt::AlignCenter); // 居中显示
        tableWidget_2->item(toplevel_count, i)->setFont(QFont("Helvetica")); // 设置字体为黑体
    }

    tableWidget_2->item(toplevel_count, 0)->setText(toplevel->uuid());
    QString caps;
    if (toplevel->capabilities() & Toplevel::Capability::Taskbar)
        caps += "taskbar ";
    if (toplevel->capabilities() & Toplevel::Capability::Switcher)
        caps += "switcher";
    tableWidget_2->item(toplevel_count, 4)->setText(caps);

    tableWidget_2->item(toplevel_count, 2)->setText(toplevel->appId());
    tableWidget_2->item(toplevel_count, 1)->setText(toplevel->title());

    if (toplevel->isActivated())
        tableWidget_2->item(toplevel_count, 7)->setText("true");
    else
        tableWidget_2->item(toplevel_count, 7)->setText("false");

    if (toplevel->isMinimized())
        tableWidget_2->item(toplevel_count, 8)->setText("true");
    else
        tableWidget_2->item(toplevel_count, 8)->setText("false");

    if (toplevel->isMaximized())
        tableWidget_2->item(toplevel_count, 9)->setText("true");
    else
        tableWidget_2->item(toplevel_count, 9)->setText("false");

    if (toplevel->isFullscreen())
        tableWidget_2->item(toplevel_count, 10)->setText("true");
    else
        tableWidget_2->item(toplevel_count, 10)->setText("false");

    tableWidget_2->item(toplevel_count, 6)->setText(toplevel->primaryOutput());

    QStringList workspaces = toplevel->workspaces();
    QComboBox *comBox_mode = new QComboBox();
    if (!workspaces.isEmpty()) {
        for (int i = 0; i < workspaces.size(); i++)
            comBox_mode->addItem(workspaces.at(i));
    }
    tableWidget_2->setCellWidget(toplevel_count, 11, comBox_mode);

    Toplevel *parent = toplevel->parent();
    if (parent)
        tableWidget_2->item(toplevel_count, 5)->setText(parent->uuid());
    else
        tableWidget_2->item(toplevel_count, 5)->setText("NULL");

    tableWidget_2->item(toplevel_count, 3)->setText(toplevel->icon());

    tableWidget_2->show();
    connect(toplevel, &Toplevel::stateUpdate, this, &MainWindow::update_toplevel_item);
    connect(toplevel, &Toplevel::isDeleted, this, &MainWindow::delete_toplevel_item);
    toplevel_count++;
}

void MainWindow::add_workspace_item(Workspace *workspace)
{
    int rows = tableWidget_0->rowCount();
    int columns = tableWidget_0->columnCount();
    if (workspace_count > (rows - 1))
        tableWidget_0->insertRow(workspace_count); // 插入

    for (int i = 0; i < columns; i++) {
        tableWidget_0->setItem(workspace_count, i, new QTableWidgetItem);
        tableWidget_0->setRowHeight(workspace_count, 30);
        tableWidget_0->item(workspace_count, i)->setTextAlignment(Qt::AlignCenter); // 居中显示
        tableWidget_0->item(workspace_count, i)->setFont(QFont("Helvetica")); // 设置字体为黑体
    }

    tableWidget_0->item(workspace_count, 0)->setText(workspace->uuid());
    tableWidget_0->item(workspace_count, 1)->setText(workspace->name());
    tableWidget_0->item(workspace_count, 2)->setText(QString("%1").arg(workspace->position()));

    if (workspace->isActivated())
        tableWidget_0->item(workspace_count, 3)->setText("true");
    else
        tableWidget_0->item(workspace_count, 3)->setText("false");

    connect(workspace, &Workspace::stateUpdate, this, &MainWindow::update_workspace_item);
    connect(workspace, &Workspace::isDeleted, this, &MainWindow::delete_workspace_item);
    workspace_count++;
}

void MainWindow::delete_workspace_item()
{
    Workspace *workspace = qobject_cast<Workspace *>(sender());
    int cur_row = get_cur_rows(tableWidget_0, workspace->uuid());
    if (cur_row < 0)
        return;

    tableWidget_0->removeRow(cur_row); // 删除
    delete workspace;
    workspace_count--;
}

void MainWindow::update_workspace_item(Workspace::Masks mask)
{
    Workspace *workspace = qobject_cast<Workspace *>(sender());
    int cur_row = get_cur_rows(tableWidget_0, workspace->uuid());
    if (cur_row < 0)
        return;

    if (mask & Workspace::Mask::Name)
        tableWidget_0->item(cur_row, 1)->setText(workspace->name());

    if (mask & Workspace::Mask::Position)
        tableWidget_0->item(cur_row, 2)->setText(QString("%1").arg(workspace->position()));

    if (mask & Workspace::Mask::Activated) {
        if (workspace->isActivated())
            tableWidget_0->item(cur_row, 3)->setText("true");
        else
            tableWidget_0->item(cur_row, 3)->setText("false");
    }
}

void MainWindow::move_up_btn_clicked()
{
    int cur_row = tableWidget_0->currentRow();
    int columns = tableWidget_0->columnCount();
    if (cur_row != 0) {
        tableWidget_0->insertRow(cur_row - 1);
        for (int i = 0; i < columns; ++i) {
            tableWidget_0->setItem(
                cur_row - 1, i, new QTableWidgetItem(tableWidget_0->item(cur_row + 1, i)->text()));
            tableWidget_0->item(cur_row - 1, i)->setTextAlignment(Qt::AlignCenter); // 居中显示
        }

        tableWidget_0->removeRow(cur_row + 1);
        tableWidget_0->selectRow(cur_row - 1);

        QString uuid = tableWidget_0->item(tableWidget_0->currentRow(), 0)->text();
        Workspace *workspace = context->findWorkspace(uuid);
        workspace->move(workspace->position() - 1);
    }
}

void MainWindow::move_down_btn_clicked()
{
    int cur_row = tableWidget_0->currentRow();
    int columns = tableWidget_0->columnCount();
    if (cur_row < (tableWidget_0->rowCount() - 1)) {
        tableWidget_0->insertRow(cur_row + 2); // rowi为选中行的索引
        for (int i = 0; i < columns; ++i) {
            tableWidget_0->setItem(cur_row + 2, i,
                                   new QTableWidgetItem(tableWidget_0->item(cur_row, i)->text()));
            tableWidget_0->item(cur_row + 2, i)->setTextAlignment(Qt::AlignCenter);
        }

        tableWidget_0->removeRow(cur_row);
        tableWidget_0->selectRow(cur_row + 1);

        QString uuid = tableWidget_0->item(tableWidget_0->currentRow(), 0)->text();
        Workspace *workspace = context->findWorkspace(uuid);
        workspace->move(workspace->position() + 1);
    }
}

void MainWindow::delete_btn_clicked()
{
    int cur_row = tableWidget_0->currentRow();
    if (cur_row < 0)
        return;

    QString uuid = tableWidget_0->item(cur_row, 0)->text();
    Workspace *workspace = context->findWorkspace(uuid);
    workspace->remove();
}

void MainWindow::activited_btn_clicked()
{
    int cur_row = tableWidget_0->currentRow();
    if (cur_row < 0)
        return;
    QString uuid = tableWidget_0->item(cur_row, 0)->text();
    Workspace *workspace = context->findWorkspace(uuid);
    workspace->setActivate();
}

void MainWindow::add_btn_clicked()
{
    context->addWorkspace(workspace_count);
}

void MainWindow::show_menu(const QPoint pos)
{
    // 设置菜单选项
    QMenu *menu = new QMenu(tableWidget_2);
    QAction *action1 = new QAction("set_maximized", tableWidget_2);
    QAction *action2 = new QAction("unset_maximized", tableWidget_2);
    QAction *action3 = new QAction("set_minimized", tableWidget_2);
    QAction *action4 = new QAction("unset_minimized", tableWidget_2);
    QAction *action5 = new QAction("set_fullscreen", tableWidget_2);
    QAction *action6 = new QAction("unset_fullscreen", tableWidget_2);
    QAction *action7 = new QAction("set_activate", tableWidget_2);
    QAction *action8 = new QAction("close", tableWidget_2);
    QAction *action9 = new QAction("enter_workspace", tableWidget_2);
    QAction *action10 = new QAction("leave_workspace", tableWidget_2);
    QAction *action11 = new QAction("toplevel_move_to_workspace", tableWidget_2);
    QAction *action12 = new QAction("send_to_output", tableWidget_2);

    menu->addAction(action1);
    menu->addAction(action2);
    menu->addAction(action3);
    menu->addAction(action4);
    menu->addAction(action5);
    menu->addAction(action6);
    menu->addAction(action7);
    menu->addAction(action8);
    menu->addAction(action9);
    menu->addAction(action10);
    menu->addAction(action11);
    menu->addAction(action12);

    menu->move(cursor().pos());
    menu->show();

    connect(action1, SIGNAL(triggered()), this, SLOT(toplevel_set_maximized()));
    connect(action2, SIGNAL(triggered()), this, SLOT(toplevel_unset_maximized()));
    connect(action3, SIGNAL(triggered()), this, SLOT(toplevel_set_minimized()));
    connect(action4, SIGNAL(triggered()), this, SLOT(toplevel_unset_minimized()));
    connect(action5, SIGNAL(triggered()), this, SLOT(toplevel_set_fullscreen()));
    connect(action6, SIGNAL(triggered()), this, SLOT(toplevel_unset_fullscreen()));
    connect(action7, SIGNAL(triggered()), this, SLOT(toplevel_set_activate()));
    connect(action8, SIGNAL(triggered()), this, SLOT(toplevel_close()));

    // 获得鼠标点击的x，y坐标点
    int x = pos.x();
    int y = pos.y();
    QModelIndex index = tableWidget_2->indexAt(QPoint(x, y));
    int row = index.row(); // 获得QTableWidget列表点击的行数

    QMenu *moreWorspace = new QMenu();
    for (int i = 0; i < tableWidget_0->rowCount(); i++) {
        QAction *action = new QAction(tableWidget_0->item(i, 1)->text());
        moreWorspace->addAction(action);
        connect(action, SIGNAL(triggered()), this, SLOT(toplevel_enter_workspace()));
    }

    QMenu *leaveWorspaces = new QMenu();
    for (int i = 0; i < tableWidget_0->rowCount(); i++) {
        QWidget *widget = tableWidget_2->cellWidget(row, 11);
        QComboBox *combox = (QComboBox *)widget;
        for (int j = 0; j < combox->count(); j++) {
            if (combox->itemText(j) == tableWidget_0->item(i, 0)->text()) {
                QAction *action = new QAction(tableWidget_0->item(i, 1)->text());
                leaveWorspaces->addAction(action);
                connect(action, SIGNAL(triggered()), this, SLOT(toplevel_leave_workspace()));
            }
        }
    }

    QMenu *moveWorspace = new QMenu();
    for (int i = 0; i < tableWidget_0->rowCount(); i++) {
        QAction *action = new QAction(tableWidget_0->item(i, 1)->text());
        moveWorspace->addAction(action);
        connect(action, SIGNAL(triggered()), this, SLOT(toplevel_move_to_workspace()));
    }

    QWidget *widget = tableWidget_2->cellWidget(row, 11);
    QComboBox *combox = (QComboBox *)widget;
    if (combox->count() != 0) {
        action9->setMenu(moreWorspace);
        action10->setMenu(leaveWorspaces);
        action11->setMenu(moveWorspace);
    }

    QMenu *moreOutput = new QMenu();
    for (int i = 0; i < tableWidget_1->rowCount(); i++) {
        QAction *action = new QAction(tableWidget_1->item(i, 1)->text());
        moreOutput->addAction(action);
        connect(action, SIGNAL(triggered()), this, SLOT(toplevel_send_to_output()));
    }
    action12->setMenu(moreOutput);
}

void MainWindow::toplevel_set_maximized()
{
    int row = tableWidget_2->currentRow();
    QString output = tableWidget_2->item(row, 6)->text();
    QString uuid = tableWidget_2->item(row, 0)->text();
    Toplevel *toplevel = context->findToplevel(uuid);
    toplevel->setMaximized(output);
}

void MainWindow::toplevel_unset_maximized()
{
    int row = tableWidget_2->currentRow();
    QString output = tableWidget_2->item(row, 6)->text();
    QString uuid = tableWidget_2->item(row, 0)->text();
    Toplevel *toplevel = context->findToplevel(uuid);
    toplevel->unsetMaximized();
}

void MainWindow::toplevel_set_minimized()
{
    int row = tableWidget_2->currentRow();
    QString uuid = tableWidget_2->item(row, 0)->text();
    Toplevel *toplevel = context->findToplevel(uuid);
    toplevel->setMinimized();
}

void MainWindow::toplevel_unset_minimized()
{
    int row = tableWidget_2->currentRow();
    QString uuid = tableWidget_2->item(row, 0)->text();
    Toplevel *toplevel = context->findToplevel(uuid);
    toplevel->unsetMinimized();
}

void MainWindow::toplevel_set_fullscreen()
{
    int row = tableWidget_2->currentRow();
    QString output = tableWidget_2->item(row, 6)->text();
    QString uuid = tableWidget_2->item(row, 0)->text();
    Toplevel *toplevel = context->findToplevel(uuid);
    toplevel->setFullscreen(output);
}

void MainWindow::toplevel_unset_fullscreen()
{
    int row = tableWidget_2->currentRow();
    QString uuid = tableWidget_2->item(row, 0)->text();
    Toplevel *toplevel = context->findToplevel(uuid);
    toplevel->unsetFullscreen();
}

void MainWindow::toplevel_set_activate()
{
    int row = tableWidget_2->currentRow();
    QString uuid = tableWidget_2->item(row, 0)->text();
    Toplevel *toplevel = context->findToplevel(uuid);
    toplevel->setActivate();
}

void MainWindow::toplevel_close()
{
    int row = tableWidget_2->currentRow();
    QString uuid = tableWidget_2->item(row, 0)->text();
    Toplevel *toplevel = context->findToplevel(uuid);
    toplevel->close();
}

void MainWindow::toplevel_enter_workspace()
{
    QAction *action = qobject_cast<QAction *>(sender());

    int row = tableWidget_2->currentRow();
    QString uuid = tableWidget_2->item(row, 0)->text();
    Toplevel *toplevel = context->findToplevel(uuid);

    for (int i = 0; i < tableWidget_0->rowCount(); i++) {
        if (tableWidget_0->item(i, 1)->text() == action->text()) {
            QString workspace = tableWidget_0->item(i, 0)->text();
            toplevel->enterWorkspace(workspace);
        }
    }
}

void MainWindow::toplevel_leave_workspace()
{
    QAction *action = qobject_cast<QAction *>(sender());

    int row = tableWidget_2->currentRow();
    QString uuid = tableWidget_2->item(row, 0)->text();
    Toplevel *toplevel = context->findToplevel(uuid);

    for (int i = 0; i < tableWidget_0->rowCount(); i++) {
        if (tableWidget_0->item(i, 1)->text() == action->text()) {
            QString workspace = tableWidget_0->item(i, 0)->text();
            toplevel->leaveWorkspace(workspace);
        }
    }
}

void MainWindow::toplevel_move_to_workspace()
{
    QAction *action = qobject_cast<QAction *>(sender());

    int row = tableWidget_2->currentRow();
    QString uuid = tableWidget_2->item(row, 0)->text();
    Toplevel *toplevel = context->findToplevel(uuid);

    for (int i = 0; i < tableWidget_0->rowCount(); i++) {
        if (tableWidget_0->item(i, 1)->text() == action->text()) {
            QString workspace = tableWidget_0->item(i, 0)->text();
            toplevel->moveToWorkspace(workspace);
        }
    }
}

void MainWindow::toplevel_send_to_output()
{
    QAction *action = qobject_cast<QAction *>(sender());

    int row = tableWidget_2->currentRow();
    QString uuid = tableWidget_2->item(row, 0)->text();
    Toplevel *toplevel = context->findToplevel(uuid);

    for (int i = 0; i < tableWidget_1->rowCount(); i++) {
        if (tableWidget_1->item(i, 1)->text() == action->text()) {
            QString output = tableWidget_1->item(i, 0)->text();
            toplevel->moveToOutput(output);
        }
    }
}
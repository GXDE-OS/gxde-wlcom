#ifndef WIDGET_H
#define WIDGET_H

#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QWidget>

#include "context.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

  public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

  private Q_SLOTS:
    void add_output_item(Output *output);
    void update_output_item(Output::Masks mask);
    void delete_output_item();

    void add_toplevel_item(Toplevel *toplevel);
    void update_toplevel_item(Toplevel::Masks mask);
    void delete_toplevel_item();

    void add_workspace_item(Workspace *workspace);
    void delete_workspace_item();
    void update_workspace_item(Workspace::Masks mask);

    void add_btn_clicked();
    void move_up_btn_clicked();
    void move_down_btn_clicked();
    void delete_btn_clicked();
    void activited_btn_clicked();

    void toplevel_set_maximized();
    void toplevel_unset_maximized();
    void toplevel_set_minimized();
    void toplevel_unset_minimized();
    void toplevel_set_fullscreen();
    void toplevel_unset_fullscreen();
    void toplevel_set_activate();
    void toplevel_close();
    void toplevel_enter_workspace();
    void toplevel_leave_workspace();
    void toplevel_send_to_output();
    void toplevel_move_to_workspace();

    void show_menu(const QPoint pos);
    void ShowTooltip(QModelIndex index);

    void show_thumbnail();

  private:
    Context *context = nullptr;
    QTableWidget *tableWidget_0 = nullptr;
    QTableWidget *tableWidget_1 = nullptr;
    QTableWidget *tableWidget_2 = nullptr;
    QLabel *pri_label = nullptr;
    int workspace_count;
    int toplevel_count;
    int outputs_count;
    void init_form();
    void init_workspace_widget(QWidget *widget);
    void init_output_widget(QWidget *widget);
    void init_toplevel_widget(QWidget *widget);

    void autoAdjustTableItemWidth();
    void autoAdjustTableItemHeight();
};
#endif // MAINWINDOW_H
#pragma once

#include <adwaita.h>
#include "util/core.h"
#include "util/mem.h"
#include "util/string.h"

// X(AppletInitFn, symbolic_icon_name, icon_on_bottom)
#define EACH_APPLET(X)\
    X(ui_todo_init, "kronomi-todo-symbolic", 0)\
    X(ui_alarm_init, "kronomi-alarm-symbolic", 0)\
    X(ui_timer_init, "kronomi-timer-symbolic", 0)\
    X(ui_pomodoro_init, "kronomi-pomodoro-symbolic", 0)\
    X(ui_stopwatch_init, "kronomi-stopwatch-symbolic", 0)\
    X(ui_flashcards_init, "kronomi-flashcards-symbolic", 0)\
    X(ui_manual_init, "kronomi-question-symbolic", 1)

struct AppletRoots {
    GtkWidget *icon;
    GtkWidget *box;
};

struct Applet {
    String name;
    AppletRoots roots;
};

typedef Applet *(AppletInitFn) (AppletRoots *);

struct MainContext {
    GtkWidget *window;
    GtkWidget *root_box;
    GtkWidget *applet_switch_box_top;
    GtkWidget *applet_switch_box_bottom;

    Applet *visible_applet;
    Array<Applet*> applets;

    String config_dir_path;

    String base_color;
    String accent_color;
    String error_color;
    String success_color;
    String warning_color;
};

extern MainContext main_context;

Int  ui_main        (Int argc, Char **argv);
Void ui_applet_show (Applet *);

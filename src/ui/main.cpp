#include "ui/main.h"
#include "ui/widgets.h"
#include "ui/markup.h"
#include "ui/util.h"
#include "util/core.h"
#include "util/string.h"
#include "util/mem.h"
#include "os/fs.h"

#include "ui/todo.h"
#include "ui/alarm.h"
#include "ui/timer.h"
#include "ui/pomodoro.h"
#include "ui/stopwatch.h"
#include "ui/flashcards.h"
#include "ui/manual.h"

MainContext main_context;

Void ui_applet_show (Applet *applet) {
    if (main_context.visible_applet) {
        gtk_widget_remove_css_class(main_context.visible_applet->roots.icon, "active");
        gtk_widget_set_visible(main_context.visible_applet->roots.box, false);
    }

    main_context.visible_applet = applet;
    gtk_widget_add_css_class(applet->roots.icon, "active");
    gtk_widget_set_visible(applet->roots.box, true);
}

static Void init_applet (AppletInitFn init, CString icon_name, Bool icon_on_bottom) {
    AppletRoots roots = {};

    roots.box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(main_context.root_box), roots.box);
    gtk_widget_set_visible(roots.box, false);

    roots.icon = ui_button_new(&mem_root, icon_name, 0, false, false, true)->widget;
    gtk_box_append(GTK_BOX(icon_on_bottom ? main_context.applet_switch_box_bottom : main_context.applet_switch_box_top), roots.icon);
    gtk_widget_add_css_class(roots.icon, "kronomi-applet-switcher-button");

    Applet *applet = init(&roots);
    array_iter (a, &main_context.applets) assert_always(! str_match(applet->name, a->name));
    array_push(&main_context.applets, applet);

    g_signal_connect(roots.icon, "clicked", G_CALLBACK(+[](GtkWidget*, Void *data){
        Auto applet = static_cast<Applet*>(data);
        ui_applet_show(applet);
    }), applet);
}

static Void init (GtkApplication *app, Void *) {
    GtkIconTheme *theme = gtk_icon_theme_get_for_display(gdk_display_get_default());
    // gtk_icon_theme_add_search_path(theme, "data/icons/");
    gtk_icon_theme_add_resource_path(theme, "/org/zagortenay/kronomi/data/icons");

    GtkCssProvider *css_provider = gtk_css_provider_new();
    gtk_css_provider_load_from_path(css_provider, "data/style.css");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(css_provider), GTK_STYLE_PROVIDER_PRIORITY_USER);
    g_object_unref(css_provider);

    main_context.window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(main_context.window), "Kronomi");
    gtk_window_set_default_size(GTK_WINDOW(main_context.window), 800, 600);
    gtk_widget_add_css_class(main_context.window, "window");

    main_context.root_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_window_set_child(GTK_WINDOW(main_context.window), main_context.root_box);

    GtkWidget *switch_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(main_context.root_box), switch_box);
    gtk_widget_add_css_class(switch_box, "kronomi-applet-switch-bar");
    gtk_widget_add_css_class(switch_box, "frame");

    main_context.applet_switch_box_top = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(switch_box), main_context.applet_switch_box_top);
    gtk_widget_set_vexpand(main_context.applet_switch_box_top, true);

    main_context.applet_switch_box_bottom = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(switch_box), main_context.applet_switch_box_bottom);

    { // Load some css colors:
        gtk_widget_add_css_class(main_context.root_box, "kronomi-error-color");
        GdkRGBA color;
        gtk_widget_get_color(main_context.root_box, &color);
        main_context.error_color = ui_rgba_to_string(&mem_root, &color);
        gtk_widget_remove_css_class(main_context.root_box, "kronomi-error-color");

        gtk_widget_add_css_class(main_context.root_box, "kronomi-accent-color");
        gtk_widget_get_color(main_context.root_box, &color);
        main_context.accent_color = ui_rgba_to_string(&mem_root, &color);
        gtk_widget_remove_css_class(main_context.root_box, "kronomi-accent-color");

        gtk_widget_add_css_class(main_context.root_box, "kronomi-warning-color");
        gtk_widget_get_color(main_context.root_box, &color);
        main_context.warning_color = ui_rgba_to_string(&mem_root, &color);
        gtk_widget_remove_css_class(main_context.root_box, "kronomi-warning-color");

        gtk_widget_add_css_class(main_context.root_box, "kronomi-success-color");
        gtk_widget_get_color(main_context.root_box, &color);
        main_context.success_color = ui_rgba_to_string(&mem_root, &color);
        gtk_widget_remove_css_class(main_context.root_box, "kronomi-success-color");

        gtk_widget_add_css_class(main_context.root_box, "kronomi-base-color");
        gtk_widget_get_color(main_context.root_box, &color);
        main_context.base_color = ui_rgba_to_string(&mem_root, &color);
        gtk_widget_remove_css_class(main_context.root_box, "kronomi-base-color");
    }

    { // Ensure ~/.config/kronomi dir exists:
        AString a = astr_new(&mem_root);
        astr_push_cstr(&a, g_get_home_dir());

        astr_push_cstr(&a, "/.config/");
        fs_make_dir(astr_to_str(&a));

        astr_push_cstr(&a, "/kronomi");
        fs_make_dir(astr_to_str(&a));

        main_context.config_dir_path = astr_to_str(&a);
    }

    { // Init applets:
        array_init(&main_context.applets, &mem_root);

        #define X(init_fn, icon_name, icon_on_bottom) init_applet(init_fn, icon_name, icon_on_bottom );
            EACH_APPLET(X)
        #undef X

        ui_applet_show(array_get(&main_context.applets, 0));
    }

    gtk_window_present(GTK_WINDOW(main_context.window));
}

Int ui_main (Int argc, Char **argv) {
    Auto app = gtk_application_new("org.zagortenay.kronomi", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(init), 0);
    Int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}

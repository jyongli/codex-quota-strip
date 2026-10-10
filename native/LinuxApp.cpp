#include "NativeCore.h"
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <glib-unix.h>
#include <libayatana-appindicator/app-indicator.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <cairo.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <unistd.h>
#include <signal.h>
#include <iostream>

using namespace nativequota;
namespace {
// 窗口可能在读取属性前已关闭，X11 异步错误不应结束配额程序。
int IgnoreStaleWindow(Display*, XErrorEvent*) { return 0; }
struct App {
    Options options;
    Engine engine;
    GtkWidget *strip = nullptr, *stripText = nullptr, *config = nullptr, *status = nullptr;
    GtkWidget *seconds = nullptr, *stale = nullptr, *online = nullptr, *only = nullptr, *show = nullptr, *side = nullptr, *home = nullptr, *binary = nullptr, *hostName = nullptr;
    AppIndicator* indicators[2]{};
    std::filesystem::path configRoot, cache;
    Display* display = nullptr;
    ::Window root = 0, hostWindow = 0;
    Atom activeAtom = 0, clientsAtom = 0, pidAtom = 0;
    guint source = 0;
    bool closing = false, x11 = false, testPassed = false;
    int iconGeneration = 0, testStage = 0;
    ::Window testHost = 0, testOther = 0;
    std::string lastSummary;
    explicit App(Options value) : options(std::move(value)), engine(options.preferences) {
        configRoot = options.smoke ? std::filesystem::temp_directory_path() / ("quota-linux-smoke-" + std::to_string(getpid())) : ConfigDirectory();
        cache = std::filesystem::path(g_get_user_cache_dir()) / "codex-quota-strip" / std::to_string(getpid());
        std::filesystem::create_directories(cache); chmod(cache.c_str(), 0700);
    }
    ~App() {
        closing = true; engine.stop();
        if (source) g_source_remove(source);
        if (display) XCloseDisplay(display);
        for (auto indicator : indicators) if (indicator) { app_indicator_set_status(indicator, APP_INDICATOR_STATUS_PASSIVE); g_object_unref(indicator); }
        for (int metric = 0; metric < 2; ++metric) for (int generation = 0; generation < 2; ++generation) std::filesystem::remove(cache / (std::to_string(metric) + "-" + std::to_string(generation) + ".png"));
        std::filesystem::remove(cache);
        if (options.smoke) { std::filesystem::remove(configRoot / "settings.ini"); std::filesystem::remove(configRoot / "instance.lock"); std::filesystem::remove(configRoot); }
    }
    static void Refresh(GtkMenuItem*, gpointer data) { static_cast<App*>(data)->engine.refresh(); }
    static void Settings(GtkMenuItem*, gpointer data) { static_cast<App*>(data)->openSettings(); }
    static void Quit(GtkMenuItem*, gpointer data) { static_cast<App*>(data)->closing = true; gtk_main_quit(); }
    static gboolean Update(gpointer data) { auto& app = *static_cast<App*>(data); if (!app.closing) app.paint(); return G_SOURCE_REMOVE; }
    static gboolean StaleTick(gpointer data) { auto& app = *static_cast<App*>(data); if (!app.closing) app.paint(); return !app.closing; }
    GtkWidget* menu() {
        auto menu = gtk_menu_new();
        for (auto item : {std::pair<const char*, GCallback>{"Refresh now", G_CALLBACK(Refresh)}, {"Settings…", G_CALLBACK(Settings)}, {"Quit", G_CALLBACK(Quit)}}) {
            auto entry = gtk_menu_item_new_with_label(item.first); g_signal_connect(entry, "activate", item.second, this); gtk_menu_shell_append(GTK_MENU_SHELL(menu), entry);
        }
        gtk_widget_show_all(menu); return menu;
    }
    void initialize() {
        strip = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_decorated(GTK_WINDOW(strip), FALSE); gtk_window_set_resizable(GTK_WINDOW(strip), FALSE);
        gtk_window_set_skip_taskbar_hint(GTK_WINDOW(strip), TRUE); gtk_window_set_skip_pager_hint(GTK_WINDOW(strip), TRUE);
        gtk_window_set_accept_focus(GTK_WINDOW(strip), FALSE); gtk_window_set_focus_on_map(GTK_WINDOW(strip), FALSE);
        gtk_window_set_keep_above(GTK_WINDOW(strip), TRUE); gtk_window_set_title(GTK_WINDOW(strip), "Codex Quota Strip");
        stripText = gtk_label_new("5h -   W -"); gtk_container_add(GTK_CONTAINER(strip), stripText);
        auto style = gtk_css_provider_new();
        gtk_css_provider_load_from_data(style, "window { background: #1f2227; border-radius: 7px; } label { color: #e8edf3; padding: 4px 8px; font: 13px sans-serif; }", -1, nullptr);
        gtk_style_context_add_provider(gtk_widget_get_style_context(strip), GTK_STYLE_PROVIDER(style), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        gtk_style_context_add_provider(gtk_widget_get_style_context(stripText), GTK_STYLE_PROVIDER(style), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION); g_object_unref(style);
        for (int i = 0; i < 2; ++i) {
            std::string id = "codex-quota-" + std::to_string(getpid()) + "-" + std::to_string(i);
            indicators[i] = app_indicator_new(id.c_str(), "utilities-system-monitor", APP_INDICATOR_CATEGORY_APPLICATION_STATUS);
            app_indicator_set_title(indicators[i], i == 0 ? "Codex five-hour remaining quota" : "Codex weekly remaining quota");
            app_indicator_set_menu(indicators[i], GTK_MENU(menu())); app_indicator_set_status(indicators[i], APP_INDICATOR_STATUS_ACTIVE);
        }
        // Wayland 不提供通用的其他应用窗口坐标，避免把 XWayland 误报为完整支持。
        x11 = GDK_IS_X11_DISPLAY(gdk_display_get_default()) && g_strcmp0(g_getenv("XDG_SESSION_TYPE"), "wayland") != 0;
        if (x11) {
            display = XOpenDisplay(nullptr); x11 = display != nullptr;
            if (display) {
                XSetErrorHandler(IgnoreStaleWindow); root = DefaultRootWindow(display);
                activeAtom = XInternAtom(display, "_NET_ACTIVE_WINDOW", False); clientsAtom = XInternAtom(display, "_NET_CLIENT_LIST", False); pidAtom = XInternAtom(display, "_NET_WM_PID", False);
                XSelectInput(display, root, PropertyChangeMask); XFlush(display);
                source = g_unix_fd_add(ConnectionNumber(display), G_IO_IN, [](gint, GIOCondition, gpointer context) -> gboolean {
                    auto& app = *static_cast<App*>(context);
                    while (XPending(app.display)) { XEvent event{}; XNextEvent(app.display, &event); if (event.type == PropertyNotify || event.type == ConfigureNotify || event.type == DestroyNotify) app.position(); }
                    return !app.closing;
                }, this);
            }
        }
        if (!x11) {
            // Wayland 无法检测任意宿主窗口；托盘查询按间隔运行，不使用不可能满足的门控。
            auto prefs = engine.preferences(); prefs.onlyWithCodex = false; prefs.overlay = false; engine.configure(prefs);
        }
        engine.start([this] { g_idle_add(Update, this); }); paint(); position();
        g_unix_signal_add(SIGTERM, [](gpointer data) -> gboolean { auto& app = *static_cast<App*>(data); app.closing = true; gtk_main_quit(); return G_SOURCE_REMOVE; }, this);
        g_unix_signal_add(SIGINT, [](gpointer data) -> gboolean { auto& app = *static_cast<App*>(data); app.closing = true; gtk_main_quit(); return G_SOURCE_REMOVE; }, this);
        g_timeout_add_seconds(30, StaleTick, this);
        if (options.settings) openSettings();
        if (options.smokeWayland) {
            openSettings();
            testPassed = !x11 && !engine.preferences().onlyWithCodex && !gtk_widget_get_sensitive(only) && !gtk_widget_get_sensitive(show);
            std::cout << (testPassed ? "PASS: Wayland tray mode does not pause queries behind unavailable host detection\n" : "FAIL: Wayland query gate remains enabled\n");
            g_timeout_add(100, [](gpointer data) -> gboolean { auto& app = *static_cast<App*>(data); app.closing = true; gtk_main_quit(); return G_SOURCE_REMOVE; }, this);
        } else if (options.smoke) startSmoke();
    }
    std::vector<unsigned long> property(::Window window, Atom key, Atom expected) {
        Atom type; int format; unsigned long count = 0, remaining = 0; unsigned char* bytes = nullptr;
        std::vector<unsigned long> result;
        if (XGetWindowProperty(display, window, key, 0, 512, False, expected, &type, &format, &count, &remaining, &bytes) == Success && type == expected && format == 32 && bytes) {
            auto data = reinterpret_cast<unsigned long*>(bytes); result.assign(data, data + count);
        }
        if (bytes) XFree(bytes); return result;
    }
    bool matches(::Window candidate) {
        auto pid = property(candidate, pidAtom, XA_CARDINAL);
        if (!pid.empty() && pid[0] == static_cast<unsigned long>(getpid())) return false;
        XClassHint hint{}; if (!XGetClassHint(display, candidate, &hint)) return false;
        std::string name = hint.res_class ? hint.res_class : ""; if (hint.res_name) name += " " + std::string(hint.res_name);
        if (hint.res_class) XFree(hint.res_class); if (hint.res_name) XFree(hint.res_name);
        return HostMatches(name, engine.preferences().host);
    }
    void position() {
        auto prefs = engine.preferences();
        if (!x11) { gtk_widget_hide(strip); engine.hostAvailable(false); return; }
        bool available = false; auto clients = property(root, clientsAtom, XA_WINDOW);
        for (auto candidate : clients) if (matches(candidate)) { available = true; break; }
        auto active = property(root, activeAtom, XA_WINDOW); ::Window next = active.empty() ? 0 : active[0];
        bool foreground = next && matches(next);
        engine.hostAvailable(available || foreground);
        if (!foreground || !prefs.overlay) { gtk_widget_hide(strip); return; }
        if (hostWindow != next) { hostWindow = next; XSelectInput(display, hostWindow, StructureNotifyMask | PropertyChangeMask); XFlush(display); }
        XWindowAttributes frame{};
        if (!XGetWindowAttributes(display, next, &frame) || frame.map_state != IsViewable) { gtk_widget_hide(strip); return; }
        int x = 0, y = 0; ::Window child; XTranslateCoordinates(display, next, root, 0, 0, &x, &y, &child);
        auto extents = property(next, XInternAtom(display, "_NET_FRAME_EXTENTS", False), XA_CARDINAL);
        int left = extents.size() >= 4 ? static_cast<int>(extents[0]) : 0, top = extents.size() >= 4 ? static_cast<int>(extents[2]) : 0;
        double scale = std::max(1, gtk_widget_get_scale_factor(strip));
        auto placement = Placement({(x - left) / scale, (y - top) / scale, frame.width / scale, (frame.height + top) / scale}, prefs.left, quota::WeeklyOnly(engine.state().snapshot));
        gtk_window_move(GTK_WINDOW(strip), static_cast<int>(placement.x), static_cast<int>(placement.y));
        gtk_window_resize(GTK_WINDOW(strip), static_cast<int>(placement.width), static_cast<int>(placement.height)); gtk_widget_show_all(strip);
        // 配额条不接收鼠标，拖动/点击仍交给下方 Codex 窗口。
        if (auto window = gtk_widget_get_window(strip)) { auto empty = cairo_region_create(); gdk_window_input_shape_combine_region(window, empty, 0, 0); cairo_region_destroy(empty); }
    }
    void icon(int index, const std::string& value, bool old) {
        auto image = cache / (std::to_string(index) + "-" + std::to_string(iconGeneration % 2) + ".png");
        auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 32, 32); auto context = cairo_create(surface);
        cairo_set_operator(context, CAIRO_OPERATOR_SOURCE); cairo_set_source_rgba(context, 0, 0, 0, 0); cairo_paint(context); cairo_set_operator(context, CAIRO_OPERATOR_OVER);
        cairo_select_font_face(context, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_source_rgb(context, old ? .65 : index ? .38 : .37, old ? .65 : .82, old ? .65 : index ? .64 : .95);
        cairo_set_font_size(context, 9); cairo_move_to(context, 9, 10); cairo_show_text(context, index ? "W" : "5h");
        auto digits = value; if (!digits.empty() && digits.back() == '%') digits.pop_back();
        cairo_set_font_size(context, digits.size() > 3 ? 12 : 16); cairo_text_extents_t size{}; cairo_text_extents(context, digits.c_str(), &size);
        cairo_move_to(context, (32 - size.width) / 2 - size.x_bearing, 29); cairo_show_text(context, digits.c_str());
        cairo_surface_write_to_png(surface, image.c_str()); cairo_destroy(context); cairo_surface_destroy(surface);
        app_indicator_set_icon_full(indicators[index], image.c_str(), value.c_str());
    }
    void paint() {
        auto state = engine.state(); auto prefs = engine.preferences(); auto now = NowMs(); auto summary = Summary(state, prefs, now);
        bool changed = summary != lastSummary;
        if (changed) {
            app_indicator_set_status(indicators[0], quota::WeeklyOnly(state.snapshot) ? APP_INDICATOR_STATUS_PASSIVE : APP_INDICATOR_STATUS_ACTIVE);
            ++iconGeneration; icon(0, Remaining(state.snapshot.fiveHour, now), Stale(state, prefs, now)); icon(1, Remaining(state.snapshot.weekly, now), Stale(state, prefs, now)); lastSummary = summary;
        }
        std::string text = prefs.left ? (quota::WeeklyOnly(state.snapshot) ? "" : "5h\n" + Remaining(state.snapshot.fiveHour, now) + "\n") + "W\n" + Remaining(state.snapshot.weekly, now) : summary;
        gtk_label_set_text(GTK_LABEL(stripText), text.c_str());
        if (changed) position();
        if (status) {
            std::string message = summary + "\n" + (state.refreshing ? "Refreshing…" : !state.error.empty() ? state.error : !prefs.online ? "Online queries disabled" : "Account quota");
            if (!x11) message += "\nWayland: overlay/host detection unavailable. Scheduling uses the interval.";
            gtk_label_set_text(GTK_LABEL(status), message.c_str());
        }
    }
    static void Apply(GtkButton*, gpointer data) {
        auto& app = *static_cast<App*>(data); auto prefs = app.engine.preferences();
        prefs.seconds = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app.seconds)); prefs.staleMinutes = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app.stale));
        prefs.online = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.online)); prefs.onlyWithCodex = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.only)); prefs.overlay = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.show));
        prefs.left = gtk_combo_box_get_active(GTK_COMBO_BOX(app.side)) == 1;
        prefs.home = gtk_entry_get_text(GTK_ENTRY(app.home)); prefs.binary = gtk_entry_get_text(GTK_ENTRY(app.binary)); prefs.host = gtk_entry_get_text(GTK_ENTRY(app.hostName));
        if (!Save(app.configRoot / "settings.ini", prefs)) { gtk_label_set_text(GTK_LABEL(app.status), "Cannot save: check absolute home path, values and permissions"); return; }
        app.engine.configure(prefs); app.lastSummary.clear(); app.paint(); app.position();
    }
    void openSettings() {
        if (config) { gtk_window_present(GTK_WINDOW(config)); return; }
        auto prefs = engine.preferences(); config = gtk_window_new(GTK_WINDOW_TOPLEVEL); gtk_window_set_title(GTK_WINDOW(config), "Codex Quota — Settings"); gtk_window_set_default_size(GTK_WINDOW(config), 520, 470);
        g_signal_connect(config, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) { auto& app = *static_cast<App*>(data); app.config = nullptr; app.status = nullptr; }), this);
        auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14); gtk_container_set_border_width(GTK_CONTAINER(box), 24); gtk_container_add(GTK_CONTAINER(config), box);
        status = gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(status), 0); gtk_label_set_line_wrap(GTK_LABEL(status), TRUE); gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 0);
        auto grid = gtk_grid_new(); gtk_grid_set_row_spacing(GTK_GRID(grid), 12); gtk_grid_set_column_spacing(GTK_GRID(grid), 18); gtk_box_pack_start(GTK_BOX(box), grid, TRUE, TRUE, 0);
        int row = 0;
        auto field = [&](const char* label, GtkWidget* control) { auto title = gtk_label_new(label); gtk_label_set_xalign(GTK_LABEL(title), 0); gtk_grid_attach(GTK_GRID(grid), title, 0, row, 1, 1); gtk_widget_set_hexpand(control, TRUE); gtk_grid_attach(GTK_GRID(grid), control, 1, row++, 1, 1); return control; };
        auto check = [&](const char* label, bool enabled) { auto control = gtk_check_button_new_with_label(label); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(control), enabled); gtk_grid_attach(GTK_GRID(grid), control, 0, row++, 2, 1); return control; };
        online = check("Query Codex account online", prefs.online); only = check("Query only while the host app is open", prefs.onlyWithCodex); show = check("Show window-following overlay (X11)", prefs.overlay);
        gtk_widget_set_sensitive(only, x11); gtk_widget_set_sensitive(show, x11);
        seconds = field("Refresh interval (30–3600 s)", gtk_spin_button_new_with_range(30, 3600, 1)); gtk_spin_button_set_value(GTK_SPIN_BUTTON(seconds), prefs.seconds);
        stale = field("Stale after (1–120 min)", gtk_spin_button_new_with_range(1, 120, 1)); gtk_spin_button_set_value(GTK_SPIN_BUTTON(stale), prefs.staleMinutes);
        side = field("Overlay position", gtk_combo_box_text_new()); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(side), "Top"); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(side), "Left"); gtk_combo_box_set_active(GTK_COMBO_BOX(side), prefs.left ? 1 : 0);
        home = field("Codex data directory", gtk_entry_new()); gtk_entry_set_text(GTK_ENTRY(home), prefs.home.c_str());
        binary = field("Codex executable (blank = auto)", gtk_entry_new()); gtk_entry_set_text(GTK_ENTRY(binary), prefs.binary.c_str());
        hostName = field("Host window class contains", gtk_entry_new()); gtk_entry_set_text(GTK_ENTRY(hostName), prefs.host.c_str());
        auto buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL); gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END); gtk_box_pack_end(GTK_BOX(box), buttons, FALSE, FALSE, 0);
        auto refresh = gtk_button_new_with_label("Refresh now"), apply = gtk_button_new_with_label("Save and apply"); gtk_container_add(GTK_CONTAINER(buttons), refresh); gtk_container_add(GTK_CONTAINER(buttons), apply);
        g_signal_connect(refresh, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) { static_cast<App*>(data)->engine.refresh(); }), this); g_signal_connect(apply, "clicked", G_CALLBACK(Apply), this);
        paint(); gtk_widget_show_all(config);
    }
    void activate(::Window window) { unsigned long value = window; XChangeProperty(display, root, activeAtom, XA_WINDOW, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&value), 1); XFlush(display); }
    void startSmoke() {
        if (!x11) throw std::runtime_error("Smoke test requires X11");
        auto prefs = engine.preferences(); prefs.host = "QuotaFixture"; engine.configure(prefs);
        testHost = XCreateSimpleWindow(display, root, 100, 80, 800, 600, 0, 0, 0); testOther = XCreateSimpleWindow(display, root, 10, 10, 300, 300, 0, 0, 0);
        XClassHint hint{const_cast<char*>("QuotaFixture"), const_cast<char*>("QuotaFixture")}; XSetClassHint(display, testHost, &hint); XMapWindow(display, testHost); XMapWindow(display, testOther); activate(testHost);
        g_timeout_add(150, [](gpointer data) -> gboolean {
            auto& app = *static_cast<App*>(data);
            auto fail = [&](const char* reason) { std::cerr << "FAIL: " << reason << '\n'; app.closing = true; gtk_main_quit(); return G_SOURCE_REMOVE; };
            switch (app.testStage++) {
            case 0: if (!gtk_widget_get_visible(app.strip)) return fail("Foreground host must show overlay"); XMoveWindow(app.display, app.testHost, 200, 120); XFlush(app.display); break;
            case 1: { int x, y; gtk_window_get_position(GTK_WINDOW(app.strip), &x, &y); if (x != 476 || y != 126) return fail("Moving host must reposition overlay"); app.activate(app.testOther); break; }
            case 2: if (gtk_widget_get_visible(app.strip)) return fail("Other foreground application must hide overlay"); app.activate(app.testHost); break;
            case 3: if (!gtk_widget_get_visible(app.strip)) return fail("Returning to host must restore overlay"); app.openSettings(); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app.seconds), 45); Apply(nullptr, &app); break;
            case 4: if (app.engine.preferences().seconds != 45 || Load(app.configRoot / "settings.ini").seconds != 45) return fail("Native settings must apply and persist"); XDestroyWindow(app.display, app.testHost); app.activate(0); break;
            case 5: {
                if (gtk_widget_get_visible(app.strip)) return fail("Closed host must hide overlay");
                app.testPassed = true; std::cout << "PASS: Linux tray/settings, X11 focus hiding, move-following, restore and host exit\n";
                if (app.config) gtk_widget_destroy(app.config);
                auto usage = std::make_shared<rusage>(); getrusage(RUSAGE_SELF, usage.get());
                auto measurement = new std::pair<App*, std::shared_ptr<rusage>>(&app, usage);
                g_timeout_add(2000, [](gpointer data) -> gboolean {
                    std::unique_ptr<std::pair<App*, std::shared_ptr<rusage>>> measurement(static_cast<std::pair<App*, std::shared_ptr<rusage>>*>(data));
                    rusage after{}; getrusage(RUSAGE_SELF, &after);
                    auto cpu = [](const rusage& value) { return value.ru_utime.tv_sec + value.ru_stime.tv_sec + (value.ru_utime.tv_usec + value.ru_stime.tv_usec) / 1000000.0; };
                    std::cout << "{\"idle_seconds\":2,\"idle_cpu_seconds\":" << cpu(after) - cpu(*measurement->second) << ",\"peak_rss_mib\":" << after.ru_maxrss / 1024.0 << "}\n";
                    measurement->first->closing = true; gtk_main_quit(); return G_SOURCE_REMOVE;
                }, measurement);
                return G_SOURCE_REMOVE;
            }
            }
            return G_SOURCE_CONTINUE;
        }, this);
    }
};
}
int main(int argc, char** argv) {
    try {
        auto options = ParseOptions(argc, argv);
        if (!gtk_init_check(nullptr, nullptr)) { std::cerr << "A graphical Linux session is required\n"; return 2; }
        g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, nullptr);
        App app(options);
        // 原生版在用户配置目录内保持一个实例，切换账号目录不会改变锁身份。
        InstanceLock singleton(app.configRoot / "instance.lock"); if (!singleton.acquired()) return 0;
        app.initialize(); gtk_main(); return options.smoke && !app.testPassed ? 1 : 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

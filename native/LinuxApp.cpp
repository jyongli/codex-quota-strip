#include "NativeCore.h"
#include "Localization.h"
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
#include <ctime>
#include <fstream>

using namespace nativequota;
namespace {
// 窗口可能在读取属性前已关闭，X11 异步错误不应结束配额程序。
int IgnoreStaleWindow(Display*, XErrorEvent*) { return 0; }
struct App {
    Options options;
    Engine engine;
    GtkWidget *strip = nullptr, *stripText = nullptr, *config = nullptr, *status = nullptr;
    GtkWidget *seconds = nullptr, *stale = nullptr, *online = nullptr, *only = nullptr, *show = nullptr, *side = nullptr, *home = nullptr, *binary = nullptr, *hostName = nullptr;
    GtkWidget* language = nullptr;
    GtkWidget *configScroll = nullptr, *configActions = nullptr, *configApply = nullptr;
    GtkWidget *historyEnabled = nullptr, *historyDirectory = nullptr;
    GtkWidget *historyWindow = nullptr, *historyStack = nullptr, *historyMessage = nullptr, *historyChart = nullptr;
    GtkListStore* historyModel = nullptr;
    quota::HistoryReadResult historyRows;
    GtkWidget *historyKind = nullptr, *historyTable = nullptr;
    quota::RequestReadResult requestRows;
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
    bool applicationIconLoaded = false;
    explicit App(Options value) : options(std::move(value)), engine(options.preferences) {
        configRoot = options.smoke ? std::filesystem::temp_directory_path() / ("quota-linux-smoke-" + std::to_string(getpid())) : ConfigDirectory();
        // 测试始终使用隔离目录，构造 Engine 后、启动查询前覆盖默认日志路径。
        if (options.smoke) { auto prefs = engine.preferences(); prefs.home = (configRoot / "home").string(); prefs.historyDirectory = (configRoot / "history").string(); engine.configure(prefs); }
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
        if (options.smoke) std::filesystem::remove_all(configRoot);
    }
    static void Refresh(GtkMenuItem*, gpointer data) { static_cast<App*>(data)->engine.refresh(); }
    static void Settings(GtkMenuItem*, gpointer data) { static_cast<App*>(data)->openSettings(); }
    static void History(GtkMenuItem*, gpointer data) { static_cast<App*>(data)->openHistory(); }
    static void Quit(GtkMenuItem*, gpointer data) { static_cast<App*>(data)->closing = true; gtk_main_quit(); }
    static gboolean Update(gpointer data) { auto& app = *static_cast<App*>(data); if (!app.closing) app.paint(); return G_SOURCE_REMOVE; }
    static gboolean StaleTick(gpointer data) { auto& app = *static_cast<App*>(data); if (!app.closing) app.paint(); return !app.closing; }
    const char* ui(const char* english) { return Translate(english, engine.preferences().chinese); }
    GtkWidget* menu() {
        auto menu = gtk_menu_new();
        for (auto item : {std::pair<const char*, GCallback>{"Refresh now", G_CALLBACK(Refresh)}, {"Quota history…", G_CALLBACK(History)}, {"Settings…", G_CALLBACK(Settings)}, {"Quit", G_CALLBACK(Quit)}}) {
            auto entry = gtk_menu_item_new_with_label(ui(item.first)); g_signal_connect(entry, "activate", item.second, this); gtk_menu_shell_append(GTK_MENU_SHELL(menu), entry);
        }
        gtk_widget_show_all(menu); return menu;
    }
    // 开发构建、私有 TGZ 安装和系统包分别从可执行文件旁或 prefix/share 读取同一品牌 PNG。
    bool loadApplicationIcon() {
        gtk_window_set_default_icon_name("codex-quota-strip");
        char executable[4096]; auto length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
        if (length <= 0) return false; executable[length] = '\0';
        auto directory = std::filesystem::path(executable).parent_path();
        for (const auto& path : {directory / "app.png", directory / "resources/app.png", directory.parent_path() / "share/icons/hicolor/256x256/apps/codex-quota-strip.png"}) {
            GError* error = nullptr; auto icon = gdk_pixbuf_new_from_file(path.c_str(), &error);
            if (error) g_error_free(error);
            if (!icon) continue;
            gtk_window_set_default_icon(icon); g_object_unref(icon); return true;
        }
        return false;
    }
    void initialize() {
        applicationIconLoaded = loadApplicationIcon();
        if (options.smoke && !applicationIconLoaded) throw std::runtime_error("Packaged application PNG icon is missing or unreadable");
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
            app_indicator_set_title(indicators[i], i == 0 ? ui("Codex five-hour remaining quota") : ui("Codex weekly remaining quota"));
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
            std::string message = summary + "\n" + (state.refreshing ? ui("Refreshing…") : !state.error.empty() ? ui(state.error.c_str()) : !prefs.online ? ui("Online queries disabled") : ui("Account quota"));
            if (!state.historyError.empty()) message += "\n" + std::string(ui(state.historyError.c_str()));
            if (!state.requestError.empty()) message += "\n" + std::string(ui(state.requestError.c_str()));
            if (!x11) message += ui("\nWayland: overlay/host detection unavailable. Scheduling uses the interval.");
            gtk_label_set_text(GTK_LABEL(status), message.c_str());
        }
    }
    static void Apply(GtkButton*, gpointer data) {
        auto& app = *static_cast<App*>(data); auto prefs = app.engine.preferences();
        prefs.seconds = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app.seconds)); prefs.staleMinutes = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(app.stale));
        prefs.online = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.online)); prefs.onlyWithCodex = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.only)); prefs.overlay = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.show));
        prefs.left = gtk_combo_box_get_active(GTK_COMBO_BOX(app.side)) == 1;
        bool changedLanguage = prefs.chinese != (gtk_combo_box_get_active(GTK_COMBO_BOX(app.language)) == 0);
        prefs.chinese = gtk_combo_box_get_active(GTK_COMBO_BOX(app.language)) == 0;
        prefs.home = gtk_entry_get_text(GTK_ENTRY(app.home)); prefs.binary = gtk_entry_get_text(GTK_ENTRY(app.binary)); prefs.host = gtk_entry_get_text(GTK_ENTRY(app.hostName));
        prefs.historyEnabled = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app.historyEnabled));
        prefs.historyDirectory = gtk_entry_get_text(GTK_ENTRY(app.historyDirectory));
        std::string directoryError;
        // 关闭记录必须能够摆脱失效目录；禁用时只由 Save 校验路径格式，不探测目录。
        if (prefs.historyEnabled && !quota::HistoryStore::ValidateDirectory(std::filesystem::path(prefs.historyDirectory), directoryError)) {
            gtk_label_set_text(GTK_LABEL(app.status), app.ui("Cannot use history directory; choose an absolute writable path.")); return;
        }
        if (!Save(app.configRoot / "settings.ini", prefs)) { gtk_label_set_text(GTK_LABEL(app.status), app.ui("Cannot save: check absolute home path, values and permissions")); return; }
        app.engine.configure(prefs); app.lastSummary.clear(); app.paint(); app.position();
        if (app.historyWindow) {
            if (prefs.historyEnabled) app.refreshHistory();
            else { app.historyRows = {}; app.requestRows = {}; app.renderHistory(); }
        }
        if(changedLanguage){
            // 保存成功后重建固定标签，不丢失当前刷新和显示设置。
            for(auto indicator:app.indicators)app_indicator_set_menu(indicator,GTK_MENU(app.menu()));
            gtk_widget_destroy(app.config);app.openSettings();
            if (app.historyWindow) { gtk_widget_destroy(app.historyWindow); app.openHistory(prefs.historyEnabled); }
        }
    }
    void openSettings() {
        if (config) { gtk_window_present(GTK_WINDOW(config)); return; }
        auto prefs = engine.preferences(); config = gtk_window_new(GTK_WINDOW_TOPLEVEL); gtk_window_set_title(GTK_WINDOW(config), ui("Codex Quota — Settings"));
        GdkRectangle workarea{0, 0, 1024, 768}; auto display = gtk_widget_get_display(config);
        auto monitor = gdk_display_get_primary_monitor(display); if (!monitor) monitor = gdk_display_get_monitor_at_point(display, 0, 0);
        if (monitor) gdk_monitor_get_workarea(monitor, &workarea);
        gtk_window_set_default_size(GTK_WINDOW(config), 560, std::min(650, std::max(260, workarea.height - 80)));
        g_signal_connect(config, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) { auto& app = *static_cast<App*>(data); app.config = nullptr; app.status = nullptr; app.configScroll = app.configActions = app.configApply = nullptr; }), this);
        auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14); gtk_container_set_border_width(GTK_CONTAINER(box), 24); gtk_container_add(GTK_CONTAINER(config), box);
        // 设置内容可滚动，操作区保留在滚动区外，较矮屏幕也能直接保存。
        configScroll = gtk_scrolled_window_new(nullptr, nullptr); gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(configScroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(configScroll), 120); gtk_box_pack_start(GTK_BOX(box), configScroll, TRUE, TRUE, 0);
        auto content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14); gtk_container_add(GTK_CONTAINER(configScroll), content);
        status = gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(status), 0); gtk_label_set_line_wrap(GTK_LABEL(status), TRUE); gtk_box_pack_start(GTK_BOX(content), status, FALSE, FALSE, 0);
        auto grid = gtk_grid_new(); gtk_grid_set_row_spacing(GTK_GRID(grid), 12); gtk_grid_set_column_spacing(GTK_GRID(grid), 18); gtk_box_pack_start(GTK_BOX(content), grid, FALSE, FALSE, 0);
        int row = 0;
        auto field = [&](const char* label, GtkWidget* control) { auto title = gtk_label_new(ui(label)); gtk_label_set_xalign(GTK_LABEL(title), 0); gtk_grid_attach(GTK_GRID(grid), title, 0, row, 1, 1); gtk_widget_set_hexpand(control, TRUE); gtk_grid_attach(GTK_GRID(grid), control, 1, row++, 1, 1); return control; };
        auto check = [&](const char* label, bool enabled) { auto control = gtk_check_button_new_with_label(ui(label)); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(control), enabled); gtk_grid_attach(GTK_GRID(grid), control, 0, row++, 2, 1); return control; };
        online = check("Query Codex account online", prefs.online); only = check("Query only while the host app is open", prefs.onlyWithCodex); show = check("Show window-following overlay (X11)", prefs.overlay);
        gtk_widget_set_sensitive(only, x11); gtk_widget_set_sensitive(show, x11);
        seconds = field("Refresh interval (30–3600 s)", gtk_spin_button_new_with_range(30, 3600, 1)); gtk_spin_button_set_value(GTK_SPIN_BUTTON(seconds), prefs.seconds);
        stale = field("Stale after (1–120 min)", gtk_spin_button_new_with_range(1, 120, 1)); gtk_spin_button_set_value(GTK_SPIN_BUTTON(stale), prefs.staleMinutes);
        side = field("Overlay position", gtk_combo_box_text_new()); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(side), ui("Top")); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(side), ui("Left")); gtk_combo_box_set_active(GTK_COMBO_BOX(side), prefs.left ? 1 : 0);
        home = field("Codex data directory", gtk_entry_new()); gtk_entry_set_text(GTK_ENTRY(home), prefs.home.c_str());
        binary = field("Codex executable (blank = auto)", gtk_entry_new()); gtk_entry_set_text(GTK_ENTRY(binary), prefs.binary.c_str());
        hostName = field("Host window class contains", gtk_entry_new()); gtk_entry_set_text(GTK_ENTRY(hostName), prefs.host.c_str());
        language=field("Language",gtk_combo_box_text_new());gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(language),"中文");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(language),"English");gtk_combo_box_set_active(GTK_COMBO_BOX(language),prefs.chinese?0:1);
        historyEnabled = check("Record quota changes and request tokens", prefs.historyEnabled);
        auto directoryBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        historyDirectory = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(historyDirectory), prefs.historyDirectory.c_str()); gtk_widget_set_hexpand(historyDirectory, TRUE);
        gtk_box_pack_start(GTK_BOX(directoryBox), historyDirectory, TRUE, TRUE, 0);
        auto browse = gtk_button_new_with_label(ui("Browse…")); gtk_box_pack_start(GTK_BOX(directoryBox), browse, FALSE, FALSE, 0); field("History directory", directoryBox);
        g_signal_connect(browse, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            auto& app = *static_cast<App*>(data);
            auto dialog = gtk_file_chooser_dialog_new(app.ui("Choose history directory"), GTK_WINDOW(app.config), GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                app.ui("Cancel"), GTK_RESPONSE_CANCEL, app.ui("Choose"), GTK_RESPONSE_ACCEPT, nullptr);
            gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(dialog), gtk_entry_get_text(GTK_ENTRY(app.historyDirectory)));
            if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
                auto path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)); if (path) { gtk_entry_set_text(GTK_ENTRY(app.historyDirectory), path); g_free(path); }
            }
            gtk_widget_destroy(dialog);
        }), this);
        auto buttons = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL); gtk_button_box_set_layout(GTK_BUTTON_BOX(buttons), GTK_BUTTONBOX_END); gtk_box_pack_end(GTK_BOX(box), buttons, FALSE, FALSE, 0);
        configActions = buttons;
        auto history = gtk_button_new_with_label(ui("Quota history…")); gtk_container_add(GTK_CONTAINER(buttons), history);
        g_signal_connect(history, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) { static_cast<App*>(data)->openHistory(); }), this);
        auto refresh = gtk_button_new_with_label(ui("Refresh now")), apply = gtk_button_new_with_label(ui("Save and apply")); gtk_container_add(GTK_CONTAINER(buttons), refresh); gtk_container_add(GTK_CONTAINER(buttons), apply);
        configApply = apply;
        g_signal_connect(refresh, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) { static_cast<App*>(data)->engine.refresh(); }), this); g_signal_connect(apply, "clicked", G_CALLBACK(Apply), this);
        paint(); gtk_widget_show_all(config);
    }
    static std::string historyTime(long long ms) {
        std::time_t seconds = static_cast<std::time_t>(ms / 1000); std::tm local{}; localtime_r(&seconds, &local);
        char text[40]; std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local); return text;
    }
    static std::string historyPercent(const quota::Window& value) {
        if (!value.present) return "—";
        char text[32]; std::snprintf(text, sizeof(text), "%.1f%%", std::clamp(100.0 - value.used, 0.0, 100.0)); return text;
    }
    static std::string historyDelta(const std::optional<double>& value) {
        if (!value) return "—";
        char text[32]; std::snprintf(text, sizeof(text), "%+.1f", *value); return text;
    }
    bool showingRequests() const { return historyKind && gtk_combo_box_get_active(GTK_COMBO_BOX(historyKind)) == 1; }
    // 额度和请求表格复用原生列表模型，未知费用和未知 token 均保留破折号。
    void renderRequests() {
        for (auto it = requestRows.entries.rbegin(); it != requestRows.entries.rend(); ++it) {
            const auto& row = *it;
            auto count = [&](long long quota::TokenCounts::*member) { return row.tokens ? std::to_string((*row.tokens).*member) : "—"; };
            std::string status = ui(row.status == "completed" ? "Completed" : row.status == "interrupted" ? "Interrupted" : row.status == "incomplete" ? "Incomplete" : row.status.c_str());
            if (row.partial) status += " / " + std::string(ui("Partial"));
            // 标题仅用于显示；解析缺失时依次保留会话 ID 和本地化未知占位。
            const std::string conversation = !row.conversationLabel.empty() ? row.conversationLabel : !row.threadId.empty() ? row.threadId : ui("Unknown conversation");
            std::vector<std::string> values{historyTime(row.ended), conversation, row.model.empty() ? "—" : row.model, count(&quota::TokenCounts::input), count(&quota::TokenCounts::cachedInput),
                count(&quota::TokenCounts::cacheWriteInput), count(&quota::TokenCounts::output), count(&quota::TokenCounts::reasoningOutput), count(&quota::TokenCounts::total),
                historyPercent(row.quotaStart.fiveHour) + " → " + historyPercent(row.quotaEnd.fiveHour), historyPercent(row.quotaStart.weekly) + " → " + historyPercent(row.quotaEnd.weekly),
                historyDelta(row.fiveChange), historyDelta(row.weeklyChange), "—", status,
                (row.threadId.empty() ? "—" : row.threadId) + " / " + row.turnId};
            GtkTreeIter iter; gtk_list_store_append(historyModel, &iter);
            for (int column = 0; column < static_cast<int>(values.size()); ++column) gtk_list_store_set(historyModel, &iter, column, values[column].c_str(), -1);
        }
    }
    // 只读有界快照；列表与图表共用同一批数据，切换视图不访问磁盘。
    void renderHistory() {
        gtk_list_store_clear(historyModel);
        // 切换数据集时重建列标题，行数与磁盘读取仍保持有界。
        auto columns = gtk_tree_view_get_columns(GTK_TREE_VIEW(historyTable));
        for (auto item = columns; item; item = item->next) gtk_tree_view_remove_column(GTK_TREE_VIEW(historyTable), GTK_TREE_VIEW_COLUMN(item->data));
        g_list_free(columns);
        std::vector<const char*> titles = showingRequests() ? std::vector<const char*>{"Time (local)", "Conversation", "Model", "Input tokens", "Cached input tokens", "Cache write tokens", "Output tokens", "Reasoning tokens", "Total tokens", "5h quota (start → end)", "Weekly quota (start → end)", "5h account change (pp)", "Weekly account change (pp)", "Actual cost", "Status", "Thread / turn"} :
            std::vector<const char*>{"Time (local)", "5h remaining", "Weekly remaining", "5h change (pp)", "Weekly change (pp)", "Source / event"};
        int column = 0;
        for (auto title : titles) {
            auto cell = gtk_cell_renderer_text_new(); auto heading = gtk_tree_view_column_new_with_attributes(ui(title), cell, "text", column, nullptr);
            if (showingRequests() && column == 1) {
                // 长标题保持单行省略，不让内容自动撑宽列表；仍允许用户在有界范围内调整。
                g_object_set(cell, "ellipsize", PANGO_ELLIPSIZE_END, "single-paragraph-mode", TRUE, nullptr);
                gtk_tree_view_column_set_sizing(heading, GTK_TREE_VIEW_COLUMN_FIXED); gtk_tree_view_column_set_min_width(heading, 120); gtk_tree_view_column_set_max_width(heading, 420); gtk_tree_view_column_set_fixed_width(heading, 260);
            }
            gtk_tree_view_column_set_resizable(heading, TRUE); gtk_tree_view_append_column(GTK_TREE_VIEW(historyTable), heading); ++column;
        }
        if (showingRequests()) renderRequests();
        else for (auto it = historyRows.entries.rbegin(); it != historyRows.entries.rend(); ++it) {
            const auto& row = *it;
            std::string event = std::string(ui(row.source == "account" ? "Account query" : row.source == "local-event" ? "Local event" : row.source.c_str())) + " / " + ui(row.kind == "baseline" ? "Baseline" : row.kind == "reset" ? "Reset" : "Change");
            GtkTreeIter iter; gtk_list_store_append(historyModel, &iter);
            gtk_list_store_set(historyModel, &iter, 0, historyTime(row.recorded).c_str(), 1, historyPercent(row.snapshot.fiveHour).c_str(),
                2, historyPercent(row.snapshot.weekly).c_str(), 3, historyDelta(row.fiveChange).c_str(), 4, historyDelta(row.weeklyChange).c_str(), 5, event.c_str(), -1);
        }
        std::string message = ui("Latest 30 UTC days; at most 2000 records. Refresh manually.");
        if (!engine.preferences().historyEnabled) message += "\n" + std::string(ui("History recording is disabled."));
        const auto readError = showingRequests() ? requestRows.error : historyRows.error;
        if (!readError.empty()) message += "\n" + std::string(ui(readError.c_str()));
        auto writeError = showingRequests() ? engine.state().requestError : engine.state().historyError;
        if (!writeError.empty()) message += "\n" + std::string(ui(writeError.c_str()));
        if (showingRequests() ? requestRows.truncated : historyRows.truncated) message += "\n" + std::string(ui("Showing newest records; older records were omitted."));
        if (showingRequests()) {
            message += "\n" + std::string(ui("Account quota changes may include other work. Actual cost is unavailable.")) + "\n" + ui("Cached input is part of input; reasoning is part of output.");
            if (requestRows.entries.empty()) message += "\n" + std::string(ui("No work requests recorded yet."));
        } else { message += "\n" + std::string(ui("Solid: continuous records. Dashed: baseline/source/reset boundaries, reference only, not consumption. Missing values stay disconnected.")); if (historyRows.entries.empty()) message += "\n" + std::string(ui("No quota changes recorded yet.")); }
        gtk_label_set_text(GTK_LABEL(historyMessage), message.c_str()); gtk_widget_queue_draw(historyChart);
    }
    void refreshHistory() { if (showingRequests()) requestRows = engine.requests(2000); else historyRows = engine.history(2000); renderHistory(); }
    void drawHistory(cairo_t* context, double width, double height) {
        const double left = 55, top = 45, right = std::max(left + 1, width - 28), bottom = std::max(top + 1, height - 50);
        cairo_set_source_rgb(context, .12, .13, .15); cairo_paint(context);
        cairo_select_font_face(context, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL); cairo_set_font_size(context, 12);
        // Cairo 的 toy text API 不负责中文字体回退，图中标签通过 Pango 绘制。
        auto label = [&](const std::string& text, double x, double y) {
            auto layout = pango_cairo_create_layout(context); pango_layout_set_text(layout, text.c_str(), -1);
            cairo_set_source_rgb(context, .86, .88, .92); cairo_move_to(context, x, y); pango_cairo_show_layout(context, layout); g_object_unref(layout);
        };
        if (showingRequests()) {
            label(ui("Tokens per request: input (blue), output (green), cached input (orange)"), left, 12);
            if (requestRows.entries.empty()) { label(ui("No work requests recorded yet."), left + 12, top + 30); return; }
            double maximum = 1; for (const auto& row : requestRows.entries) if (row.tokens) maximum = std::max(maximum, static_cast<double>(std::max(row.tokens->input, row.tokens->output)));
            for (int tick = 0; tick <= 4; ++tick) { double y = bottom - (bottom - top) * tick / 4; label(std::to_string(static_cast<long long>(maximum * tick / 4)), 4, y - 8); }
            auto start = requestRows.entries.front().ended, end = requestRows.entries.back().ended; double span = std::max(1LL, end - start);
            double bar = std::clamp((right - left) / (requestRows.entries.size() * 4), 1.0, 8.0);
            // 分组柱只绘有真实 token 的请求；未知值不落到零轴。
            for (const auto& row : requestRows.entries) if (row.tokens) {
                const long long values[]{row.tokens->input, row.tokens->output, row.tokens->cachedInput};
                double x = left + 12 + (right - left - 24) * (row.ended - start) / span;
                for (int series = 0; series < 3; ++series) {
                    cairo_set_source_rgb(context, series == 2 ? .95 : .30, series == 0 ? .65 : .80, series == 0 ? .95 : .40);
                    double h = (bottom - top) * values[series] / maximum; cairo_rectangle(context, x + (series - 1) * bar, bottom - h, bar, h); cairo_fill(context);
                }
            }
            label(historyTime(start), left, bottom + 12); if (right - left > 360) label(historyTime(end), right - 160, bottom + 12); return;
        }
        label(ui("5h (blue) / Weekly (green) — remaining %"), left, 12);
        for (int percent : {0, 25, 50, 75, 100}) {
            double y = bottom - (bottom - top) * percent / 100.0;
            cairo_set_source_rgb(context, .28, .30, .33); cairo_move_to(context, left, y); cairo_line_to(context, right, y); cairo_stroke(context);
            label(std::to_string(percent), 15, y - 8);
        }
        if (historyRows.entries.empty()) { label(ui("No quota changes recorded yet."), left + 12, top + 30); return; }
        auto start = historyRows.entries.front().recorded, end = historyRows.entries.back().recorded;
        double span = std::max(1LL, end - start);
        for (int metric = 0; metric < 2; ++metric) {
            const quota::HistoryEntry* previous = nullptr; double previousX = 0, previousY = 0;
            cairo_set_source_rgb(context, metric ? .30 : .35, metric ? .83 : .68, metric ? .57 : .98); cairo_set_line_width(context, 2);
            for (const auto& row : historyRows.entries) {
                const auto& value = metric ? row.snapshot.weekly : row.snapshot.fiveHour;
                if (!value.present) { previous = nullptr; continue; }
                double x = left + (right - left) * (row.recorded - start) / span;
                double y = bottom - (bottom - top) * std::clamp(100.0 - value.used, 0.0, 100.0) / 100.0;
                // 参考快照可用虚线相连；缺失点中断路径，不插值为零。
                if (previous) { auto kind = quota::HistoryLineBetween(*previous, row, metric != 0); if (kind != quota::HistoryLineKind::Gap) {
                    double pattern[]{4, 4}; cairo_set_dash(context, kind == quota::HistoryLineKind::Dashed ? pattern : nullptr, kind == quota::HistoryLineKind::Dashed ? 2 : 0, 0);
                    cairo_new_path(context); cairo_move_to(context, previousX, previousY); cairo_line_to(context, x, y); cairo_stroke(context);
                } }
                cairo_set_dash(context, nullptr, 0, 0); cairo_new_path(context); cairo_arc(context, x, y, 2.5, 0, 6.283185307); cairo_fill(context);
                previous = &row; previousX = x; previousY = y;
            }
        }
        label(historyTime(start), left, bottom + 12);
        if (right - left > 360) label(historyTime(end), right - 160, bottom + 12);
    }
    void openHistory(bool readHistory = true) {
        if (historyWindow) { refreshHistory(); gtk_window_present(GTK_WINDOW(historyWindow)); return; }
        historyWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL); gtk_window_set_title(GTK_WINDOW(historyWindow), ui("Codex Quota — History"));
        gtk_window_set_default_size(GTK_WINDOW(historyWindow), 960, 520); gtk_window_set_resizable(GTK_WINDOW(historyWindow), TRUE);
        g_signal_connect(historyWindow, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer data) {
            auto& app = *static_cast<App*>(data); app.historyWindow = app.historyStack = app.historyMessage = app.historyChart = nullptr;
            g_object_unref(app.historyModel); app.historyModel = nullptr; app.historyRows = {}; app.requestRows = {}; app.historyKind = app.historyTable = nullptr;
        }), this);
        auto box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10); gtk_container_set_border_width(GTK_CONTAINER(box), 16); gtk_container_add(GTK_CONTAINER(historyWindow), box);
        auto toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10); gtk_box_pack_start(GTK_BOX(box), toolbar, FALSE, FALSE, 0);
        historyKind = gtk_combo_box_text_new(); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(historyKind), ui("Quota changes")); gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(historyKind), ui("Work requests")); gtk_combo_box_set_active(GTK_COMBO_BOX(historyKind), 0); gtk_box_pack_start(GTK_BOX(toolbar), historyKind, FALSE, FALSE, 0);
        historyStack = gtk_stack_new(); auto switcher = gtk_stack_switcher_new(); gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher), GTK_STACK(historyStack));
        gtk_box_pack_start(GTK_BOX(toolbar), switcher, FALSE, FALSE, 0);
        auto refresh = gtk_button_new_with_label(ui("Refresh history")); gtk_box_pack_end(GTK_BOX(toolbar), refresh, FALSE, FALSE, 0);
        g_signal_connect(refresh, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) { static_cast<App*>(data)->refreshHistory(); }), this);
        historyMessage = gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(historyMessage), 0); gtk_label_set_line_wrap(GTK_LABEL(historyMessage), TRUE); gtk_box_pack_start(GTK_BOX(box), historyMessage, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), historyStack, TRUE, TRUE, 0);
        GType types[16]; std::fill(std::begin(types), std::end(types), G_TYPE_STRING); historyModel = gtk_list_store_newv(16, types);
        historyTable = gtk_tree_view_new_with_model(GTK_TREE_MODEL(historyModel));
        auto scroll = gtk_scrolled_window_new(nullptr, nullptr); gtk_container_add(GTK_CONTAINER(scroll), historyTable); gtk_stack_add_titled(GTK_STACK(historyStack), scroll, "list", ui("List"));
        historyChart = gtk_drawing_area_new(); gtk_widget_set_size_request(historyChart, 380, 240);
        g_signal_connect(historyChart, "draw", G_CALLBACK(+[](GtkWidget* widget, cairo_t* context, gpointer data) -> gboolean {
            static_cast<App*>(data)->drawHistory(context, gtk_widget_get_allocated_width(widget), gtk_widget_get_allocated_height(widget)); return FALSE;
        }), this);
        gtk_stack_add_titled(GTK_STACK(historyStack), historyChart, "chart", ui("Chart"));
        g_signal_connect(historyKind, "changed", G_CALLBACK(+[](GtkComboBox*, gpointer data) { static_cast<App*>(data)->refreshHistory(); }), this);
        if (readHistory) refreshHistory(); else { historyRows = {}; requestRows = {}; renderHistory(); }
        gtk_widget_show_all(historyWindow);
    }
    void activate(::Window window) { unsigned long value = window; XChangeProperty(display, root, activeAtom, XA_WINDOW, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&value), 1); XFlush(display); }
    void startSmoke() {
        if (!x11) throw std::runtime_error("Smoke test requires X11");
        auto icons = gtk_window_get_default_icon_list(); bool hasIcon = applicationIconLoaded && icons && gdk_pixbuf_get_width(GDK_PIXBUF(icons->data)) > 0; g_list_free(icons);
        if (!hasIcon) throw std::runtime_error("Application icon must load from the build or installed package resources");
        std::cout << "PASS: Linux packaged brand icon is available to native windows\n";
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
            case 3: if (!gtk_widget_get_visible(app.strip)) return fail("Returning to host must restore overlay"); app.openSettings(); gtk_entry_set_text(GTK_ENTRY(app.historyDirectory), (app.configRoot / "selected-history").c_str()); gtk_spin_button_set_value(GTK_SPIN_BUTTON(app.seconds), 45); gtk_combo_box_set_active(GTK_COMBO_BOX(app.language),0); Apply(nullptr, &app); break;
            case 4: if (app.engine.preferences().seconds != 45 || Load(app.configRoot / "settings.ini").seconds != 45 || !Load(app.configRoot / "settings.ini").chinese || std::string(gtk_window_get_title(GTK_WINDOW(app.config)))!="Codex 配额 — 设置") return fail("Native settings and Chinese UI must apply and persist"); gtk_combo_box_set_active(GTK_COMBO_BOX(app.language),1);Apply(nullptr,&app);if(std::string(gtk_window_get_title(GTK_WINDOW(app.config)))!="Codex Quota — Settings")return fail("English UI must restore live");gtk_window_resize(GTK_WINDOW(app.config), 560, 360);XDestroyWindow(app.display, app.testHost); app.activate(0); break;
            case 5: {
                if (gtk_widget_get_visible(app.strip)) return fail("Closed host must hide overlay");
                if (Load(app.configRoot / "settings.ini").historyDirectory != (app.configRoot / "selected-history").string()) return fail("History directory must persist");
                auto adjustment = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(app.configScroll));
                if (!GTK_IS_SCROLLED_WINDOW(app.configScroll) || gtk_widget_get_parent(app.configApply) != app.configActions || gtk_widget_is_ancestor(app.configApply, app.configScroll) ||
                    !gtk_widget_get_mapped(app.configApply) || gtk_widget_get_allocated_height(app.configApply) <= 0 || gtk_adjustment_get_upper(adjustment) <= gtk_adjustment_get_page_size(adjustment)) return fail("Settings must scroll while Save remains outside the viewport");
                // 禁用记录既不能创建新目录，也必须能保存指向普通文件子路径的失效目录。
                auto uncreated = app.configRoot / "disabled-uncreated";
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.historyEnabled), FALSE); gtk_entry_set_text(GTK_ENTRY(app.historyDirectory), uncreated.c_str()); Apply(nullptr, &app);
                if (app.engine.preferences().historyEnabled || Load(app.configRoot / "settings.ini").historyEnabled || std::filesystem::exists(uncreated)) return fail("Disabled history must save without creating a directory");
                auto blocker = app.configRoot / "history-blocker"; { std::ofstream file(blocker); file << "fixture"; }
                auto invalidDirectory = blocker / "child"; gtk_entry_set_text(GTK_ENTRY(app.historyDirectory), invalidDirectory.c_str()); Apply(nullptr, &app);
                if (app.engine.preferences().historyDirectory != invalidDirectory.string() || Load(app.configRoot / "settings.ini").historyDirectory != invalidDirectory.string()) return fail("Disabled history must save an inaccessible directory without a write probe");
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app.historyEnabled), TRUE); gtk_entry_set_text(GTK_ENTRY(app.historyDirectory), (app.configRoot / "selected-history").c_str()); Apply(nullptr, &app);
                app.openHistory();
                if (!app.historyRows.entries.empty() || gtk_tree_model_iter_n_children(GTK_TREE_MODEL(app.historyModel), nullptr) != 0) return fail("Isolated history must start empty");
                // 合成缺失／基线／重置行，验证两个视图共用快照且可实际绘制。
                quota::HistoryEntry first; first.recorded = NowMs() - 1000; first.snapshot.observed = first.recorded; first.snapshot.fiveHour = {true, 20, 0}; first.kind = "baseline"; first.source = "smoke";
                auto second = first; second.recorded += 500; second.kind = "change"; second.snapshot.fiveHour.used = 30; second.fiveChange = -10;
                auto third = second; third.recorded += 500; third.kind = "reset"; third.snapshot.fiveHour.present = false; third.snapshot.weekly = {true, 15, 0}; third.fiveChange.reset();
                app.historyRows.entries = {first, second, third}; app.renderHistory();
                if (gtk_tree_model_iter_n_children(GTK_TREE_MODEL(app.historyModel), nullptr) != 3) return fail("History list must display synthetic rows");
                gtk_stack_set_visible_child_name(GTK_STACK(app.historyStack), "chart");
                if (std::string(gtk_stack_get_visible_child_name(GTK_STACK(app.historyStack))) != "chart") return fail("History chart switch failed");
                auto surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 800, 400); auto context = cairo_create(surface); app.drawHistory(context, 800, 400); cairo_destroy(context); cairo_surface_destroy(surface);
                gtk_stack_set_visible_child_name(GTK_STACK(app.historyStack), "list");
                if (std::string(gtk_stack_get_visible_child_name(GTK_STACK(app.historyStack))) != "list") return fail("History list switch failed");
                gtk_combo_box_set_active(GTK_COMBO_BOX(app.historyKind), 1);
                if (gtk_tree_model_iter_n_children(GTK_TREE_MODEL(app.historyModel), nullptr) != 0) return fail("Request history must start empty");
                quota::RequestEntry request; request.turnId = "smoke-turn"; request.ended = NowMs(); request.tokens = quota::TokenCounts{100, 20, 0, 30, 5, 130}; request.status = "completed";
                request.threadId = "smoke-source-thread"; request.conversationLabel = "配额工具 · Smoke conversation " + std::string(600, 'x');
                auto fallback = request; fallback.turnId = "fallback-turn"; fallback.threadId = "smoke-fallback-thread"; fallback.conversationLabel.clear(); fallback.ended += 5;
                auto unknown = request; unknown.turnId = "unknown-turn"; unknown.threadId.clear(); unknown.conversationLabel.clear(); unknown.ended += 10; unknown.tokens.reset(); unknown.partial = true;
                app.requestRows.entries = {request, fallback, unknown}; app.renderHistory();
                GtkTreeIter firstRow; gchar *cost = nullptr, *input = nullptr; gtk_tree_model_get_iter_first(GTK_TREE_MODEL(app.historyModel), &firstRow);
                gtk_tree_model_get(GTK_TREE_MODEL(app.historyModel), &firstRow, 13, &cost, 3, &input, -1);
                bool unknownPreserved = cost && input && std::string(cost) == "—" && std::string(input) == "—"; g_free(cost); g_free(input);
                if (!unknownPreserved || gtk_tree_model_iter_n_children(GTK_TREE_MODEL(app.historyModel), nullptr) != 3) return fail("Request list must preserve unknown tokens and actual cost");
                // 检查实际单元格值，覆盖标题优先、原始 ID 回退及未知对话的双语显示。
                auto cellText = [&](int row, int column) { GtkTreeIter iter; gchar* value = nullptr; gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(app.historyModel), &iter, nullptr, row); gtk_tree_model_get(GTK_TREE_MODEL(app.historyModel), &iter, column, &value, -1); std::string result = value ? value : ""; g_free(value); return result; };
                auto conversationColumn = gtk_tree_view_get_column(GTK_TREE_VIEW(app.historyTable), 1);
                if (cellText(0, 1) != "Unknown conversation" || cellText(1, 1) != fallback.threadId || cellText(2, 1) != request.conversationLabel || cellText(2, 15) != "smoke-source-thread / smoke-turn" ||
                    gtk_tree_view_column_get_fixed_width(conversationColumn) != 260 || gtk_tree_view_column_get_max_width(conversationColumn) > 420) return fail("Conversation column must prefer the title, retain ID fallbacks and limit long-title width");
                auto languagePrefs = app.engine.preferences(); languagePrefs.chinese = true; app.engine.configure(languagePrefs); app.renderHistory();
                if (cellText(0, 1) != "未知对话" || std::string(gtk_tree_view_column_get_title(gtk_tree_view_get_column(GTK_TREE_VIEW(app.historyTable), 1))) != "所属对话") return fail("Unknown conversation and column title must translate into Chinese");
                languagePrefs.chinese = false; app.engine.configure(languagePrefs); app.renderHistory();
                surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 800, 400); context = cairo_create(surface); app.drawHistory(context, 800, 400); cairo_destroy(context); cairo_surface_destroy(surface);
                gtk_widget_destroy(app.historyWindow);
                app.testPassed = true; std::cout << "PASS: Linux tray/settings, X11 focus hiding, move-following, restore and host exit\n";
                std::cout << "PASS: Linux isolated history directory persistence, empty/list/chart states and synthetic drawing\n";
                std::cout << "PASS: Linux disabled history avoids directory probes; scrollable settings keep Save reachable\n";
                std::cout << "PASS: Linux request selector, token list/chart and unknown actual cost\n";
                std::cout << "PASS: Linux conversation title, ID/unknown fallbacks, localization and bounded column width\n";
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

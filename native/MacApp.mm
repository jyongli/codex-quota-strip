#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include "NativeCore.h"
#include "Localization.h"
#include <unistd.h>
#include <signal.h>
#include <sys/resource.h>
#include <iostream>
#include <memory>
#include <ctime>
#include <fstream>

using namespace nativequota;
static Options options;
static NSString* Text(const std::string& value) { return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding] ?: @"-"; }
static NSString* HistoryTime(long long ms) {
    std::time_t seconds = static_cast<std::time_t>(ms / 1000); std::tm local{}; localtime_r(&seconds, &local);
    char text[40]; std::strftime(text, sizeof(text), "%Y-%m-%d %H:%M:%S", &local); return Text(text);
}
static NSString* HistoryPercent(const quota::Window& value) {
    if (!value.present) return @"—";
    return [NSString stringWithFormat:@"%.1f%%", std::clamp(100.0 - value.used, 0.0, 100.0)];
}
static NSString* HistoryDelta(const std::optional<double>& value) { return value ? [NSString stringWithFormat:@"%+.1f", *value] : @"—"; }
// 图表只保留手动读取的有界快照；没有额外定时器或 WebView。
@interface QuotaHistoryChart : NSView {
@public
    std::vector<quota::HistoryEntry> rows;
    std::vector<quota::RequestEntry> requests;
    bool chinese;
    bool requestMode;
}
@end
@implementation QuotaHistoryChart
- (void)drawRect:(NSRect)dirtyRect {
    (void)dirtyRect; [[NSColor windowBackgroundColor] setFill]; NSRectFill(self.bounds);
    double left = 55, bottom = 48, right = std::max(left + 1, self.bounds.size.width - 28.0), top = std::max(bottom + 1, self.bounds.size.height - 48.0);
    NSDictionary* attributes = @{NSFontAttributeName: [NSFont systemFontOfSize:11], NSForegroundColorAttributeName: NSColor.labelColor};
    auto label = [&](NSString* text, double x, double y) { [text drawAtPoint:NSMakePoint(x, y) withAttributes:attributes]; };
    if (requestMode) {
        label(Text(Translate("Tokens per request: input (blue), output (green), cached input (orange)", chinese)), left, top + 18);
        if (requests.empty()) { label(Text(Translate("No work requests recorded yet.", chinese)), left + 12, top - 35); return; }
        double maximum = 1; for (const auto& row : requests) if (row.tokens) maximum = std::max(maximum, static_cast<double>(std::max(row.tokens->input, row.tokens->output)));
        for (int tick = 0; tick <= 4; ++tick) label(Text(std::to_string(static_cast<long long>(maximum * tick / 4))), 4, bottom + (top - bottom) * tick / 4 - 6);
        auto start = requests.front().ended, end = requests.back().ended; double span = std::max(1LL, end - start);
        double bar = std::clamp((right - left) / (requests.size() * 4), 1.0, 8.0);
        // 不把未知 token 画成零；缓存读取是输入的子集，单独上色以便观察。
        for (const auto& row : requests) if (row.tokens) {
            const long long values[]{row.tokens->input, row.tokens->output, row.tokens->cachedInput};
            double x = left + 12 + (right - left - 24) * (row.ended - start) / span;
            for (int series = 0; series < 3; ++series) {
                [(series == 0 ? NSColor.systemBlueColor : series == 1 ? NSColor.systemGreenColor : NSColor.systemOrangeColor) setFill];
                double h = (top - bottom) * values[series] / maximum; NSRectFill(NSMakeRect(x + (series - 1) * bar, bottom, bar, h));
            }
        }
        label(HistoryTime(start), left, 20); if (right - left > 360) label(HistoryTime(end), right - 160, 20); return;
    }
    label(Text(Translate("5h (blue) / Weekly (green) — remaining %", chinese)), left, top + 18);
    for (int percent : {0, 25, 50, 75, 100}) {
        double y = bottom + (top - bottom) * percent / 100.0;
        [NSColor.separatorColor setStroke]; auto line = [NSBezierPath bezierPath]; [line moveToPoint:NSMakePoint(left, y)]; [line lineToPoint:NSMakePoint(right, y)]; [line stroke];
        label(Text(std::to_string(percent)), 15, y - 6);
    }
    if (rows.empty()) { label(Text(Translate("No quota changes recorded yet.", chinese)), left + 12, top - 35); return; }
    auto start = rows.front().recorded, end = rows.back().recorded; double span = std::max(1LL, end - start);
    for (int metric = 0; metric < 2; ++metric) {
        NSColor* color = metric ? NSColor.systemGreenColor : NSColor.systemBlueColor; const quota::HistoryEntry* previousRow = nullptr; NSPoint previous{};
        for (const auto& row : rows) {
            const auto& value = metric ? row.snapshot.weekly : row.snapshot.fiveHour;
            if (!value.present) { previousRow = nullptr; continue; }
            auto point = NSMakePoint(left + (right - left) * (row.recorded - start) / span, bottom + (top - bottom) * std::clamp(100.0 - value.used, 0.0, 100.0) / 100.0);
            // 基线/重置用虚线参考连接；不把不同区间的跨度当作消耗。
            if (previousRow) { auto kind = quota::HistoryLineBetween(*previousRow, row, metric != 0); if (kind != quota::HistoryLineKind::Gap) {
                auto line = [NSBezierPath bezierPath]; line.lineWidth = 2;
                if (kind == quota::HistoryLineKind::Dashed) { CGFloat pattern[]{4, 4}; [line setLineDash:pattern count:2 phase:0]; }
                [color setStroke]; [line moveToPoint:previous]; [line lineToPoint:point]; [line stroke];
            } }
            [color setFill]; [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(point.x - 2.5, point.y - 2.5, 5, 5)] fill];
            previous = point; previousRow = &row;
        }
    }
    label(HistoryTime(start), left, 20); if (right - left > 360) label(HistoryTime(end), right - 160, 20);
}
@end
@interface QuotaPanel : NSPanel
@end
@implementation QuotaPanel
- (BOOL)canBecomeKeyWindow { return NO; }
- (BOOL)canBecomeMainWindow { return NO; }
@end

@interface QuotaDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate, NSTableViewDataSource> {
    std::unique_ptr<Engine> engine;
    std::unique_ptr<InstanceLock> singleton;
    NSStatusItem* statusItem;
    QuotaPanel* strip;
    NSTextField* stripText;
    NSWindow* config;
    NSView *configForm, *configActions;
    NSScrollView* configScroll;
    NSButton* configApply;
    NSTextField *status, *seconds, *stale, *home, *binary, *hostName;
    NSButton *online, *only, *show;
    NSPopUpButton* position;
    NSPopUpButton* language;
    NSButton* historyEnabled;
    NSTextField* historyDirectory;
    NSWindow* historyWindow;
    NSTextField* historyMessage;
    NSSegmentedControl* historyMode;
    NSScrollView* historyScroll;
    NSTableView* historyTable;
    QuotaHistoryChart* historyChart;
    quota::HistoryReadResult historyRows;
    NSPopUpButton* historyKind;
    quota::RequestReadResult requestRows;
    AXObserverRef observer;
    AXUIElementRef application, focused;
    pid_t observedPid;
    bool lastWeeklyOnly;
    std::filesystem::path configRoot;
    dispatch_source_t terminateSignal, interruptSignal;
}
@property BOOL smokePassed;
- (void)update;
- (void)updateHost;
- (void)clearObserver;
- (void)openSettings:(id)sender;
- (void)apply:(id)sender;
- (void)refresh:(id)sender;
- (void)permission:(id)sender;
- (void)quit:(id)sender;
- (NSString*)ui:(NSString*)english;
- (void)translateMenu;
- (void)openHistory:(id)sender;
- (void)showHistory:(BOOL)readHistory;
- (void)refreshHistory:(id)sender;
- (void)renderHistory;
- (BOOL)showingRequests;
- (void)switchHistory:(id)sender;
- (void)browseHistory:(id)sender;
@end
static void AxChanged(AXObserverRef, AXUIElementRef, CFStringRef, void* context) { [(__bridge QuotaDelegate*)context updateHost]; }

@implementation QuotaDelegate
- (NSString*)ui:(NSString*)english { return Text(Translate(english.UTF8String ?: "",engine && engine->preferences().chinese)); }
- (void)translateMenu {
    NSArray* labels=@[@"Refresh now",@"Quota history…",@"Settings…",@"Allow window following…",@"Quit"];
    for(NSUInteger i=0;i<labels.count;++i)[statusItem.menu itemAtIndex:i].title=[self ui:labels[i]];
}
- (BOOL)matches:(NSRunningApplication*)app {
    if (!app || app.processIdentifier == getpid()) return NO;
    auto prefs = engine->preferences();
    return HostMatches(app.bundleIdentifier.UTF8String ?: "", prefs.host) || HostMatches(app.localizedName.UTF8String ?: "", prefs.host);
}
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    // bundle 的品牌图标同时用于 Finder 和原生应用身份；菜单栏继续显示实时数值。
    NSString* iconPath = [NSBundle.mainBundle pathForResource:@"app" ofType:@"icns"];
    NSImage* applicationIcon = iconPath ? [[NSImage alloc] initWithContentsOfFile:iconPath] : nil;
    if (applicationIcon.isValid) NSApp.applicationIconImage = applicationIcon;
    configRoot = options.smoke ? std::filesystem::temp_directory_path() / ("quota-mac-smoke-" + std::to_string(getpid())) : ConfigDirectory();
    // 冒烟测试在 Engine 启动前覆盖日志路径，不触碰真实账户的默认目录。
    if (options.smoke) { options.preferences.home = (configRoot / "home").string(); options.preferences.historyDirectory = (configRoot / "history").string(); }
    // 用户级实例锁独立于所选账号目录，设置切换后仍阻止重复启动。
    singleton = std::make_unique<InstanceLock>(configRoot / "instance.lock");
    if (!singleton->acquired()) { [NSApp terminate:nil]; return; }
    engine = std::make_unique<Engine>(options.preferences);
    statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    statusItem.button.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightMedium];
    NSMenu* menu = [NSMenu new];
    for (NSArray* spec in @[@[@"Refresh now", NSStringFromSelector(@selector(refresh:))], @[@"Quota history…", NSStringFromSelector(@selector(openHistory:))], @[@"Settings…", NSStringFromSelector(@selector(openSettings:))], @[@"Allow window following…", NSStringFromSelector(@selector(permission:))], @[@"Quit", NSStringFromSelector(@selector(quit:))]]) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:spec[0] action:NSSelectorFromString(spec[1]) keyEquivalent:@""]; item.target = self; [menu addItem:item];
    }
    statusItem.menu = menu;
    [self translateMenu];
    strip = [[QuotaPanel alloc] initWithContentRect:NSMakeRect(0, 0, 248, 28) styleMask:NSWindowStyleMaskNonactivatingPanel backing:NSBackingStoreBuffered defer:NO];
    strip.opaque = NO; strip.backgroundColor = [NSColor colorWithRed:.12 green:.13 blue:.15 alpha:.97]; strip.level = NSFloatingWindowLevel;
    strip.ignoresMouseEvents = YES; strip.hidesOnDeactivate = NO;
    strip.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary | NSWindowCollectionBehaviorTransient;
    strip.contentView.wantsLayer = YES; strip.contentView.layer.cornerRadius = 7;
    stripText = [NSTextField labelWithString:@"5h -    W -"]; stripText.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightMedium]; stripText.textColor = NSColor.whiteColor; stripText.alignment = NSTextAlignmentCenter;
    stripText.frame = NSMakeRect(4, 3, 240, 22); [strip.contentView addSubview:stripText];
    __weak QuotaDelegate* weak = self;
    engine->start([weak] { dispatch_async(dispatch_get_main_queue(), ^{ [weak update]; }); });
    for (NSString* name in @[NSWorkspaceDidActivateApplicationNotification, NSWorkspaceDidLaunchApplicationNotification, NSWorkspaceDidTerminateApplicationNotification]) {
        [[NSWorkspace sharedWorkspace].notificationCenter addObserverForName:name object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification* note) { (void)note; [weak updateHost]; }];
    }
    [NSTimer scheduledTimerWithTimeInterval:30 repeats:YES block:^(NSTimer* timer) { (void)timer; [weak update]; }];
    // 将正常终止信号接入主循环，让查询线程有机会取消并清理子进程。
    signal(SIGTERM, SIG_IGN); signal(SIGINT, SIG_IGN);
    terminateSignal = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, SIGTERM, 0, dispatch_get_main_queue());
    dispatch_source_set_event_handler(terminateSignal, ^{ [weak quit:nil]; }); dispatch_resume(terminateSignal);
    interruptSignal = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, SIGINT, 0, dispatch_get_main_queue());
    dispatch_source_set_event_handler(interruptSignal, ^{ [weak quit:nil]; }); dispatch_resume(interruptSignal);
    [self update]; [self updateHost];
    if (options.settings) [self openSettings:nil];
    if (options.smoke) {
        [self openSettings:nil]; historyDirectory.stringValue = Text((configRoot / "selected-history").string()); seconds.stringValue = @"45";[language selectItemAtIndex:0]; [self apply:nil];
        auto stored = Load(configRoot / "settings.ini");
        auto frame = Placement({100, 50, 900, 600}, false);
        [strip setFrame:NSMakeRect(frame.x, frame.y, frame.width, frame.height) display:YES]; [strip orderFront:nil];
        BOOL passed = statusItem.button != nil && config.isVisible && engine->preferences().seconds == 45 && stored.seconds == 45 && stored.chinese && [config.title isEqualToString:@"Codex 配额 — 设置"] && !strip.canBecomeKeyWindow;
        passed = passed && applicationIcon.isValid && [[NSBundle.mainBundle objectForInfoDictionaryKey:@"CFBundleIconFile"] isEqualToString:@"app.icns"];
        if (passed) std::cout << "PASS: macOS packaged ICNS and bundle icon declaration\n";
        [language selectItemAtIndex:1];[self apply:nil];passed=passed&&[config.title isEqualToString:@"Codex Quota — Settings"]&&!Load(configRoot/"settings.ini").chinese;
        passed = passed && stored.historyDirectory == (configRoot / "selected-history").string();
        passed = passed && configScroll.documentView == configForm && configScroll.hasVerticalScroller && configApply.superview == configActions &&
            NSContainsRect(config.contentView.bounds, configActions.frame) && NSContainsRect(configActions.bounds, configApply.frame) && config.frame.size.height <= NSScreen.mainScreen.visibleFrame.size.height;
        [configForm scrollRectToVisible:historyDirectory.frame]; passed = passed && NSIntersectsRect(configForm.visibleRect, historyDirectory.frame);
        // 禁用时不创建目录；普通文件的子路径也应能够作为已禁用配置保存。
        auto uncreated = configRoot / "disabled-uncreated"; historyEnabled.state = NSControlStateValueOff; historyDirectory.stringValue = Text(uncreated.string()); [self apply:nil];
        passed = passed && !engine->preferences().historyEnabled && !Load(configRoot / "settings.ini").historyEnabled && !std::filesystem::exists(uncreated);
        auto blocker = configRoot / "history-blocker"; { std::ofstream file(blocker); file << "fixture"; }
        auto invalidDirectory = blocker / "child"; historyDirectory.stringValue = Text(invalidDirectory.string()); [self apply:nil];
        passed = passed && engine->preferences().historyDirectory == invalidDirectory.string() && Load(configRoot / "settings.ini").historyDirectory == invalidDirectory.string();
        historyEnabled.state = NSControlStateValueOn; historyDirectory.stringValue = Text((configRoot / "selected-history").string()); [self apply:nil];
        [self openHistory:nil]; passed = passed && historyRows.entries.empty() && historyTable.numberOfRows == 0;
        // 合成记录不写入账户日志；断线边界与空态均由相同的 UI 渲染路径处理。
        quota::HistoryEntry first; first.recorded = NowMs() - 1000; first.snapshot.observed = first.recorded; first.snapshot.fiveHour = {true, 20, 0}; first.kind = "baseline"; first.source = "smoke";
        auto second = first; second.recorded += 500; second.kind = "change"; second.snapshot.fiveHour.used = 30; second.fiveChange = -10;
        auto third = second; third.recorded += 500; third.kind = "reset"; third.snapshot.fiveHour.present = false; third.snapshot.weekly = {true, 15, 0}; third.fiveChange.reset();
        historyRows.entries = {first, second, third}; [self renderHistory]; passed = passed && historyTable.numberOfRows == 3;
        historyMode.selectedSegment = 1; [self switchHistory:nil]; [historyChart display]; passed = passed && !historyChart.hidden && historyScroll.hidden;
        historyMode.selectedSegment = 0; [self switchHistory:nil]; passed = passed && historyChart.hidden && !historyScroll.hidden;
        [historyKind selectItemAtIndex:1]; [self refreshHistory:nil]; passed = passed && historyTable.numberOfRows == 0;
        quota::RequestEntry request; request.turnId = "smoke-turn"; request.ended = NowMs(); request.tokens = quota::TokenCounts{100, 20, 0, 30, 5, 130}; request.status = "completed";
        request.threadId = "smoke-source-thread"; request.conversationLabel = "配额工具 · Smoke conversation " + std::string(600, 'x');
        auto fallback = request; fallback.turnId = "fallback-turn"; fallback.threadId = "smoke-fallback-thread"; fallback.conversationLabel.clear(); fallback.ended += 5;
        auto unknown = request; unknown.turnId = "unknown-turn"; unknown.threadId.clear(); unknown.conversationLabel.clear(); unknown.ended += 10; unknown.tokens.reset(); unknown.partial = true;
        requestRows.entries = {request, fallback, unknown}; [self renderHistory];
        passed = passed && historyTable.numberOfRows == 3 && [[self tableView:historyTable objectValueForTableColumn:historyTable.tableColumns[13] row:0] isEqualToString:@"—"] &&
            [[self tableView:historyTable objectValueForTableColumn:historyTable.tableColumns[3] row:0] isEqualToString:@"—"];
        // 直接读取真实表格单元格，确保显示标题时仍保留末列的原始标识。
        passed = passed && [[self tableView:historyTable objectValueForTableColumn:historyTable.tableColumns[1] row:0] isEqualToString:@"Unknown conversation"] &&
            [[self tableView:historyTable objectValueForTableColumn:historyTable.tableColumns[1] row:1] isEqualToString:Text(fallback.threadId)] &&
            [[self tableView:historyTable objectValueForTableColumn:historyTable.tableColumns[1] row:2] isEqualToString:Text(request.conversationLabel)] &&
            [[self tableView:historyTable objectValueForTableColumn:historyTable.tableColumns[15] row:2] isEqualToString:@"smoke-source-thread / smoke-turn"] && historyTable.tableColumns[1].width >= 120 && historyTable.tableColumns[1].maxWidth <= 420;
        auto languagePrefs = engine->preferences(); languagePrefs.chinese = true; engine->configure(languagePrefs); [self renderHistory];
        passed = passed && [[self tableView:historyTable objectValueForTableColumn:historyTable.tableColumns[1] row:0] isEqualToString:@"未知对话"] && [historyTable.tableColumns[1].title isEqualToString:@"所属对话"];
        languagePrefs.chinese = false; engine->configure(languagePrefs); [self renderHistory];
        historyMode.selectedSegment = 1; [self switchHistory:nil]; [historyChart display]; passed = passed && !historyChart.hidden; [historyWindow close];
        [strip orderOut:nil]; self.smokePassed = passed && !strip.isVisible;
        std::cout << (self.smokePassed ? "PASS: macOS menu bar, native settings persistence, passive overlay and hide lifecycle\n" : "FAIL: macOS native UI smoke test\n");
        if (self.smokePassed) std::cout << "PASS: macOS isolated history directory persistence, empty/list/chart states and synthetic drawing\n";
        if (self.smokePassed) std::cout << "PASS: macOS disabled history avoids directory probes; scrollable settings keep Save reachable\n";
        if (self.smokePassed) std::cout << "PASS: macOS request selector, token list/chart and unknown actual cost\n";
        if (self.smokePassed) std::cout << "PASS: macOS conversation title, ID/unknown fallbacks, localization and bounded column width\n";
        [config close];
        rusage before{}; getrusage(RUSAGE_SELF, &before);
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), dispatch_get_main_queue(), ^{
            rusage after{}; getrusage(RUSAGE_SELF, &after);
            auto cpu = [](const rusage& value) { return value.ru_utime.tv_sec + value.ru_stime.tv_sec + (value.ru_utime.tv_usec + value.ru_stime.tv_usec) / 1000000.0; };
            std::cout << "{\"idle_seconds\":2,\"idle_cpu_seconds\":" << cpu(after) - cpu(before) << ",\"peak_rss_mib\":" << after.ru_maxrss / (1024.0 * 1024.0) << "}\n";
            [weak quit:nil];
        });
    }
}
- (void)clearObserver {
    if (observer) { CFRunLoopRemoveSource(CFRunLoopGetMain(), AXObserverGetRunLoopSource(observer), kCFRunLoopCommonModes); CFRelease(observer); observer = nullptr; }
    if (focused) { CFRelease(focused); focused = nullptr; }
    if (application) { CFRelease(application); application = nullptr; }
    observedPid = 0;
}
- (void)updateHost {
    if (!engine || options.smoke) return;
    BOOL running = NO;
    for (NSRunningApplication* app in [NSWorkspace sharedWorkspace].runningApplications) if ([self matches:app]) { running = YES; break; }
    engine->hostAvailable(running);
    NSRunningApplication* front = [NSWorkspace sharedWorkspace].frontmostApplication;
    if (![self matches:front] || !engine->preferences().overlay || !AXIsProcessTrusted()) { [strip orderOut:nil]; [self clearObserver]; [self update]; return; }
    if (observedPid != front.processIdentifier) {
        [self clearObserver]; observedPid = front.processIdentifier; application = AXUIElementCreateApplication(observedPid);
        if (AXObserverCreate(observedPid, AxChanged, &observer) != kAXErrorSuccess) { observer = nullptr; [strip orderOut:nil]; return; }
        for (CFStringRef name : {kAXFocusedWindowChangedNotification, kAXWindowCreatedNotification}) AXObserverAddNotification(observer, application, name, (__bridge void*)self);
        CFRunLoopAddSource(CFRunLoopGetMain(), AXObserverGetRunLoopSource(observer), kCFRunLoopCommonModes);
    }
    CFTypeRef next = nullptr;
    if (AXUIElementCopyAttributeValue(application, kAXFocusedWindowAttribute, &next) != kAXErrorSuccess || !next || CFGetTypeID(next) != AXUIElementGetTypeID()) { if (next) CFRelease(next); [strip orderOut:nil]; return; }
    if (!focused || !CFEqual(focused, next)) {
        if (focused) {
            for (CFStringRef name : {kAXMovedNotification, kAXResizedNotification, kAXUIElementDestroyedNotification, kAXWindowMiniaturizedNotification, kAXWindowDeminiaturizedNotification}) AXObserverRemoveNotification(observer, focused, name);
            CFRelease(focused);
        }
        focused = static_cast<AXUIElementRef>(next);
        for (CFStringRef name : {kAXMovedNotification, kAXResizedNotification, kAXUIElementDestroyedNotification, kAXWindowMiniaturizedNotification, kAXWindowDeminiaturizedNotification}) AXObserverAddNotification(observer, focused, name, (__bridge void*)self);
    } else CFRelease(next);
    CFTypeRef point = nullptr, size = nullptr, minimized = nullptr; CGPoint origin{}; CGSize dimensions{};
    bool valid = AXUIElementCopyAttributeValue(focused, kAXPositionAttribute, &point) == kAXErrorSuccess && AXUIElementCopyAttributeValue(focused, kAXSizeAttribute, &size) == kAXErrorSuccess && point && size &&
        CFGetTypeID(point) == AXValueGetTypeID() && CFGetTypeID(size) == AXValueGetTypeID() && AXValueGetValue(static_cast<AXValueRef>(point), static_cast<AXValueType>(kAXValueCGPointType), &origin) && AXValueGetValue(static_cast<AXValueRef>(size), static_cast<AXValueType>(kAXValueCGSizeType), &dimensions);
    if (AXUIElementCopyAttributeValue(focused, kAXMinimizedAttribute, &minimized) == kAXErrorSuccess && minimized == kCFBooleanTrue) valid = false;
    if (point) CFRelease(point); if (size) CFRelease(size); if (minimized) CFRelease(minimized);
    if (!valid || dimensions.width < 100 || dimensions.height < 40) { [strip orderOut:nil]; return; }
    auto frame = Placement({origin.x, origin.y, dimensions.width, dimensions.height}, engine->preferences().left, quota::WeeklyOnly(engine->state().snapshot));
    // AX 使用主屏左上角坐标；AppKit 窗口使用左下角坐标，保留跨屏的全局 X/Y。
    double mainHeight = NSMaxY([NSScreen screens].firstObject.frame);
    [strip setFrame:NSMakeRect(frame.x, mainHeight - frame.y - frame.height, frame.width, frame.height) display:YES];
    stripText.frame = NSMakeRect(4, 3, frame.width - 8, frame.height - 6); [self update]; [strip orderFront:nil];
}
- (void)update {
    if (!engine) return;
    auto current = engine->state(); auto prefs = engine->preferences(); auto now = NowMs(); auto text = Summary(current, prefs, now);
    statusItem.button.title = Text(text);
    bool weeklyOnly = quota::WeeklyOnly(current.snapshot);
    std::string panel = prefs.left ? (weeklyOnly ? "" : "5h\n" + Remaining(current.snapshot.fiveHour, now) + "\n") + "W\n" + Remaining(current.snapshot.weekly, now) : text;
    stripText.stringValue = Text(panel); stripText.maximumNumberOfLines = prefs.left ? 4 : 1;
    stripText.textColor = Stale(current, prefs, now) ? NSColor.lightGrayColor : NSColor.whiteColor;
    if (lastWeeklyOnly != weeklyOnly) { lastWeeklyOnly = weeklyOnly; [self updateHost]; }
    if (status) {
        std::string detail = text + "\n" + Translate(current.refreshing ? "Refreshing…" : !current.error.empty() ? current.error.c_str() : !prefs.online ? "Online queries disabled" : "Account quota",prefs.chinese);
        if (!current.historyError.empty()) detail += "\n" + std::string(Translate(current.historyError.c_str(), prefs.chinese));
        if (!current.requestError.empty()) detail += "\n" + std::string(Translate(current.requestError.c_str(), prefs.chinese));
        if (!options.smoke && prefs.overlay && !AXIsProcessTrusted()) detail += Translate("\nAllow Accessibility access to follow Codex windows.",prefs.chinese);
        status.stringValue = Text(detail);
    }
}
- (NSTextField*)label:(NSString*)title y:(CGFloat)y {
    auto label = [NSTextField labelWithString:[self ui:title]]; label.frame = NSMakeRect(24, y, 235, 24); [configForm addSubview:label]; return label;
}
- (NSTextField*)field:(NSString*)title value:(NSString*)value y:(CGFloat)y {
    [self label:title y:y]; auto input = [[NSTextField alloc] initWithFrame:NSMakeRect(260, y, 276, 24)]; input.stringValue = value; [configForm addSubview:input]; return input;
}
- (NSButton*)check:(NSString*)title enabled:(BOOL)enabled y:(CGFloat)y {
    auto button = [NSButton checkboxWithTitle:[self ui:title] target:nil action:nil]; button.frame = NSMakeRect(24, y, 500, 25); button.state = enabled ? NSControlStateValueOn : NSControlStateValueOff; [configForm addSubview:button]; return button;
}
- (void)openSettings:(id)sender {
    (void)sender;
    if (config) { [NSApp activateIgnoringOtherApps:YES]; [config makeKeyAndOrderFront:nil]; return; }
    auto prefs = engine->preferences();
    NSRect workarea = NSScreen.mainScreen ? NSScreen.mainScreen.visibleFrame : NSMakeRect(0, 0, 1024, 768);
    CGFloat height = std::min(640.0, std::max(260.0, workarea.size.height - 80.0));
    config = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 560, height) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
    config.title = [self ui:@"Codex Quota — Settings"]; config.releasedWhenClosed = NO; config.delegate = self;
    // 表单保留原坐标，放入滚动视口；底部操作区不滚动，保存键始终可达。
    configScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 80, 560, height - 80)]; configScroll.hasVerticalScroller = YES;
    configScroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    configForm = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 560, 760)]; configScroll.documentView = configForm; [config.contentView addSubview:configScroll];
    configActions = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 560, 80)]; configActions.autoresizingMask = NSViewWidthSizable | NSViewMaxYMargin; [config.contentView addSubview:configActions];
    status = [NSTextField wrappingLabelWithString:@""]; status.frame = NSMakeRect(24, 660, 512, 76); status.font = [NSFont systemFontOfSize:14]; [configForm addSubview:status];
    online = [self check:@"Query Codex account online" enabled:prefs.online y:620];
    only = [self check:@"Query only while the host application is open" enabled:prefs.onlyWithCodex y:586];
    show = [self check:@"Show window-following overlay" enabled:prefs.overlay y:552];
    seconds = [self field:@"Refresh interval (30–3600 s)" value:Text(std::to_string(prefs.seconds)) y:510];
    stale = [self field:@"Stale after (1–120 min)" value:Text(std::to_string(prefs.staleMinutes)) y:472];
    [self label:@"Overlay position" y:434]; position = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(260, 434, 276, 26) pullsDown:NO]; [position addItemsWithTitles:@[[self ui:@"Top"], [self ui:@"Left"]]]; [position selectItemAtIndex:prefs.left ? 1 : 0]; [configForm addSubview:position];
    home = [self field:@"Codex data directory" value:Text(prefs.home) y:396];
    binary = [self field:@"Codex executable (blank = auto)" value:Text(prefs.binary) y:358];
    hostName = [self field:@"Host application contains" value:Text(prefs.host) y:320];
    [self label:@"Language" y:282];language=[[NSPopUpButton alloc]initWithFrame:NSMakeRect(260,282,276,26)pullsDown:NO];[language addItemsWithTitles:@[@"中文",@"English"]];[language selectItemAtIndex:prefs.chinese?0:1];[configForm addSubview:language];
    historyEnabled = [self check:@"Record quota changes and request tokens" enabled:prefs.historyEnabled y:240];
    historyDirectory = [self field:@"History directory" value:Text(prefs.historyDirectory) y:202]; historyDirectory.frame = NSMakeRect(260, 202, 190, 24);
    auto browse = [NSButton buttonWithTitle:[self ui:@"Browse…"] target:self action:@selector(browseHistory:)]; browse.frame = NSMakeRect(454, 199, 82, 30); [configForm addSubview:browse];
    auto history = [NSButton buttonWithTitle:[self ui:@"Quota history…"] target:self action:@selector(openHistory:)]; history.frame = NSMakeRect(24, 156, 230, 32); [configForm addSubview:history];
    auto permission = [NSButton buttonWithTitle:[self ui:@"Allow Accessibility…"] target:self action:@selector(permission:)]; permission.frame = NSMakeRect(24, 85, 230, 32); [configForm addSubview:permission];
    auto refresh = [NSButton buttonWithTitle:[self ui:@"Refresh now"] target:self action:@selector(refresh:)]; refresh.frame = NSMakeRect(24, 30, 130, 34); [configActions addSubview:refresh];
    configApply = [NSButton buttonWithTitle:[self ui:@"Save and apply"] target:self action:@selector(apply:)]; configApply.frame = NSMakeRect(380, 30, 156, 34); configApply.keyEquivalent = @"\r"; [configActions addSubview:configApply];
    [configScroll.contentView scrollToPoint:NSMakePoint(0, std::max(0.0, configForm.frame.size.height - configScroll.contentView.bounds.size.height))]; [configScroll reflectScrolledClipView:configScroll.contentView];
    [config center]; [self update]; [NSApp activateIgnoringOtherApps:YES]; [config makeKeyAndOrderFront:nil];
}
- (void)windowWillClose:(NSNotification*)notification {
    if (notification.object == config) { config = nil; status = nil; seconds = nil; stale = nil; home = nil; binary = nil; hostName = nil; online = nil; only = nil; show = nil; position = nil; language=nil; historyDirectory=nil; historyEnabled=nil; configForm=nil; configActions=nil; configScroll=nil; configApply=nil; }
    if (notification.object == historyWindow) { historyWindow=nil; historyMessage=nil; historyMode=nil; historyScroll=nil; historyTable=nil; historyChart=nil; historyKind=nil; historyRows={}; requestRows={}; }
}
- (void)apply:(id)sender {
    (void)sender; auto prefs = engine->preferences();
    auto number = [](NSString* value, int& result) { NSScanner* scanner = [NSScanner scannerWithString:value]; NSInteger parsed; if (![scanner scanInteger:&parsed] || !scanner.isAtEnd || parsed < 0 || parsed > 3600) return false; result = static_cast<int>(parsed); return true; };
    if (!number(seconds.stringValue, prefs.seconds) || !number(stale.stringValue, prefs.staleMinutes)) { status.stringValue = [self ui:@"Enter valid whole-number intervals."]; return; }
    prefs.online = online.state == NSControlStateValueOn; prefs.onlyWithCodex = only.state == NSControlStateValueOn; prefs.overlay = show.state == NSControlStateValueOn; prefs.left = position.indexOfSelectedItem == 1;
    bool changedLanguage=prefs.chinese!=(language.indexOfSelectedItem==0);prefs.chinese=language.indexOfSelectedItem==0;
    prefs.home = home.stringValue.UTF8String ?: ""; prefs.binary = binary.stringValue.UTF8String ?: ""; prefs.host = hostName.stringValue.UTF8String ?: "";
    prefs.historyEnabled = historyEnabled.state == NSControlStateValueOn; prefs.historyDirectory = historyDirectory.stringValue.UTF8String ?: "";
    std::string directoryError;
    // 禁用历史只校验配置格式；不创建目录，也不向旧失效目录写探针。
    if (prefs.historyEnabled && !quota::HistoryStore::ValidateDirectory(std::filesystem::path(prefs.historyDirectory), directoryError)) { status.stringValue = [self ui:@"Cannot use history directory; choose an absolute writable path."]; return; }
    if (!Save(configRoot / "settings.ini", prefs)) { status.stringValue = [self ui:@"Cannot save: check absolute home path, values and permissions."]; return; }
    engine->configure(prefs); [self updateHost]; [self update];
    if (historyWindow) { if (prefs.historyEnabled) [self refreshHistory:nil]; else { historyRows = {}; requestRows = {}; [self renderHistory]; } }
    if(changedLanguage){[self translateMenu];[config close];[self openSettings:nil];if(historyWindow){[historyWindow close];[self showHistory:prefs.historyEnabled];}}
}
- (void)browseHistory:(id)sender {
    (void)sender; auto panel = [NSOpenPanel openPanel]; panel.canChooseDirectories = YES; panel.canChooseFiles = NO; panel.allowsMultipleSelection = NO; panel.canCreateDirectories = YES;
    panel.title = [self ui:@"Choose history directory"]; panel.prompt = [self ui:@"Choose"];
    panel.directoryURL = [NSURL fileURLWithPath:historyDirectory.stringValue isDirectory:YES];
    [panel beginSheetModalForWindow:config completionHandler:^(NSModalResponse response) {
        if (response == NSModalResponseOK && panel.URL.isFileURL && historyDirectory) historyDirectory.stringValue = panel.URL.path;
    }];
}
- (BOOL)showingRequests { return historyKind && historyKind.indexOfSelectedItem == 1; }
- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView { (void)tableView; return static_cast<NSInteger>([self showingRequests] ? requestRows.entries.size() : historyRows.entries.size()); }
- (id)tableView:(NSTableView*)tableView objectValueForTableColumn:(NSTableColumn*)column row:(NSInteger)index {
    (void)tableView;
    if ([self showingRequests]) {
        if (index < 0 || static_cast<size_t>(index) >= requestRows.entries.size()) return @"";
        const auto& row = requestRows.entries[requestRows.entries.size() - 1 - static_cast<size_t>(index)];
        auto count = [&](long long quota::TokenCounts::*member) -> NSString* { return row.tokens ? Text(std::to_string((*row.tokens).*member)) : @"—"; };
        NSString* status = [self ui:row.status == "completed" ? @"Completed" : row.status == "interrupted" ? @"Interrupted" : row.status == "incomplete" ? @"Incomplete" : Text(row.status)];
        if (row.partial) status = [status stringByAppendingFormat:@" / %@", [self ui:@"Partial"]];
        switch (column.identifier.integerValue) {
            case 0: return HistoryTime(row.ended);
            // 标题不写回日志；显示时保留 ID 回退，未知对话沿用当前界面语言。
            case 1: return !row.conversationLabel.empty() ? Text(row.conversationLabel) : !row.threadId.empty() ? Text(row.threadId) : [self ui:@"Unknown conversation"];
            case 2: return row.model.empty() ? @"—" : Text(row.model);
            case 3: return count(&quota::TokenCounts::input); case 4: return count(&quota::TokenCounts::cachedInput); case 5: return count(&quota::TokenCounts::cacheWriteInput);
            case 6: return count(&quota::TokenCounts::output); case 7: return count(&quota::TokenCounts::reasoningOutput); case 8: return count(&quota::TokenCounts::total);
            case 9: return [NSString stringWithFormat:@"%@ → %@", HistoryPercent(row.quotaStart.fiveHour), HistoryPercent(row.quotaEnd.fiveHour)];
            case 10: return [NSString stringWithFormat:@"%@ → %@", HistoryPercent(row.quotaStart.weekly), HistoryPercent(row.quotaEnd.weekly)];
            case 11: return HistoryDelta(row.fiveChange); case 12: return HistoryDelta(row.weeklyChange); case 13: return @"—";
            case 14: return status;
            default: return [NSString stringWithFormat:@"%@ / %@", row.threadId.empty() ? @"—" : Text(row.threadId), Text(row.turnId)];
        }
    }
    if (index < 0 || static_cast<size_t>(index) >= historyRows.entries.size()) return @"";
    const auto& row = historyRows.entries[historyRows.entries.size() - 1 - static_cast<size_t>(index)];
    switch (column.identifier.integerValue) {
        case 0: return HistoryTime(row.recorded);
        case 1: return HistoryPercent(row.snapshot.fiveHour);
        case 2: return HistoryPercent(row.snapshot.weekly);
        case 3: return HistoryDelta(row.fiveChange);
        case 4: return HistoryDelta(row.weeklyChange);
        default: return [NSString stringWithFormat:@"%@ / %@", [self ui:row.source == "account" ? @"Account query" : row.source == "local-event" ? @"Local event" : Text(row.source)], [self ui:row.kind == "baseline" ? @"Baseline" : row.kind == "reset" ? @"Reset" : @"Change"]];
    }
}
- (void)renderHistory {
    if (!historyWindow) return;
    auto prefs = engine->preferences(); historyChart->rows = historyRows.entries; historyChart->chinese = prefs.chinese;
    historyChart->requests = requestRows.entries; historyChart->requestMode = [self showingRequests];
    for (NSTableColumn* column in [historyTable.tableColumns copy]) [historyTable removeTableColumn:column];
    NSArray* headings = [self showingRequests] ? @[@"Time (local)", @"Conversation", @"Model", @"Input tokens", @"Cached input tokens", @"Cache write tokens", @"Output tokens", @"Reasoning tokens", @"Total tokens", @"5h quota (start → end)", @"Weekly quota (start → end)", @"5h account change (pp)", @"Weekly account change (pp)", @"Actual cost", @"Status", @"Thread / turn"] :
        @[@"Time (local)", @"5h remaining", @"Weekly remaining", @"5h change (pp)", @"Weekly change (pp)", @"Source / event"];
    for (NSUInteger index = 0; index < headings.count; ++index) {
        auto column = [[NSTableColumn alloc] initWithIdentifier:[NSString stringWithFormat:@"%lu", static_cast<unsigned long>(index)]];
        column.title = [self ui:headings[index]]; column.width = index == 0 ? 175 : 150; column.minWidth = 90; column.editable = NO; [historyTable addTableColumn:column];
        if ([self showingRequests] && index == 1) {
            // 对话列单行截断并限制最大宽度，长标题不会挤走其余额度和 token 列。
            column.minWidth = 120; column.maxWidth = 420; column.width = 260;
            NSTextFieldCell* cell = (NSTextFieldCell*)column.dataCell; cell.usesSingleLineMode = YES; cell.lineBreakMode = NSLineBreakByTruncatingTail;
        }
    }
    [historyTable reloadData]; historyChart.needsDisplay = YES;
    NSMutableString* message = [[self ui:@"Latest 30 UTC days; at most 2000 records. Refresh manually."] mutableCopy];
    if (!prefs.historyEnabled) [message appendFormat:@"\n%@", [self ui:@"History recording is disabled."]];
    auto readError = [self showingRequests] ? requestRows.error : historyRows.error;
    if (!readError.empty()) [message appendFormat:@"\n%@", [self ui:Text(readError)]];
    auto writeError = [self showingRequests] ? engine->state().requestError : engine->state().historyError;
    if (!writeError.empty()) [message appendFormat:@"\n%@", [self ui:Text(writeError)]];
    if ([self showingRequests] ? requestRows.truncated : historyRows.truncated) [message appendFormat:@"\n%@", [self ui:@"Showing newest records; older records were omitted."]];
    if ([self showingRequests]) {
        [message appendFormat:@"\n%@\n%@", [self ui:@"Account quota changes may include other work. Actual cost is unavailable."], [self ui:@"Cached input is part of input; reasoning is part of output."]];
        if (requestRows.entries.empty()) [message appendFormat:@"\n%@", [self ui:@"No work requests recorded yet."]];
    } else { [message appendFormat:@"\n%@", [self ui:@"Solid: continuous records. Dashed: baseline/source/reset boundaries, reference only, not consumption. Missing values stay disconnected."]]; if (historyRows.entries.empty()) [message appendFormat:@"\n%@", [self ui:@"No quota changes recorded yet."]]; }
    historyMessage.stringValue = message;
}
- (void)refreshHistory:(id)sender { (void)sender; if (historyWindow) { if ([self showingRequests]) requestRows = engine->requests(2000); else historyRows = engine->history(2000); [self renderHistory]; } }
- (void)switchHistory:(id)sender {
    (void)sender; BOOL chart = historyMode.selectedSegment == 1; historyScroll.hidden = chart; historyChart.hidden = !chart;
    if (chart) historyChart.needsDisplay = YES;
}
- (void)openHistory:(id)sender {
    (void)sender; [self showHistory:YES];
}
- (void)showHistory:(BOOL)readHistory {
    if (historyWindow) { [self refreshHistory:nil]; [NSApp activateIgnoringOtherApps:YES]; [historyWindow makeKeyAndOrderFront:nil]; return; }
    historyWindow = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 960, 540) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable backing:NSBackingStoreBuffered defer:NO];
    historyWindow.title = [self ui:@"Codex Quota — History"]; historyWindow.minSize = NSMakeSize(640, 400); historyWindow.releasedWhenClosed = NO; historyWindow.delegate = self;
    historyMode = [[NSSegmentedControl alloc] initWithFrame:NSMakeRect(16, 496, 190, 28)]; historyMode.segmentCount = 2;
    [historyMode setLabel:[self ui:@"List"] forSegment:0]; [historyMode setLabel:[self ui:@"Chart"] forSegment:1]; historyMode.selectedSegment = 0; historyMode.target = self; historyMode.action = @selector(switchHistory:); historyMode.autoresizingMask = NSViewMaxXMargin | NSViewMinYMargin; [historyWindow.contentView addSubview:historyMode];
    historyKind = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(220, 496, 230, 28) pullsDown:NO]; [historyKind addItemsWithTitles:@[[self ui:@"Quota changes"], [self ui:@"Work requests"]]]; historyKind.target = self; historyKind.action = @selector(refreshHistory:); historyKind.autoresizingMask = NSViewMaxXMargin | NSViewMinYMargin; [historyWindow.contentView addSubview:historyKind];
    auto refresh = [NSButton buttonWithTitle:[self ui:@"Refresh history"] target:self action:@selector(refreshHistory:)]; refresh.frame = NSMakeRect(764, 496, 180, 28); refresh.autoresizingMask = NSViewMinXMargin | NSViewMinYMargin; [historyWindow.contentView addSubview:refresh];
    historyMessage = [NSTextField wrappingLabelWithString:@""]; historyMessage.frame = NSMakeRect(16, 392, 928, 94); historyMessage.autoresizingMask = NSViewWidthSizable | NSViewMinYMargin; [historyWindow.contentView addSubview:historyMessage];
    historyScroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(16, 16, 928, 366)]; historyScroll.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable; historyScroll.hasVerticalScroller = YES; historyScroll.hasHorizontalScroller = YES;
    historyTable = [[NSTableView alloc] initWithFrame:historyScroll.bounds]; historyTable.dataSource = self;
    historyScroll.documentView = historyTable; [historyWindow.contentView addSubview:historyScroll];
    historyChart = [[QuotaHistoryChart alloc] initWithFrame:historyScroll.frame]; historyChart.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable; historyChart.hidden = YES; [historyWindow.contentView addSubview:historyChart];
    if (readHistory) [self refreshHistory:nil]; else { historyRows = {}; [self renderHistory]; }
    [historyWindow center]; [NSApp activateIgnoringOtherApps:YES]; [historyWindow makeKeyAndOrderFront:nil];
}
- (void)refresh:(id)sender { (void)sender; if (engine) engine->refresh(); }
- (void)permission:(id)sender {
    (void)sender;
    // 只有用户点击此按钮才向系统请求窗口观察权限；不请求录屏权限。
    NSDictionary* settings = @{(__bridge NSString*)kAXTrustedCheckOptionPrompt: @YES}; AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)settings); [self updateHost];
}
- (void)quit:(id)sender { (void)sender; [NSApp terminate:nil]; }
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    (void)sender; if (engine) engine->stop(); [self clearObserver];
    if (statusItem) [[NSStatusBar systemStatusBar] removeStatusItem:statusItem];
    if (options.smoke) { std::filesystem::remove(configRoot / "settings.ini"); }
    singleton.reset();
    if (options.smoke) std::filesystem::remove_all(configRoot);
    // NSApplication 正常终止默认使用状态 0；测试失败必须显式传回非零状态。
    if (options.smoke && !self.smokePassed) std::exit(1);
    return NSTerminateNow;
}
@end
int main(int argc, char** argv) {
    @autoreleasepool {
        try {
            options = ParseOptions(argc, argv); [NSApplication sharedApplication]; [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
            QuotaDelegate* delegate = [QuotaDelegate new]; NSApp.delegate = delegate; [NSApp run]; return options.smoke && !delegate.smokePassed ? 1 : 0;
        } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    }
}

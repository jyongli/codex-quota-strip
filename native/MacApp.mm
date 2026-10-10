#import <AppKit/AppKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include "NativeCore.h"
#include <unistd.h>
#include <signal.h>
#include <sys/resource.h>
#include <iostream>
#include <memory>

using namespace nativequota;
static Options options;
static NSString* Text(const std::string& value) { return [[NSString alloc] initWithBytes:value.data() length:value.size() encoding:NSUTF8StringEncoding] ?: @"-"; }
@interface QuotaPanel : NSPanel
@end
@implementation QuotaPanel
- (BOOL)canBecomeKeyWindow { return NO; }
- (BOOL)canBecomeMainWindow { return NO; }
@end

@interface QuotaDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate> {
    std::unique_ptr<Engine> engine;
    std::unique_ptr<InstanceLock> singleton;
    NSStatusItem* statusItem;
    QuotaPanel* strip;
    NSTextField* stripText;
    NSWindow* config;
    NSTextField *status, *seconds, *stale, *home, *binary, *hostName;
    NSButton *online, *only, *show;
    NSPopUpButton* position;
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
@end
static void AxChanged(AXObserverRef, AXUIElementRef, CFStringRef, void* context) { [(__bridge QuotaDelegate*)context updateHost]; }

@implementation QuotaDelegate
- (BOOL)matches:(NSRunningApplication*)app {
    if (!app || app.processIdentifier == getpid()) return NO;
    auto prefs = engine->preferences();
    return HostMatches(app.bundleIdentifier.UTF8String ?: "", prefs.host) || HostMatches(app.localizedName.UTF8String ?: "", prefs.host);
}
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    (void)notification;
    configRoot = options.smoke ? std::filesystem::temp_directory_path() / ("quota-mac-smoke-" + std::to_string(getpid())) : ConfigDirectory();
    // 用户级实例锁独立于所选账号目录，设置切换后仍阻止重复启动。
    singleton = std::make_unique<InstanceLock>(configRoot / "instance.lock");
    if (!singleton->acquired()) { [NSApp terminate:nil]; return; }
    engine = std::make_unique<Engine>(options.preferences);
    statusItem = [[NSStatusBar systemStatusBar] statusItemWithLength:NSVariableStatusItemLength];
    statusItem.button.font = [NSFont monospacedSystemFontOfSize:12 weight:NSFontWeightMedium];
    NSMenu* menu = [NSMenu new];
    for (NSArray* spec in @[@[@"Refresh now", NSStringFromSelector(@selector(refresh:))], @[@"Settings…", NSStringFromSelector(@selector(openSettings:))], @[@"Allow window following…", NSStringFromSelector(@selector(permission:))], @[@"Quit", NSStringFromSelector(@selector(quit:))]]) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:spec[0] action:NSSelectorFromString(spec[1]) keyEquivalent:@""]; item.target = self; [menu addItem:item];
    }
    statusItem.menu = menu;
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
        [self openSettings:nil]; seconds.stringValue = @"45"; [self apply:nil];
        auto stored = Load(configRoot / "settings.ini");
        auto frame = Placement({100, 50, 900, 600}, false);
        [strip setFrame:NSMakeRect(frame.x, frame.y, frame.width, frame.height) display:YES]; [strip orderFront:nil];
        BOOL passed = statusItem.button != nil && config.isVisible && engine->preferences().seconds == 45 && stored.seconds == 45 && !strip.canBecomeKeyWindow;
        [strip orderOut:nil]; self.smokePassed = passed && !strip.isVisible;
        std::cout << (self.smokePassed ? "PASS: macOS menu bar, native settings persistence, passive overlay and hide lifecycle\n" : "FAIL: macOS native UI smoke test\n");
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
        std::string detail = text + "\n" + (current.refreshing ? "Refreshing…" : !current.error.empty() ? current.error : !prefs.online ? "Online queries disabled" : "Account quota");
        if (!options.smoke && prefs.overlay && !AXIsProcessTrusted()) detail += "\nAllow Accessibility access to follow Codex windows.";
        status.stringValue = Text(detail);
    }
}
- (NSTextField*)label:(NSString*)title y:(CGFloat)y {
    auto label = [NSTextField labelWithString:title]; label.frame = NSMakeRect(24, y, 235, 24); [config.contentView addSubview:label]; return label;
}
- (NSTextField*)field:(NSString*)title value:(NSString*)value y:(CGFloat)y {
    [self label:title y:y]; auto input = [[NSTextField alloc] initWithFrame:NSMakeRect(260, y, 276, 24)]; input.stringValue = value; [config.contentView addSubview:input]; return input;
}
- (NSButton*)check:(NSString*)title enabled:(BOOL)enabled y:(CGFloat)y {
    auto button = [NSButton checkboxWithTitle:title target:nil action:nil]; button.frame = NSMakeRect(24, y, 500, 25); button.state = enabled ? NSControlStateValueOn : NSControlStateValueOff; [config.contentView addSubview:button]; return button;
}
- (void)openSettings:(id)sender {
    (void)sender;
    if (config) { [NSApp activateIgnoringOtherApps:YES]; [config makeKeyAndOrderFront:nil]; return; }
    auto prefs = engine->preferences();
    config = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 560, 610) styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
    config.title = @"Codex Quota — Settings"; config.releasedWhenClosed = NO; config.delegate = self;
    status = [NSTextField wrappingLabelWithString:@""]; status.frame = NSMakeRect(24, 510, 512, 76); status.font = [NSFont systemFontOfSize:14]; [config.contentView addSubview:status];
    online = [self check:@"Query Codex account online" enabled:prefs.online y:470];
    only = [self check:@"Query only while the host application is open" enabled:prefs.onlyWithCodex y:436];
    show = [self check:@"Show window-following overlay" enabled:prefs.overlay y:402];
    seconds = [self field:@"Refresh interval (30–3600 s)" value:Text(std::to_string(prefs.seconds)) y:360];
    stale = [self field:@"Stale after (1–120 min)" value:Text(std::to_string(prefs.staleMinutes)) y:322];
    [self label:@"Overlay position" y:284]; position = [[NSPopUpButton alloc] initWithFrame:NSMakeRect(260, 284, 276, 26) pullsDown:NO]; [position addItemsWithTitles:@[@"Top", @"Left"]]; [position selectItemAtIndex:prefs.left ? 1 : 0]; [config.contentView addSubview:position];
    home = [self field:@"Codex data directory" value:Text(prefs.home) y:246];
    binary = [self field:@"Codex executable (blank = auto)" value:Text(prefs.binary) y:208];
    hostName = [self field:@"Host application contains" value:Text(prefs.host) y:170];
    auto permission = [NSButton buttonWithTitle:@"Allow Accessibility…" target:self action:@selector(permission:)]; permission.frame = NSMakeRect(24, 115, 230, 32); [config.contentView addSubview:permission];
    auto refresh = [NSButton buttonWithTitle:@"Refresh now" target:self action:@selector(refresh:)]; refresh.frame = NSMakeRect(24, 30, 130, 34); [config.contentView addSubview:refresh];
    auto apply = [NSButton buttonWithTitle:@"Save and apply" target:self action:@selector(apply:)]; apply.frame = NSMakeRect(380, 30, 156, 34); apply.keyEquivalent = @"\r"; [config.contentView addSubview:apply];
    [config center]; [self update]; [NSApp activateIgnoringOtherApps:YES]; [config makeKeyAndOrderFront:nil];
}
- (void)windowWillClose:(NSNotification*)notification {
    if (notification.object == config) { config = nil; status = nil; seconds = nil; stale = nil; home = nil; binary = nil; hostName = nil; online = nil; only = nil; show = nil; position = nil; }
}
- (void)apply:(id)sender {
    (void)sender; auto prefs = engine->preferences();
    auto number = [](NSString* value, int& result) { NSScanner* scanner = [NSScanner scannerWithString:value]; NSInteger parsed; if (![scanner scanInteger:&parsed] || !scanner.isAtEnd || parsed < 0 || parsed > 3600) return false; result = static_cast<int>(parsed); return true; };
    if (!number(seconds.stringValue, prefs.seconds) || !number(stale.stringValue, prefs.staleMinutes)) { status.stringValue = @"Enter valid whole-number intervals."; return; }
    prefs.online = online.state == NSControlStateValueOn; prefs.onlyWithCodex = only.state == NSControlStateValueOn; prefs.overlay = show.state == NSControlStateValueOn; prefs.left = position.indexOfSelectedItem == 1;
    prefs.home = home.stringValue.UTF8String ?: ""; prefs.binary = binary.stringValue.UTF8String ?: ""; prefs.host = hostName.stringValue.UTF8String ?: "";
    if (!Save(configRoot / "settings.ini", prefs)) { status.stringValue = @"Cannot save: check absolute home path, values and permissions."; return; }
    engine->configure(prefs); [self updateHost]; [self update];
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
    if (options.smoke) { std::filesystem::remove(configRoot / "instance.lock"); std::filesystem::remove(configRoot); }
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

#include "NativeCore.h"
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    auto root = std::filesystem::temp_directory_path() / ("quota-native-test-" + std::to_string(getpid()));
    std::filesystem::create_directories(root);
    try {
        auto settings = nativequota::Defaults(); settings.home = (root / "home with spaces").string();
        std::filesystem::create_directories(settings.home);
        { std::ofstream auth(std::filesystem::path(settings.home) / "auth.json"); auth << "synthetic-account-a"; }
        settings.seconds = 45; settings.staleMinutes = 2; settings.left = true; settings.onlyWithCodex = false;
        auto file = root / "settings.ini";
        require(nativequota::Save(file, settings), "Valid settings must save");
        auto restored = nativequota::Load(file);
        require(restored.seconds == 45 && restored.staleMinutes == 2 && restored.left && restored.home == settings.home, "Settings must persist across restart");
        struct stat attributes{}; stat(file.c_str(), &attributes);
        require((attributes.st_mode & 0777) == 0600, "Settings must be private to the current user");
        settings.seconds = 0; require(!nativequota::Save(file, settings) && nativequota::Load(file).seconds == 45, "Invalid settings must not overwrite valid file"); settings.seconds = 45;
        settings.host = "Codex\nonline=0"; require(!nativequota::Save(file, settings), "Multiline setting injection must be rejected"); settings.host = "Codex";
        { std::ofstream out(file); out << std::string(70000, 'x'); }
        require(nativequota::Load(file).seconds == 120, "Oversized settings must fall back to defaults");
        auto top = nativequota::Placement({100, 50, 900, 600}, false), left = nativequota::Placement({100, 50, 900, 600}, true);
        require(top.x == 426 && top.y == 56 && top.width == 248 && left.x == 106 && left.y == 294, "Overlay must follow host coordinates");
        require(nativequota::HostMatches("com.openai.Codex", "codex") && !nativequota::HostMatches("Browser", "Codex"), "Host identification must exclude unrelated apps");
        require(nativequota::Remaining({true, 20, 1}, 2000) == "-", "Expired quota must become unknown");
        { nativequota::InstanceLock first(root / "instance.lock"); require(first.acquired(), "First instance must acquire lock"); nativequota::InstanceLock second(root / "instance.lock"); require(!second.acquired(), "Duplicate instance must fail lock"); }
        { nativequota::InstanceLock third(root / "instance.lock"); require(third.acquired(), "Exited instance must release lock"); }
        settings.binary = std::filesystem::absolute(argv[1]).string(); settings.online = true;
        unsetenv("QUOTA_FAKE_MODE");
        nativequota::Engine engine(settings); std::atomic<int> notifications{0};
        engine.start([&] { ++notifications; });
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while ((!engine.state().snapshot.observed || notifications.load() < 2) && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(engine.state().snapshot.fiveHour.used == 20, "Refresh engine must query real fake-server process");
        int before = notifications.load(); std::this_thread::sleep_for(std::chrono::milliseconds(200));
        require(notifications.load() == before, "Idle engine must block rather than poll its UI");
        settings.online = false; engine.configure(settings);
        { std::ofstream auth(std::filesystem::path(settings.home) / "auth.json"); auth << "synthetic-account-b-with-new-metadata"; }
        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (engine.state().snapshot.observed && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        require(engine.state().snapshot.observed == 0, "Auth-file change must clear the previous account even while online refresh is disabled");
        settings.home = (root / "second account").string(); settings.online = false; engine.configure(settings);
        require(engine.state().snapshot.observed == 0, "Changing account home must clear previous snapshot"); engine.stop();
        std::filesystem::remove(root / "home with spaces/auth.json"); std::filesystem::remove(root / "home with spaces");
        std::filesystem::remove(file); std::filesystem::remove(root / "instance.lock"); std::filesystem::remove(root);
        std::cout << "PASS: preferences, input bounds, overlay geometry, expiry, single instance and blocking refresh lifecycle\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}

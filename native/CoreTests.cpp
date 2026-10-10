#include "../source/QuotaCore.h"
#include <iostream>
#include <stdexcept>

void require(bool value, const char* reason) { if (!value) throw std::runtime_error(reason); }
int main() {
    try {
        // 时间戳必须使用 UTC，在不同平台和本地时区下保持相同。
        require(quota::Timestamp("2026-09-30T13:05:00.321Z") == 1790773500321LL, "UTC timestamp must be portable");
        quota::Snapshot result;
        require(quota::ReadLimits(quota::JsonParser(R"({"rateLimits":{"limitId":"codex","primary":{"usedPercent":88,"windowDurationMins":10080},"secondary":{"usedPercent":20,"windowDurationMins":300}}})").parse(), result), "Camel-case quota response must parse");
        require(result.weekly.present && result.weekly.used == 88 && result.fiveHour.present && result.fiveHour.used == 20, "Quota windows must map by duration");
        require(quota::Remaining({}) == L"-", "Missing values must display a dash");
        require(quota::Remaining(result.fiveHour) == L"80%", "Remaining value must be 100 minus used");
        require(!quota::WeeklyOnly(result), "Unknown startup must retain five-hour placeholder");
        result.observed = 1; result.fiveHour.present = false;
        require(quota::WeeklyOnly(result), "Confirmed weekly-only data must hide five-hour UI");
        result.fiveHour = {true, 20, 1};
        require(!quota::WeeklyOnly(result), "Known but expired five-hour window must not be hidden as unsupported");
        bool rejected = false;
        try { quota::JsonParser(std::string(40000, 'x')).parse(); } catch (...) { rejected = true; }
        require(rejected, "Oversized messages must be rejected");
        std::cout << "PASS: portable UTC, limits, missing values and bounded parsing\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}

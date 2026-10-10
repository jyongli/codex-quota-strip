#include "NativeCore.h"
namespace nativequota {
QueryResult Query(const std::string&, const std::string&, const std::atomic<bool>&, int) {
    return {}; // 回归测试先观察真实请求路径尚未实现的失败。
}
}

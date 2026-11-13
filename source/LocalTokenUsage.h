#pragma once
#include "RequestUsage.h"
#include <limits>
#include <iterator>

namespace quota {
inline constexpr size_t LocalTokenUsageLimit=4096;
// 本次运行的已记录用量，不代表账户账单；仅由成功写入且去重后的完成请求更新。
struct LocalTokenUsage {
    long long sinceMs=0,tokens=0;
    size_t requests=0,knownRequests=0,missingRequests=0;
    bool partial=false;
    void reset(long long now){*this={};sinceMs=now;}
};
// 独立的有界身份缓存只由监听线程持有；UI仅复制上面的轻量数值快照。
class LocalTokenMeter:public LocalTokenUsage {
    std::set<request_detail::RequestIdentity> identities_;
public:
    explicit LocalTokenMeter(long long now=0){reset(now);}
    void reset(long long now){LocalTokenUsage::reset(now);identities_.clear();}
    LocalTokenUsage snapshot()const{return *this;}
    void add(const RequestEntry& entry) {
        if(entry.ended<sinceMs||entry.ended<=0)return;
        if(entry.turnId.empty()){partial=true;return;}
        auto identity=request_detail::RequestKey(entry.threadId,entry.turnId);
        if(identities_.count(identity))return;
        // 此缓存不受历史写入缓存驱逐影响；旧起点前的请求不占用身份额度。
        if(requests>=LocalTokenUsageLimit){partial=true;return;}
        identities_.insert(std::move(identity));
        ++requests;
        if(!entry.tokens){++missingRequests;partial=true;return;}
        if(entry.tokens->total<0){++missingRequests;partial=true;return;}
        ++knownRequests;partial=partial||entry.partial;
        if(entry.tokens->total>std::numeric_limits<long long>::max()-tokens){tokens=std::numeric_limits<long long>::max();partial=true;}
        else tokens+=entry.tokens->total;
    }
};
inline std::string CompactTokens(long long value){
    value=std::max(0LL,value);if(value<1000)return std::to_string(value);
    constexpr char units[]{'K','M','B','T','P','E'};long long divisor=1000;size_t unit=0;
    while(unit+1<std::size(units)&&value/divisor>=1000){divisor*=1000;++unit;}
    auto whole=value/divisor;auto tenth=(value%divisor)/(divisor/10);
    // 托盘保留至多四个字符；完整整数在提示和配置窗口说明中提供。
    auto text=std::to_string(whole);if(whole<10&&tenth)text+="."+std::to_string(tenth);
    return text+units[unit];
}
inline std::string LocalTokenValue(const LocalTokenUsage& usage,bool available=true){
    if(!available||(!usage.knownRequests&&usage.missingRequests))return "-";
    return CompactTokens(usage.tokens)+(usage.partial?"+":"");
}
inline std::string LocalTokenExact(const LocalTokenUsage& usage,bool available=true){
    if(!available||(!usage.knownRequests&&usage.missingRequests))return "-";
    return std::to_string(usage.tokens)+(usage.partial?"+":"");
}
}

#pragma once
#include "QuotaCore.h"

namespace quota {
// 登录方式只来自 app-server 的安全账号摘要，绝不读取认证文件或保存密钥。
enum class AccountMode { Unknown, Subscription, ApiKey, ExternalProvider, SignedOut };
inline AccountMode ReadAccountMode(const Json& response) {
    const auto& required=response.get("requiresOpenaiAuth");
    // 活跃第三方 provider 优先于磁盘上可能仍保留的 ChatGPT 登录。
    if(required.kind==Json::Boolean&&!required.num)return AccountMode::ExternalProvider;
    const auto& account=response.get("account");
    const auto& type=account.get("type");
    if(type.kind==Json::String){
        if(type.str=="apiKey")return AccountMode::ApiKey;
        if(type.str=="chatgpt"||type.str=="chatgptAuthTokens"||type.str=="personalAccessToken"||type.str=="agentIdentity")return AccountMode::Subscription;
        if(type.str=="amazonBedrock"||type.str=="gcp")return AccountMode::ExternalProvider;
    }
    if(response.members.count("account")&&account.kind==Json::Null&&required.kind==Json::Boolean&&required.num)return AccountMode::SignedOut;
    return AccountMode::Unknown;
}
inline bool NoSubscriptionQuota(AccountMode mode) { return mode==AccountMode::ApiKey||mode==AccountMode::ExternalProvider; }
inline const char* AccountModeId(AccountMode mode) {
    switch(mode){
    case AccountMode::Subscription:return "subscription";
    case AccountMode::ApiKey:return "api-key";
    case AccountMode::ExternalProvider:return "external-provider";
    case AccountMode::SignedOut:return "signed-out";
    default:return "unknown";
    }
}
}

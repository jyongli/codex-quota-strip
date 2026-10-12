#pragma once
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include "../third_party/tomlplusplus/toml.hpp"

namespace quota {
using ProxySettings = std::map<std::string,std::string>;
// 只复用网络代理四项，不把 shell 环境中的令牌、其他密钥或命令注入查询进程。
inline std::string ProxyKey(std::string key){
    for(char& c:key)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');
    return key=="HTTP_PROXY"||key=="HTTPS_PROXY"||key=="ALL_PROXY"||key=="NO_PROXY"?key:std::string{};
}
inline bool ProxyTable(const toml::node_view<const toml::node>& policy,ProxySettings& values){
    const auto set=policy["set"];if(!set)return true;
    const auto* table=set.as_table();if(!table)return false;
    ProxySettings layer;
    for(const auto& item:*table){
        auto key=ProxyKey(std::string(item.first.str()));if(key.empty())continue;
        auto value=item.second.value<std::string>();
        if(!value||value->size()>4096||value->find_first_of("\r\n")!=std::string::npos||value->find('\0')!=std::string::npos)return false;
        // 同一层的大小写别名若值不同则报错，避免不同 CLI 优先级导致走错代理。
        auto previous=layer.find(key);if(previous!=layer.end()&&previous->second!=*value)return false;
        layer[key]=std::move(*value);
    }
    for(auto& item:layer)values[item.first]=std::move(item.second);
    return true;
}
// 每次查询时读取所选 CODEX_HOME 的配置，变更下次查询生效；无需常驻监听或额外进程。
// 文件与变量均限制大小，解析异常只返回固定错误，不输出含密码的配置片段。
inline bool LoadCodexProxy(const std::filesystem::path& home,ProxySettings& result){
    result.clear();
    try{
        const auto file=home/"config.toml";std::error_code error;
        if(!std::filesystem::exists(file,error))return !error;
        const auto size=std::filesystem::file_size(file,error);if(error||size>512*1024)return false;
        std::ifstream input(file,std::ios::binary);if(!input)return false;
        std::string bytes(static_cast<size_t>(size),'\0');if(size&&!input.read(bytes.data(),static_cast<std::streamsize>(size)))return false;
        const auto config=toml::parse(bytes);ProxySettings values;
        if(!ProxyTable(config["shell_environment_policy"],values))return false;
        // 用户在 config.toml 中选定的 profile 覆盖全局同名代理；不猜测命令行选择的 profile。
        if(const auto selected=config["profile"]){
            auto name=selected.value<std::string>();if(!name)return false;
            const auto profile=config["profiles"][*name];if(!profile.is_table())return false;
            if(!ProxyTable(profile["shell_environment_policy"],values))return false;
        }
        result=std::move(values);return true;
    }catch(...){return false;}
}
}

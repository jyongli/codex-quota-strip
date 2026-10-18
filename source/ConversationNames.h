#pragma once
#include "RequestUsage.h"
#include "ConversationIndex.h"
#include <array>

namespace quota {
namespace conversation_detail {
// 标题和旧记录归属只在用户打开／刷新列表时解析；所有读盘、遍历和内存均有上限。
constexpr size_t IndexLimit=2*1024*1024, HeaderLimit=64*1024, FileLimit=32;
struct Metadata { std::string parent, agent; };
inline std::string DisplayText(const std::string& value) {
    if(value.size()>1024)return {};std::string out;
    for(size_t i=0;i<value.size();){unsigned char c=value[i];if(c<32||c==127){out+=' ';++i;continue;}
        size_t size=c<128?1:c>=0xc2&&c<=0xdf?2:c>=0xe0&&c<=0xef?3:c>=0xf0&&c<=0xf4?4:0;
        if(!size||i+size>value.size())return {};
        for(size_t j=1;j<size;++j)if((static_cast<unsigned char>(value[i+j])&0xc0)!=0x80)return {};
        if(size>=3){auto next=static_cast<unsigned char>(value[i+1]);if((c==0xe0&&next<0xa0)||(c==0xed&&next>=0xa0)||(c==0xf0&&next<0x90)||(c==0xf4&&next>=0x90))return {};}
        out.append(value,i,size);i+=size;
    }
    auto begin=out.find_first_not_of(' ');return begin==std::string::npos?std::string{}:out.substr(begin,out.find_last_not_of(' ')-begin+1);
}
template<class F> inline void ReadTail(const std::filesystem::path& path,size_t limit,F accept){
    std::error_code error;auto type=std::filesystem::symlink_status(path,error);if(error||!std::filesystem::is_regular_file(type))return;
    std::ifstream input(path,std::ios::binary);if(!input)return;input.seekg(0,std::ios::end);auto end=input.tellg();if(end<0)return;
    auto size=static_cast<size_t>(std::min<std::streamoff>(static_cast<std::streamoff>(end),static_cast<std::streamoff>(limit)));
    input.seekg(static_cast<std::streamoff>(end)-static_cast<std::streamoff>(size));MetadataLines lines(static_cast<std::streamoff>(end)>static_cast<std::streamoff>(size));
    std::array<char,4096> bytes{};while(size&&input){auto wanted=std::min(size,bytes.size());input.read(bytes.data(),static_cast<std::streamsize>(wanted));auto count=static_cast<size_t>(input.gcount());if(!count)break;lines.feed(bytes.data(),count,accept);size-=count;}
}
inline std::map<std::string,std::string> Titles(const std::filesystem::path& home){
    std::map<std::string,std::string> titles;
    ReadTail(home/"session_index.jsonl",IndexLimit,[&](const std::string& line){try{auto row=JsonParser(line).parse();auto id=row.get("id").str;if(id.empty()||!request_detail::Label(id))return;auto title=DisplayText(row.get("thread_name").str);if(title.empty())titles.erase(id);else titles[id]=title;}catch(...){} });
    return titles;
}
inline std::vector<std::filesystem::path> Recent(const std::filesystem::path& home){
    using Entry=std::pair<std::filesystem::file_time_type,std::filesystem::path>;std::vector<Entry> files;size_t budget=8192;
    std::function<void(const std::filesystem::path&,int)> walk=[&](const auto& dir,int depth){
        if(!budget||depth>8)return;std::error_code error;auto type=std::filesystem::symlink_status(dir,error);if(error||!std::filesystem::is_directory(type))return;
        std::vector<std::filesystem::path> children;
        for(std::filesystem::directory_iterator it(dir,std::filesystem::directory_options::skip_permission_denied,error),end;!error&&it!=end&&budget;it.increment(error)){
            --budget;auto status=it->symlink_status(error);if(error)break;
            if(std::filesystem::is_directory(status))children.push_back(it->path());
            else if(std::filesystem::is_regular_file(status)&&it->path().extension()==".jsonl"){
                auto time=it->last_write_time(error);if(error){error.clear();continue;}files.emplace_back(time,it->path());std::sort(files.rbegin(),files.rend());if(files.size()>FileLimit)files.pop_back();
            }
        }
        std::sort(children.rbegin(),children.rend());for(const auto& child:children)walk(child,depth+1);
    };
    walk(home/"sessions",0);walk(home/"archived_sessions",0);std::vector<std::filesystem::path> paths;for(const auto& file:files)paths.push_back(file.second);return paths;
}
inline std::string Header(const std::filesystem::path& path,Metadata& metadata){
    std::ifstream input(path,std::ios::binary);if(!input)return {};MetadataLines lines(false,true);std::string header;bool done=false;size_t remaining=HeaderLimit;std::array<char,4096> bytes{};
    while(remaining&&input&&!done){auto wanted=std::min(remaining,bytes.size());input.read(bytes.data(),static_cast<std::streamsize>(wanted));size_t count=static_cast<size_t>(input.gcount());if(!count)break;remaining-=count;
        auto newline=std::find(bytes.begin(),bytes.begin()+count,'\n');if(newline!=bytes.begin()+count){count=static_cast<size_t>(newline-bytes.begin())+1;done=true;}
        lines.feed(bytes.data(),count,[&](const auto& value){header=value;});
    }
    try{auto root=JsonParser(header).parse();if(root.get("type").str!="session_meta")return {};const auto& row=root.get("payload");auto id=row.get("id").str;if(id.empty()||!request_detail::Label(id))return {};
        const auto& spawned=row.get("source").get("subagent").get("thread_spawn");metadata.parent=row.get("parent_thread_id").str;if(metadata.parent.empty())metadata.parent=spawned.get("parent_thread_id").str;
        if(!request_detail::Label(metadata.parent))metadata.parent.clear();metadata.agent=DisplayText(row.get("agent_nickname").str);if(metadata.agent.empty())metadata.agent=DisplayText(spawned.get("agent_nickname").str);return id;
    }catch(...){return {};}
}
}
// CSV 中只保留稳定 ID；显示时补上当前标题，重命名后刷新即可生效，旧日志无需迁移。
inline void ResolveRequestConversations(const std::filesystem::path& home,std::vector<RequestEntry>& rows){
    if(rows.empty())return;auto titles=conversation_detail::Titles(home);std::set<std::string> missing;
    for(auto& row:rows){row.conversationLabel.clear();if(row.threadId.empty()&&!row.turnId.empty())missing.insert(row.turnId);}
    std::map<std::string,conversation_detail::Metadata> metadata;std::map<std::string,std::string> identities;std::set<std::string> ambiguous;
    // 已有标题且没有缺失 ID 时无须再读会话文件；子代理通常没有独立的标题索引。
    bool needsFiles=!missing.empty()||std::any_of(rows.begin(),rows.end(),[&](const auto& row){return !row.threadId.empty()&&!titles.count(row.threadId);});
    if(needsFiles)for(const auto& path:conversation_detail::Recent(home)){
        conversation_detail::Metadata meta;auto id=conversation_detail::Header(path,meta);if(id.empty())id=request_detail::ThreadFromFile(path);if(id.empty())continue;metadata.emplace(id,meta);
    }
    if(!missing.empty()){
        std::set<std::string> wanted;for(auto it=rows.rbegin();it!=rows.rend()&&wanted.size()<128;++it)if(it->threadId.empty()&&!it->turnId.empty())wanted.insert(it->turnId);
        for(const auto& pair:conversation_detail::IndexedTurns(home,wanted)){if(!request_detail::Label(pair.first)){ambiguous.insert(pair.second);continue;}auto prior=identities.emplace(pair.second,pair.first);if(!prior.second&&prior.first->second!=pair.first)ambiguous.insert(pair.second);}
    }
    for(auto& row:rows){
        if(row.threadId.empty()&&!ambiguous.count(row.turnId)){auto found=identities.find(row.turnId);if(found!=identities.end())row.threadId=found->second;}
        if(row.threadId.empty())continue;std::string current=row.threadId;std::set<std::string> visited;
        for(size_t depth=0;depth<8&&visited.insert(current).second;++depth){auto entry=metadata.find(current);if(entry==metadata.end()||entry->second.parent.empty()||visited.count(entry->second.parent))break;current=entry->second.parent;}
        auto title=titles.find(current);row.conversationLabel=title==titles.end()?current:title->second;
        if(current!=row.threadId){const auto& agent=metadata[row.threadId].agent;row.conversationLabel+=" / "+(agent.empty()?row.threadId:agent);}
    }
}
}

#pragma once
#include <chrono>
#include <filesystem>
#include <set>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace quota { namespace conversation_detail {
struct sqlite3;struct sqlite3_stmt;
// 复用系统 SQLite；不可用或索引版本不匹配时回退到有界文件解析，不增加打包依赖。
class SqliteApi {
#ifdef _WIN32
    HMODULE module_=nullptr;
    void* symbol(const char* name){return reinterpret_cast<void*>(GetProcAddress(module_,name));}
#else
    void* module_=nullptr;
    void* symbol(const char* name){return dlsym(module_,name);}
#endif
public:
    int(*open)(const char*,sqlite3**,int,const char*)=nullptr;
    int(*close)(sqlite3*)=nullptr;
    int(*prepare)(sqlite3*,const char*,int,sqlite3_stmt**,const char**)=nullptr;
    int(*bind)(sqlite3_stmt*,int,const char*,int,void(*)(void*))=nullptr;
    int(*step)(sqlite3_stmt*)=nullptr;
    int(*reset)(sqlite3_stmt*)=nullptr;
    int(*finalize)(sqlite3_stmt*)=nullptr;
    const unsigned char*(*column)(sqlite3_stmt*,int)=nullptr;
    int(*bytes)(sqlite3_stmt*,int)=nullptr;
    int(*limit)(sqlite3*,int,int)=nullptr;
    int(*busy)(sqlite3*,int)=nullptr;
    void(*progress)(sqlite3*,int,int(*)(void*),void*)=nullptr;
    SqliteApi(){
#ifdef _WIN32
        module_=LoadLibraryExW(L"winsqlite3.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
#elif defined(__APPLE__)
        module_=dlopen("/usr/lib/libsqlite3.dylib",RTLD_NOW|RTLD_LOCAL);
#else
        module_=dlopen("libsqlite3.so.0",RTLD_NOW|RTLD_LOCAL);
#endif
        if(!module_)return;
        open=reinterpret_cast<decltype(open)>(symbol("sqlite3_open_v2"));close=reinterpret_cast<decltype(close)>(symbol("sqlite3_close"));prepare=reinterpret_cast<decltype(prepare)>(symbol("sqlite3_prepare_v2"));
        bind=reinterpret_cast<decltype(bind)>(symbol("sqlite3_bind_text"));step=reinterpret_cast<decltype(step)>(symbol("sqlite3_step"));reset=reinterpret_cast<decltype(reset)>(symbol("sqlite3_reset"));
        finalize=reinterpret_cast<decltype(finalize)>(symbol("sqlite3_finalize"));column=reinterpret_cast<decltype(column)>(symbol("sqlite3_column_text"));busy=reinterpret_cast<decltype(busy)>(symbol("sqlite3_busy_timeout"));progress=reinterpret_cast<decltype(progress)>(symbol("sqlite3_progress_handler"));
        bytes=reinterpret_cast<decltype(bytes)>(symbol("sqlite3_column_bytes"));limit=reinterpret_cast<decltype(limit)>(symbol("sqlite3_limit"));
    }
    ~SqliteApi(){if(module_){
#ifdef _WIN32
        FreeLibrary(module_);
#else
        dlclose(module_);
#endif
    }}
    explicit operator bool()const{return open&&close&&prepare&&bind&&step&&reset&&finalize&&column&&bytes&&limit&&busy&&progress;}
    SqliteApi(const SqliteApi&)=delete;SqliteApi& operator=(const SqliteApi&)=delete;
};
inline std::vector<std::pair<std::string,std::string>> IndexedTurns(const std::filesystem::path& home,const std::set<std::string>& turns){
    std::vector<std::pair<std::string,std::string>> result;auto path=home/"thread_history_1.sqlite";std::error_code error;
    if(turns.empty()||turns.size()>128||!std::filesystem::is_regular_file(std::filesystem::symlink_status(path,error))||error)return result;
    SqliteApi api;if(!api)return result;sqlite3* database=nullptr;
    // READONLY | NOMUTEX：只查 ID 索引，不读取 item_json、会话内容或账单字段。
    if(api.open(path.u8string().c_str(),&database,1|0x8000,nullptr)!=0){if(database)api.close(database);return result;}
    auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);api.busy(database,10);
    api.limit(database,0,2048);
    api.progress(database,1000,+[](void* context){return std::chrono::steady_clock::now()>*static_cast<std::chrono::steady_clock::time_point*>(context)?1:0;},&deadline);
    auto execute=[&](const char* sql){sqlite3_stmt* statement=nullptr;bool ok=api.prepare(database,sql,-1,&statement,nullptr)==0&&api.step(statement)==101;if(statement)api.finalize(statement);return ok;};
    bool ok=execute("PRAGMA cache_size=-256")&&execute("PRAGMA query_only=ON");
    // 全局核对轻量 ID 覆盖索引；候选窗口外也可能有重复 turn，不能据局部唯一推断归属。
    std::string sql="SELECT thread_id,turn_id FROM thread_turns INDEXED BY sqlite_autoindex_thread_turns_1 WHERE turn_id IN (";
    size_t count=0;for(const auto& turn:turns){(void)turn;if(count++)sql+=',';sql+='?'+std::to_string(count);}sql+=')';sqlite3_stmt* statement=nullptr;
    if(ok)ok=api.prepare(database,sql.c_str(),-1,&statement,nullptr)==0;
    if(ok){int parameter=1;for(const auto& turn:turns)if(api.bind(statement,parameter++,turn.c_str(),static_cast<int>(turn.size()),nullptr)!=0)ok=false;}
    if(ok){int status=0;while((status=api.step(statement))==100){
            if(result.size()>=256||api.bytes(statement,0)<1||api.bytes(statement,0)>160||api.bytes(statement,1)<1||api.bytes(statement,1)>160||std::chrono::steady_clock::now()>deadline){ok=false;break;}
            auto id=api.column(statement,0),turn=api.column(statement,1);if(!id||!turn){ok=false;break;}result.emplace_back(reinterpret_cast<const char*>(id),reinterpret_cast<const char*>(turn));
        }
        if(status!=101)ok=false;
    }
    if(statement)api.finalize(statement);api.close(database);
    // 查询中断时不采信半批结果，避免只读到重复 turn 的一个来源便作出错误归属。
    if(!ok)result.clear();return result;
}
}}

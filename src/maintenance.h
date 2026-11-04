#pragma once
// Implementation fragment included after the restore engine in cbackup.cpp.
// Health data lives independently of the backup destination, so drive failures remain visible.
struct HealthState {
    int64_t created=time(nullptr), last_success=0, last_failure=0, last_test=0, test_failure=0, last_offline=0;
    uint64_t test_cursor=0;
    string error, test_error, tested_id, offline_id;
    map<string,string> notices;
};
static fs::path health_dir(const Job &j) {return fs::path(home())/".config/cbackup/state"/j.name;}
static HealthState load_health(const Job &j) {
    HealthState s; fs::path file=health_dir(j)/"health.conf";
    if(!fs::exists(file))return s;
    std::ifstream in(file,std::ios::binary); require(bool(in),"Cannot read job health state"); string line;
    while(std::getline(in,line)) {
        auto pos=line.find('='); require(pos!=string::npos,"Invalid job health state");
        string key=line.substr(0,pos), value=read_quoted(line.substr(pos+1));
        if(key=="error")s.error=unhex(value);
        else if(key=="test_error")s.test_error=unhex(value);
        else if(key=="tested_id")s.tested_id=value;
        else if(key=="offline_id")s.offline_id=value;
        else if(key.rfind("notice_",0)==0)s.notices[key.substr(7)]=value;
        else {
            int64_t n=parse_num<int64_t>(value,"Invalid health timestamp");require(n>=0,"Invalid health timestamp");
            if(key=="created")s.created=n; else if(key=="last_success")s.last_success=n;
            else if(key=="last_failure")s.last_failure=n; else if(key=="last_test")s.last_test=n;
            else if(key=="test_failure")s.test_failure=n; else if(key=="last_offline")s.last_offline=n;
            else if(key=="test_cursor")s.test_cursor=static_cast<uint64_t>(n);
            else fail("Unknown health state key: "+key);
        }
    }
    require(in.eof(),"Cannot finish reading health state");return s;
}
static void save_health(const Job &j,const HealthState &s) {
    std::ostringstream out;
    auto put=[&](const string &k,const string &v){out<<k<<"="<<quoted(v)<<"\n";};
    put("created",std::to_string(s.created));put("last_success",std::to_string(s.last_success));
    put("last_failure",std::to_string(s.last_failure));put("last_test",std::to_string(s.last_test));
    put("test_failure",std::to_string(s.test_failure));put("last_offline",std::to_string(s.last_offline));
    put("test_cursor",std::to_string(s.test_cursor));put("error",hex(s.error));put("test_error",hex(s.test_error));
    put("tested_id",s.tested_id);put("offline_id",s.offline_id);
    for(const auto &n:s.notices)put("notice_"+n.first,n.second);
    atomic_write(health_dir(j)/"health.conf",out.str());
}
static void notify_health(const Job &j,HealthState &s,const string &kind,const string &message) {
    if(!j.notifications)return;
    int64_t now=time(nullptr);string signature=sha256_text(message);
    auto previous=s.notices.find(kind);
    if(previous!=s.notices.end()) {
        std::istringstream old(previous->second);int64_t at=0;string hash;old>>at>>hash;
        if(hash==signature&&now-at<static_cast<int64_t>(j.notify_repeat_hours)*3600)return;
    }
    fs::path file=health_dir(j)/"alerts.log";
    if(fs::exists(file)&&fs::file_size(file)>static_cast<uintmax_t>(j.log_max_mb)*1024*1024) {
        std::error_code ec;fs::remove(health_dir(j)/"alerts.log.1",ec);cb::replace_file(file,health_dir(j)/"alerts.log.1");
    }
    fs::create_directories(health_dir(j));
    std::ofstream out(file,std::ios::app|std::ios::binary);require(bool(out),"Cannot write health alert");
    out<<now<<"\t"<<kind<<"\t"<<hex(message)<<"\n";out.flush();require(bool(out),"Cannot flush health alert");
    s.notices[kind]=std::to_string(now)+" "+signature;
    std::cerr<<"ALERT ["<<j.name<<"/"<<kind<<"] "<<message<<"\n";
    if(j.desktop_notifications)try {
        string title="CBackup: "+j.name, body=message.substr(0,1500);int rc=127;
#ifdef _WIN32
        DWORD session=0;
        if(ProcessIdToSessionId(GetCurrentProcessId(),&session))
            rc=cb::exec({"msg.exe",std::to_string(session),"/time:30",title+": "+body},true);
#elif defined(__APPLE__)
        rc=cb::exec({"osascript","-e","on run argv\ndisplay notification (item 2 of argv) with title (item 1 of argv)\nend run",title,body},true);
#else
        rc=cb::exec({"notify-send","--",title,body},true);
#endif
        if(rc)std::cerr<<"Desktop notification unavailable; alert saved locally.\n";
    } catch(const std::exception&) {std::cerr<<"Desktop notification unavailable; alert saved locally.\n";}
}
static void record_run_failure(const Job &j,const string &message) {
    cb::ProcessLock lock(health_dir(j)/".lock",true);
    auto s=load_health(j);s.last_failure=time(nullptr);s.error=message;
    notify_health(j,s,"run_failed",message);save_health(j,s);
}
static bool health_check(const Job &j,bool report) {
    cb::ProcessLock lock(health_dir(j)/".lock",true);
    auto s=load_health(j); vector<std::pair<string,string>> issues;
    if(s.last_failure&& !s.error.empty())issues.push_back({"run_failed",s.error});
    if(j.stale_hours&&(!s.last_success||time(nullptr)-s.last_success>static_cast<int64_t>(j.stale_hours)*3600))
        issues.push_back({"stale",s.last_success?"No successful backup run within "+std::to_string(j.stale_hours)+" hours":"No successful backup run has been recorded"});
    if(s.test_failure&&!s.test_error.empty())issues.push_back({"test_failed",s.test_error});
    if(j.offline_reminder_days&&time(nullptr)-(s.last_offline?s.last_offline:s.created)>=static_cast<int64_t>(j.offline_reminder_days)*86400)
        issues.push_back({"offline_due","A separate offline copy is due; after copying and disconnecting it, run: cbackup offline-confirm "+j.name+" SNAPSHOT_ID"});
    try { quota_check(j); }
    catch(const std::exception &e) {issues.push_back({"storage",e.what()});}
    std::set<string> active;
    for(const auto &issue:issues) {active.insert(issue.first);notify_health(j,s,issue.first,issue.second);}
    for(auto it=s.notices.begin();it!=s.notices.end();) {
        if(!active.count(it->first))it=s.notices.erase(it);else ++it;
    }
    save_health(j,s);
    if(report) {
        auto stamp=[](int64_t at){return at?fmt_local(at):string("never");};
        std::cout<<"Health: "<<(issues.empty()?"OK":"ATTENTION")<<"\nLast successful run: "<<stamp(s.last_success)
                 <<"\nLast successful test restore: "<<stamp(s.last_test)<<" ("<<s.tested_id<<")"
                 <<"\nLast offline confirmation: "<<stamp(s.last_offline)<<" ("<<s.offline_id<<")\n";
        for(const auto &issue:issues)std::cout<<issue.first<<": "<<issue.second<<"\n";
    }
    return issues.empty();
}
static void test_restore(const Job &j) {
    cb::ProcessLock state_lock(health_dir(j)/".lock",true);
    auto s=load_health(j);fs::path stage;
    try {
        auto all=get_manifests(j);require(!all.empty(),"Test restore requires an archive snapshot; mirror has no restore chain");
        const auto &final=all.back();auto chain=chain_for(all,final.id);verify_chain(j,chain);
        vector<string> eligible;uintmax_t limit=static_cast<uintmax_t>(j.test_restore_max_mb)*1024*1024;
        for(const auto &kv:final.files)if(kv.second.size<=limit)eligible.push_back(kv.first);
        require(final.files.empty()||!eligible.empty(),"No test sample fits test_restore_max_mb; increase the sample limit");
        std::set<string> want;uintmax_t needed=65536;
        if(!eligible.empty()) {want.insert(eligible[s.test_cursor%eligible.size()]);needed+=final.files.at(*want.begin()).size;}
        uintmax_t encrypted_scratch=0;
        for(const auto *m:chain)if(fs::path(m->archive).extension()==".enc")
            encrypted_scratch=std::max(encrypted_scratch,fs::file_size(job_dir(j)/m->archive));
        auto parent=fs::temp_directory_path();require(needed+encrypted_scratch<=fs::space(parent).available,"Insufficient temporary space for test restore");
        stage=parent/("cbackup-test-"+j.name+"-"+new_id());
        require(fs::create_directory(stage),"Test restore temporary directory already exists");
        extract_chain(j,chain,final,want,stage);
        fs::remove_all(stage);stage.clear();
        s.last_test=time(nullptr);s.tested_id=final.id;s.test_failure=0;s.test_error.clear();++s.test_cursor;s.notices.erase("test_failed");
        save_health(j,s);log_event(j,"test-restore",final.id+(want.empty()?" empty snapshot":" "+*want.begin()));
        std::cout<<"Test restore OK: "<<final.id<<"; sample: "<<(want.empty()?"empty snapshot":*want.begin())<<" (SHA-256 verified; temporary files removed)\n";
    } catch(const std::exception &e) {
        if(!stage.empty()) {std::error_code ec;fs::remove_all(stage,ec);}
        s.test_failure=time(nullptr);s.test_error=e.what();notify_health(j,s,"test_failed",s.test_error);save_health(j,s);throw;
    }
}
static void finish_run(const Job &j,bool mirror) {
    bool due=false;
    {
        cb::ProcessLock lock(health_dir(j)/".lock",true);auto s=load_health(j);
        s.last_success=time(nullptr);s.last_failure=0;s.error.clear();s.notices.erase("run_failed");s.notices.erase("stale");
        due=!mirror&&j.test_restore_days&&(!s.last_test||time(nullptr)-s.last_test>=static_cast<int64_t>(j.test_restore_days)*86400);
        save_health(j,s);
    }
    if(due)test_restore(j);
    health_check(j,false);
}
static void offline_confirm(const Job &j,const string &id) {
    auto all=get_manifests(j);auto chain=chain_for(all,id);verify_chain(j,chain);
    cb::ProcessLock lock(health_dir(j)/".lock",true);auto s=load_health(j);
    s.last_offline=time(nullptr);s.offline_id=id;s.notices.erase("offline_due");save_health(j,s);
    log_event(j,"offline-confirm",id);
    std::cout<<"Offline copy confirmed for "<<id<<". This records your confirmation; keep that copy disconnected.\n";
}
static void show_alerts(const Job &j) {
    std::ifstream in(health_dir(j)/"alerts.log",std::ios::binary);string line;
    while(std::getline(in,line)) {auto v=split_tabs(line);require(v.size()==3,"Invalid alerts log");std::cout<<v[0]<<"\t"<<v[1]<<"\t"<<unhex(v[2])<<"\n";}
}

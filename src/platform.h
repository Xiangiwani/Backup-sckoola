#pragma once
// OS adapters: keep filesystem paths native and never pass commands through a shell.
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <vector>
#include <cerrno>
#include <cstring>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
#include <process.h>
#include <sys/stat.h>
#include <io.h>
#define getpid _getpid
#define umask _umask
#define isatty _isatty
#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#endif
#ifdef _MSC_VER
inline std::tm *localtime_r(const time_t *t, std::tm *v) { return localtime_s(v,t)==0?v:nullptr; }
inline std::tm *gmtime_r(const time_t *t, std::tm *v) { return gmtime_s(v,t)==0?v:nullptr; }
#endif
#else
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <pwd.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <sys/mount.h>
#endif
#endif

namespace cb {
namespace fs = std::filesystem;
inline void check(bool ok,const std::string &message) { if(!ok) throw std::runtime_error(message); }
#ifdef _WIN32
inline std::wstring wide(const std::string &s) {
    if(s.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);
    check(n>0,"Invalid UTF-8 text"); std::wstring out(n,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),out.data(),n); return out;
}
inline std::string utf8(const std::wstring &s) {
    if(s.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
    check(n>0,"Cannot convert Windows text"); std::string out(n,'\0');
    WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),out.data(),n,nullptr,nullptr); return out;
}
inline std::wstring win_quote(const std::wstring &s) {
    std::wstring out=L"\""; size_t slashes=0;
    for(wchar_t c:s) {
        if(c==L'\\') { ++slashes; continue; }
        if(c==L'\"') out.append(slashes*2+1,L'\\'); else out.append(slashes,L'\\');
        slashes=0; out+=c;
    }
    out.append(slashes*2,L'\\'); return out+L"\"";
}
inline std::string current_sid() {
    HANDLE token=nullptr;check(OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token)!=0,"Cannot identify task user");
    DWORD bytes=0;GetTokenInformation(token,TokenUser,nullptr,0,&bytes);
    if(!bytes){CloseHandle(token);throw std::runtime_error("Cannot read task user");}
    std::vector<unsigned char> data(bytes);
    BOOL ok=GetTokenInformation(token,TokenUser,data.data(),bytes,&bytes);CloseHandle(token);check(ok!=0,"Cannot read task user");
    LPWSTR sid=nullptr;check(ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(data.data())->User.Sid,&sid)!=0,"Cannot format task user");
    auto out=utf8(sid);LocalFree(sid);return out;
}
#endif
inline std::vector<std::string> arguments(int argc,char **argv) {
#ifdef _WIN32
    (void)argc; (void)argv;
    int count=0; auto v=CommandLineToArgvW(GetCommandLineW(),&count); check(v!=nullptr,"Cannot read command line");
    std::vector<std::string> out; for(int i=0;i<count;++i) out.push_back(utf8(v[i])); LocalFree(v); return out;
#else
    return {argv,argv+argc};
#endif
}
inline std::string home() {
#ifdef _WIN32
    // HOME is deliberately supported for portable installs and isolated tests.
    auto p=_wgetenv(L"HOME"); if(!p||!*p) p=_wgetenv(L"USERPROFILE");
    check(p&&*p,"USERPROFILE is not set"); return utf8(p);
#else
    auto p=getenv("HOME"); check(p&&*p,"HOME is not set"); return p;
#endif
}
inline fs::path path(const std::string &s) { return fs::u8path(s); }
inline int exec(const std::vector<std::string> &args,bool quiet=false) {
    check(!args.empty(),"Empty command"); std::cout.flush(); std::cerr.flush();
#ifdef _WIN32
    std::wstring command;
    for(const auto &arg:args) { if(!command.empty()) command+=L' '; command+=win_quote(wide(arg)); }
    STARTUPINFOW si{}; si.cb=sizeof(si); PROCESS_INFORMATION pi{};
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE}; HANDLE null=INVALID_HANDLE_VALUE;
    if(quiet) {
        null=CreateFileW(L"NUL",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
        si.dwFlags=STARTF_USESTDHANDLES; si.hStdInput=null; si.hStdOutput=null; si.hStdError=null;
    }
    BOOL ok=CreateProcessW(nullptr,command.data(),nullptr,nullptr,quiet?TRUE:FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi);
    if(null!=INVALID_HANDLE_VALUE) CloseHandle(null);
    if(!ok) return 127;
    WaitForSingleObject(pi.hProcess,INFINITE); DWORD code=128; GetExitCodeProcess(pi.hProcess,&code);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess); return (int)code;
#else
    pid_t pid=fork(); check(pid>=0,"fork failed");
    if(pid==0) {
        if(quiet) { int fd=open("/dev/null",O_RDWR); if(fd>=0) { dup2(fd,1);dup2(fd,2);if(fd>2)close(fd); } }
        std::vector<char*> v; for(const auto &s:args)v.push_back(const_cast<char*>(s.c_str())); v.push_back(nullptr);
        execvp(v[0],v.data()); _exit(127);
    }
    int status=0; while(waitpid(pid,&status,0)<0)check(errno==EINTR,"waitpid failed");
    return WIFEXITED(status)?WEXITSTATUS(status):128;
#endif
}
inline fs::path executable() {
#ifdef _WIN32
    std::wstring buf(32768,L'\0'); auto n=GetModuleFileNameW(nullptr,buf.data(),(DWORD)buf.size());
    check(n>0&&n<buf.size(),"Cannot locate executable"); buf.resize(n); return fs::path(buf);
#elif defined(__APPLE__)
    uint32_t n=0; _NSGetExecutablePath(nullptr,&n); std::vector<char> buf(n);
    check(_NSGetExecutablePath(buf.data(),&n)==0,"Cannot locate executable"); return fs::weakly_canonical(buf.data());
#else
    return fs::read_symlink("/proc/self/exe");
#endif
}
inline void replace_file(const fs::path &from,const fs::path &to) {
#ifdef _WIN32
    check(MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0,"Cannot atomically replace file: "+to.u8string());
#else
    fs::rename(from,to);
#endif
}
struct FileStat { uintmax_t size=0; int64_t mtime_ns=0,ctime_ns=0,mtime=0; uint64_t ino=0; unsigned mode=0600; };
inline FileStat stat(const fs::path &p) {
    FileStat out;
#ifdef _WIN32
    HANDLE h=CreateFileW(p.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    check(h!=INVALID_HANDLE_VALUE,"Cannot read metadata: "+p.u8string());
    BY_HANDLE_FILE_INFORMATION info{}; FILE_BASIC_INFO basic{};
    bool ok=GetFileInformationByHandle(h,&info)&&GetFileInformationByHandleEx(h,FileBasicInfo,&basic,sizeof(basic)); CloseHandle(h);
    check(ok&&!(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY),"Not a readable regular file: "+p.u8string());
    constexpr int64_t offset=116444736000000000LL;
    out.size=((uint64_t)info.nFileSizeHigh<<32)|info.nFileSizeLow;
    out.mtime_ns=(basic.LastWriteTime.QuadPart-offset)*100;
    out.ctime_ns=(basic.ChangeTime.QuadPart-offset)*100;
    out.ino=((uint64_t)info.nFileIndexHigh<<32)|info.nFileIndexLow; out.mtime=out.mtime_ns/1000000000LL;
#else
    struct ::stat sb{}; check(::stat(p.c_str(),&sb)==0&&S_ISREG(sb.st_mode),"Cannot read file: "+p.string());
    out.size=(uintmax_t)sb.st_size; out.mode=sb.st_mode&0777; out.ino=sb.st_ino; out.mtime=sb.st_mtime;
#ifdef __APPLE__
    out.mtime_ns=(int64_t)sb.st_mtimespec.tv_sec*1000000000LL+sb.st_mtimespec.tv_nsec;
    out.ctime_ns=(int64_t)sb.st_ctimespec.tv_sec*1000000000LL+sb.st_ctimespec.tv_nsec;
#else
    out.mtime_ns=(int64_t)sb.st_mtim.tv_sec*1000000000LL+sb.st_mtim.tv_nsec;
    out.ctime_ns=(int64_t)sb.st_ctim.tv_sec*1000000000LL+sb.st_ctim.tv_nsec;
#endif
#endif
    return out;
}
inline void set_mtime(const fs::path &p,int64_t sec) {
#ifdef _WIN32
    HANDLE h=CreateFileW(p.c_str(),FILE_WRITE_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(h==INVALID_HANDLE_VALUE)return;
    uint64_t ticks=(uint64_t)(sec*10000000LL+116444736000000000LL); FILETIME t{(DWORD)ticks,(DWORD)(ticks>>32)};
    SetFileTime(h,nullptr,nullptr,&t);CloseHandle(h);
#else
    struct timespec ts[2];ts[0].tv_sec=0;ts[0].tv_nsec=UTIME_NOW;ts[1].tv_sec=(time_t)sec;ts[1].tv_nsec=0;
    (void)utimensat(AT_FDCWD,p.c_str(),ts,0);
#endif
}
class ProcessLock {
#ifdef _WIN32
    HANDLE h=INVALID_HANDLE_VALUE;
#else
    int fd=-1;
#endif
public:
    ProcessLock(const fs::path &p,bool wait) {
        fs::create_directories(p.parent_path());
#ifdef _WIN32
        h=CreateFileW(p.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        check(h!=INVALID_HANDLE_VALUE,"Cannot open job lock"); OVERLAPPED ov{};
        if(!LockFileEx(h,LOCKFILE_EXCLUSIVE_LOCK|(wait?0:LOCKFILE_FAIL_IMMEDIATELY),0,1,0,&ov)) { CloseHandle(h);h=INVALID_HANDLE_VALUE;throw std::runtime_error("Another operation is running for this job"); }
#else
        fd=open(p.c_str(),O_CREAT|O_RDWR|O_CLOEXEC,0600);check(fd>=0,"Cannot open job lock");
        if(flock(fd,LOCK_EX|(wait?0:LOCK_NB))!=0) {close(fd);fd=-1;throw std::runtime_error("Another operation is running for this job");}
#endif
    }
    ~ProcessLock() {
#ifdef _WIN32
        if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);
#else
        if(fd>=0)close(fd);
#endif
    }
    ProcessLock(const ProcessLock&)=delete;ProcessLock &operator=(const ProcessLock&)=delete;
};
inline void initialize() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);SetConsoleCP(CP_UTF8);SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS);
#else
    umask(0077); (void)nice(10);
#endif
}
// Portable glob matching, including [] ranges and ?; * also matches path separators.
inline bool glob(const std::string &pattern,const std::string &value,size_t pi=0,size_t vi=0) {
    while(pi<pattern.size()) {
        char p=pattern[pi++];
        if(p=='*') { while(pi<pattern.size()&&pattern[pi]=='*')++pi; if(pi==pattern.size())return true;
            for(size_t n=vi;n<=value.size();++n) { if(glob(pattern,value,pi,n))return true; } return false; }
        if(vi==value.size())return false;
        if(p=='?') {++vi;continue;}
        if(p=='[') {
            size_t end=pattern.find(']',pi); if(end!=std::string::npos) {
                bool inverse=pi<end&&(pattern[pi]=='!'||pattern[pi]=='^');if(inverse)++pi; bool matched=false;
                while(pi<end) {char lo=pattern[pi++],hi=lo;if(pi+1<end&&pattern[pi]=='-'){hi=pattern[pi+1];pi+=2;} if(value[vi]>=lo&&value[vi]<=hi)matched=true;}
                if(matched==inverse)return false;
                pi=end+1;++vi;continue;
            }
        }
        if(p=='\\'&&pi<pattern.size())p=pattern[pi++];
        if(p!=value[vi++])return false;
    }
    return vi==value.size();
}
inline std::string xml(const std::string &s) {
    std::string out; for(char c:s) {if(c=='&')out+="&amp;";else if(c=='<')out+="&lt;";else if(c=='>')out+="&gt;";else if(c=='\"')out+="&quot;";else if(c=='\'')out+="&apos;";else out+=c;}return out;
}
inline void write(const fs::path &p,const std::string &s) {
    fs::create_directories(p.parent_path()); std::ofstream f(p,std::ios::binary|std::ios::trunc);check(bool(f),"Cannot write: "+p.u8string());f<<s;f.close();check(bool(f),"Cannot finish writing: "+p.u8string());
}
inline int interval_seconds(const std::string &schedule) {
    if(schedule=="hourly")return 3600;
    if(schedule.rfind("every ",0)!=0)return 0;
    auto value=schedule.substr(6);size_t n=0;long long count=std::stoll(value,&n);auto unit=value.substr(n);
    long long seconds=count*(unit=="min"?60:unit=="h"?3600:unit=="d"?86400:0);
    check(seconds>0&&seconds<=2147483647,"Unsupported schedule interval");return (int)seconds;
}
inline fs::path scheduler_dir(const std::string &h) {
#ifdef _WIN32
    return path(h)/".config/cbackup/tasks";
#elif defined(__APPLE__)
    return path(h)/"Library/LaunchAgents";
#else
    return path(h)/".config/systemd/user";
#endif
}
inline bool schedule_active(const std::string &name,const std::string &h) {
#ifdef _WIN32
    (void)h; return exec({"schtasks.exe","/Query","/TN","CBackup-"+name},true)==0||exec({"schtasks.exe","/Query","/TN","CBackup-"+name+"-startup"},true)==0;
#elif defined(__APPLE__)
    (void)h;return exec({"launchctl","print","gui/"+std::to_string(getuid())+"/org.cbackup."+name},true)==0;
#else
    (void)h;return exec({"systemctl","--user","is-active","--quiet","cbackup-"+name+".timer"},true)==0||exec({"systemctl","--user","is-active","--quiet","cbackup-"+name+"-startup.service"},true)==0;
#endif
}
inline void schedule_remove(const std::string &name,const std::string &h) {
    auto dir=scheduler_dir(h); std::error_code ec;
#ifdef _WIN32
    for(auto suffix:{"","-startup","-health"}) {auto task="CBackup-"+name+suffix;(void)exec({"schtasks.exe","/Delete","/TN",task,"/F"},true);fs::remove(dir/(task+".xml"),ec);}
#elif defined(__APPLE__)
    for(auto suffix:{"",".startup",".health"}) {auto label="org.cbackup."+name+suffix;(void)exec({"launchctl","bootout","gui/"+std::to_string(getuid())+"/"+label},true);fs::remove(dir/(label+".plist"),ec);}
#else
    auto base="cbackup-"+name;
    (void)exec({"systemctl","--user","disable","--now",base+".timer",base+"-startup.service",base+"-health.timer"},true);
    for(auto suffix:{".timer",".service","-startup.service","-health.service","-health.timer"})fs::remove(dir/(base+suffix),ec);
    (void)exec({"systemctl","--user","daemon-reload"},true);
#endif
}
inline void schedule_install(const std::string &name,const std::string &schedule,bool autostart,bool watch,const std::string &h,const fs::path &exe) {
    auto dir=scheduler_dir(h); fs::create_directories(dir);
    const int seconds=interval_seconds(schedule);
    auto kind=schedule.substr(0,schedule.find(' '));auto value=schedule.find(' ')==std::string::npos?"":schedule.substr(schedule.find(' ')+1);
    std::string day,clock=value;if(kind=="weekly"){auto at=value.find('@');day=value.substr(0,at);clock=value.substr(at+1);}
    const std::vector<std::string> days={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    int weekday=-1;for(size_t i=0;i<days.size();++i)if(days[i]==day)weekday=(int)i;
#ifdef _WIN32
    const std::string sid=current_sid();
    auto action=[&](const std::string &command) {return "<Actions Context=\"Author\"><Exec><Command>"+xml(exe.u8string())+"</Command><Arguments>"+command+" "+name+"</Arguments><WorkingDirectory>"+xml(exe.parent_path().u8string())+"</WorkingDirectory></Exec></Actions>";};
    auto register_task=[&](const std::string &task,const std::string &trigger,const std::string &command) {
        std::string content="<?xml version=\"1.0\" encoding=\"UTF-8\"?><Task version=\"1.2\" xmlns=\"http://schemas.microsoft.com/windows/2004/02/mit/task\"><Triggers>"+trigger+"</Triggers><Principals><Principal id=\"Author\"><UserId>"+sid+"</UserId><LogonType>InteractiveToken</LogonType><RunLevel>LeastPrivilege</RunLevel></Principal></Principals><Settings><MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy><DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries><StopIfGoingOnBatteries>false</StopIfGoingOnBatteries><StartWhenAvailable>true</StartWhenAvailable><ExecutionTimeLimit>PT0S</ExecutionTimeLimit><Priority>7</Priority></Settings>"+action(command)+"</Task>";
        auto file=dir/(task+".xml");write(file,content);check(exec({"schtasks.exe","/Create","/TN",task,"/XML",file.u8string(),"/F"})==0,"Cannot register Windows scheduled task");
    };
    std::string trigger;
    if(seconds)trigger="<TimeTrigger><Repetition><Interval>PT"+std::to_string(seconds)+"S</Interval><StopAtDurationEnd>false</StopAtDurationEnd></Repetition><StartBoundary>2020-01-01T00:00:00</StartBoundary><Enabled>true</Enabled></TimeTrigger>";
    else if(kind=="daily"||kind=="weekly") {
        const std::vector<std::string> full={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
        auto calendar=kind=="daily"?"<ScheduleByDay><DaysInterval>1</DaysInterval></ScheduleByDay>":"<ScheduleByWeek><WeeksInterval>1</WeeksInterval><DaysOfWeek><"+full.at(weekday)+"/></DaysOfWeek></ScheduleByWeek>";
        trigger="<CalendarTrigger><StartBoundary>2020-01-01T"+clock+":00</StartBoundary><Enabled>true</Enabled>"+calendar+"</CalendarTrigger>";
    } else check(schedule.empty()||schedule=="startup","Unsupported schedule");
    if(!trigger.empty())register_task("CBackup-"+name,trigger,"run");
    else { (void)exec({"schtasks.exe","/Delete","/TN","CBackup-"+name,"/F"},true);std::error_code ec;fs::remove(dir/("CBackup-"+name+".xml"),ec); }
    if(autostart)register_task("CBackup-"+name+"-startup","<LogonTrigger><Enabled>true</Enabled><UserId>"+sid+"</UserId></LogonTrigger>",watch?"watch":"run");
    else (void)exec({"schtasks.exe","/Delete","/TN","CBackup-"+name+"-startup","/F"},true);
    register_task("CBackup-"+name+"-health","<TimeTrigger><Repetition><Interval>PT1H</Interval><StopAtDurationEnd>false</StopAtDurationEnd></Repetition><StartBoundary>2020-01-01T00:00:00</StartBoundary><Enabled>true</Enabled></TimeTrigger>","health");
#elif defined(__APPLE__)
    auto register_agent=[&](const std::string &label,const std::string &timing,const std::string &command,bool keepalive) {
        auto file=dir/(label+".plist");(void)exec({"launchctl","bootout","gui/"+std::to_string(getuid())+"/"+label},true);
        write(file,"<?xml version=\"1.0\" encoding=\"UTF-8\"?><!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\"><plist version=\"1.0\"><dict><key>Label</key><string>"+label+"</string><key>ProgramArguments</key><array><string>"+xml(exe.u8string())+"</string><string>"+command+"</string><string>"+name+"</string></array><key>Nice</key><integer>10</integer><key>LowPriorityIO</key><true/><key>ProcessType</key><string>Background</string>"+timing+(keepalive?"<key>KeepAlive</key><true/>":"")+"</dict></plist>");
        check(exec({"launchctl","bootstrap","gui/"+std::to_string(getuid()),file.u8string()})==0,"Cannot register launchd agent");
    };
    std::string timing;
    if(seconds)timing="<key>StartInterval</key><integer>"+std::to_string(seconds)+"</integer>";
    else if(kind=="daily"||kind=="weekly")timing="<key>StartCalendarInterval</key><dict><key>Hour</key><integer>"+std::to_string(std::stoi(clock.substr(0,2)))+"</integer><key>Minute</key><integer>"+std::to_string(std::stoi(clock.substr(3,2)))+"</integer>"+(kind=="weekly"?"<key>Weekday</key><integer>"+std::to_string(weekday)+"</integer>":"")+"</dict>";
    else check(schedule.empty()||schedule=="startup","Unsupported schedule");
    if(autostart&&!watch)timing+="<key>RunAtLoad</key><true/>";
    register_agent("org.cbackup."+name,timing,"run",false);
    register_agent("org.cbackup."+name+".health","<key>StartInterval</key><integer>3600</integer><key>RunAtLoad</key><true/>","health",false);
    if(watch&&autostart)register_agent("org.cbackup."+name+".startup","<key>RunAtLoad</key><true/>","watch",true);
    else {(void)exec({"launchctl","bootout","gui/"+std::to_string(getuid())+"/org.cbackup."+name+".startup"},true);std::error_code ec;fs::remove(dir/("org.cbackup."+name+".startup.plist"),ec);}
#else
    auto quote=[](const std::string &s) {std::string out="\"";for(char c:s){if(c=='\\'||c=='\"')out+='\\';if(c=='%')out+='%';out+=c;}return out+'\"';};
    auto base="cbackup-"+name;
    const std::string service="[Unit]\nDescription=CBackup "+name+"\n[Service]\nType=oneshot\nNice=10\nIOSchedulingClass=idle\nExecStart="+quote(exe.u8string())+" run "+name+"\n";
    write(dir/(base+".service"),service);
    std::string timing;
    if(seconds)timing="OnActiveSec=1min\nOnUnitInactiveSec="+std::to_string(seconds)+"s\n";
    else if(kind=="daily")timing="OnCalendar=*-*-* "+clock+":00\nPersistent=true\n";
    else if(kind=="weekly")timing="OnCalendar="+day+" *-*-* "+clock+":00\nPersistent=true\n";
    else check(schedule.empty()||schedule=="startup","Unsupported schedule");
    if(!timing.empty())write(dir/(base+".timer"),"[Unit]\nDescription=CBackup schedule "+name+"\n[Timer]\n"+timing+"Unit="+base+".service\n[Install]\nWantedBy=timers.target\n");
    if(autostart)write(dir/(base+"-startup.service"),"[Unit]\nDescription=CBackup startup "+name+"\n[Service]\nType="+(watch?std::string("simple"):std::string("oneshot"))+"\nNice=10\nIOSchedulingClass=idle\nExecStart="+quote(exe.u8string())+(watch?" watch ":" run ")+name+"\n"+(watch?"Restart=on-failure\nRestartSec=30\n":"")+"[Install]\nWantedBy=default.target\n");
    else {(void)exec({"systemctl","--user","disable","--now",base+"-startup.service"},true);std::error_code ec;fs::remove(dir/(base+"-startup.service"),ec);}
    write(dir/(base+"-health.service"),"[Unit]\nDescription=CBackup health "+name+"\n[Service]\nType=oneshot\nNice=10\nExecStart="+quote(exe.u8string())+" health "+name+"\n");
    write(dir/(base+"-health.timer"),"[Unit]\nDescription=CBackup hourly health "+name+"\n[Timer]\nOnCalendar=hourly\nPersistent=true\nUnit="+base+"-health.service\n[Install]\nWantedBy=timers.target\n");
    check(exec({"systemctl","--user","daemon-reload"})==0,"systemd user session unavailable");
    if(!timing.empty()) {check(exec({"systemctl","--user","enable","--now",base+".timer"})==0,"Cannot enable backup timer");check(exec({"systemctl","--user","restart",base+".timer"})==0,"Cannot restart backup timer");}
    else {(void)exec({"systemctl","--user","disable","--now",base+".timer"},true);std::error_code ec;fs::remove(dir/(base+".timer"),ec);}
    if(autostart)check(exec({"systemctl","--user","enable","--now",base+"-startup.service"})==0,"Cannot enable backup startup");
    check(exec({"systemctl","--user","enable","--now",base+"-health.timer"})==0,"Cannot enable health timer");
#endif
}
inline bool mounted_external(const fs::path &p) {
#ifdef _WIN32
    wchar_t root[32768]{}; if(!GetVolumePathNameW(p.c_str(),root,32768))return false;
    wchar_t windows[32768]{};if(!GetWindowsDirectoryW(windows,32768))return false;wchar_t sysroot[32768]{};if(!GetVolumePathNameW(windows,sysroot,32768))return false;
    auto type=GetDriveTypeW(root); return (type==DRIVE_REMOVABLE||type==DRIVE_FIXED)&&_wcsicmp(root,sysroot)!=0&&GetVolumeInformationW(root,nullptr,0,nullptr,nullptr,nullptr,nullptr,0);
#elif defined(__APPLE__)
    struct statfs s{};if(statfs(p.c_str(),&s)!=0)return false;return std::strcmp(s.f_mntonname,"/")!=0&&(s.f_flags&MNT_LOCAL);
#else
    auto inside=[](const fs::path &child,const fs::path &parent) { auto ci=child.begin();for(auto pi=parent.begin();pi!=parent.end();++pi,++ci)if(ci==child.end()||*ci!=*pi)return false;return true; };
    auto unescape=[](const std::string &s) {std::string out;for(size_t i=0;i<s.size();++i){if(s[i]=='\\'&&i+3<s.size()&&s[i+1]>='0'&&s[i+1]<='7'&&s[i+2]>='0'&&s[i+2]<='7'&&s[i+3]>='0'&&s[i+3]<='7'){out+=(char)((s[i+1]-'0')*64+(s[i+2]-'0')*8+s[i+3]-'0');i+=3;}else out+=s[i];}return out;};
    std::ifstream input("/proc/self/mountinfo"); if(!input)return false;
    std::string line; auto target=fs::weakly_canonical(p);
    while(std::getline(input,line)) {std::istringstream row(line);std::string id,parent,device,root,mount;if(!(row>>id>>parent>>device>>root>>mount))continue;auto base=fs::path(unescape(mount));if(base!="/"&&inside(target,base)&&fs::exists(base))return true;}return false;
#endif
}
} // namespace cb

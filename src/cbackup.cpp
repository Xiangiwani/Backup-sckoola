#define _POSIX_C_SOURCE 200809L
#include <archive.h>
#include <archive_entry.h>
#include <openssl/evp.h>
#include "platform.h"
#include "crypto.h"
#include "localization.h"
#include <thread>
#include <csignal>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using std::string;
using std::vector;
using std::map;

static const char *VERSION = "1.3";

static void fail(const string &s) { throw std::runtime_error(s); }
static void require(bool b, const string &s) { if (!b) fail(s); }
static string lower(string s) { for (char &c : s) c = static_cast<char>(std::tolower((unsigned char)c)); return s; }
static bool all_digits(const string &s) { return !s.empty() && std::all_of(s.begin(), s.end(), [](char c){ return std::isdigit((unsigned char)c); }); }
static bool valid_name(const string &v) {
    if (v.empty() || v.size() > 60) return false;
    for (char c : v) if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
    return true;
}
static string home() { return cb::home(); }
static bool valid_env_name(const string &v) {
    if(v.empty()||v.size()>128||!(std::isalpha(static_cast<unsigned char>(v[0]))||v[0]=='_'))return false;
    return std::all_of(v.begin(),v.end(),[](unsigned char c){return std::isalnum(c)||c=='_';});
}
static fs::path conf_dir() { return fs::path(home()) / ".config/cbackup/jobs"; }
static string job_file(const string &job) { require(valid_name(job), "Invalid job name (letters, digits, _ and -)"); return (conf_dir() / (job + ".conf")).string(); }
static string hex(const string &s) {
    static const char *d = "0123456789abcdef";
    string out; out.reserve(s.size() * 2);
    for (unsigned char c : s) { out += d[c>>4]; out += d[c&15]; }
    return out;
}
static string unhex(const string &s) {
    require(s.size() % 2 == 0, "Invalid hex in manifest"); string o;
    for (size_t i=0; i<s.size(); i+=2) {
        auto digit=[](char c)->int { if (c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; return -1; };
        int a=digit(s[i]), b=digit(s[i+1]); require(a>=0 && b>=0, "Invalid hex");
        o += char((a<<4)|b);
    }
    return o;
}
static string sha256_stream(std::istream &f) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new(); require(ctx, "EVP_MD_CTX_new failed");
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ptr(ctx, EVP_MD_CTX_free);
    require(EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1, "SHA256 initialization failed");
    std::array<char, 131072> buf{};
    while (f.read(buf.data(), buf.size()) || f.gcount()) {
        require(EVP_DigestUpdate(ctx, buf.data(), (size_t)f.gcount()) == 1, "SHA256 update failed");
    }
    require(!f.bad(), "Read error during SHA256");
    unsigned char digest[EVP_MAX_MD_SIZE]; unsigned int length=0;
    require(EVP_DigestFinal_ex(ctx, digest, &length) == 1, "SHA256 finalization failed");
    return hex(string((const char*)digest, length));
}
static string sha256_file(const fs::path &path) {
    std::ifstream f(path, std::ios::binary); require(bool(f), "Cannot read file: " + path.string());
    return sha256_stream(f);
}
static string sha256_text(const string &s) { std::istringstream i(s); return sha256_stream(i); }
static fs::path abs_path(const fs::path &p) { return fs::absolute(p).lexically_normal(); }
static bool under(const fs::path &child, const fs::path &parent) {
    fs::path c=fs::weakly_canonical(child), p=fs::weakly_canonical(parent);
    auto ci=c.begin(), pi=p.begin();
    for (; pi!=p.end(); ++pi,++ci) if(ci==c.end() || *ci!=*pi) return false;
    return true;
}
static void no_newline(const string &s) { require(s.find('\n')==string::npos && s.find('\r')==string::npos && s.find('\0')==string::npos, "Newline/NUL is not allowed"); }
static void atomic_write(const fs::path &path, const string &content) {
    fs::create_directories(path.parent_path());
    fs::path tmp=path; tmp += ".tmp." + std::to_string(getpid());
    try {
        std::ofstream f(tmp, std::ios::binary|std::ios::trunc);
        require(bool(f), "Cannot write: " + tmp.string());
        f << content; f.flush(); require(bool(f), "Write error: " + tmp.string()); f.close();
        cb::replace_file(tmp,path);
    } catch(...) { std::error_code ec; fs::remove(tmp,ec); throw; }
}
static int exec_args(const vector<string> &args, bool quiet=false) {
    return cb::exec(args,quiet);
}
static string read_quoted(const string &s) {
    std::istringstream ss(s); string o; ss >> std::quoted(o);
    require(bool(ss), "Invalid configuration value: " + s); return o;
}
static string quoted(const string &s) { no_newline(s); std::ostringstream o; o << std::quoted(s); return o.str(); }
static int positive_int(const string &s, int low, int high) {
    try { size_t n=0; long v=std::stol(s,&n); require(n==s.size()&&v>=low&&v<=high, "Invalid value: " + s); return (int)v; }
    catch(const std::exception &) { fail("Number out of range ("+std::to_string(low)+"-"+std::to_string(high)+"): " + s); }
    return 0;
}
static string join(const vector<string>&v, const string &sep) { string out; for(auto &s:v){if(!out.empty())out+=sep;out+=s;}return out; }
static string human_size(uintmax_t b) {
    const char *u[]={"B","KB","MB","GB","TB"}; double v=(double)b; int i=0;
    while(v>=1024 && i<4) { v/=1024; ++i; }
    std::ostringstream o; if(i==0) o<<b<<" B"; else o<<std::fixed<<std::setprecision(1)<<v<<" "<<u[i];
    return o.str();
}

// ---------------------------------------------------------------- tijd
// Snapshot-ID's zijn UTC: JJJJMMDD_UUMMSS_micro_pid. Weergave en dagkeuze gebeuren in lokale tijd.
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y-399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153*(m > 2 ? m-3 : m+9) + 2)/5 + d-1;
    const unsigned doe = yoe * 365 + yoe/4 - yoe/100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}
static int64_t id_time(const string &id) {
    if(id.size()<15 || id[8]!='_') return -1;
    for(int i=0;i<15;++i) if(i!=8 && !std::isdigit((unsigned char)id[i])) return -1;
    int y=std::stoi(id.substr(0,4)), mo=std::stoi(id.substr(4,2)), d=std::stoi(id.substr(6,2));
    int H=std::stoi(id.substr(9,2)), M=std::stoi(id.substr(11,2)), S=std::stoi(id.substr(13,2));
    if(mo<1||mo>12||d<1||d>31||H>23||M>59||S>60) return -1;
    return days_from_civil(y,(unsigned)mo,(unsigned)d)*86400 + H*3600 + M*60 + S;
}
static const char *NL_DAYS[]={"zondag","maandag","dinsdag","woensdag","donderdag","vrijdag","zaterdag"};
static const char *NL_SHORT[]={"zo","ma","di","wo","do","vr","za"};
static const char *EN_DAYS[]={"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
static const char *EN_SHORT[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static string fmt_local(int64_t t, bool seconds=true) {
    if(t<0) return "?";
    time_t tt=(time_t)t; std::tm lt{}; localtime_r(&tt,&lt);
    std::ostringstream o; o<<(cb_i18n::language()=="nl"?NL_SHORT:EN_SHORT)[lt.tm_wday]<<" "<<std::put_time(&lt, seconds?"%d-%m-%Y %H:%M:%S":"%d-%m-%Y %H:%M");
    return o.str();
}
struct YMD { int y=0, m=0, d=0; };
static std::tm tm_of(const YMD &x, int h, int mi) { std::tm t{}; t.tm_year=x.y-1900; t.tm_mon=x.m-1; t.tm_mday=x.d; t.tm_hour=h; t.tm_min=mi; t.tm_isdst=-1; return t; }
static YMD today_local() { time_t now=time(nullptr); std::tm lt{}; localtime_r(&now,&lt); return {lt.tm_year+1900, lt.tm_mon+1, lt.tm_mday}; }
static YMD add_days(const YMD &x, int n) { std::tm t=tm_of(x,12,0); t.tm_mday+=n; mktime(&t); return {t.tm_year+1900,t.tm_mon+1,t.tm_mday}; }
static int weekday_of(const YMD &x) { std::tm t=tm_of(x,12,0); mktime(&t); return t.tm_wday; }
static bool valid_ymd(const YMD &x) { if(x.y<1970||x.y>9999||x.m<1||x.m>12||x.d<1||x.d>31) return false; YMD n=add_days(x,0); return n.y==x.y&&n.m==x.m&&n.d==x.d; }
static bool ymd_after(const YMD &a, const YMD &b) { return std::tie(a.y,a.m,a.d) > std::tie(b.y,b.m,b.d); }
static int64_t local_at(const YMD &x, int h, int mi) { std::tm t=tm_of(x,h,mi); return (int64_t)mktime(&t); }
static string fmt_day(const YMD &x) {
    std::ostringstream o; o<<(cb_i18n::language()=="nl"?NL_DAYS:EN_DAYS)[weekday_of(x)]<<" "<<std::setfill('0')<<std::setw(2)<<x.d<<"-"<<std::setw(2)<<x.m<<"-"<<x.y; return o.str();
}

// ---------------------------------------------------------------- compressie
struct CompInfo { const char *name, *ext; int maxlevel, deflevel; const char *note; };
static const vector<CompInfo> &comp_table() {
    static const vector<CompInfo> t={
        {"zstd", ".tar.zst", 22, 3, "snel en klein - aanbevolen (standaard)"},
        {"gzip", ".tar.gz",   9, 6, "overal te openen, redelijk snel"},
        {"xz",   ".tar.xz",   9, 6, "kleinste archieven, langzaam (zwaar op een Pi)"},
        {"bzip2",".tar.bz2",  9, 9, "klassiek; meestal slechter dan xz of zstd"},
        {"lz4",  ".tar.lz4",  9, 1, "very fast, less compression"},
        {"zip",  ".zip",      9, 6, "direct te openen in Windows/macOS"},
        {"none", ".tar",      0, 0, "no compression (fastest, largest)"}};
    return t;
}
static const CompInfo *comp_info(const string &c) { for(auto &i:comp_table()) if(c==i.name) return &i; return nullptr; }
static string comp_names() { vector<string> v; for(auto &i:comp_table()) v.push_back(i.name); return join(v,"|"); }
static void check_level(const string &type, int level) {
    const CompInfo *ci=comp_info(type); require(ci, "Unknown compression '"+type+"'. Choose from: "+comp_names());
    if(level==0) return;
    require(ci->maxlevel>0, "Compression 'none' has no level");
    require(level>=1 && level<=ci->maxlevel, "Level for "+type+" must be 1-"+std::to_string(ci->maxlevel)+" (0 = default)");
}
// "xz" of "xz:9" -> type + niveau (0 = standaardniveau van het type)
static void parse_compression(const string &v, string &type, int &level) {
    size_t c=v.find(':'); type=lower(v.substr(0,c)); level=0;
    if(type=="zst") type="zstd"; else if(type=="gz") type="gzip"; else if(type=="bz2") type="bzip2"; else if(type=="none"||type=="store") type="none";
    if(c!=string::npos) level=positive_int(v.substr(c+1),0,99);
    check_level(type,level);
}
static string comp_label(const string &type, int level) {
    const CompInfo *ci=comp_info(type); if(!ci || ci->maxlevel==0) return type;
    return type+" (level "+(level?std::to_string(level):"default = "+std::to_string(ci->deflevel))+")";
}
static string ext_for(const string &compression) { const CompInfo *ci=comp_info(compression); require(ci,"Unknown compression"); return ci->ext; }
static string archive_err(struct archive *a) { const char *e=archive_error_string(a); return e?e:"unknown error"; }
static int setup_writer(struct archive *a, const string &c, int level) {
    int rc;
    string lv=std::to_string(level);
    if(c=="zip") {
        rc=archive_write_set_format_zip(a); if(rc!=ARCHIVE_OK) return rc;
        return level>0 ? archive_write_set_format_option(a,"zip","compression-level",lv.c_str()) : ARCHIVE_OK;
    }
    rc = c=="zstd"?archive_write_add_filter_zstd(a) : c=="gzip"?archive_write_add_filter_gzip(a)
       : c=="xz"?archive_write_add_filter_xz(a) : c=="bzip2"?archive_write_add_filter_bzip2(a)
       : c=="lz4"?archive_write_add_filter_lz4(a) : archive_write_add_filter_none(a);
    if(rc!=ARCHIVE_OK) return rc;
    rc=archive_write_set_format_pax_restricted(a); if(rc!=ARCHIVE_OK) return rc;
    return level>0 ? archive_write_set_filter_option(a,nullptr,"compression-level",lv.c_str()) : ARCHIVE_OK;
}
// Lege proefarchief in het geheugen: is dit type (en niveau) op dit systeem bruikbaar?
static string compression_problem(const string &c, int level) {
    struct archive *a=archive_write_new(); if(!a) return "archive_write_new failed";
    string err; vector<char> buf(1<<20); size_t used=0;
    if(setup_writer(a,c,level)!=ARCHIVE_OK) err=archive_err(a);
    else if(archive_write_open_memory(a,buf.data(),buf.size(),&used)!=ARCHIVE_OK) err=archive_err(a);
    else if(archive_write_close(a)!=ARCHIVE_OK) err=archive_err(a);
    archive_write_free(a);
    return err;
}
static void require_compression(const string &c, int level) {
    string p=compression_problem(c,level);
    require(p.empty(), "Compression "+comp_label(c,level)+" is unavailable on this system: "+p+" (zie: cbackup compression)");
}
static void show_compressions() {
    std::cout<<"Compression formats - select with: cbackup config JOB set compression TYPE[:LEVEL]\n\n";
    std::cout<<"  "<<std::left<<std::setw(7)<<"TYPE"<<std::setw(10)<<"ARCHIVE"<<std::setw(9)<<"NIVEAU"<<std::setw(11)<<"STANDAARD"<<std::setw(48)<<"OMSCHRIJVING"<<"STATUS\n";
    for(auto &i:comp_table()) {
        string p=compression_problem(i.name,0);
        std::cout<<"  "<<std::left<<std::setw(7)<<i.name<<std::setw(10)<<i.ext
                 <<std::setw(9)<<(i.maxlevel?"1-"+std::to_string(i.maxlevel):"-")
                 <<std::setw(11)<<(i.maxlevel?std::to_string(i.deflevel):"-")<<std::setw(48)<<i.note
                 <<(p.empty()?"ok":"NOT available ("+p+")")<<"\n";
    }
    std::cout<<R"(
Examples:
  cbackup config mybackup set compression zstd       # default
  cbackup config mybackup set compression xz:9       # smallest archive
  cbackup config mybackup set compression lz4        # fastest
  cbackup run mybackup --compression zip             # one-time override
Each backup records its format; you can switch formats and still restore every snapshot.
)";
}

// ---------------------------------------------------------------- planning (systemd-gebruikerstimer)
// Waarde in config: "hourly" | "every 10min" | "daily 22:00" | "weekly Sun@02:00" | "" (uit)
static bool hhmm_ok(const string &v) {
    if(!(v.size()==5&&v[2]==':'&&std::isdigit((unsigned char)v[0])&&std::isdigit((unsigned char)v[1])&&std::isdigit((unsigned char)v[3])&&std::isdigit((unsigned char)v[4]))) return false;
    return std::stoi(v.substr(0,2))<24 && std::stoi(v.substr(3,2))<60;
}
struct TimerSpec { string timerpart, desc; };
static TimerSpec timer_spec(const string &s) {
    if(s=="startup") return {"OnStartupSec=1min\n","at startup/login"};
    size_t sp=s.find(' '); string kind=s.substr(0,sp), val=sp==string::npos?"":s.substr(sp+1);
    if(kind=="hourly") { require(val.empty(),"Invalid schedule"); return {"OnCalendar=hourly\nPersistent=true\n","hourly (on the hour)"}; }
    if(kind=="every") {
        require(!val.empty()&&val.size()<=16,"Invalid interval");
        size_t n=val.find_first_not_of("0123456789");
        require(n!=string::npos&&n>0,"Use e.g. 10min, 2h or 1d");
        positive_int(val.substr(0,n),1,100000);
        require(val.substr(n)=="min"||val.substr(n)=="h"||val.substr(n)=="d","Use 10min, 2h or 1d");
        return {"OnActiveSec=1min\nOnUnitInactiveSec="+val+"\n","every "+val+" (measured from the end of the previous run)"};
    }
    if(kind=="daily") { require(hhmm_ok(val),"Use HH:MM, e.g. 22:00"); return {"OnCalendar=*-*-* "+val+":00\nPersistent=true\n","daily at "+val}; }
    if(kind=="weekly") {
        size_t at=val.find('@'); require(at!=string::npos,"Use e.g. Sun@02:00");
        string day=val.substr(0,at), t=val.substr(at+1);
        require(day=="Mon"||day=="Tue"||day=="Wed"||day=="Thu"||day=="Fri"||day=="Sat"||day=="Sun","Use Mon..Sun");
        require(hhmm_ok(t),"Use HH:MM");
        return {"OnCalendar="+day+" *-*-* "+t+":00\nPersistent=true\n","weekly on "+day+" at "+t};
    }
    fail("Invalid schedule: "+s); return {};
}

// ---------------------------------------------------------------- job
struct Job {
    string name, dest, usb, cloud, mode="auto", compression="zstd", schedule;
    string encryption="none", password_env="CBACKUP_PASSWORD", profile="all";
    bool exclude_dependencies=true, autostart=true, watch=false;
    int compression_level=0, full_every=12, keep_chains=1, keep_days=90, watch_seconds=30, log_max_mb=5;
    bool notifications=true, desktop_notifications=false;
    int stale_hours=24, notify_repeat_hours=24, test_restore_days=7, test_restore_max_mb=64, offline_reminder_days=14;
    int storage_quota_mb=0;
    vector<string> sources, exts, includes, excludes;
};
static void save_job(const Job &j) {
    std::ostringstream o;
    o << "# cbackup " << VERSION << "; wijzig via cbackup config / cbackup schedule\n";
    auto put=[&](const string &k,const string &v){o<<k<<"="<<quoted(v)<<"\n";};
    put("dest",j.dest); put("usb",j.usb); put("cloud",j.cloud); put("mode",j.mode); put("compression",j.compression);
    o<<"compression_level="<<j.compression_level<<"\nfull_every="<<j.full_every<<"\nkeep_chains="<<j.keep_chains<<"\nkeep_days="<<j.keep_days<<"\n";
    put("schedule",j.schedule);
    put("encryption",j.encryption); put("password_env",j.password_env); put("profile",j.profile);
    put("exclude_dependencies",j.exclude_dependencies?"true":"false");
    put("autostart",j.autostart?"true":"false"); put("watch",j.watch?"true":"false");
    o<<"watch_seconds="<<j.watch_seconds<<"\nlog_max_mb="<<j.log_max_mb<<"\n";
    put("notifications",j.notifications?"true":"false");put("desktop_notifications",j.desktop_notifications?"true":"false");
    o<<"stale_hours="<<j.stale_hours<<"\nnotify_repeat_hours="<<j.notify_repeat_hours
     <<"\ntest_restore_days="<<j.test_restore_days<<"\ntest_restore_max_mb="<<j.test_restore_max_mb
     <<"\noffline_reminder_days="<<j.offline_reminder_days<<"\nstorage_quota_mb="<<j.storage_quota_mb<<"\n";
    for(auto &s:j.sources)put("source",s);
    for(auto &s:j.exts)put("ext",s);
    for(auto &s:j.includes)put("include",s);
    for(auto &s:j.excludes)put("exclude",s);
    atomic_write(job_file(j.name),o.str());
}
static Job load_job(const string &name) {
    Job j; j.name=name; std::ifstream f(job_file(name)); require(bool(f), "Job bestaat niet: " + name + " (use init)");
    // Preserve legacy retention/selection when loading old jobs; new init uses the new defaults.
    j.keep_days=14; j.keep_chains=3; j.full_every=24; j.exclude_dependencies=false;
    string line;
    while(std::getline(f,line)) {
        if(line.empty() || line[0]=='#')continue;
        size_t p=line.find('='); require(p!=string::npos, "Invalid job configuration line");
        string k=line.substr(0,p), v=read_quoted(line.substr(p+1));
        if(k=="dest")j.dest=v; else if(k=="usb")j.usb=v; else if(k=="cloud")j.cloud=v;
        else if(k=="mode")j.mode=v; else if(k=="compression")j.compression=v;
        else if(k=="compression_level")j.compression_level=positive_int(v,0,99);
        else if(k=="full_every")j.full_every=positive_int(v,1,100000);
        else if(k=="keep_chains")j.keep_chains=positive_int(v,0,100000);
        else if(k=="keep_days")j.keep_days=positive_int(v,0,100000);
        else if(k=="schedule")j.schedule=v;
        else if(k=="encryption") { require(v=="none"||v=="aes-256-gcm","Invalid encryption"); j.encryption=v; }
        else if(k=="password_env") { require(valid_env_name(v),"Invalid environment variable name"); j.password_env=v; }
        else if(k=="profile") { require(v=="all"||v=="settings"||v=="installers","Invalid profile"); j.profile=v; }
        else if(k=="exclude_dependencies"||k=="autostart"||k=="watch") {
            require(v=="true"||v=="false","Use true or false");
            if(k=="exclude_dependencies")j.exclude_dependencies=v=="true";
            else if(k=="autostart")j.autostart=v=="true"; else j.watch=v=="true";
        }
        else if(k=="watch_seconds")j.watch_seconds=positive_int(v,5,86400);
        else if(k=="log_max_mb")j.log_max_mb=positive_int(v,1,1024);
        else if(k=="notifications") {require(v=="true"||v=="false","Use true or false");j.notifications=v=="true";}
        else if(k=="desktop_notifications") {require(v=="true"||v=="false","Use true or false");j.desktop_notifications=v=="true";}
        else if(k=="stale_hours")j.stale_hours=positive_int(v,0,87600);
        else if(k=="notify_repeat_hours")j.notify_repeat_hours=positive_int(v,1,87600);
        else if(k=="test_restore_days")j.test_restore_days=positive_int(v,0,3650);
        else if(k=="test_restore_max_mb")j.test_restore_max_mb=positive_int(v,1,1048576);
        else if(k=="offline_reminder_days")j.offline_reminder_days=positive_int(v,0,3650);
        else if(k=="storage_quota_mb")j.storage_quota_mb=positive_int(v,0,2147483647);
        else if(k=="source")j.sources.push_back(v);
        else if(k=="ext")j.exts.push_back(v);
        else if(k=="include")j.includes.push_back(v);
        else if(k=="exclude")j.excludes.push_back(v);
        else fail("Unknown setting: " + k);
    }
    require(!j.dest.empty(), "No destination configured");
    require(j.mode=="auto"||j.mode=="full"||j.mode=="incremental"||j.mode=="differential"||j.mode=="mirror", "Unknown type");
    check_level(j.compression,j.compression_level);
    if(!j.schedule.empty()) timer_spec(j.schedule);
    return j;
}
static string norm_ext(string e) { e=lower(e); if(e=="*")return e; if(!e.empty() && e[0]!='.')e="."+e; require(e.size()>1, "Invalid extension"); return e; }
static fs::path job_dir(const Job &j) { return abs_path(j.dest)/j.name; }
static void log_event(const Job &j,const string &event,const string &detail="") {
    fs::create_directories(job_dir(j));
    fs::path p=job_dir(j)/"events.log";
    if(fs::exists(p)&&fs::file_size(p)>static_cast<uintmax_t>(j.log_max_mb)*1024*1024) {
        std::error_code ec; fs::remove(job_dir(j)/"events.log.1",ec);
        cb::replace_file(p,job_dir(j)/"events.log.1");
    }
    std::ofstream f(p,std::ios::app|std::ios::binary);
    require(bool(f),"Cannot write event log");
    f<<fmt_local(time(nullptr))<<"\t"<<event<<"\t"<<hex(detail)<<"\n";
    f.flush(); require(bool(f),"Cannot flush event log");
}
static string fingerprint(const Job &j) {
    std::ostringstream out; for(auto &s:j.sources)out<<"S"<<abs_path(s).string()<<"\n";
    for(auto &s:j.exts)out<<"E"<<s<<"\n";
    for(auto &s:j.includes)out<<"I"<<s<<"\n";
    for(auto &s:j.excludes)out<<"X"<<s<<"\n";
    // Old manifests must retain the same selection fingerprint for in-place restore.
    if(j.profile!="all"||j.exclude_dependencies||j.encryption!="none")
        out<<"profile="<<j.profile<<"\ndeps="<<j.exclude_dependencies<<"\nencryption="<<j.encryption<<"\n";
    return sha256_text(out.str());
}

static string unit_base(const Job &j) { return "cbackup-"+j.name; }
static bool timer_active(const Job &j) { return cb::schedule_active(j.name,home()); }
static void install_timer(const Job &j) {
    cb::schedule_install(j.name,j.schedule,j.autostart,j.watch,home(),cb::executable());
}
static void remove_timer(const Job &j) { cb::schedule_remove(j.name,home()); }
static void linger_hint() {
#ifdef __linux__
    std::cout<<"For backups outside a login session: sudo loginctl enable-linger $USER\n";
#endif
}
// Activeer de planning uit de config als er bronnen zijn en de timer nog niet loopt. Fouten = waarschuwing.
static void ensure_schedule(const Job &j) {
    if(j.schedule.empty() || j.sources.empty() || timer_active(j)) return;
    try {
        install_timer(j);
        std::cout<<"Schedule active: "<<timer_spec(j.schedule).desc<<" (timer "<<unit_base(j)<<".timer)\n";
        linger_hint();
    } catch(const std::exception &e) {
        std::cout<<"WARNING: automatic scheduling could not be activated ("<<e.what()<<").\n"
                 <<"        Try again later with: cbackup schedule "<<j.name<<" on\n";
    }
}
static string schedule_status(const Job &j) {
    bool active=timer_active(j);
    if(j.schedule.empty()) return active?"task active (legacy schedule)":"off";
    string d=timer_spec(j.schedule).desc;
    if(active) return d+" - task active";
    return d+(j.sources.empty()?" - activated once you add a source":" - timer NOT active (cbackup schedule "+j.name+" on)");
}
static void show_job(const Job &j) {
    std::cout << "Job: " << j.name << "\nSources:\n";
    if(j.sources.empty()) std::cout<<"  (none yet - cbackup config "<<j.name<<" add-source PAD)\n";
    for(auto &s:j.sources)std::cout<<"  "<<s<<"\n";
    std::cout<<"Destination: "<<j.dest<<"\nUSB: "<< (j.usb.empty()?"off":j.usb)
    <<"\nCloud: "<<(j.cloud.empty()?"off":j.cloud)<<"\nType: "<<j.mode
    <<(j.mode=="auto"?" (incremental; every "+std::to_string(j.full_every)+" backups a full backup)":"")
    <<"\nCompression: "<<comp_label(j.compression,j.compression_level)
    <<"\nSchedule: "<<schedule_status(j)
    <<"\nFull every: "<<j.full_every<<" backups"
    <<"\nRetention: at least "<<j.keep_days<<" recoverable days (keep_days, 0 = off) and at least "<<j.keep_chains<<" full chains (keep_chains, 0 = keep everything)"
    <<"\nExtensions: "<<(j.exts.empty()?"all":join(j.exts,", "))
    <<"\nIncludes: "<<(j.includes.empty()?"all":join(j.includes,", "))
    <<"\nExcludes: "<<(j.excludes.empty()?"none":join(j.excludes,", "))<<"\n";
    std::cout<<"Encryption: "<<j.encryption<<" (password_env: "<<j.password_env<<")\n"
             <<"Profile: "<<j.profile<<"\nExclude dependencies: "<<j.exclude_dependencies
             <<"\nAutostart: "<<j.autostart<<"\nWatch: "<<j.watch<<" ("<<j.watch_seconds<<" seconds)\n";
    std::cout<<"Desktop notifications: "<<j.desktop_notifications<<"\nNotifications: "<<j.notifications<<"; stale_hours: "<<j.stale_hours<<"; notify_repeat_hours: "<<j.notify_repeat_hours
             <<"\nTest restore every: "<<j.test_restore_days<<" days; sample limit: "<<j.test_restore_max_mb<<" MiB"
             <<"\nStorage quota: "<<j.storage_quota_mb<<" MiB (0 = unlimited)"
             <<"\nOffline reminder every: "<<j.offline_reminder_days<<" days (0 = off)\n";
}

// ---------------------------------------------------------------- manifests
struct Item { uintmax_t size=0; string hash; int64_t mtime_ns=0, ctime_ns=0; uint64_t ino=0; bool meta=false; };
struct Manifest {
    string id,type,base,parent,fingerprint,archive,archive_hash,sources;
    long changed=-1;
    map<string,Item> files;
};
struct SourceRef { char type; fs::path path; };
static vector<SourceRef> decode_sources(const string &hexs) {
    vector<SourceRef> out; string s=unhex(hexs); std::istringstream in(s); string line;
    while(std::getline(in,line)) {
        require(line.size()>=2&&(line[0]=='D'||line[0]=='F'),"Invalid source list in manifest");
        string raw=line.substr(1);
        bool drive=raw.size()>=3&&std::isalpha(static_cast<unsigned char>(raw[0]))&&raw[1]==':'&&(raw[2]=='/'||raw[2]=='\\');
        bool unc=raw.size()>2&&raw[0]=='\\'&&raw[1]=='\\';
        require(raw[0]=='/'||drive||unc,"Source path in manifest must be absolute");
        out.push_back({line[0],fs::u8path(raw)});
    }
    require(!out.empty(),"Empty source list in manifest");
    return out;
}
static bool safe_relative(const string &s) {
    if(s.empty() || s.find('\0')!=string::npos || s[0]=='/' || s.find('\\')!=string::npos)return false;
    fs::path p(s); if(p.is_absolute())return false;
    for(auto &c:p) {
        if(c==".." || c=="." || c.empty())return false;
#ifdef _WIN32
        string part=lower(c.string());
        if(part.find(':')!=string::npos||part.back()=='.'||part.back()==' ')return false;
        string stem=part.substr(0,part.find('.'));
        if(stem=="con"||stem=="prn"||stem=="aux"||stem=="nul"||
           (stem.size()==4&&(stem.rfind("com",0)==0||stem.rfind("lpt",0)==0)&&stem[3]>='1'&&stem[3]<='9'))return false;
#endif
    }
    return p.generic_string()==s && s.find("//")==string::npos;
}
static string manifest_text(const Manifest &m) {
    std::ostringstream o;
    o<<"CBACKUP\t2\n"<<"id\t"<<m.id<<"\n"<<"type\t"<<m.type
    <<"\nbase\t"<<m.base<<"\nparent\t"<<m.parent<<"\nfingerprint\t"<<m.fingerprint
    <<"\narchive\t"<<m.archive<<"\narchive_hash\t"<<m.archive_hash<<"\nchanged\t"<<m.changed<<"\nsources\t"<<m.sources<<"\n";
    for(auto &kv:m.files)o<<"file\t"<<hex(kv.first)<<"\t"<<kv.second.size<<"\t"<<kv.second.hash
        <<"\t"<<kv.second.mtime_ns<<"\t"<<kv.second.ctime_ns<<"\t"<<kv.second.ino<<"\n";
    return o.str();
}
static vector<string> split_tabs(const string &s) {
    vector<string> v; size_t p=0; for(;;){size_t q=s.find('\t',p);v.push_back(s.substr(p,q==string::npos?q:q-p));if(q==string::npos)break;p=q+1;}return v;
}
template<class T> static T parse_num(const string &s, const string &what) {
    require(!s.empty() && (std::isdigit((unsigned char)s[0]) || s[0]=='-'), what);
    try { size_t n=0; long long v=std::stoll(s,&n); require(n==s.size(),what); return (T)v; } catch(const std::exception&) { fail(what); }
    return 0;
}
static Manifest parse_manifest(const fs::path &path) {
    std::ifstream f(path,std::ios::binary); require(bool(f), "Cannot read manifest: " + path.string());
    string line;
    auto read_line=[&]() { bool ok=bool(std::getline(f,line)); if(ok&&!line.empty()&&line.back()=='\r')line.pop_back(); return ok; };
    require(read_line()&&(line=="CBACKUP\t1"||line=="CBACKUP\t2"), "Invalid manifest: " + path.string());
    const bool v2=line=="CBACKUP\t2";
    Manifest m; std::set<string> seen;
    while(read_line()) {
        auto v=split_tabs(line); require(!v.empty(),"Lege regel");
        if(v[0]=="file") {
            require(v.size()==4 || (v2 && v.size()==7), "Invalid file entry");
            string key=unhex(v[1]); require(safe_relative(key) && key.rfind("source_",0)==0 && key.find('/')!=string::npos, "Unsafe file path in manifest");
            Item it; it.size=0; try {size_t n=0;it.size=std::stoull(v[2],&n);require(n==v[2].size(),"Invalid size");}catch(...){fail("Invalid file size");}
            require(v[3].size()==64,"Invalid sha256"); it.hash=v[3];
            if(v.size()==7) {
                it.mtime_ns=parse_num<int64_t>(v[4],"Invalid mtime"); it.ctime_ns=parse_num<int64_t>(v[5],"Invalid ctime");
                try {size_t n=0;it.ino=std::stoull(v[6],&n);require(n==v[6].size(),"x");}catch(...){fail("Invalid inode");}
                it.meta=true;
            }
            require(m.files.emplace(key,it).second,"Dubbel pad in manifest");
        } else {
            require(v.size()==2,"Invalid manifest header");
            require(seen.insert(v[0]).second,"Dubbele manifest key");
            if(v[0]=="id")m.id=v[1]; else if(v[0]=="type")m.type=v[1]; else if(v[0]=="base")m.base=v[1];
            else if(v[0]=="parent")m.parent=v[1]; else if(v[0]=="fingerprint")m.fingerprint=v[1];
            else if(v[0]=="archive")m.archive=v[1]; else if(v[0]=="archive_hash")m.archive_hash=v[1];
            else if(v2 && v[0]=="changed")m.changed=parse_num<long>(v[1],"Invalid count");
            else if(v2 && v[0]=="sources"){m.sources=v[1]; if(!m.sources.empty()) decode_sources(m.sources);}
            else fail("Unknown manifest key: "+v[0]);
        }
    }
    require(f.eof(), "Manifest read error");
    require(!m.id.empty()&&valid_name(m.id)&& (m.type=="full"||m.type=="incremental"||m.type=="differential"), "Onjuist manifest type/id");
    require(!m.base.empty() && valid_name(m.base), "Invalid base");
    require(m.parent.empty()||valid_name(m.parent), "Invalid parent");
    require(m.archive.rfind("snap_",0)==0&&m.archive.find('/')==string::npos&&m.archive.find('\\')==string::npos, "Unsafe archive name");
    require(m.archive_hash.size()==64&&m.fingerprint.size()==64, "Manifest mist SHA256");
    return m;
}
static vector<Manifest> get_manifests(const Job &j) {
    vector<Manifest> out; fs::path dir=job_dir(j);
    if(!fs::exists(dir))return out;
    for(const auto &e:fs::directory_iterator(dir)) {
        if(e.is_regular_file() && e.path().filename().string().rfind("snap_",0)==0 && e.path().extension()==".manifest") {
            Manifest m=parse_manifest(e.path());
            require(e.path().filename()=="snap_"+m.id+".manifest","ID/manifest filename mismatch");
            out.push_back(std::move(m));
        }
    }
    std::sort(out.begin(),out.end(),[](const Manifest &a,const Manifest&b){return a.id<b.id;});
    for(size_t i=1;i<out.size();i++)require(out[i-1].id!=out[i].id,"Dubbel snapshot ID");
    return out;
}

// ---------------------------------------------------------------- scannen
static bool match_any(const vector<string>&patterns, const string &name, const string &relative) {
    for(auto p:patterns) {
        while(p.size()>1&&p.back()=='/')p.pop_back();
        if(cb::glob(p,name)||cb::glob(p,relative)
           ||relative.rfind(p+"/",0)==0||name.rfind(p+"/",0)==0)return true;
    }
    return false;
}
static bool dependency_path(const fs::path &p) {
    static const std::set<string> skip={"node_modules",".venv","venv","__pycache__",".cache",".next",".nuxt",".tox"};
    for(const auto &part:p)if(skip.count(lower(part.string())))return true;
    return false;
}
static bool profile_accepts(const Job &j,const fs::path &p) {
    if(j.profile=="all")return true;
    string name=lower(p.filename().string()), ext=lower(p.extension().string());
    if(j.profile=="installers") {
        static const std::set<string> types={".exe",".msi",".msix",".deb",".rpm",".pkg",".dmg",".appimage",".sh",".ps1"};
        return types.count(ext)>0;
    }
    static const std::set<string> types={".json",".yaml",".yml",".toml",".ini",".conf",".config",".xml",".env",".lock"};
    return types.count(ext)||name==".env"||name.rfind(".env.",0)==0||name==".npmrc"||name==".nvmrc"
           ||name=="requirements.txt"||name=="pipfile"||name=="gemfile"||name=="dockerfile";
}
struct File { fs::path source; uintmax_t size=0; string hash; int64_t mtime_ns=0, ctime_ns=0; uint64_t ino=0; };
using FileMap=map<string,File>;
struct ScanStats { size_t hashed=0, reused=0; };
static FileMap scan(const Job &j, const Manifest *cache=nullptr, ScanStats *stats=nullptr) {
    require(!j.sources.empty(), "Add at least one source (add-source)");
    fs::path rootdest=abs_path(j.dest);
    FileMap result; ScanStats local; ScanStats &st=stats?*stats:local;
    for(size_t i=0;i<j.sources.size();++i) {
        fs::path source=abs_path(j.sources[i]);
        require(fs::exists(source), "Source missing; no backup created: " + source.string());
        require(fs::is_directory(source)||fs::is_regular_file(source), "Source is not a file/folder: " + source.string());
        require(!under(rootdest,source), "Destination is inside the source folder: " + rootdest.string());
        require(!under(job_dir(j),source),"Backup job folder is inside the source folder: "+job_dir(j).string());
        if(!j.usb.empty())require(!under(abs_path(j.usb),source), "USB destination is inside source folder");
        string prefix="source_"+std::to_string(i)+"/";
        auto add=[&](const fs::path &p,const fs::path &rel){
            string relative=rel.generic_string(), logical=prefix+relative;
            if(j.exclude_dependencies&&dependency_path(rel))return;
            if(!profile_accepts(j,p))return;
            if(!j.exts.empty()) {
                string ext=lower(p.extension().string());
                if(std::find(j.exts.begin(),j.exts.end(),ext)==j.exts.end() && std::find(j.exts.begin(),j.exts.end(),"*")==j.exts.end())return;
            }
            if(!j.includes.empty()&&!match_any(j.includes,logical,relative))return;
            if(match_any(j.excludes,logical,relative))return;
            auto sb=cb::stat(p);
            File f; f.source=p; f.size=sb.size;
            f.mtime_ns=sb.mtime_ns; f.ctime_ns=sb.ctime_ns; f.ino=sb.ino;
            // Snelle controle: zelfde grootte, mtime, ctime en inode als in de vorige snapshot = zelfde inhoud.
            // ctime kan een gebruiker niet terugzetten (touch -r verandert hem ook), dus inhoudswijzigingen worden gezien.
            const Item *c=nullptr;
            if(cache) { auto it=cache->files.find(logical); if(it!=cache->files.end() && it->second.meta && it->second.size==f.size
                && it->second.mtime_ns==f.mtime_ns && it->second.ctime_ns==f.ctime_ns && it->second.ino==f.ino) c=&it->second; }
            if(c) { f.hash=c->hash; ++st.reused; } else { f.hash=sha256_file(p); ++st.hashed; }
            result.emplace(logical,std::move(f));
        };
        if(fs::is_regular_file(source)) { add(source,source.filename()); continue; }
        std::error_code ec;
        fs::recursive_directory_iterator it(source,fs::directory_options::none,ec), end;
        if(ec)fail("Scan failed: "+ec.message());
        for(;it!=end;it.increment(ec)) {
            if(ec)fail("Scan failed: "+ec.message());
            const auto &e=*it;
            if(e.is_symlink(ec)) { if(ec)fail(ec.message()); it.disable_recursion_pending(); continue; }
            if(e.is_directory(ec)) {
                if(ec)fail(ec.message());
                auto rel=e.path().lexically_relative(source);
                if((j.exclude_dependencies&&dependency_path(rel))||match_any(j.excludes,prefix+rel.generic_string(),rel.generic_string())) it.disable_recursion_pending();
                continue;
            }
            if(ec)fail(ec.message());
            if(e.is_regular_file(ec)) { if(ec)fail(ec.message()); add(e.path(),e.path().lexically_relative(source)); }
            if(ec)fail(ec.message());
        }
        if(ec)fail("Scan failed: "+ec.message());
    }
    return result;
}
static string new_id() {
    auto now=std::chrono::system_clock::now();
    std::time_t t=std::chrono::system_clock::to_time_t(now); std::tm tm{}; gmtime_r(&t,&tm);
    auto us=std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count()%1000000;
    std::ostringstream s; s<<std::put_time(&tm,"%Y%m%d_%H%M%S")<<"_"<<std::setw(6)<<std::setfill('0')<<us<<"_"<<getpid();
    return s.str();
}
struct ArchiveWriter {
    struct archive *a=nullptr;
    ArchiveWriter(const fs::path &p,const string &compression,int level) {
        a=archive_write_new(); require(a,"archive_write_new failed");
        require(setup_writer(a,compression,level)==ARCHIVE_OK,"Compression "+comp_label(compression,level)+" unavailable: "+archive_err(a));
        require(archive_write_open_filename(a,p.string().c_str())==ARCHIVE_OK,"Cannot open archive: "+archive_err(a));
    }
    void add(const string &logical,const File &file) {
        string name="files/"+logical; require(safe_relative(name), "Unsafe archive path");
        auto st=cb::stat(file.source);
        require(st.size==file.size,"Source changed during backup: "+file.source.string());
        std::ifstream in(file.source,std::ios::binary); require(bool(in),"Source unreadable: "+file.source.string());
        struct archive_entry *entry=archive_entry_new(); require(entry,"archive_entry_new failed");
        archive_entry_set_pathname(entry,name.c_str()); archive_entry_set_size(entry,(la_int64_t)file.size);
        archive_entry_set_filetype(entry,AE_IFREG); archive_entry_set_perm(entry,st.mode&0777);
        archive_entry_set_mtime(entry,st.mtime,0);
        int rc=archive_write_header(a,entry); archive_entry_free(entry);
        require(rc==ARCHIVE_OK,"Archive header failed: "+archive_err(a));
        std::array<char,131072> buf{}; uintmax_t total=0;
        while(in.read(buf.data(),buf.size()) || in.gcount()) {
            size_t n=(size_t)in.gcount(); total+=n;
            la_ssize_t written=archive_write_data(a,buf.data(),n);
            require(written==(la_ssize_t)n,"Archive write failed: "+archive_err(a));
        }
        require(!in.bad()&&total==file.size,"File changed/read error while archiving: "+file.source.string());
        require(sha256_file(file.source)==file.hash,"Source changed while archiving: "+file.source.string());
    }
    void finish() { require(archive_write_close(a)==ARCHIVE_OK,"Cannot close archive: "+archive_err(a)); archive_write_free(a); a=nullptr; }
    ~ArchiveWriter() { if(a){archive_write_close(a);archive_write_free(a);} }
};
struct JobLock {
    cb::ProcessLock lock;
    JobLock(const Job &j, bool wait): lock((fs::create_directories(job_dir(j)),job_dir(j)/".lock"),wait) {}
};

// ---------------------------------------------------------------- USB / cloud
// Controleer daadwerkelijk mountpoints, niet alleen het bestaan van een map:
// anders zou een afgekoppelde USB-schijf stilletjes naar de systeemschijf schrijven.
static void verify_external_mounted(const fs::path &target) {
    require(cb::mounted_external(target),"External storage is not mounted: "+target.string());
}
static void copy_checked(const fs::path &src, const fs::path &dest, bool check_sha) {
    fs::create_directories(dest.parent_path());
    fs::path temp=dest;temp += ".partial."+std::to_string(getpid());
    try {
        fs::copy_file(src,temp,fs::copy_options::overwrite_existing);
        if(check_sha)require(sha256_file(src)==sha256_file(temp),"Kopie beschadigd: "+dest.string());
        cb::replace_file(temp,dest);
    } catch (...) {std::error_code ec;fs::remove(temp,ec);throw;}
}
static bool same_bytes(const fs::path &a, const fs::path &b) {
    std::error_code ec;
    if(!fs::is_regular_file(a,ec) || !fs::is_regular_file(b,ec) || fs::file_size(a)!=fs::file_size(b)) return false;
    std::ifstream fa(a,std::ios::binary), fb(b,std::ios::binary);
    return std::equal(std::istreambuf_iterator<char>(fa),std::istreambuf_iterator<char>(),std::istreambuf_iterator<char>(fb));
}
static string cloud_base(const Job &j) {
    require(!j.cloud.empty(),"Cloud is not configured");
    string base=j.cloud;while(!base.empty()&&base.back()=='/')base.pop_back();
    require(base.find(':')!=string::npos,"Cloud must use rclone remote:path");
    return base;
}
static void push_cloud_all(const Job &j) {
    string remote=cloud_base(j)+"/"+j.name;
    auto all=get_manifests(j);
    if(all.empty()) { std::cout<<"No snapshots to copy to cloud.\n"; return; }
    string arcs, mans;
    for(const auto &m:all) {
        require(fs::is_regular_file(job_dir(j)/m.archive),"Archive missing for cloud upload: "+(job_dir(j)/m.archive).string());
        arcs+=m.archive+"\n"; mans+="snap_"+m.id+".manifest\n";
    }
    // Eén rclone-aanroep per soort (i.p.v. per bestand): bij uurlijkse back-ups scheelt dat honderden aanroepen.
    // Archieven eerst, manifests daarna; een half geüploade snapshot telt zo nooit als compleet.
    fs::path la=job_dir(j)/(".rclone-archives."+std::to_string(getpid())), lm=job_dir(j)/(".rclone-manifests."+std::to_string(getpid()));
    try {
        atomic_write(la,arcs); atomic_write(lm,mans);
        require(exec_args({"rclone","copy",job_dir(j).string(),remote,"--files-from",la.string()})==0,"Cloud archive upload failed");
        require(exec_args({"rclone","copy",job_dir(j).string(),remote,"--files-from",lm.string()})==0,"Cloud manifest upload failed");
    } catch(...) { std::error_code ec; fs::remove(la,ec); fs::remove(lm,ec); throw; }
    std::error_code ec; fs::remove(la,ec); fs::remove(lm,ec);
    std::cout<<"Cloud chain updated: "<<remote<<"/\n";
}
static void replicate(const Job &j, bool deep=false) {
    if(!j.usb.empty()) {
        verify_external_mounted(j.usb);
        fs::path dir=abs_path(j.usb)/j.name; size_t copied=0;
        for(const auto &m:get_manifests(j)) {
            fs::path arc=job_dir(j)/m.archive, man=job_dir(j)/("snap_"+m.id+".manifest");
            fs::path darc=dir/arc.filename(), dman=dir/man.filename();
            // Het manifest wordt pas na een gecontroleerde archiefkopie geschreven. Staat het er al
            // (identiek) en klopt de archiefgrootte, dan is deze snapshot eerder al geverifieerd gekopieerd.
            if(!deep && same_bytes(man,dman) && fs::is_regular_file(darc) && fs::file_size(darc)==fs::file_size(arc)) continue;
            bool same=fs::is_regular_file(darc) && fs::file_size(darc)==fs::file_size(arc) && sha256_file(darc)==m.archive_hash;
            if(!same) copy_checked(arc,darc,true);
            if(!same_bytes(man,dman)) copy_checked(man,dman,true);
            ++copied;
        }
        std::cout<<"USB chains updated: "<<dir<<" ("<<copied<<" snapshot(s) copied/verified)\n";
    }
    if(!j.cloud.empty())push_cloud_all(j);
}
static void pull_cloud(const Job &j) {
    string base=cloud_base(j);
    fs::create_directories(job_dir(j));
    require(exec_args({"rclone","copy",base+"/"+j.name,job_dir(j).string()})==0,"Cloud download failed");
    std::cout<<"Cloud archives downloaded to "<<job_dir(j)<<"\n";
}

// ---------------------------------------------------------------- bewaren
// Een keten (full + alles tot de volgende full) mag pas weg als:
//  - er daarna nog minstens keep_chains volledige ketens zijn, EN
//  - de volgende full ouder is dan keep_days (zo blijft elk moment van de laatste keep_days dagen terug te zetten).
static void prune(const Job &j) {
    if(j.keep_chains==0)return;
    auto all=get_manifests(j);
    vector<string> fulls;
    for(auto &m:all)if(m.type=="full")fulls.push_back(m.id);
    if(fulls.size()<=(size_t)j.keep_chains)return;
    const int64_t cutoff=(int64_t)time(nullptr)-(int64_t)j.keep_days*86400;
    string boundary;
    for(size_t i=0; i+(size_t)j.keep_chains<fulls.size(); ++i) {
        int64_t next=id_time(fulls[i+1]);
        if(j.keep_days>0 && (next<0 || next>=cutoff)) break;
        boundary=fulls[i+1];
    }
    if(boundary.empty()) return;
    size_t removed=0;
    for(auto &m:all) {
        if(m.id>=boundary)continue;
        fs::remove(job_dir(j)/m.archive);
        fs::remove(job_dir(j)/("snap_"+m.id+".manifest"));
        ++removed;
    }
    std::cout<<"Retention: "<<removed<<" old local snapshot(s) deleted (older than "<<fmt_local(id_time(boundary),false)<<")\n";
}

// ---------------------------------------------------------------- back-up maken
static uintmax_t backup_storage_bytes(const Job &j) {
    uintmax_t bytes=0;
    for(const auto &m:get_manifests(j)) {
        bytes+=fs::file_size(job_dir(j)/m.archive);
        bytes+=fs::file_size(job_dir(j)/("snap_"+m.id+".manifest"));
    }
    fs::path mirror=job_dir(j)/"mirror";
    if(fs::exists(mirror))for(const auto &e:fs::recursive_directory_iterator(mirror))
        if(!e.is_symlink()&&e.is_regular_file())bytes+=e.file_size();
    return bytes;
}
static void quota_check(const Job &j,uintmax_t added=0,uintmax_t replaced=0) {
    if(!j.storage_quota_mb)return;
    uintmax_t used=backup_storage_bytes(j), quota=static_cast<uintmax_t>(j.storage_quota_mb)*1024*1024;
    require(used>=replaced,"Invalid storage accounting");
    require(added<=quota&&used-replaced<=quota-added,
            "Storage quota exceeded: stored="+human_size(used)+", new="+human_size(added)+
            ", budget="+human_size(quota)+". Required restore chains and retention were preserved; increase storage_quota_mb or adjust retention.");
}
static uintmax_t space_estimate(const FileMap &files,const vector<string> &keys) {
    uintmax_t bytes=65536;
    for(const auto &key:keys)bytes+=files.at(key).size+4096;
    return bytes+bytes/50; // Conservative tar/codec overhead; exact quota checked after compression.
}
static void space_preflight(const Job &j,uintmax_t estimate,bool encrypted,bool dry) {
    uintmax_t used=backup_storage_bytes(j);
    std::cout<<"Storage: "<<human_size(used)<<" used; estimated new archive: "<<human_size(estimate)
             <<"; quota: "<<(j.storage_quota_mb?human_size(static_cast<uintmax_t>(j.storage_quota_mb)*1024*1024):"unlimited")<<"\n";
    if(dry)return;
    auto available=fs::space(job_dir(j)).available;
    require(estimate<=available/(encrypted?2:1),"Insufficient free space for estimated backup and temporary encryption files");
    if(j.storage_quota_mb&&used+estimate>static_cast<uintmax_t>(j.storage_quota_mb)*1024*1024) {
        // Preserve the existing rule: replicate before deleting any expired chains.
        replicate(j); prune(j);
    }
}
static void mirror_run(const Job &j) {
    FileMap files=scan(j);
    vector<string> keys; uintmax_t new_bytes=0, old_bytes=0;
    for(const auto &kv:files) {keys.push_back(kv.first);new_bytes+=kv.second.size;}
    space_preflight(j,space_estimate(files,keys),false,false);
    fs::path old_mirror=job_dir(j)/"mirror";
    if(fs::exists(old_mirror))for(const auto &e:fs::recursive_directory_iterator(old_mirror))
        if(!e.is_symlink()&&e.is_regular_file())old_bytes+=e.file_size();
    quota_check(j,new_bytes,old_bytes);
    fs::path dest=job_dir(j)/"mirror", temp=job_dir(j)/("mirror.tmp."+std::to_string(getpid()));
    require(!fs::exists(temp),"Temporary mirror folder already exists");
    try {
        fs::create_directories(temp);
        for(auto &kv:files) {
            fs::path out=temp/"files"/kv.first;fs::create_directories(out.parent_path());
            fs::copy_file(kv.second.source,out);
            require(sha256_file(out)==kv.second.hash,"Mirror verification failed for "+kv.first);
        }
        fs::path old=job_dir(j)/("mirror.old."+std::to_string(getpid()));
        if(fs::exists(dest))fs::rename(dest,old);
        try {cb::replace_file(temp,dest);} catch(...){if(fs::exists(old))fs::rename(old,dest);throw;}
        if(fs::exists(old))fs::remove_all(old);
    } catch(...) {std::error_code ec;fs::remove_all(temp,ec);throw;}
    std::cout<<"Mirror updated: "<<dest<<" ("<<files.size()<<" files)\n";
    if(!j.usb.empty()) {
        verify_external_mounted(j.usb);
        fs::path usb_mirror=abs_path(j.usb)/j.name/"mirror";
        // Exacte lokale spiegel, inclusief verwijderde bestanden. Alleen binnen <usb>/<job>/mirror.
        fs::path tempusb=usb_mirror;tempusb += ".tmp."+std::to_string(getpid());
        fs::create_directories(tempusb.parent_path());
        fs::copy(dest,tempusb,fs::copy_options::recursive);
        fs::path oldusb=usb_mirror;oldusb += ".old."+std::to_string(getpid());
        if(fs::exists(usb_mirror))fs::rename(usb_mirror,oldusb);
        try {fs::rename(tempusb,usb_mirror);}catch(...){if(fs::exists(oldusb))fs::rename(oldusb,usb_mirror);throw;}
        if(fs::exists(oldusb))fs::remove_all(oldusb);
    }
    if(!j.cloud.empty()) {
        // Alleen de eigen mirror-submap van de job; sync verwijdert daar oude bestanden.
        require(exec_args({"rclone","sync",dest.string(),cloud_base(j)+"/"+j.name+"/mirror"})==0,"Cloud mirror sync failed");
    }
}
static string encode_sources(const Job &j) {
    string s;
    for(auto &src:j.sources) { fs::path p=abs_path(src); if(!s.empty()) s+="\n"; s+=(fs::is_regular_file(p)?"F":"D")+p.string(); }
    return hex(s);
}
static void make_backup(const Job &j, const string &override_mode, bool dryrun) {
    fs::path dir=job_dir(j); fs::create_directories(dir);
    string desired=override_mode.empty()?j.mode:override_mode;
    if(desired=="mirror") {
        require(j.encryption=="none","Encrypted mirror is unsupported; select auto/full/incremental/differential instead");
        if(dryrun) {
            auto files=scan(j);vector<string> keys;for(const auto &kv:files)keys.push_back(kv.first);
            std::cout<<"Mirror-dry-run: "<<files.size()<<" selected files.\n";
            space_preflight(j,space_estimate(files,keys),false,true);return;
        }
        mirror_run(j);log_event(j,"mirror");return;
    }
    require(desired=="auto"||desired=="full"||desired=="incremental"||desired=="differential","Invalid backup type: "+desired);
    vector<Manifest> all=get_manifests(j);
    string fp=fingerprint(j);
    const Manifest *prev=all.empty()?nullptr:&all.back(), *full=nullptr;
    for(auto &m:all)if(m.type=="full")full=&m;
    string type=desired;
    if(type=="auto") {
        int count=0;
        if(full)for(auto &m:all)if(m.id>=full->id)++count;
        type=(!prev||!full||prev->fingerprint!=fp||count>=j.full_every||time(nullptr)-id_time(full->id)>=86400)?"full":"incremental";
    }
    if(type!="full"&&(!prev||!full||prev->fingerprint!=fp||full->fingerprint!=fp)) {
        std::cout<<"No usable base/filters changed: making a full backup automatically.\n";type="full";
    }
    // Volledige back-ups hashen altijd alles; incrementeel/differentieel hergebruikt hashes van ongewijzigde bestanden.
    ScanStats stats;
    FileMap files=scan(j, type=="full"?nullptr:prev, &stats);
    const Manifest *compare=type=="full"?nullptr:type=="incremental"?prev:full;
    vector<string> changed;
    for(auto &kv:files) {
        auto it=compare?compare->files.find(kv.first):map<string,Item>::const_iterator{};
        if(!compare||it==compare->files.end()||it->second.hash!=kv.second.hash)changed.push_back(kv.first);
    }
    size_t deleted=0;
    if(prev)for(auto &kv:prev->files)if(!files.count(kv.first))++deleted;
    std::cout<<"Type: "<<type<<", files: "<<files.size()<<", in archive: "<<changed.size()<<", verdwenen: "<<deleted
             <<", gehasht: "<<stats.hashed<<", unchanged by metadata: "<<stats.reused
             <<", compression: "<<comp_label(j.compression,j.compression_level)<<"\n";
    if(files.empty())std::cout<<"WARNING: no files selected.\n";
    space_preflight(j,space_estimate(files,changed),j.encryption!="none",dryrun);
    if(dryrun)return;
    bool unchanged=prev&&prev->files.size()==files.size();
    if(unchanged)for(const auto &kv:files) {
        auto it=prev->files.find(kv.first);
        if(it==prev->files.end()||it->second.hash!=kv.second.hash) {unchanged=false;break;}
    }
    if(type!="full"&&unchanged){
        std::cout<<"No changes; no new snapshot needed. Missing copies will be updated.\n";
        replicate(j);prune(j);quota_check(j);log_event(j,"unchanged");return;
    }
    Manifest m;
    m.id=new_id();m.type=type;m.base=type=="full"?m.id:full->id;
    m.parent=prev?prev->id:"";m.fingerprint=fp;
    m.archive="snap_"+m.id+ext_for(j.compression);
    if(j.encryption!="none")m.archive+=".enc";
    m.changed=(long)changed.size(); m.sources=encode_sources(j);
    for(auto &kv:files) { Item it; it.size=kv.second.size; it.hash=kv.second.hash; it.mtime_ns=kv.second.mtime_ns; it.ctime_ns=kv.second.ctime_ns; it.ino=kv.second.ino; it.meta=true; m.files.emplace(kv.first,it); }
    fs::path archpath=dir/m.archive, manpath=dir/("snap_"+m.id+".manifest");
    require(!fs::exists(archpath) && !fs::exists(manpath), "Snapshot name already exists, try again");
    fs::path temp=archpath;temp += ".partial";
    try {
        {
            ArchiveWriter writer(temp,j.compression,j.compression_level);
            for(auto &key:changed)writer.add(key,files.at(key));
            writer.finish();
        }
        if(j.encryption!="none") {
            fs::path encrypted=temp; encrypted+=".encrypted";
            cb_crypto::transform(temp,encrypted,j.password_env,true);
            fs::remove(temp); cb::replace_file(encrypted,temp);
        }
        m.archive_hash=sha256_file(temp);
        quota_check(j,fs::file_size(temp)+manifest_text(m).size());
        fs::rename(temp,archpath);
        atomic_write(manpath,manifest_text(m));
    }catch(...) {std::error_code ec;fs::remove(temp,ec);fs::remove(archpath,ec);fs::remove(manpath,ec);throw;}
    std::cout<<"Snapshot saved locally: "<<fmt_local(id_time(m.id))<<" ("<<m.archive<<", "<<human_size(fs::file_size(archpath))<<")\n";
    log_event(j,"snapshot",m.id+" "+type+" changed="+std::to_string(changed.size())+" deleted="+std::to_string(deleted));
    replicate(j);
    prune(j);
}

// ---------------------------------------------------------------- WANNEER: welke back-up?
struct When {
    enum Kind { ID, LATEST, BEFORE } kind=BEFORE;
    string text, id, desc, note, daylabel;
    int64_t deadline=0;               // kies de laatste snapshot met tijd < deadline
    bool day=false; int64_t day_start=0, day_end=0;
};
static size_t edit_distance(const string &a, const string &b) {
    vector<size_t> d(b.size()+1); for(size_t j=0;j<=b.size();++j) d[j]=j;
    for(size_t i=0;i<a.size();++i) { size_t prev=d[0]; d[0]=i+1;
        for(size_t j=0;j<b.size();++j) { size_t tmp=d[j+1]; d[j+1]=std::min({d[j+1]+1,d[j]+1,prev+(a[i]!=b[j])}); prev=tmp; } }
    return d[b.size()];
}
static int weekday_index(const string &s, string &note) {
    static const vector<vector<string>> names={
        {"zondag","zo","zon","sunday","sun"},
        {"maandag","ma","maa","monday","mon"},
        {"dinsdag","di","din","tuesday","tue","tues"},
        {"woensdag","wo","woe","wednesday","wed"},
        {"donderdag","do","don","thursday","thu","thur","thurs"},
        {"vrijdag","vr","vrij","friday","fri"},
        {"zaterdag","za","zat","saturday","sat"}};
    for(int i=0;i<7;++i) for(auto &n:names[i]) if(s==n) return i;
    // Tikfout van één letter in een volledige dagnaam (bijv. "woensag") wordt herkend als het eenduidig is.
    if(s.size()>=5) {
        int found=-1; bool amb=false;
        for(int i=0;i<7;++i) for(int k: {0,3}) if(edit_distance(s,names[i][k])<=1) { if(found>=0&&found!=i) amb=true; found=i; }
        if(found>=0 && !amb) { note="'"+s+"' opgevat als "+names[found][0]; return found; }
    }
    return -1;
}
static bool parse_clock(const string &s, int &h, int &m) {   // 14:00, 9:30, 14u, 14u30, 14h
    size_t i=0; while(i<s.size()&&std::isdigit((unsigned char)s[i])) ++i;
    if(i==0||i>2||i==s.size()) return false;
    char sep=s[i]; if(sep!=':'&&sep!='u'&&sep!='h') return false;
    string rest=s.substr(i+1); int hh=std::stoi(s.substr(0,i)), mm=0;
    if(sep==':'&&rest.empty()) return false;
    if(!rest.empty()) { if(rest.size()!=2||!all_digits(rest)) return false; mm=std::stoi(rest); }
    if(hh>23||mm>59) fail("Invalid time: "+s);
    h=hh; m=mm; return true;
}
static bool parse_date(const string &s, YMD &out, const YMD &today) {   // 2026-11-04, 04-11-2026, 4/11, 04.11.26, 20261104
    vector<string> p; string cur;
    for(char c:s) { if(std::isdigit((unsigned char)c)) cur+=c; else if(c=='-'||c=='/'||c=='.') { p.push_back(cur); cur.clear(); } else return false; }
    p.push_back(cur);
    if(p.size()>1) for(auto &x:p) if(x.empty()||x.size()>4) return false;
    YMD r;
    if(p.size()==1) { if(p[0].size()!=8) return false; r={std::stoi(p[0].substr(0,4)),std::stoi(p[0].substr(4,2)),std::stoi(p[0].substr(6,2))}; }
    else if(p.size()==3 && p[0].size()==4) r={std::stoi(p[0]),std::stoi(p[1]),std::stoi(p[2])};
    else if(p.size()==3 && p[0].size()<=2 && p[1].size()<=2 && (p[2].size()==4||p[2].size()==2)) { int y=std::stoi(p[2]); if(p[2].size()==2) y+=2000; r={y,std::stoi(p[1]),std::stoi(p[0])}; }
    else if(p.size()==2 && p[0].size()<=2 && p[1].size()<=2) { r={today.y,std::stoi(p[1]),std::stoi(p[0])}; if(valid_ymd(r) && ymd_after(r,today)) r.y--; }
    else return false;
    require(valid_ymd(r),"Invalid date: "+s);
    out=r; return true;
}
static int64_t unit_seconds(const string &u) {
    if(u=="m"||u=="min"||u=="mins"||u=="minuut"||u=="minuten"||u=="minute"||u=="minutes") return 60;
    if(u=="u"||u=="uur"||u=="uren"||u=="h"||u=="hour"||u=="hours") return 3600;
    if(u=="d"||u=="dag"||u=="dagen"||u=="day"||u=="days") return 86400;
    if(u=="w"||u=="week"||u=="weken"||u=="weeks") return 604800;
    return 0;
}
static const char *WHEN_EXAMPLES="Wednesday, last Wednesday, yesterday, yesterday 14:00, 2026-11-04, 04-11, 3 hours ago, latest or an ID from 'cbackup list'";
static When parse_when(const vector<string> &raw, const vector<Manifest> &all) {
    require(!raw.empty(), string("Specify a backup, e.g.: ")+WHEN_EXAMPLES);
    When w; w.text=join(raw," ");
    if(raw.size()==1) for(auto &m:all) if(m.id==raw[0]) { w.kind=When::ID; w.id=m.id; w.desc="ID "+m.id; return w; }
    static const std::set<string> fillers={"om","at","van","de","het","op","on","backup","back-up","rond","around","the","of"};
    vector<string> t;
    for(auto &r:raw) { std::istringstream in(lower(r)); string s; while(in>>s) if(!fillers.count(s)) t.push_back(s); }
    require(!t.empty(), string("Specify a backup, e.g.: ")+WHEN_EXAMPLES);
    if(t.size()==1 && (t[0]=="laatste"||t[0]=="latest"||t[0]=="nieuwste"||t[0]=="newest"||t[0]=="nu"||t[0]=="now"||t[0]=="last")) { w.kind=When::LATEST; w.desc="the latest backup"; return w; }
    const int64_t now=(int64_t)time(nullptr);
    if(t.back()=="geleden"||t.back()=="ago") {   // "3 uur geleden", "2 dagen geleden", "90min geleden", "2h ago"
        require(t.size()==2||t.size()==3, "Use e.g.: 3 hours ago, 2 days ago");
        string num=t[0], unit=t.size()==3?t[1]:"";
        if(t.size()==2) { size_t k=num.find_first_not_of("0123456789"); require(k!=string::npos&&k>0,"Use e.g.: 3 hours ago"); unit=num.substr(k); num=num.substr(0,k); }
        require(all_digits(num),"Use e.g.: 3 hours ago");
        int64_t sec=unit_seconds(unit); require(sec>0,"Unknown unit '"+unit+"' (use minutes, hours, days or weeks)");
        int64_t at=now-(int64_t)positive_int(num,1,100000)*sec;
        w.deadline=at+1; w.desc=num+" "+unit+" geleden ("+fmt_local(at,false)+")"; return w;
    }
    const YMD today=today_local();
    YMD day=today; bool prev=false, haveday=false, weekday=false; int hh=-1, mm=0;
    for(auto &s:t) {
        if(s=="vorige"||s=="vorig"||s=="afgelopen"||s=="previous") { prev=true; continue; }
        int h,m; if(parse_clock(s,h,m)) { require(hh<0,"Two times specified in: "+w.text); hh=h; mm=m; continue; }
        require(!haveday, "Two days specified in: "+w.text);
        string note; int wd;
        if(s=="vandaag"||s=="today") day=today;
        else if(s=="gisteren"||s=="yesterday") day=add_days(today,-1);
        else if(s=="eergisteren") day=add_days(today,-2);
        else if((wd=weekday_index(s,note))>=0) {
            int delta=(weekday_of(today)-wd+7)%7; if(delta==0&&prev) delta=7;
            day=add_days(today,-delta); weekday=true; w.note=note;
        }
        else if(parse_date(s,day,today)) {}
        else fail("Unknown date/time expression '"+s+"'. Examples: "+WHEN_EXAMPLES);
        haveday=true;
    }
    require(!prev||weekday, "'last' requires a weekday, e.g. 'last Wednesday'");
    w.day=true; w.day_start=local_at(day,0,0); w.day_end=local_at(add_days(day,1),0,0); w.daylabel=fmt_day(day);
    require(w.day_start<=now, "Die datum ligt in de toekomst: "+fmt_day(day));
    if(hh>=0) { w.deadline=local_at(day,hh,mm)+60; std::ostringstream o; o<<fmt_day(day)<<" at "<<std::setfill('0')<<std::setw(2)<<hh<<":"<<std::setw(2)<<mm; w.desc=o.str(); }
    else { w.deadline=w.day_end; w.desc=fmt_day(day)+(day.y==today.y&&day.m==today.m&&day.d==today.d?" (today)":""); }
    return w;
}
static const Manifest *pick(const vector<Manifest> &all, const When &w, string &msg) {
    msg.clear();
    if(all.empty()) { msg="No backups yet."; return nullptr; }
    if(w.kind==When::LATEST) return &all.back();
    if(w.kind==When::ID) { for(auto &m:all) if(m.id==w.id) return &m; msg="Snapshot not found: "+w.id; return nullptr; }
    const Manifest *best=nullptr;
    for(auto &m:all) { int64_t t=id_time(m.id); if(t>=0 && t<w.deadline) best=&m; }
    if(!best) { msg="No backup found at "+w.desc+" or earlier. Oldest available backup: "+fmt_local(id_time(all.front().id)); return nullptr; }
    int64_t t=id_time(best->id);
    if(w.day && t<w.day_start)
        msg="Let op: op "+w.daylabel+" zelf is "+string(w.deadline<w.day_end?"up to that time ":"")+"no new snapshot was created (nothing changed, or the computer was off).\n"
            "Selected the last backup before then: the most recent saved state preceding that moment.";
    return best;
}
static const Manifest &choose(const vector<Manifest> &all, const When &w) {
    string msg; const Manifest *m=pick(all,w,msg);
    if(!m) fail(msg);
    if(!w.note.empty()) std::cout<<"("<<w.note<<")\n";
    std::cout<<"Requested: "<<w.desc<<"\nSelected backup: "<<fmt_local(id_time(m->id))<<"  ("<<m->type<<", ID "<<m->id<<")\n";
    if(!msg.empty()) std::cout<<msg<<"\n";
    return *m;
}

// ---------------------------------------------------------------- lijst / verify / restore
static void list_snapshots(const Job &j, const vector<string> &when) {
    auto all=get_manifests(j);
    if(all.empty()) { std::cout<<"No snapshots.\n"; return; }
    const Manifest *chosen=nullptr; When w; string msg;
    if(!when.empty()) { w=parse_when(when,all); chosen=pick(all,w,msg); if(!w.note.empty()) std::cout<<"("<<w.note<<")\n"; }
    std::cout<<std::left<<std::setw(25)<<"Date/time (local)"<<std::setw(14)<<"Type"<<std::right<<std::setw(10)<<"Files"<<std::setw(12)<<"In archive"
             <<std::setw(11)<<"Grootte"<<"  "<<std::left<<"ID\n";
    size_t shown=0;
    for(auto &m:all) {
        int64_t t=id_time(m.id);
        if(w.day && !(t>=w.day_start && t<w.day_end) && &m!=chosen) continue;
        std::error_code ec; uintmax_t sz=fs::file_size(job_dir(j)/m.archive,ec);
        std::cout<<std::left<<std::setw(25)<<fmt_local(t)<<std::setw(14)<<m.type<<std::right<<std::setw(10)<<m.files.size()
                 <<std::setw(12)<<(m.changed>=0?std::to_string(m.changed):"-")<<std::setw(11)<<(ec?"MISSING":human_size(sz))<<"  "<<m.id
                 <<(&m==chosen?"   <= selected for restore":"")<<"\n";
        ++shown;
    }
    if(!when.empty()) {
        if(!chosen) std::cout<<msg<<"\n";
        else {
            if(!msg.empty()) std::cout<<msg<<"\n";
            std::cout<<"\nTerugzetten: cbackup restore "<<j.name<<" "<<(w.text.find(' ')!=string::npos?"'"+w.text+"'":w.text)<<" --to ~/Teruggezet\n";
        }
    } else {
        std::cout<<"\n"<<shown<<" snapshot(s). Restore by date/time, e.g.: cbackup restore "<<j.name<<" Wednesday --to ~/Restored\n";
    }
}
static vector<const Manifest*> chain_for(const vector<Manifest> &all,const string &id) {
    const Manifest *target=nullptr, *base=nullptr;
    for(auto &m:all)if(m.id==id)target=&m;
    require(target,"Snapshot not found: "+id);
    for(auto &m:all)if(m.id==target->base)base=&m;
    require(base && base->type=="full","Base backup missing (removed by retention policy?)");
    // Controleer parent-verwijzingen over de volledige geschiedenis tussen full en doel.
    // Differentieel reset de herstelde staat naar full + diff; oudere incrementele
    // wijzigingen moeten daarna juist NIET opnieuw worden toegepast.
    string parent=base->id;
    vector<const Manifest*> chain{base};
    if(target->id==base->id)return chain;
    bool found=false;
    for(auto &m:all) {
        if(m.id<=base->id || m.id>target->id)continue;
        require(m.parent==parent,"Snapshot chain incomplete at "+m.id);
        require(m.base==base->id,"Base changes within restore chain");
        if(m.type=="differential")chain={base,&m};
        else require(m.type=="incremental","Unexpected full backup in chain"), chain.push_back(&m);
        parent=m.id;
        if(m.id==target->id)found=true;
    }
    require(found,"Target snapshot is not in chain");
    return chain;
}
static void verify_chain(const Job &j,const vector<const Manifest*>&chain) {
    for(auto *m:chain) {
        fs::path f=job_dir(j)/m->archive;
        require(fs::is_regular_file(f),"Archive missing: "+f.string());
        require(sha256_file(f)==m->archive_hash,"Archive SHA256 mismatch: "+f.string());
    }
}
static string chain_desc(const vector<const Manifest*> &chain) {
    map<string,int> c; for(auto *m:chain) c[m->type]++;
    vector<string> parts; for(auto t:{"full","differential","incremental"}) if(c[t]) parts.push_back(std::to_string(c[t])+" "+t);
    return join(parts," + ");
}
static void set_mtime(const fs::path &p, int64_t sec) { cb::set_mtime(p,sec); }
// Pakt uit naar stage/files/<key>, alleen voor sleutels in 'want'. Alle entries worden wel gevalideerd.
static void extract_archive(const fs::path &archivepath,const Manifest &m,const fs::path &stage,const std::set<string> &want) {
    struct archive *a=archive_read_new(); require(a,"archive_read_new failed");
    auto cleanup=[&]{archive_read_close(a);archive_read_free(a);};
    try {
        archive_read_support_filter_all(a);archive_read_support_format_tar(a);archive_read_support_format_zip(a);
        require(archive_read_open_filename(a,archivepath.string().c_str(),65536)==ARCHIVE_OK,"Cannot read archive: "+archivepath.string());
        struct archive_entry *e=nullptr;
        std::set<string> names;
        for(;;) {
            int rc=archive_read_next_header(a,&e);
            if(rc==ARCHIVE_EOF)break;
            require(rc==ARCHIVE_OK,"Archive error: "+archive_err(a));
            const char *raw=archive_entry_pathname(e);require(raw,"Archive entry has no path");
            string name(raw); require(name.rfind("files/",0)==0&&safe_relative(name),"Unsafe archive path: "+name);
            string key=name.substr(6);
            require(names.insert(key).second,"Duplicate archive entry");
            auto expected=m.files.find(key);require(expected!=m.files.end(),"Unexpected archive entry: "+name);
            require(archive_entry_filetype(e)==AE_IFREG && !archive_entry_hardlink(e)&&!archive_entry_symlink(e),"Non-regular file in archive");
            require(!archive_entry_size_is_set(e) || (archive_entry_size(e)>=0 && (uintmax_t)archive_entry_size(e)==expected->second.size),"File size mismatch");
            if(!want.count(key)) { require(archive_read_data_skip(a)==ARCHIVE_OK,"Skip error: "+archive_err(a)); continue; }
            fs::path output=stage/name;fs::create_directories(output.parent_path());
            std::ofstream out(output,std::ios::binary|std::ios::trunc);require(bool(out),"Cannot write restore file: "+output.string());
            std::array<char,131072> buf{}; uintmax_t bytes=0;
            for(;;) {
                la_ssize_t n=archive_read_data(a,buf.data(),buf.size());
                require(n>=0,"Extraction error: "+archive_err(a));if(n==0)break;
                bytes+=(uintmax_t)n;require(bytes<=expected->second.size,"Archive entry exceeds limit");
                out.write(buf.data(),n);require(bool(out),"Write error while restoring");
            }
            out.close();require(bytes==expected->second.size,"Archive entry too short");
            require(sha256_file(output)==expected->second.hash,"Restored file SHA256 mismatch: "+key);
            std::error_code ec;
            fs::permissions(output,(fs::perms)(archive_entry_perm(e)&0777),fs::perm_options::replace,ec);
            set_mtime(output,(int64_t)archive_entry_mtime(e));
        }
        cleanup();
    }catch(...){cleanup();throw;}
}
// Zet de keten uit naar stage en controleert elk gevraagd bestand tegen het doelmanifest.
static void extract_chain(const Job &j,const vector<const Manifest*> &chain,const Manifest &final,const std::set<string> &want,const fs::path &stage) {
    fs::create_directories(stage);
    for(auto *m:chain) {
        if(fs::path(m->archive).extension()==".enc") {
            fs::path plain=stage/".decrypt.partial";
            try {
                cb_crypto::transform(job_dir(j)/m->archive,plain,j.password_env,false);
                extract_archive(plain,*m,stage,want);
                fs::remove(plain);
            } catch(...) { std::error_code ec; fs::remove(plain,ec); throw; }
        } else extract_archive(job_dir(j)/m->archive,*m,stage,want);
    }
    fs::path files=stage/"files";
    for(auto &key:want) {
        const Item &it=final.files.at(key);
        fs::path p=files/key;
        require(fs::is_regular_file(p),"File missing after restore: "+key);
        require(fs::file_size(p)==it.size&&sha256_file(p)==it.hash,"Integrity error: "+key);
    }
}
static std::set<string> select_keys(const Manifest &final, const vector<string> &only) {
    std::set<string> keys;
    for(auto &kv:final.files) {
        if(only.empty()) { keys.insert(kv.first); continue; }
        const string &logical=kv.first; string rel=logical.substr(logical.find('/')+1), base=fs::path(logical).filename().string();
        for(auto p:only) {
            while(p.size()>1&&p.back()=='/') p.pop_back();
            string dir=p+"/*";
            if(cb::glob(p,logical)||cb::glob(p,rel)||cb::glob(p,base)
               ||cb::glob(dir,logical)||cb::glob(dir,rel)) { keys.insert(logical); break; }
        }
    }
    if(!only.empty()) require(!keys.empty(),"No files in this backup match --only "+join(only,", "));
    return keys;
}
static vector<SourceRef> snapshot_sources(const Job &j, const Manifest &m) {
    if(!m.sources.empty()) return decode_sources(m.sources);
    // Oudere manifests (v1) bewaren de bronpaden niet; die zijn alleen eenduidig als de jobconfig nog gelijk is.
    require(m.fingerprint==fingerprint(j),"This older backup does not contain original source paths and the job sources/filters have changed.\n"
            "Restore to a new folder instead: --to NEW_FOLDER");
    vector<SourceRef> r; for(auto &s:j.sources) { fs::path p=abs_path(s); r.push_back({fs::is_regular_file(p)?'F':'D',p}); }
    return r;
}
static fs::path original_path(const vector<SourceRef> &src, const string &key) {
    size_t slash=key.find('/'); require(key.rfind("source_",0)==0&&slash!=string::npos,"Invalid path in manifest: "+key);
    int n=positive_int(key.substr(7,slash-7),0,1000000); require((size_t)n<src.size(),"Unknown source number for "+key);
    require(src[n].path.is_absolute(),"Original source path belongs to another platform; use restore --to NEW_FOLDER");
    string rel=key.substr(slash+1);
    if(src[n].type=='F') { require(rel==src[n].path.filename().string(),"Unexpected source file path: "+key); return src[n].path; }
    return src[n].path/rel;
}
static void write_source_info(const Job &j, const Manifest &final, const fs::path &dir) {
    try {
        auto src=snapshot_sources(j,final); std::ofstream o(dir/"bronnen.txt");
        o<<"Restored backup: "<<fmt_local(id_time(final.id))<<" (ID "<<final.id<<")\n";
        for(size_t i=0;i<src.size();++i) o<<"files/source_"<<i<<"  =  "<<src[i].path.string()<<(src[i].type=='F'?" (file)":"")<<"\n";
    } catch(...) {}
}
static void restore_to_dir(const Job &j,const vector<const Manifest*> &chain,const Manifest &final,const std::set<string> &keys,const fs::path &out,bool dry) {
    fs::path target=abs_path(out);
    require(!fs::exists(target),"Restore destination already exists; use a NEW folder: "+target.string());
    if(dry) { std::cout<<"Dry-run: "<<keys.size()<<" file(s) would be restored under "<<(target/"files").string()<<"/source_N/\n"; return; }
    fs::create_directories(target.parent_path());
    fs::path stage=target;stage += ".partial."+std::to_string(getpid());
    require(!fs::exists(stage),"Temporary restore folder already exists");
    try {
        extract_chain(j,chain,final,keys,stage);
        write_source_info(j,final,stage);
        fs::rename(stage,target);
    } catch(...) {std::error_code ec;fs::remove_all(stage,ec);throw;}
    std::cout<<"Restored: "<<keys.size()<<" file(s) under "<<(target/"files").string()<<"/source_N/\n";
    std::ifstream info(target/"bronnen.txt"); string line; std::getline(info,line);
    while(std::getline(info,line)) std::cout<<"  "<<line<<"\n";
}
static bool confirm(const string &question) {
    if(!isatty(STDIN_FILENO)) return false;
    std::cout<<question<<" Type 'yes' to continue: "<<std::flush;
    string answer; if(!std::getline(std::cin,answer)) return false;
    answer=lower(answer); return answer=="ja"||answer=="j"||answer=="yes"||answer=="y";
}
static void reject_symlink_path(const fs::path &target) {
    fs::path part;
    for(const auto &component:abs_path(target)) {
        part/=component;
        require(!fs::is_symlink(fs::symlink_status(part)),"Symlink in restore target: "+part.string());
    }
}
static void undo_cmd(const Job &j,const string &id,bool yes,bool dry) {
    require(valid_name(id),"Invalid undo ID");
    JobLock lock(j,true);
    fs::path safety=job_dir(j)/"vervangen"/id;
    require(!fs::exists(safety/"undone"),"Restore has already been undone");
    std::ifstream in(safety/"journal",std::ios::binary);
    require(bool(in),"Unknown undo ID (see undo-list)");
    struct Undo { fs::path target, saved; string before, after; bool existed; };
    vector<Undo> ops; string line;
    while(std::getline(in,line)) {
        auto v=split_tabs(line); require(v.size()==6,"Invalid undo journal");
        string key=unhex(v[0]); require(safe_relative(key),"Invalid undo key");
        Undo o{fs::path(unhex(v[1])),safety/"files"/key,v[3],v[4],v[2]=="1"};
        require(o.target.is_absolute(),"Invalid undo path"); reject_symlink_path(o.target);
        bool exists=fs::exists(o.target);
        if(o.existed) {
            require(fs::is_regular_file(o.saved)&&sha256_file(o.saved)==o.before,"Undo safety copy missing or damaged");
            if(exists&&fs::is_regular_file(o.target)&&sha256_file(o.target)==o.before)continue;
        } else if(!exists)continue;
        require(exists&&fs::is_regular_file(o.target)&&sha256_file(o.target)==o.after,
                "Undo conflict: file changed after restore: "+o.target.string());
        ops.push_back(o);
    }
    require(in.eof(),"Cannot read undo journal");
    std::cout<<"Undo ID: "<<id<<"; files: "<<ops.size()<<"\n";
    if(dry)return;
    if(!yes&&!confirm("Undo restore?"))fail("Confirmation required; use --yes");
    for(const auto &o:ops) {
        reject_symlink_path(o.target);
        require(fs::is_regular_file(o.target)&&sha256_file(o.target)==o.after,
                "Undo conflict: file changed while waiting: "+o.target.string());
        if(o.existed) {
            require(fs::is_regular_file(o.saved)&&sha256_file(o.saved)==o.before,"Undo safety copy changed");
            fs::path tmp=o.target; tmp+=".cbackup-undo."+std::to_string(getpid());
            try { fs::copy_file(o.saved,tmp,fs::copy_options::overwrite_existing); set_mtime(tmp,cb::stat(o.saved).mtime); cb::replace_file(tmp,o.target); }
            catch(...) { std::error_code ec; fs::remove(tmp,ec); throw; }
        } else fs::remove(o.target);
    }
    atomic_write(safety/"undone","done\n"); log_event(j,"undo",id);
}
static void undo_list(const Job &j) {
    fs::path dir=job_dir(j)/"vervangen";
    if(!fs::exists(dir))return;
    for(auto &e:fs::directory_iterator(dir))if(e.is_directory()&&fs::exists(e.path()/"journal"))
        std::cout<<e.path().filename().string()<<"\t"<<(fs::exists(e.path()/"undone")?"undone":"available")<<"\n";
}
static void restore_original(const Job &j,const vector<const Manifest*> &chain,const Manifest &final,const std::set<string> &keys,bool yes,bool dry) {
    auto src=snapshot_sources(j,final);
    struct Op { string key; fs::path target; bool exists; };
    vector<Op> ops; size_t same=0;
    for(auto &key:keys) {
        const Item &it=final.files.at(key);
        fs::path target=original_path(src,key);
        reject_symlink_path(target);
        auto st=fs::symlink_status(target);
        if(!fs::exists(st)) {
            fs::path p=target.parent_path(); while(!p.empty() && p!=p.root_path() && !fs::exists(fs::symlink_status(p))) p=p.parent_path();
            require(p.empty()||fs::is_directory(p),"Cannot restore: "+p.string()+" is not a folder (required for "+target.string()+")");
            ops.push_back({key,target,false});
        } else {
            require(!fs::is_directory(st),"Cannot restore: at "+target.string()+" a folder already exists");
            if(fs::is_regular_file(st) && fs::file_size(target)==it.size && sha256_file(target)==it.hash) { ++same; continue; }
            ops.push_back({key,target,true});
        }
    }
    size_t over=std::count_if(ops.begin(),ops.end(),[](const Op &o){return o.exists;});
    fs::path safety=job_dir(j)/"vervangen"/new_id();
    std::cout<<"Restore to original locations:\n"
             <<"  create new (currently absent):       "<<ops.size()-over<<"\n"
             <<"  overwrite (currently different):     "<<over<<"\n"
             <<"  identical (will be skipped):         "<<same<<"\n";
    size_t shown=0;
    for(auto &o:ops) if(o.exists && shown<10) { std::cout<<"    overwrites: "<<o.target.string()<<"\n"; ++shown; }
    if(over>shown) std::cout<<"    ... and "<<over-shown<<" more\n";
    if(over) std::cout<<"Current versions will first be saved in: "<<safety.string()<<"\n";
    std::cout<<"Existing files absent from this backup are kept (nothing is deleted).\n";
    if(dry) { std::cout<<"Dry run: nothing changed.\n"; return; }
    if(ops.empty()) { std::cout<<"Everything already matches this backup; nothing to do.\n"; return; }
    if(!yes && !confirm(std::to_string(ops.size())+" file(s)?"))
        fail(isatty(STDIN_FILENO)?"Cancelled; nothing changed.":"Confirmation required: add --yes (nothing changed).");
    std::set<string> want; for(auto &o:ops) want.insert(o.key);
    fs::path stage=job_dir(j)/(".restore.partial."+std::to_string(getpid()));
    size_t done=0;
    try {
        extract_chain(j,chain,final,want,stage);
        fs::create_directories(safety);
        std::ofstream journal(safety/"journal",std::ios::binary|std::ios::app);
        require(bool(journal),"Cannot create undo journal");
        for(auto &o:ops) {
            fs::path staged=stage/"files"/o.key;
            reject_symlink_path(o.target);
            require(o.exists?fs::is_regular_file(o.target):!fs::exists(o.target),
                    "Restore conflict: target appeared or changed type: "+o.target.string());
            fs::create_directories(o.target.parent_path());
            if(o.exists) {
                fs::path keep=safety/"files"/o.key; fs::create_directories(keep.parent_path());
                if(fs::is_symlink(fs::symlink_status(o.target))) fs::copy_symlink(o.target,keep); else fs::copy_file(o.target,keep);
                set_mtime(keep,cb::stat(o.target).mtime);
            }
            string before=o.exists?sha256_file(safety/"files"/o.key):"-";
            journal<<hex(o.key)<<"\t"<<hex(o.target.string())<<"\t"<<(o.exists?"1":"0")<<"\t"<<before<<"\t"<<final.files.at(o.key).hash<<"\tprepared\n";
            journal.flush(); require(bool(journal),"Cannot flush undo journal");
            fs::path tmp=o.target; tmp += ".cbackup-restore."+std::to_string(getpid());
            try {
                fs::copy_file(staged,tmp,fs::copy_options::overwrite_existing);
                fs::permissions(tmp,fs::status(staged).permissions(),fs::perm_options::replace);
                set_mtime(tmp,cb::stat(staged).mtime);
                cb::replace_file(tmp,o.target);   // per bestand atomair: nooit een half bestand
            } catch(...) { std::error_code ec; fs::remove(tmp,ec); throw; }
            ++done;
        }
    } catch(const std::exception &e) {
        std::error_code ec; fs::remove_all(stage,ec);
        fail(string(e.what())+"\nRestore interrupted after "+std::to_string(done)+" of "+std::to_string(ops.size())+" file(s)."
             +(over?" Previous versions are in "+safety.string():""));
    }
    std::error_code ec; fs::remove_all(stage,ec);
    std::cout<<"Restored: "<<done<<" file(s) at their original location.\n";
    if(over) std::cout<<"Previous versions saved in: "<<safety.string()<<"\n";
    std::cout<<"Undo ID: "<<safety.filename().string()<<" (cbackup undo "<<j.name<<" "<<safety.filename().string()<<" --yes)\n";
    log_event(j,"restore-original",final.id+" undo="+safety.filename().string());
}
static void restore_cmd(const Job &j,const vector<string> &when,const string &to,bool original,const vector<string> &only,bool yes,bool dry) {
    JobLock lock(j,true);
    auto all=get_manifests(j); require(!all.empty(),"No backups yet for job "+j.name);
    When w=parse_when(when,all);
    const Manifest &final=choose(all,w);
    auto chain=chain_for(all,final.id); verify_chain(j,chain);
    std::cout<<"Restore chain: "<<chain_desc(chain)<<" (SHA-256 verified)\n";
    auto keys=select_keys(final,only);
    if(!only.empty()) std::cout<<"Selected with --only: "<<keys.size()<<" of "<<final.files.size()<<" file(s)\n";
    if(original) restore_original(j,chain,final,keys,yes,dry); else restore_to_dir(j,chain,final,keys,to,dry);
    if(!dry&&!original)log_event(j,"restore-copy",final.id+" to="+abs_path(to).string());
}

// ---------------------------------------------------------------- commando's
#include "maintenance.h"
static void help() { cb_i18n::help(std::cout,VERSION); }
static void add_sources(Job &j, const vector<string> &paths) {
    for(auto &raw:paths) {
        no_newline(raw); require(!raw.empty(),"Empty value is not allowed");
        string p=abs_path(raw).string();
        bool dup=false; for(auto &s:j.sources) if(s==p || abs_path(s).string()==p) dup=true;
        if(dup) continue;
        if(!fs::exists(p)) std::cout<<"WARNING: source does not (yet) exist: "<<p<<"\n";
        j.sources.push_back(p);
    }
}
static void config_cmd(const vector<string> &a) {
    require(a.size()>=4,"Usage: config JOB ACTION [values]");
    Job j=load_job(a[2]);string cmd=a[3];
    if(cmd=="show"){require(a.size()==4,"Too many arguments");show_job(j);return;}
    auto add=[&](vector<string>&vec,bool ext){
        require(a.size()>=5,"Specify at least one value");
        for(size_t i=4;i<a.size();++i) {
            string v=ext?norm_ext(a[i]):a[i];no_newline(v);
            require(!v.empty(),"Empty value is not allowed");
            if(std::find(vec.begin(),vec.end(),v)==vec.end())vec.push_back(v);
        }
    };
    bool sources_added=false;
    if(cmd=="add-source") { require(a.size()>=5,"Specify at least one path"); add_sources(j,vector<string>(a.begin()+4,a.end())); sources_added=true; }
    else if(cmd=="remove-source") {
        require(a.size()==5,"Usage: remove-source PATH");
        auto it=std::find_if(j.sources.begin(),j.sources.end(),[&](const string &s){ return s==a[4] || abs_path(s)==abs_path(a[4]); });
        require(it!=j.sources.end(),"Source not found");j.sources.erase(it);
    } else if(cmd=="add-ext")add(j.exts,true);
    else if(cmd=="add-include")add(j.includes,false);
    else if(cmd=="add-exclude")add(j.excludes,false);
    else if(cmd=="clear-ext") {require(a.size()==4,"Too many arguments");j.exts.clear();}
    else if(cmd=="clear-include") {require(a.size()==4,"Too many arguments");j.includes.clear();}
    else if(cmd=="clear-exclude") {require(a.size()==4,"Too many arguments");j.excludes.clear();}
    else if(cmd=="set") {
        require(a.size()==6,"Usage: set KEY VALUE");
        string k=a[4],v=a[5];no_newline(v);
        if(k=="dest") {require(!v.empty(),"dest must not be empty");j.dest=abs_path(v).string();}
        else if(k=="usb") {j.usb=v.empty()?"":abs_path(v).string();}
        else if(k=="cloud") {require(v.empty()||v.find(':')!=string::npos,"Use remote:path");j.cloud=v;}
        else if(k=="mode") {require(v=="auto"||v=="full"||v=="incremental"||v=="differential"||v=="mirror","Invalid mode");j.mode=v;}
        else if(k=="compression") {
            parse_compression(v,j.compression,j.compression_level);
            require_compression(j.compression,j.compression_level);
        }
        else if(k=="compression_level"||k=="level") {
            int lv=positive_int(v,0,99); check_level(j.compression,lv);
            require_compression(j.compression,lv); j.compression_level=lv;
        }
        else if(k=="full_every")j.full_every=positive_int(v,1,100000);
        else if(k=="keep_chains")j.keep_chains=positive_int(v,0,100000);
        else if(k=="keep_days")j.keep_days=positive_int(v,0,100000);
        else if(k=="encryption") {require(v=="none"||v=="aes-256-gcm","Use none or aes-256-gcm");j.encryption=v;}
        else if(k=="password_env") {require(valid_env_name(v),"Invalid environment variable name");j.password_env=v;}
        else if(k=="profile") {require(v=="all"||v=="settings"||v=="installers","Use all, settings or installers");j.profile=v;}
        else if(k=="watch_seconds")j.watch_seconds=positive_int(v,5,86400);
        else if(k=="log_max_mb")j.log_max_mb=positive_int(v,1,1024);
        else if(k=="notifications") {require(v=="true"||v=="false","Use true or false");j.notifications=v=="true";}
        else if(k=="desktop_notifications") {require(v=="true"||v=="false","Use true or false");j.desktop_notifications=v=="true";}
        else if(k=="stale_hours")j.stale_hours=positive_int(v,0,87600);
        else if(k=="notify_repeat_hours")j.notify_repeat_hours=positive_int(v,1,87600);
        else if(k=="test_restore_days")j.test_restore_days=positive_int(v,0,3650);
        else if(k=="test_restore_max_mb")j.test_restore_max_mb=positive_int(v,1,1048576);
        else if(k=="offline_reminder_days")j.offline_reminder_days=positive_int(v,0,3650);
        else if(k=="storage_quota_mb")j.storage_quota_mb=positive_int(v,0,2147483647);
        else if(k=="autostart"||k=="watch"||k=="exclude_dependencies") {
            require(v=="true"||v=="false","Use true or false");
            if(k=="autostart")j.autostart=v=="true";
            else if(k=="watch")j.watch=v=="true"; else j.exclude_dependencies=v=="true";
        }
        else fail("Unknown setting: "+k+" (change scheduling with: cbackup schedule)");
    }else fail("Unknown config command: "+cmd);
    save_job(j);std::cout<<"Configuration saved: "<<j.name<<"\n";
    log_event(j,"config",cmd);
    if(cmd=="set"&&(a[4]=="compression"||a[4]=="compression_level"||a[4]=="level")) std::cout<<"Compression: "<<comp_label(j.compression,j.compression_level)<<"\n";
    if(sources_added) ensure_schedule(j);
    if(cmd=="set"&&(a[4]=="autostart"||a[4]=="watch")&&!j.sources.empty()&&!j.schedule.empty()) {
        if(!j.schedule.empty()||(j.watch&&j.autostart))install_timer(j); else remove_timer(j);
    }
}
static void schedule_cmd(Job &j, const vector<string> &args) {
    require(args.size()>=4,"Usage: schedule JOB --hourly | --every 10min | --daily HH:MM | --weekly Mon@HH:MM | on | off");
    const string &opt=args[3];
    if(opt=="off") {
        require(args.size()==4,"Too many arguments");
        j.schedule=""; save_job(j); remove_timer(j);
        std::cout<<"Schedule disabled. Enable again with: cbackup schedule "<<j.name<<" on\n"; return;
    }
    if(opt=="on") { require(args.size()==4,"Too many arguments"); if(j.schedule.empty()) j.schedule="every 2h"; }
    else if(opt=="--startup") { require(args.size()==4,"Too many arguments"); j.schedule="startup"; j.autostart=true; }
    else if(opt=="--hourly") { require(args.size()==4,"Too many arguments"); j.schedule="hourly"; }
    else {
        require(args.size()==5,"Invalid number of schedule arguments");
        if(opt=="--every") j.schedule="every "+args[4];
        else if(opt=="--daily") j.schedule="daily "+args[4];
        else if(opt=="--weekly") j.schedule="weekly "+args[4];
        else fail("Use --hourly / --every / --daily / --weekly / on / off");
    }
    TimerSpec spec=timer_spec(j.schedule);
    save_job(j);
    if(j.sources.empty()) { std::cout<<"Schedule saved ("<<spec.desc<<"); activated once you add a source.\n"; return; }
    install_timer(j);
    std::cout<<"Timer actief: "<<unit_base(j)<<".timer - "<<spec.desc<<"\n";
    linger_hint();
}
static void init_cmd(const vector<string> &a) {
    const string &name=a[2];
    require(!fs::exists(job_file(name)),"Job bestaat al");
    Job j; j.name=name; j.dest=(fs::path(home())/"backups").string(); j.schedule="every 2h";
    vector<string> srcs;
    for(size_t i=3;i<a.size();++i) {
        if(a[i]=="--no-schedule") j.schedule="";
        else if(a[i]=="--dest") {require(++i<a.size(),"--dest requires a path");j.dest=abs_path(a[i]).string();}
        else if(a[i].rfind("--",0)==0) fail("Unknown option: "+a[i]);
        else srcs.push_back(a[i]);
    }
    add_sources(j,srcs);
    save_job(j);
    {cb::ProcessLock lock(health_dir(j)/".lock",true);save_health(j,load_health(j));}
    std::cout<<"Job gemaakt: "<<name<<"\n"
             <<"  Backups:    "<<job_dir(j).string()<<"  (change with: cbackup config "<<name<<" set dest PATH)\n"
             <<"  Type:       incremental; every "<<j.full_every<<" backups a full backup automatically\n"
             <<"  Compression: "<<comp_label(j.compression,j.compression_level)<<"  (choose another: cbackup compression)\n"
             <<"  Schedule:   "<<(j.schedule.empty()?"off (enable: cbackup schedule "+name+" on)":timer_spec(j.schedule).desc)<<"\n"
             <<"  Retention:  every saved point in the last "<<j.keep_days<<" days remains recoverable\n";
    for(auto &s:j.sources) std::cout<<"  Source:     "<<s<<"\n";
    if(j.sources.empty()) {
        std::cout<<"Add a source: cbackup config "<<name<<" add-source PAD";
        if(!j.schedule.empty()) std::cout<<"  (scheduling is then activated automatically)";
        std::cout<<"\n"; return;
    }
    ensure_schedule(j);
    std::cout<<"Create the first (full) backup now with: cbackup run "<<name<<"\n";
}
static void changes_cmd(const Job &j,const vector<string> &ids) {
    require(ids.size()==1||ids.size()==2,"Usage: changes JOB ID [ID]");
    auto all=get_manifests(j);
    auto find=[&](const string &id)->const Manifest* {
        for(const auto &m:all)if(m.id==id)return &m;
        fail("Unknown snapshot ID: "+id); return nullptr;
    };
    const Manifest *after=find(ids.back()), *before=nullptr;
    if(ids.size()==2)before=find(ids.front());
    else { for(const auto &m:all)if(m.id<after->id)before=&m; }
    for(const auto &kv:after->files) {
        if(!before||!before->files.count(kv.first))std::cout<<"ADDED\t"<<kv.first<<"\n";
        else if(before->files.at(kv.first).hash!=kv.second.hash)std::cout<<"MODIFIED\t"<<kv.first<<"\n";
    }
    if(before)for(const auto &kv:before->files)if(!after->files.count(kv.first))std::cout<<"DELETED\t"<<kv.first<<"\n";
}
static void log_cmd(const Job &j,int tail) {
    std::ifstream in(job_dir(j)/"events.log",std::ios::binary); vector<string> lines; string s;
    while(std::getline(in,s)) {
        auto v=split_tabs(s); if(v.size()==3)lines.push_back(v[0]+"\t"+v[1]+"\t"+unhex(v[2]));
    }
    for(size_t i=lines.size()>static_cast<size_t>(tail)?lines.size()-tail:0;i<lines.size();++i)std::cout<<lines[i]<<"\n";
}
static void dependencies_cmd(const Job &j,const string &id,const string &out) {
    // Restore the actual version inputs, never run code from a backup automatically.
    vector<string> patterns={"package.json","package-lock.json","npm-shrinkwrap.json","yarn.lock","pnpm-lock.yaml",
        ".npmrc",".nvmrc",".node-version","requirements*.txt","pyproject.toml","poetry.lock","uv.lock","Pipfile*",
        "Cargo.toml","Cargo.lock","rust-toolchain*",".python-version",".tool-versions",
        "composer.json","composer.lock","Gemfile*","go.mod","go.sum","global.json","packages.lock.json",
        "*.csproj","*.fsproj","*.vbproj","Directory.Packages.props","NuGet.Config","nuget.config"};
    restore_cmd(j,{id},out,false,patterns,false,false);
    atomic_write(abs_path(out)/"DEPENDENCIES.txt",
        "Run commands manually inside each restored project after reviewing its scripts.\n"
        "npm with package-lock.json/npm-shrinkwrap.json: npm ci\n"
        "pnpm-lock.yaml: pnpm install --frozen-lockfile\nyarn.lock: use the project's pinned Yarn version and immutable/frozen-lockfile mode\n"
        "uv.lock: uv sync --frozen\npoetry.lock: poetry install\n"
        "requirements.txt: exact versions require == pins (prefer hashes); unpinned files cannot guarantee original versions\n"
        "Cargo.lock: cargo build --locked\ncomposer.lock: composer install\nGemfile.lock: bundle config set --local frozen true; then bundle install\n"
        "go.mod/go.sum: go mod download\npackages.lock.json: dotnet restore --locked-mode\n"
        "Use the original runtime, package-manager version and platform. Packages may no longer be downloadable.\n"
        "Lockfiles are included in ordinary backups unless you explicitly filter them out. No lockfile = no exact-version guarantee.\n");
}
static volatile std::sig_atomic_t watch_stop=0;
static void stop_watch(int) { watch_stop=1; }
static void watch_cmd(Job j) {
    fs::create_directories(job_dir(j));
    cb::ProcessLock watch_lock(job_dir(j)/".watch.lock",false);
    require(j.watch,"Enable watch first: config JOB set watch true");
    std::signal(SIGINT,stop_watch); std::signal(SIGTERM,stop_watch);
    log_event(j,"watch-start");
    while(!watch_stop) {
        j=load_job(j.name); if(!j.watch)break;
        bool attempted=false;
        try {
            JobLock lock(j,false);
            attempted=true;
            auto all=get_manifests(j);
            const Manifest *prev=all.empty()?nullptr:&all.back();
            auto files=scan(j,prev);
            bool changed=!prev||prev->fingerprint!=fingerprint(j)||files.size()!=prev->files.size();
            if(!changed)for(const auto &kv:files) {
                auto it=prev->files.find(kv.first);
                if(it==prev->files.end()||it->second.hash!=kv.second.hash) {changed=true;break;}
            }
            bool heartbeat=false;
            {cb::ProcessLock state_lock(health_dir(j)/".lock",true);heartbeat=time(nullptr)-load_health(j).last_success>=3600;}
            if(changed||heartbeat) {attempted=true;make_backup(j,"",false);finish_run(j,j.mode=="mirror");}
        } catch(const std::exception &e) {
            if(attempted||string(e.what())!="Another operation is running for this job")record_run_failure(j,e.what());
            try {log_event(j,"watch-error",e.what());}catch(...) {}
        }
        // Poll metadata only; unchanged files reuse snapshot hashes. Debounce saves at watch_seconds.
        for(int elapsed=0;elapsed<j.watch_seconds&&!watch_stop;++elapsed)std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    log_event(j,"watch-stop");
}
int main(int argc,char **argv) {
    cb::initialize(); // private files on Unix, UTF-8 and low process priority
    string active_job;bool active_run=false;
    try {
        vector<string> a=cb::arguments(argc,argv);
        std::ifstream language_file(fs::path(home())/".config/cbackup/language"); string language;
        if(language_file>>language) cb_i18n::set_language(language);
        for(size_t i=1;i<a.size();) {
            if(a[i]=="--lang") {require(i+1<a.size(),"--lang requires a language code");require(cb_i18n::set_language(a[i+1]),"Unknown language code");a.erase(a.begin()+i,a.begin()+i+2);}
            else ++i;
        }
        argc=static_cast<int>(a.size());
        if(argc>=2&&a[1]=="language") {
            if(argc==2) { cb_i18n::languages(std::cout); return 0; }
            require(argc==3&&cb_i18n::set_language(a[2]),"Usage: language CODE");
            atomic_write(fs::path(home())/".config/cbackup/language",a[2]+"\n");
            std::cout<<"Language: "<<cb_i18n::language()<<"\n"; return 0;
        }
        if(argc>=2&&a[1]=="settings") {cb_i18n::settings(std::cout);return 0;}
        if(argc<2||a[1]=="--help"||a[1]=="help"||a[1]=="-h"){help();return 0;}
        string cmd=a[1];
        if(cmd=="--version"||cmd=="version") { std::cout<<"cbackup "<<VERSION<<"\n"; return 0; }
        if(cmd=="compression"||cmd=="compressie") { show_compressions(); return 0; }
        require(argc>=3,"Missing job name (see: cbackup help)");
        string name=a[2];require(valid_name(name),"Invalid job name");
        active_job=name;
        if(cmd=="init") { init_cmd(a); return 0; }
        if(cmd=="config"){config_cmd(a);return 0;}
        Job j=load_job(name);
        if(cmd=="health") {require(argc==3,"Usage: health JOB");return health_check(j,true)?0:2;}
        if(cmd=="alerts") {require(argc==3,"Usage: alerts JOB");show_alerts(j);return 0;}
        if(cmd=="test-restore") {require(argc==3,"Usage: test-restore JOB");JobLock lock(j,true);test_restore(j);return 0;}
        if(cmd=="offline-confirm") {require(argc==4,"Usage: offline-confirm JOB SNAPSHOT_ID");JobLock lock(j,true);offline_confirm(j,a[3]);return 0;}
        if(cmd=="ids") {require(argc==3,"Usage: ids JOB");for(const auto &m:get_manifests(j))std::cout<<m.id<<"\n";return 0;}
        if(cmd=="changes") {changes_cmd(j,vector<string>(a.begin()+3,a.end()));return 0;}
        if(cmd=="undo-list") {require(argc==3,"Usage: undo-list JOB");undo_list(j);return 0;}
        if(cmd=="undo") {
            require(argc>=4,"Usage: undo JOB ID [--yes] [--dry-run]");bool yes=false,dry=false;
            for(int i=4;i<argc;++i) {if(a[i]=="--yes")yes=true;else if(a[i]=="--dry-run")dry=true;else fail("Unknown undo option");}
            undo_cmd(j,a[3],yes,dry);return 0;
        }
        if(cmd=="log") {int tail=100; if(argc!=3){require(argc==5&&a[3]=="--tail","Usage: log JOB [--tail N]");tail=positive_int(a[4],1,100000);}log_cmd(j,tail);return 0;}
        if(cmd=="watch") {require(argc==3,"Usage: watch JOB");watch_cmd(j);return 0;}
        if(cmd=="dependencies") {require(argc==6&&a[4]=="--to","Usage: dependencies JOB ID --to NEW_DIR");dependencies_cmd(j,a[3],a[5]);return 0;}
        if(cmd=="run") {
            string mode;bool dry=false;
            for(int i=3;i<argc;++i) {
                if(a[i]=="--dry-run")dry=true;
                else if(a[i]=="--type") {require(++i<argc,"Missing backup type");mode=a[i];}
                else if(a[i]=="--compression"||a[i]=="--compressie") {
                    require(++i<argc,"Missing compression type");
                    parse_compression(a[i],j.compression,j.compression_level); require_compression(j.compression,j.compression_level);
                }
                else fail("Unknown option: "+a[i]);
            }
            active_run=!dry;
            JobLock lock(j,false);
            make_backup(j,mode,dry);
            if(!dry)finish_run(j,(mode.empty()?j.mode:mode)=="mirror");
            return 0;
        }
        auto rest=[&](){ return vector<string>(a.begin()+3,a.end()); };
        if(cmd=="list") { list_snapshots(j,rest()); return 0; }
        if(cmd=="verify") {
            require(argc>=4,string("Usage: verify JOB WHEN, e.g.: ")+WHEN_EXAMPLES);
            auto all=get_manifests(j); When w=parse_when(rest(),all); const Manifest &m=choose(all,w);
            auto chain=chain_for(all,m.id);verify_chain(j,chain);
            std::cout<<"SHA256 of "<<chain.size()<<" archive(s) verified ("<<chain_desc(chain)<<"); restore chain complete.\n";return 0;
        }
        if(cmd=="restore") {
            vector<string> when, only; string to; bool original=false, yes=false, dry=false;
            for(int i=3;i<argc;++i) {
                if(a[i]=="--to"||a[i]=="--naar") { require(++i<argc,"--to requires a folder"); to=a[i]; require(!to.empty(),"--to requires a folder"); }
                else if(a[i]=="--original"||a[i]=="--origineel") original=true;
                else if(a[i]=="--only"||a[i]=="--alleen") { require(++i<argc,"--only requires a pattern"); only.push_back(a[i]); }
                else if(a[i]=="--yes"||a[i]=="-y"||a[i]=="--ja") yes=true;
                else if(a[i]=="--dry-run") dry=true;
                else if(a[i].size()>1&&a[i][0]=='-') fail("Unknown option: "+a[i]);
                else when.push_back(a[i]);
            }
            require(original!=!to.empty(),"Usage: restore JOB WHEN --to NEW_FOLDER   or   restore JOB WHEN --original");
            restore_cmd(j,when,to,original,only,yes,dry); return 0;
        }
        if(cmd=="cloud-pull") {require(argc==3,"Usage: cloud-pull JOB");JobLock lock(j,true);pull_cloud(j);return 0;}
        if(cmd=="cloud-push") {require(argc==3,"Usage: cloud-push JOB");push_cloud_all(j);return 0;}
        if(cmd=="replicate") {require(argc==3||(argc==4&&a[3]=="--check"),"Usage: replicate JOB [--check]");replicate(j,argc==4);return 0;}
        if(cmd=="schedule") {schedule_cmd(j,a);return 0;}
        fail("Unknown command: "+cmd+" (see: cbackup help)");
    } catch(const std::exception &e) {
        std::cerr<<"ERROR: "<<cb_i18n::tr(e.what())<<"\n";
        if(!active_job.empty()) {
            if(active_run&&string(e.what())!="Another operation is running for this job")
                try {record_run_failure(load_job(active_job),e.what());}catch(const std::exception &error) {std::cerr<<"Health state error: "<<error.what()<<"\n";}
            try {log_event(load_job(active_job),"error",e.what());}catch(...) {}
        }
        return 1;
    }
    return 0;
}

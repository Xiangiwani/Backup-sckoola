#define _POSIX_C_SOURCE 200809L
#include <archive.h>
#include <archive_entry.h>
#include <openssl/evp.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using std::string;
using std::vector;
using std::map;

static void fail(const string &s) { throw std::runtime_error(s); }
static void require(bool b, const string &s) { if (!b) fail(s); }
static string lower(string s) { for (char &c : s) c = static_cast<char>(std::tolower((unsigned char)c)); return s; }
static bool valid_name(const string &v) {
    if (v.empty() || v.size() > 60) return false;
    for (char c : v) if (!(std::isalnum((unsigned char)c) || c == '_' || c == '-')) return false;
    return true;
}
static string home() { const char *p = getenv("HOME"); require(p && *p, "HOME ontbreekt"); return p; }
static fs::path conf_dir() { return fs::path(home()) / ".config/cbackup/jobs"; }
static string job_file(const string &job) { require(valid_name(job), "Ongeldige jobnaam (letters, cijfers, _ en -)"); return (conf_dir() / (job + ".conf")).string(); }
static string hex(const string &s) {
    static const char *d = "0123456789abcdef";
    string out; out.reserve(s.size() * 2);
    for (unsigned char c : s) { out += d[c>>4]; out += d[c&15]; }
    return out;
}
static string unhex(const string &s) {
    require(s.size() % 2 == 0, "Ongeldige hex in manifest"); string o;
    for (size_t i=0; i<s.size(); i+=2) {
        auto digit=[](char c)->int { if (c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; return -1; };
        int a=digit(s[i]), b=digit(s[i+1]); require(a>=0 && b>=0, "Ongeldige hex");
        o += char((a<<4)|b);
    }
    return o;
}
static string sha256_stream(std::istream &f) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new(); require(ctx, "EVP_MD_CTX_new mislukt");
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ptr(ctx, EVP_MD_CTX_free);
    require(EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1, "SHA256 init mislukt");
    std::array<char, 131072> buf{};
    while (f.read(buf.data(), buf.size()) || f.gcount()) {
        require(EVP_DigestUpdate(ctx, buf.data(), (size_t)f.gcount()) == 1, "SHA256 update mislukt");
    }
    require(!f.bad(), "Leesfout tijdens SHA256");
    unsigned char digest[EVP_MAX_MD_SIZE]; unsigned int length=0;
    require(EVP_DigestFinal_ex(ctx, digest, &length) == 1, "SHA256 final mislukt");
    return hex(string((const char*)digest, length));
}
static string sha256_file(const fs::path &path) {
    std::ifstream f(path, std::ios::binary); require(bool(f), "Kan bestand niet lezen: " + path.string());
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
static void no_newline(const string &s) { require(s.find('\n')==string::npos && s.find('\r')==string::npos && s.find('\0')==string::npos, "Nieuwe regel/NUL niet toegestaan"); }
static void atomic_write(const fs::path &path, const string &content) {
    fs::create_directories(path.parent_path());
    fs::path tmp=path; tmp += ".tmp." + std::to_string(getpid());
    try {
        std::ofstream f(tmp, std::ios::binary|std::ios::trunc);
        require(bool(f), "Kan niet schrijven: " + tmp.string());
        f << content; f.flush(); require(bool(f), "Schrijffout: " + tmp.string()); f.close();
        fs::rename(tmp,path);
    } catch(...) { std::error_code ec; fs::remove(tmp,ec); throw; }
}
static int exec_args(const vector<string> &args) {
    require(!args.empty(), "Lege opdracht");
    pid_t pid=fork(); require(pid>=0, "fork mislukt");
    if(pid==0) {
        vector<char*> v; for(const auto &s:args) v.push_back(const_cast<char*>(s.c_str())); v.push_back(nullptr);
        execvp(v[0],v.data()); perror(args[0].c_str()); _exit(127);
    }
    int status=0; while (waitpid(pid,&status,0)<0) { if(errno!=EINTR) fail("waitpid mislukt"); }
    return WIFEXITED(status)?WEXITSTATUS(status):128;
}
static string read_quoted(const string &s) {
    std::istringstream ss(s); string o; ss >> std::quoted(o);
    require(bool(ss), "Ongeldige configuratiewaarde: " + s); return o;
}
static string quoted(const string &s) { no_newline(s); std::ostringstream o; o << std::quoted(s); return o.str(); }

struct Job {
    string name, dest, usb, cloud, mode="auto", compression="zstd";
    int full_every=7, keep_chains=3;
    vector<string> sources, exts, includes, excludes;
};
static void save_job(const Job &j) {
    std::ostringstream o;
    o << "# cbackup 1.0; wijzig via cbackup config\n";
    auto put=[&](const string &k,const string &v){o<<k<<"="<<quoted(v)<<"\n";};
    put("dest",j.dest); put("usb",j.usb); put("cloud",j.cloud); put("mode",j.mode); put("compression",j.compression);
    o<<"full_every="<<j.full_every<<"\nkeep_chains="<<j.keep_chains<<"\n";
    for(auto &s:j.sources)put("source",s);
    for(auto &s:j.exts)put("ext",s);
    for(auto &s:j.includes)put("include",s);
    for(auto &s:j.excludes)put("exclude",s);
    atomic_write(job_file(j.name),o.str());
}
static int positive_int(const string &s, int low, int high) {
    try { size_t n=0; long v=std::stol(s,&n); require(n==s.size()&&v>=low&&v<=high, "Ongeldige waarde: " + s); return (int)v; }
    catch(const std::exception &) { fail("Getal buiten bereik: " + s); }
    return 0;
}
static Job load_job(const string &name) {
    Job j; j.name=name; std::ifstream f(job_file(name)); require(bool(f), "Job bestaat niet: " + name + " (gebruik init)");
    string line;
    while(std::getline(f,line)) {
        if(line.empty() || line[0]=='#')continue;
        size_t p=line.find('='); require(p!=string::npos, "Ongeldige regel in jobconfig");
        string k=line.substr(0,p), v=read_quoted(line.substr(p+1));
        if(k=="dest")j.dest=v; else if(k=="usb")j.usb=v; else if(k=="cloud")j.cloud=v;
        else if(k=="mode")j.mode=v; else if(k=="compression")j.compression=v;
        else if(k=="full_every")j.full_every=positive_int(v,1,100000);
        else if(k=="keep_chains")j.keep_chains=positive_int(v,0,100000);
        else if(k=="source")j.sources.push_back(v);
        else if(k=="ext")j.exts.push_back(v);
        else if(k=="include")j.includes.push_back(v);
        else if(k=="exclude")j.excludes.push_back(v);
        else fail("Onbekende instelling: " + k);
    }
    require(!j.dest.empty(), "Geen dest ingesteld");
    require(j.mode=="auto"||j.mode=="full"||j.mode=="incremental"||j.mode=="differential"||j.mode=="mirror", "Onbekend type");
    require(j.compression=="zstd"||j.compression=="gzip"||j.compression=="none", "Onbekende compressie");
    return j;
}
static string norm_ext(string e) { e=lower(e); if(e=="*")return e; if(!e.empty() && e[0]!='.')e="."+e; require(e.size()>1, "Ongeldige extensie"); return e; }
static string join(const vector<string>&v, const string &sep) { string out; for(auto &s:v){if(!out.empty())out+=sep;out+=s;}return out; }
static void show_job(const Job &j) {
    std::cout << "Job: " << j.name << "\nBronnen:\n";
    for(auto &s:j.sources)std::cout<<"  "<<s<<"\n";
    std::cout<<"Doel: "<<j.dest<<"\nUSB: "<< (j.usb.empty()?"uit":j.usb)
    <<"\nCloud: "<<(j.cloud.empty()?"uit":j.cloud)<<"\nType: "<<j.mode
    <<"\nCompressie: "<<j.compression<<"\nFull elke: "<<j.full_every
    <<" back-ups\nBewaar volledige ketens: "<<j.keep_chains
    <<" (0 = onbeperkt)\nExtensies: "<<(j.exts.empty()?"alle":join(j.exts,", "))
    <<"\nIncludes: "<<(j.includes.empty()?"alle":join(j.includes,", "))
    <<"\nExcludes: "<<(j.excludes.empty()?"geen":join(j.excludes,", "))<<"\n";
}
static string fingerprint(const Job &j) {
    std::ostringstream out; for(auto &s:j.sources)out<<"S"<<abs_path(s).string()<<"\n";
    for(auto &s:j.exts)out<<"E"<<s<<"\n";
    for(auto &s:j.includes)out<<"I"<<s<<"\n";
    for(auto &s:j.excludes)out<<"X"<<s<<"\n";
    return sha256_text(out.str());
}
static bool match_any(const vector<string>&patterns, const string &name, const string &relative) {
    for(auto &p:patterns) if(!fnmatch(p.c_str(),name.c_str(),0)||!fnmatch(p.c_str(),relative.c_str(),0))return true;
    return false;
}
struct File { fs::path source; uintmax_t size=0; string hash; };
using FileMap=map<string,File>;
static FileMap scan(const Job &j) {
    require(!j.sources.empty(), "Voeg minimaal een bron toe (add-source)");
    fs::path rootdest=abs_path(j.dest);
    FileMap result;
    for(size_t i=0;i<j.sources.size();++i) {
        fs::path source=abs_path(j.sources[i]);
        require(fs::exists(source), "Bron ontbreekt; geen back-up gemaakt: " + source.string());
        require(fs::is_directory(source)||fs::is_regular_file(source), "Bron is geen bestand/map: " + source.string());
        require(!under(rootdest,source), "Doelmap ligt binnen de bronmap: " + rootdest.string());
        if(!j.usb.empty())require(!under(abs_path(j.usb),source), "USB-doelmap ligt binnen bronmap");
        string prefix="source_"+std::to_string(i)+"/";
        auto add=[&](const fs::path &p,const fs::path &rel){
            string relative=rel.generic_string(), logical=prefix+relative;
            if(!j.exts.empty()) {
                string ext=lower(p.extension().string());
                if(std::find(j.exts.begin(),j.exts.end(),ext)==j.exts.end() && std::find(j.exts.begin(),j.exts.end(),"*")==j.exts.end())return;
            }
            if(!j.includes.empty()&&!match_any(j.includes,logical,relative))return;
            if(match_any(j.excludes,logical,relative))return;
            File f; f.source=p; f.size=fs::file_size(p); f.hash=sha256_file(p);
            result.emplace(logical,std::move(f));
        };
        if(fs::is_regular_file(source)) { add(source,source.filename()); continue; }
        std::error_code ec;
        fs::recursive_directory_iterator it(source,fs::directory_options::none,ec), end;
        if(ec)fail("Scannen mislukt: "+ec.message());
        for(;it!=end;it.increment(ec)) {
            if(ec)fail("Scannen mislukt: "+ec.message());
            const auto &e=*it;
            if(e.is_symlink(ec)) { if(ec)fail(ec.message()); it.disable_recursion_pending(); continue; }
            if(e.is_directory(ec)) { if(ec)fail(ec.message()); continue; }
            if(ec)fail(ec.message());
            if(e.is_regular_file(ec)) { if(ec)fail(ec.message()); add(e.path(),e.path().lexically_relative(source)); }
            if(ec)fail(ec.message());
        }
        if(ec)fail("Scannen mislukt: "+ec.message());
    }
    return result;
}
struct Item { uintmax_t size; string hash; };
struct Manifest {
    string id,type,base,parent,fingerprint,archive,archive_hash;
    map<string,Item> files;
};
static bool safe_relative(const string &s) {
    if(s.empty() || s.find('\0')!=string::npos || s[0]=='/' || s.find('\\')!=string::npos)return false;
    fs::path p(s); if(p.is_absolute())return false;
    for(auto &c:p) if(c==".." || c=="." || c.empty())return false;
    return p.generic_string()==s && s.find("//")==string::npos;
}
static string manifest_text(const Manifest &m) {
    std::ostringstream o;
    o<<"CBACKUP\t1\n"<<"id\t"<<m.id<<"\n"<<"type\t"<<m.type
    <<"\nbase\t"<<m.base<<"\nparent\t"<<m.parent<<"\nfingerprint\t"<<m.fingerprint
    <<"\narchive\t"<<m.archive<<"\narchive_hash\t"<<m.archive_hash<<"\n";
    for(auto &kv:m.files)o<<"file\t"<<hex(kv.first)<<"\t"<<kv.second.size<<"\t"<<kv.second.hash<<"\n";
    return o.str();
}
static vector<string> split_tabs(const string &s) {
    vector<string> v; size_t p=0; for(;;){size_t q=s.find('\t',p);v.push_back(s.substr(p,q==string::npos?q:q-p));if(q==string::npos)break;p=q+1;}return v;
}
static Manifest parse_manifest(const fs::path &path) {
    std::ifstream f(path,std::ios::binary); require(bool(f), "Kan manifest niet lezen: " + path.string());
    string line; require(bool(std::getline(f,line))&&line=="CBACKUP\t1", "Ongeldig manifest: " + path.string());
    Manifest m; std::set<string> seen;
    while(std::getline(f,line)) {
        auto v=split_tabs(line); require(!v.empty(),"Lege regel");
        if(v[0]=="file") {
            require(v.size()==4, "Ongeldige bestandsregel");
            string key=unhex(v[1]); require(safe_relative(key) && key.rfind("source_",0)==0, "Onveilig bestandspad in manifest");
            uintmax_t size=0; try {size_t n=0;size=std::stoull(v[2],&n);require(n==v[2].size(),"Ongeldige grootte");}catch(...){fail("Ongeldige bestandsgrootte");}
            require(v[3].size()==64,"Ongeldige sha256");
            require(m.files.emplace(key,Item{size,v[3]}).second,"Dubbel pad in manifest");
        } else {
            require(v.size()==2,"Ongeldige manifest header");
            require(seen.insert(v[0]).second,"Dubbele manifest key");
            if(v[0]=="id")m.id=v[1]; else if(v[0]=="type")m.type=v[1]; else if(v[0]=="base")m.base=v[1];
            else if(v[0]=="parent")m.parent=v[1]; else if(v[0]=="fingerprint")m.fingerprint=v[1];
            else if(v[0]=="archive")m.archive=v[1]; else if(v[0]=="archive_hash")m.archive_hash=v[1];
            else fail("Onbekende manifest key: "+v[0]);
        }
    }
    require(f.eof(), "Leesfout manifest");
    require(!m.id.empty()&&valid_name(m.id)&& (m.type=="full"||m.type=="incremental"||m.type=="differential"), "Onjuist manifest type/id");
    require(!m.base.empty() && valid_name(m.base), "Ongeldige base");
    require(m.parent.empty()||valid_name(m.parent), "Ongeldige parent");
    require(m.archive.rfind("snap_",0)==0&&m.archive.find('/')==string::npos&&m.archive.find('\\')==string::npos, "Onveilige archiefnaam");
    require(m.archive_hash.size()==64&&m.fingerprint.size()==64, "Manifest mist SHA256");
    return m;
}
static fs::path job_dir(const Job &j) { return abs_path(j.dest)/j.name; }
static vector<Manifest> get_manifests(const Job &j) {
    vector<Manifest> out; fs::path dir=job_dir(j);
    if(!fs::exists(dir))return out;
    for(const auto &e:fs::directory_iterator(dir)) {
        if(e.is_regular_file() && e.path().filename().string().rfind("snap_",0)==0 && e.path().extension()==".manifest") {
            Manifest m=parse_manifest(e.path());
            require(e.path().filename()=="snap_"+m.id+".manifest","ID/manifestnaam komt niet overeen");
            out.push_back(std::move(m));
        }
    }
    std::sort(out.begin(),out.end(),[](const Manifest &a,const Manifest&b){return a.id<b.id;});
    for(size_t i=1;i<out.size();i++)require(out[i-1].id!=out[i].id,"Dubbel snapshot ID");
    return out;
}
static string new_id() {
    auto now=std::chrono::system_clock::now();
    std::time_t t=std::chrono::system_clock::to_time_t(now); std::tm tm{}; gmtime_r(&t,&tm);
    auto us=std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count()%1000000;
    std::ostringstream s; s<<std::put_time(&tm,"%Y%m%d_%H%M%S")<<"_"<<std::setw(6)<<std::setfill('0')<<us<<"_"<<getpid();
    return s.str();
}
static string ext_for(const string &compression) {
    return compression=="zstd"?".tar.zst":compression=="gzip"?".tar.gz":".tar";
}
struct ArchiveWriter {
    struct archive *a=nullptr;
    ArchiveWriter(const fs::path &p,const string &compression) {
        a=archive_write_new(); require(a,"archive_write_new mislukt");
        int rc=compression=="zstd"?archive_write_add_filter_zstd(a):compression=="gzip"?archive_write_add_filter_gzip(a):archive_write_add_filter_none(a);
        require(rc==ARCHIVE_OK,"Compressiefilter niet beschikbaar: "+string(archive_error_string(a)?archive_error_string(a):""));
        require(archive_write_set_format_pax_restricted(a)==ARCHIVE_OK,"Kan tar niet instellen");
        require(archive_write_open_filename(a,p.c_str())==ARCHIVE_OK,"Kan archief niet openen: "+string(archive_error_string(a)?archive_error_string(a):""));
    }
    void add(const string &logical,const File &file) {
        string name="files/"+logical; require(safe_relative(name), "Onveilig archiefpad");
        struct stat st{}; require(::stat(file.source.c_str(),&st)==0 && S_ISREG(st.st_mode),"Bronbestand verdwenen: "+file.source.string());
        require((uintmax_t)st.st_size==file.size,"Bron gewijzigd tijdens back-up: "+file.source.string());
        std::ifstream in(file.source,std::ios::binary); require(bool(in),"Bron onleesbaar: "+file.source.string());
        struct archive_entry *entry=archive_entry_new(); require(entry,"archive_entry_new mislukt");
        archive_entry_set_pathname(entry,name.c_str()); archive_entry_set_size(entry,(la_int64_t)file.size);
        archive_entry_set_filetype(entry,AE_IFREG); archive_entry_set_perm(entry,st.st_mode&0777);
        archive_entry_set_mtime(entry,st.st_mtime,0);
        int rc=archive_write_header(a,entry); archive_entry_free(entry);
        require(rc==ARCHIVE_OK,"Tar-header mislukt: "+string(archive_error_string(a)?archive_error_string(a):""));
        std::array<char,131072> buf{}; uintmax_t total=0;
        while(in.read(buf.data(),buf.size()) || in.gcount()) {
            size_t n=(size_t)in.gcount(); total+=n;
            la_ssize_t written=archive_write_data(a,buf.data(),n);
            require(written==(la_ssize_t)n,"Tar schrijven mislukt: "+string(archive_error_string(a)?archive_error_string(a):""));
        }
        require(!in.bad()&&total==file.size,"Bestand gewijzigd/leesfout tijdens archiveren: "+file.source.string());
        require(sha256_file(file.source)==file.hash,"Bron gewijzigd tijdens archiveren: "+file.source.string());
    }
    void finish() { require(archive_write_close(a)==ARCHIVE_OK,"Kan archief niet afsluiten"); archive_write_free(a); a=nullptr; }
    ~ArchiveWriter() { if(a){archive_write_close(a);archive_write_free(a);} }
};
// Controleer daadwerkelijk mountpoints, niet alleen het bestaan van een map:
// anders zou een afgekoppelde USB-schijf stilletjes naar de systeemschijf schrijven.
static string unescape_mount(const string &s) {
    string o; for(size_t i=0;i<s.size();++i) {
        if(s[i]=='\\'&&i+3<s.size()&&s[i+1]>='0'&&s[i+1]<='7'&&s[i+2]>='0'&&s[i+2]<='7'&&s[i+3]>='0'&&s[i+3]<='7') {
            int v=(s[i+1]-'0')*64+(s[i+2]-'0')*8+(s[i+3]-'0');o+=(char)v;i+=3;
        }else o+=s[i];
    }return o;
}
static void verify_external_mounted(const fs::path &target) {
    require(fs::exists(target),"Externe doelmap bestaat niet: "+target.string());
    fs::path canonical=fs::canonical(target);
    std::ifstream f("/proc/self/mountinfo"); require(bool(f),"Kan mountinfo niet lezen (Linux vereist)");
    string line; bool mounted=false;
    while(std::getline(f,line)) {
        std::istringstream in(line); string id,parent,dev,root,mount;
        if(!(in>>id>>parent>>dev>>root>>mount))continue;
        fs::path mp=unescape_mount(mount);
        if(mp!="/"&&under(canonical,mp))mounted=true;
    }
    require(mounted,"Externe opslag lijkt NIET aangekoppeld: "+target.string());
}
static void copy_checked(const fs::path &src, const fs::path &dest, bool check_sha) {
    fs::create_directories(dest.parent_path());
    fs::path temp=dest;temp += ".partial."+std::to_string(getpid());
    try {
        fs::copy_file(src,temp,fs::copy_options::overwrite_existing);
        if(check_sha)require(sha256_file(src)==sha256_file(temp),"Kopie beschadigd: "+dest.string());
        fs::rename(temp,dest);
    } catch (...) {std::error_code ec;fs::remove(temp,ec);throw;}
}
static void push_cloud_all(const Job &j);
static void replicate(const Job &j) {
    if(!j.usb.empty()) {
        verify_external_mounted(j.usb);
        fs::path dir=abs_path(j.usb)/j.name;
        for(const auto &m:get_manifests(j)) {
            fs::path arc=job_dir(j)/m.archive, man=job_dir(j)/("snap_"+m.id+".manifest");
            auto one=[&](const fs::path &src) {
                fs::path dst=dir/src.filename();
                bool same=fs::is_regular_file(dst) && fs::file_size(dst)==fs::file_size(src) && sha256_file(dst)==sha256_file(src);
                if(!same)copy_checked(src,dst,true);
            };
            // Manifest pas na archief kopiëren; bij uitval kan een volgende run hervatten.
            one(arc); one(man);
        }
        std::cout<<"USB-ketens bijgewerkt: "<<dir<<"\n";
    }
    if(!j.cloud.empty())push_cloud_all(j);
}
static void push_cloud_all(const Job &j) {
    require(!j.cloud.empty(),"Cloud is niet ingesteld");
    string base=j.cloud;while(!base.empty()&&base.back()=='/')base.pop_back();
    require(base.find(':')!=string::npos,"Cloud moet rclone remote:path zijn");
    string remote=base+"/"+j.name+"/";
    for(const auto &m:get_manifests(j)) {
        fs::path arc=job_dir(j)/m.archive, man=job_dir(j)/("snap_"+m.id+".manifest");
        require(fs::is_regular_file(arc),"Archief ontbreekt voor cloudupload: "+arc.string());
        require(exec_args({"rclone","copyto",arc.string(),remote+arc.filename().string()})==0,"Cloudupload archief mislukt: "+m.id);
        require(exec_args({"rclone","copyto",man.string(),remote+man.filename().string()})==0,"Cloudupload manifest mislukt: "+m.id);
    }
    std::cout<<"Cloudketen bijgewerkt: "<<remote<<"\n";
}
static void prune(const Job &j) {
    if(j.keep_chains==0)return;
    auto all=get_manifests(j);
    vector<string> bases;
    for(auto &m:all)if(m.type=="full")bases.push_back(m.id);
    if(bases.size()<=(size_t)j.keep_chains)return;
    string boundary=bases[bases.size()-(size_t)j.keep_chains];
    for(auto &m:all) {
        if(m.id>=boundary)continue;
        fs::remove(job_dir(j)/m.archive);
        fs::remove(job_dir(j)/("snap_"+m.id+".manifest"));
        std::cout<<"Oude lokale keten verwijderd: "<<m.id<<"\n";
    }
}
static void mirror_run(const Job &j) {
    FileMap files=scan(j);
    fs::path dest=job_dir(j)/"mirror", temp=job_dir(j)/("mirror.tmp."+std::to_string(getpid()));
    require(!fs::exists(temp),"Tijdelijke mirrormap bestaat al");
    try {
        fs::create_directories(temp);
        for(auto &kv:files) {
            fs::path out=temp/"files"/kv.first;fs::create_directories(out.parent_path());
            fs::copy_file(kv.second.source,out);
            require(sha256_file(out)==kv.second.hash,"Mirrorcontrole mislukt voor "+kv.first);
        }
        fs::path old=job_dir(j)/("mirror.old."+std::to_string(getpid()));
        if(fs::exists(dest))fs::rename(dest,old);
        try {fs::rename(temp,dest);} catch(...){if(fs::exists(old))fs::rename(old,dest);throw;}
        if(fs::exists(old))fs::remove_all(old);
    } catch(...) {std::error_code ec;fs::remove_all(temp,ec);throw;}
    std::cout<<"Mirror vernieuwd: "<<dest<<" ("<<files.size()<<" bestanden)\n";
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
        string base=j.cloud;while(!base.empty()&&base.back()=='/')base.pop_back();
        require(base.find(':')!=string::npos,"Cloud moet rclone remote:path zijn");
        // Alleen de eigen mirror-submap van de job; sync verwijdert daar oude bestanden.
        require(exec_args({"rclone","sync",dest.string(),base+"/"+j.name+"/mirror"})==0,"Cloud mirror-sync mislukt");
    }
}
static void make_backup(const Job &j, const string &override_mode, bool dryrun) {
    fs::path dir=job_dir(j); fs::create_directories(dir);
    if((override_mode.empty()?j.mode:override_mode)=="mirror") {
        if(dryrun) {std::cout<<"Mirror-dry-run: "<<scan(j).size()<<" geselecteerde bestanden.\n";return;}
        mirror_run(j);return;
    }
    FileMap files=scan(j); vector<Manifest> all=get_manifests(j);
    string fp=fingerprint(j);
    const Manifest *prev=all.empty()?nullptr:&all.back(), *full=nullptr;
    for(auto &m:all)if(m.type=="full")full=&m;
    string desired=override_mode.empty()?j.mode:override_mode;
    require(desired=="auto"||desired=="full"||desired=="incremental"||desired=="differential","Ongeldige back-upsoort");
    string type=desired;
    if(type=="auto") {
        int count=0;
        if(full)for(auto &m:all)if(m.id>=full->id)++count;
        type=(!prev||!full||prev->fingerprint!=fp||count>=j.full_every)?"full":"incremental";
    }
    if(type!="full"&&(!prev||!full||prev->fingerprint!=fp||full->fingerprint!=fp)) {
        std::cout<<"Geen bruikbare basis/filters gewijzigd: automatisch volledige back-up.\n";type="full";
    }
    const Manifest *compare=type=="full"?nullptr:type=="incremental"?prev:full;
    vector<string> changed;
    for(auto &kv:files) {
        auto it=compare?compare->files.find(kv.first):map<string,Item>::const_iterator{};
        if(!compare||it==compare->files.end()||it->second.hash!=kv.second.hash)changed.push_back(kv.first);
    }
    size_t deleted=0;
    if(prev)for(auto &kv:prev->files)if(!files.count(kv.first))++deleted;
    std::cout<<"Type: "<<type<<", bestanden: "<<files.size()<<", in archief: "<<changed.size()<<", verdwenen: "<<deleted<<"\n";
    if(files.empty())std::cout<<"WAARSCHUWING: geen bestanden geselecteerd.\n";
    if(dryrun)return;
    if(type!="full"&&changed.empty()&&deleted==0){
        std::cout<<"Geen wijzigingen; ontbrekende kopieën worden bijgewerkt.\n";
        replicate(j);return;
    }
    Manifest m;
    m.id=new_id();m.type=type;m.base=type=="full"?m.id:full->id;
    m.parent=prev?prev->id:"";m.fingerprint=fp;
    m.archive="snap_"+m.id+ext_for(j.compression);
    for(auto &kv:files)m.files.emplace(kv.first,Item{kv.second.size,kv.second.hash});
    fs::path archpath=dir/m.archive, manpath=dir/("snap_"+m.id+".manifest");
    require(!fs::exists(archpath) && !fs::exists(manpath), "Snapshotnaam bestaat al, probeer opnieuw");
    fs::path temp=archpath;temp += ".partial";
    try {
        {
            ArchiveWriter writer(temp,j.compression);
            for(auto &key:changed)writer.add(key,files.at(key));
            writer.finish();
        }
        fs::rename(temp,archpath);
        m.archive_hash=sha256_file(archpath);
        atomic_write(manpath,manifest_text(m));
    }catch(...) {std::error_code ec;fs::remove(temp,ec);fs::remove(archpath,ec);fs::remove(manpath,ec);throw;}
    std::cout<<"Snapshot lokaal opgeslagen: "<<m.id<<" ("<<fs::file_size(archpath)<<" bytes)\n";
    replicate(j);
    prune(j);
}
static vector<const Manifest*> chain_for(const vector<Manifest> &all,const string &id) {
    const Manifest *target=nullptr, *base=nullptr;
    for(auto &m:all)if(m.id==id)target=&m;
    require(target,"Snapshot niet gevonden: "+id);
    for(auto &m:all)if(m.id==target->base)base=&m;
    require(base && base->type=="full","Basis-back-up ontbreekt");
    // Controleer parent-verwijzingen over de volledige geschiedenis tussen full en doel.
    // Differentieel reset de herstelde staat naar full + diff; oudere incrementele
    // wijzigingen moeten daarna juist NIET opnieuw worden toegepast.
    string parent=base->id;
    vector<const Manifest*> chain{base};
    if(target->id==base->id)return chain;
    bool found=false;
    for(auto &m:all) {
        if(m.id<=base->id || m.id>target->id)continue;
        require(m.parent==parent,"Snapshotketen incompleet bij "+m.id);
        require(m.base==base->id,"Basis wisselt binnen herstelketen");
        if(m.type=="differential")chain={base,&m};
        else require(m.type=="incremental","Onverwachte volledige back-up in keten"), chain.push_back(&m);
        parent=m.id;
        if(m.id==target->id)found=true;
    }
    require(found,"Doelsnapshot niet in keten");
    return chain;
}
static void verify_chain(const Job &j,const vector<const Manifest*>&chain) {
    for(auto *m:chain) {
        fs::path f=job_dir(j)/m->archive;
        require(fs::is_regular_file(f),"Archief ontbreekt: "+f.string());
        require(sha256_file(f)==m->archive_hash,"Archief-SHA256 klopt niet: "+f.string());
    }
}
static void extract_archive(const fs::path &archivepath,const Manifest &m,const fs::path &stage) {
    struct archive *a=archive_read_new(); require(a,"archive_read_new mislukt");
    auto cleanup=[&]{archive_read_close(a);archive_read_free(a);};
    try {
        archive_read_support_filter_all(a);archive_read_support_format_tar(a);
        require(archive_read_open_filename(a,archivepath.c_str(),65536)==ARCHIVE_OK,"Kan tar niet lezen");
        struct archive_entry *e=nullptr;
        std::set<string> names;
        for(;;) {
            int rc=archive_read_next_header(a,&e);
            if(rc==ARCHIVE_EOF)break;
            require(rc==ARCHIVE_OK,"Fout in tar: "+string(archive_error_string(a)?archive_error_string(a):""));
            const char *raw=archive_entry_pathname(e);require(raw,"Tar-entry zonder pad");
            string name(raw); require(name.rfind("files/",0)==0&&safe_relative(name),"Onveilig tar-pad: "+name);
            string key=name.substr(6);
            require(names.insert(key).second,"Dubbele archief-entry");
            auto expected=m.files.find(key);require(expected!=m.files.end(),"Onverwachte archief-entry: "+name);
            require(archive_entry_filetype(e)==AE_IFREG && !archive_entry_hardlink(e)&&!archive_entry_symlink(e),"Geen gewoon bestand in tar");
            require(archive_entry_size(e)>=0 && (uintmax_t)archive_entry_size(e)==expected->second.size,"Bestandsgrootte klopt niet");
            fs::path output=stage/name;fs::create_directories(output.parent_path());
            std::ofstream out(output,std::ios::binary|std::ios::trunc);require(bool(out),"Herstelbestand kan niet worden geschreven: "+output.string());
            std::array<char,131072> buf{}; uintmax_t bytes=0;
            for(;;) {
                la_ssize_t n=archive_read_data(a,buf.data(),buf.size());
                require(n>=0,"Fout tijdens uitpakken");if(n==0)break;
                bytes+=(uintmax_t)n;require(bytes<=expected->second.size,"Tar-entry overschrijdt limiet");
                out.write(buf.data(),n);require(bool(out),"Schrijffout tijdens herstellen");
            }
            out.close();require(bytes==expected->second.size,"Tar-entry te kort");
            require(sha256_file(output)==expected->second.hash,"Herstelde bestands-SHA256 klopt niet: "+key);
            std::error_code ec;
            fs::permissions(output,(fs::perms)(archive_entry_perm(e)&0777),fs::perm_options::replace,ec);
        }
        cleanup();
    }catch(...){cleanup();throw;}
}
static void restore(const Job &j,const string &id,const fs::path &out) {
    auto all=get_manifests(j);auto chain=chain_for(all,id);verify_chain(j,chain);
    fs::path target=abs_path(out);
    require(!fs::exists(target),"Hersteldoel bestaat al; gebruik een NIEUWE map: "+target.string());
    fs::create_directories(target.parent_path());
    fs::path stage=target;stage += ".partial."+std::to_string(getpid());
    require(!fs::exists(stage),"Tijdelijke herstelmap bestaat al");
    try {
        fs::create_directories(stage);
        for(auto *m:chain)extract_archive(job_dir(j)/m->archive,*m,stage);
        const Manifest &final=*chain.back();
        fs::path files=stage/"files";
        if(fs::exists(files)) {
            vector<fs::path> remove;
            for(auto &e:fs::recursive_directory_iterator(files))if(e.is_regular_file()) {
                string key=e.path().lexically_relative(files).generic_string();
                if(!final.files.count(key))remove.push_back(e.path());
            }
            for(auto &p:remove)fs::remove(p);
        }
        for(auto &kv:final.files) {
            fs::path p=files/kv.first;
            require(fs::is_regular_file(p),"Bestand ontbreekt na herstel: "+kv.first);
            require(fs::file_size(p)==kv.second.size&&sha256_file(p)==kv.second.hash,"Integriteitsfout: "+kv.first);
        }
        fs::rename(stage,target);
        std::cout<<"Hersteld: "<<final.files.size()<<" bestanden onder "<<(target/"files").string()<<"/source_N/\n";
    } catch(...) {std::error_code ec;fs::remove_all(stage,ec);throw;}
}
static void pull_cloud(const Job &j) {
    require(!j.cloud.empty(),"Cloud is niet ingesteld");
    string base=j.cloud;while(!base.empty()&&base.back()=='/')base.pop_back();
    require(base.find(':')!=string::npos,"Cloud moet rclone remote:path zijn");
    fs::create_directories(job_dir(j));
    require(exec_args({"rclone","copy",base+"/"+j.name,job_dir(j).string()})==0,"Cloud-download mislukt");
    std::cout<<"Cloud-archieven opgehaald naar "<<job_dir(j)<<"\n";
}
static void list_snapshots(const Job &j) {
    auto all=get_manifests(j);
    if(all.empty())std::cout<<"Geen snapshots.\n";
    for(auto &m:all)std::cout<<m.id<<"  "<<std::left<<std::setw(13)<<m.type<<" bestanden="<<m.files.size()<<"  basis="<<m.base<<"\n";
}
static void schedule(const Job &j,const vector<string> &args) {
    require(args.size()>=4,"Gebruik: schedule JOB --every 10min | --daily HH:MM | --weekly Mon@HH:MM | off");
    fs::path unitdir=fs::path(home())/".config/systemd/user";
    fs::create_directories(unitdir);
    string base="cbackup-"+j.name;
    fs::path service=unitdir/(base+".service"),timer=unitdir/(base+".timer");
    if(args[3]=="off") {
        (void)exec_args({"systemctl","--user","disable","--now",base+".timer"});
        fs::remove(service);fs::remove(timer);
        require(exec_args({"systemctl","--user","daemon-reload"})==0,"systemctl daemon-reload mislukt");
        std::cout<<"Planning uitgeschakeld.\n";return;
    }
    require(args.size()==5,"Ongeldig aantal schedule-argumenten");
    string timerpart, val=args[4];
    if(args[3]=="--every") {
        require(!val.empty()&&val.size()<=16,"Ongeldige interval");
        size_t n=val.find_first_not_of("0123456789");
        require(n!=string::npos&&n>0&&positive_int(val.substr(0,n),1,100000)>0,"Ongeldige interval");
        require(val.substr(n)=="min"||val.substr(n)=="h"||val.substr(n)=="d","Gebruik 10min, 2h of 1d");
        timerpart="OnStartupSec=30s\nOnUnitInactiveSec="+val+"\n";
    } else {
        string day;
        if(args[3]=="--weekly") {
            size_t at=val.find('@'); require(at!=string::npos,"Gebruik Mon@02:00");
            day=val.substr(0,at);val=val.substr(at+1);
            require(day=="Mon"||day=="Tue"||day=="Wed"||day=="Thu"||day=="Fri"||day=="Sat"||day=="Sun","Gebruik Mon..Sun");
        }else require(args[3]=="--daily","Gebruik --every / --daily / --weekly / off");
        require(val.size()==5&&val[2]==':'&&std::isdigit((unsigned char)val[0])&&std::isdigit((unsigned char)val[1])&&std::isdigit((unsigned char)val[3])&&std::isdigit((unsigned char)val[4]),"Gebruik HH:MM");
        int hh=std::stoi(val.substr(0,2)),mm=std::stoi(val.substr(3,2));require(hh<24&&mm<60,"Tijd buiten bereik");
        timerpart="OnCalendar="+(day.empty()?"*-*-* ":day+" *-*-* ")+val+":00\nPersistent=true\n";
    }
    // Gebruik het absolute installatiepad; unitnaam en jobnaam zijn streng gevalideerd.
    fs::path exe=fs::read_symlink("/proc/self/exe");
    require(exe.string().find('\n')==string::npos&&exe.string().find(' ')==string::npos,"Executablepad met spaties wordt niet ondersteund voor timers");
    atomic_write(service,"[Unit]\nDescription=CBackup job "+j.name+"\n[Service]\nType=oneshot\nExecStart="+exe.string()+" run "+j.name+"\n");
    atomic_write(timer,"[Unit]\nDescription=CBackup schema "+j.name+"\n[Timer]\n"+timerpart+"Unit="+base+".service\n[Install]\nWantedBy=timers.target\n");
    require(exec_args({"systemctl","--user","daemon-reload"})==0,"systemctl --user is niet beschikbaar (gebruik een gebruikerssessie)");
    require(exec_args({"systemctl","--user","enable","--now",base+".timer"})==0,"Kon timer niet inschakelen");
    require(exec_args({"systemctl","--user","restart",base+".timer"})==0,"Timer niet herstart");
    std::cout<<"Timer actief: "<<base<<".timer\n";
}
static void help() {
    std::cout<<R"HELP(CBackup - Linux/Raspberry Pi (C++17)

  cbackup init NAAM
  cbackup config NAAM show
  cbackup config NAAM add-source PAD
  cbackup config NAAM remove-source PAD
  cbackup config NAAM add-ext cpp [hpp ...]
  cbackup config NAAM clear-ext
  cbackup config NAAM add-include 'project/*' [PATROON ...]
  cbackup config NAAM clear-include
  cbackup config NAAM add-exclude '*.tmp' [PATROON ...]
  cbackup config NAAM clear-exclude
  cbackup config NAAM set dest|usb|cloud|mode|compression|full_every|keep_chains WAARDE
  cbackup run NAAM [--type full|incremental|differential|mirror] [--dry-run]
  cbackup list NAAM
  cbackup verify NAAM ID
  cbackup restore NAAM ID --to NIEUWE_MAP
  cbackup cloud-pull NAAM
  cbackup cloud-push NAAM
  cbackup replicate NAAM
  cbackup schedule NAAM --every 10min
  cbackup schedule NAAM --daily 22:00
  cbackup schedule NAAM --weekly Sun@02:00
  cbackup schedule NAAM off

Back-uptypen: full, incremental, differential, auto, mirror.
Compressie: zstd, gzip, none. Cloud vereist 'rclone config'.
USB vereist een daadwerkelijk gemounte externe schijf.
Restore herstelt bestanden in NIEUWE_MAP/files/source_0/ etc.
)HELP";
}
static void config_cmd(const vector<string> &a) {
    require(a.size()>=4,"Gebruik: config NAAM ACTIE [waarden]");
    Job j=load_job(a[2]);string cmd=a[3];
    if(cmd=="show"){require(a.size()==4,"Te veel argumenten");show_job(j);return;}
    auto add=[&](vector<string>&vec,bool ext){
        require(a.size()>=5,"Geef minimaal één waarde op");
        for(size_t i=4;i<a.size();++i) {
            string v=ext?norm_ext(a[i]):a[i];no_newline(v);
            require(!v.empty(),"Lege waarde niet toegestaan");
            if(std::find(vec.begin(),vec.end(),v)==vec.end())vec.push_back(v);
        }
    };
    if(cmd=="add-source")add(j.sources,false);
    else if(cmd=="remove-source") {
        require(a.size()==5,"Gebruik remove-source PAD");
        auto it=std::find(j.sources.begin(),j.sources.end(),a[4]);
        require(it!=j.sources.end(),"Bron niet gevonden");j.sources.erase(it);
    } else if(cmd=="add-ext")add(j.exts,true);
    else if(cmd=="add-include")add(j.includes,false);
    else if(cmd=="add-exclude")add(j.excludes,false);
    else if(cmd=="clear-ext") {require(a.size()==4,"Te veel argumenten");j.exts.clear();}
    else if(cmd=="clear-include") {require(a.size()==4,"Te veel argumenten");j.includes.clear();}
    else if(cmd=="clear-exclude") {require(a.size()==4,"Te veel argumenten");j.excludes.clear();}
    else if(cmd=="set") {
        require(a.size()==6,"Gebruik set sleutel waarde");
        string k=a[4],v=a[5];no_newline(v);
        if(k=="dest") {require(!v.empty(),"dest mag niet leeg zijn");j.dest=abs_path(v).string();}
        else if(k=="usb") {j.usb=v.empty()?"":abs_path(v).string();}
        else if(k=="cloud") {require(v.empty()||v.find(':')!=string::npos,"Gebruik remote:path");j.cloud=v;}
        else if(k=="mode") {require(v=="auto"||v=="full"||v=="incremental"||v=="differential"||v=="mirror","Ongeldige mode");j.mode=v;}
        else if(k=="compression") {require(v=="zstd"||v=="gzip"||v=="none","Ongeldige compressie");j.compression=v;}
        else if(k=="full_every")j.full_every=positive_int(v,1,100000);
        else if(k=="keep_chains")j.keep_chains=positive_int(v,0,100000);
        else fail("Onbekende instelling: "+k);
    }else fail("Onbekend configcommando: "+cmd);
    save_job(j);std::cout<<"Configuratie opgeslagen: "<<j.name<<"\n";
}
int main(int argc,char **argv) {
    umask(0077); // nieuwe archieven, manifests, tijdelijke mappen standaard privé
    try {
        vector<string> a(argv,argv+argc);
        if(argc<2||a[1]=="--help"||a[1]=="help"){help();return 0;}
        string cmd=a[1];
        require(argc>=3,"Jobnaam ontbreekt");
        string name=a[2];require(valid_name(name),"Ongeldige jobnaam");
        if(cmd=="init") {
            require(argc==3,"Gebruik init NAAM");require(!fs::exists(job_file(name)),"Job bestaat al");
            Job j;j.name=name;j.dest=(fs::path(home())/"backups").string();save_job(j);
            std::cout<<"Job gemaakt: "<<name<<"; voeg bronnen toe via config add-source\n";
            return 0;
        }
        if(cmd=="config"){config_cmd(a);return 0;}
        Job j=load_job(name);
        if(cmd=="run") {
            string mode;bool dry=false;
            for(int i=3;i<argc;++i) {
                if(a[i]=="--dry-run")dry=true;
                else if(a[i]=="--type") {require(++i<argc,"Type ontbreekt");mode=a[i];}
                else fail("Onbekende optie: "+a[i]);
            }
            fs::create_directories(job_dir(j));
            fs::path lock=job_dir(j)/".lock";
            int fd=open(lock.c_str(),O_CREAT|O_RDWR,0600);require(fd>=0,"Kan lock niet openen");
            if(flock(fd,LOCK_EX|LOCK_NB)!=0) {close(fd);fail("Er draait al een back-up voor deze job");}
            try {make_backup(j,mode,dry);}catch(...){close(fd);throw;}
            close(fd);return 0;
        }
        if(cmd=="list") {require(argc==3,"Gebruik list NAAM");list_snapshots(j);return 0;}
        if(cmd=="verify") {
            require(argc==4,"Gebruik verify NAAM ID");
            auto all=get_manifests(j);auto chain=chain_for(all,a[3]);verify_chain(j,chain);
            std::cout<<"SHA256 van "<<chain.size()<<" archieven gecontroleerd; herstelketen compleet.\n";return 0;
        }
        if(cmd=="restore") {
            require(argc==6 && a[4]=="--to","Gebruik restore NAAM ID --to NIEUWE_MAP");
            restore(j,a[3],a[5]);return 0;
        }
        if(cmd=="cloud-pull") {require(argc==3,"Gebruik cloud-pull NAAM");pull_cloud(j);return 0;}
        if(cmd=="cloud-push") {require(argc==3,"Gebruik cloud-push NAAM");push_cloud_all(j);return 0;}
        if(cmd=="replicate") {require(argc==3,"Gebruik replicate NAAM");replicate(j);return 0;}
        if(cmd=="schedule") {schedule(j,a);return 0;}
        fail("Onbekend commando: "+cmd);
    } catch(const std::exception &e) {std::cerr<<"FOUT: "<<e.what()<<"\n";return 1;}
    return 0;
}

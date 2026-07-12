#include "Project/RuntimeDependencies.h"
#include "Core/Sha256.h"
#include "Core/TransactionalFileWriter.h"
#include "Project/CookManifest.h"
#include "Project/JsonMigrationRegistry.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <unordered_set>
#ifdef _WIN32
#include <Windows.h>
#endif

namespace fs=std::filesystem;
namespace {
void Err(std::string* e,std::string v){if(e)*e=std::move(v);}
std::string Lower(std::string v){for(char&c:v)c=char(std::tolower((unsigned char)c));return v;}
bool ValidArchitecture(const std::string& value)
{
    return value == "x64" || value == "arm64" || value == "universal";
}
std::string HostArchitecture()
{
#if defined(__APPLE__) && defined(__aarch64__)
    return "arm64";
#elif defined(__APPLE__) && defined(__x86_64__)
    return "x64";
#elif defined(_M_ARM64) || defined(__aarch64__)
    return "arm64";
#else
    return "x64";
#endif
}
}
bool RuntimeDependencyManifest::Save(const fs::path& path,std::string* error)const{
    try{nlohmann::json list=nlohmann::json::array();for(const auto&f:files)list.push_back({{"file",f.file},{"architecture",f.architecture},{"size",f.size},{"hash",f.hash}});TransactionalWriteOptions options;options.validator=[](const fs::path& candidate,std::string* validationError){RuntimeDependencyManifest ignored;return RuntimeDependencyManifest::Load(candidate,ignored,validationError);};return TransactionalFileWriter::WriteText(path,nlohmann::json{{"version",kVersion},{"files",list}}.dump(2)+"\n",options,error);}catch(const std::exception&e){Err(error,e.what());return false;}}
bool RuntimeDependencyManifest::Load(const fs::path& path,RuntimeDependencyManifest& result,std::string* error){
    try{std::ifstream in(path);if(!in){Err(error,"RuntimeDependencies.json is missing");return false;}nlohmann::json j;in>>j;JsonMigrationRegistry migrations("runtime dependency manifest",kVersion);if(!migrations.Migrate(j,error))return false;if(!j.contains("files")||!j["files"].is_array()){Err(error,"invalid runtime dependency manifest");return false;}RuntimeDependencyManifest m;std::unordered_set<std::string> names;Sha256::Digest d{};for(const auto&i:j["files"]){RuntimeDependencyEntry f{i.value("file",std::string{}),i.value("architecture",std::string{}),i.value("size",uint64_t{}),i.value("hash",std::string{})};if(f.file.empty()||fs::path(f.file).filename()!=fs::path(f.file)||!ValidArchitecture(f.architecture)||!Sha256::FromHex(f.hash,d)||!names.insert(Lower(f.file)).second){Err(error,"invalid runtime dependency entry");return false;}m.files.push_back(std::move(f));}result=std::move(m);return true;}catch(const std::exception&e){Err(error,e.what());return false;}}
bool RuntimeDependencyManifest::ValidateFiles(const fs::path& root,std::string* error)const{for(const auto&f:files){std::error_code ec;const fs::path p=root/f.file;if(!fs::is_regular_file(p,ec)||ec||fs::file_size(p,ec)!=f.size){Err(error,"runtime dependency missing or wrong size: "+f.file);return false;}std::string he;if(Sha256::HashFile(p,&he)!=f.hash||!he.empty()){Err(error,"runtime dependency hash mismatch: "+f.file);return false;}}return true;}
bool RuntimeDependencyManifest::ValidatePackage(const fs::path& packageRoot,std::string* error){
    if(error)error->clear();
    const fs::path manifestPath=packageRoot/kFileName;
    std::error_code ec;
    if(!fs::is_regular_file(manifestPath,ec)||ec)return true;
    CookManifest cookManifest;
    if(!CookManifest::Load(packageRoot/CookManifest::kFileName,cookManifest,error))return false;
    std::string hashError;
    const std::string actualHash=Sha256::HashFile(manifestPath,&hashError);
    if(!hashError.empty()){Err(error,hashError);return false;}
    if(actualHash!=cookManifest.runtimeDependenciesHash){
        Err(error,"RuntimeDependencies.json SHA-256 does not match Cook manifest");return false;
    }
    RuntimeDependencyManifest dependencies;
    return Load(manifestPath,dependencies,error)&&dependencies.ValidateFiles(packageRoot,error);
}

#ifdef _WIN32
namespace {
bool Imports(const fs::path& path,std::vector<std::string>& names,std::string* error){
    std::ifstream in(path,std::ios::binary);std::vector<uint8_t>b((std::istreambuf_iterator<char>(in)),{});if(b.size()<sizeof(IMAGE_DOS_HEADER)){Err(error,"invalid PE: "+path.string());return false;}auto dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(b.data());if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<=0||size_t(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS64)>b.size()){Err(error,"invalid PE header: "+path.string());return false;}auto nt=reinterpret_cast<const IMAGE_NT_HEADERS64*>(b.data()+dos->e_lfanew);if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC){Err(error,"runtime binary is not x64: "+path.string());return false;}auto rvaToOffset=[&](DWORD rva)->size_t{auto sec=IMAGE_FIRST_SECTION(nt);for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){DWORD span=(std::max)(sec[i].Misc.VirtualSize,sec[i].SizeOfRawData);if(rva>=sec[i].VirtualAddress&&rva<sec[i].VirtualAddress+span)return sec[i].PointerToRawData+(rva-sec[i].VirtualAddress);}return size_t(-1);};DWORD rva=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;if(!rva)return true;size_t off=rvaToOffset(rva);while(off!=size_t(-1)&&off+sizeof(IMAGE_IMPORT_DESCRIPTOR)<=b.size()){auto d=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(b.data()+off);if(!d->Name)break;size_t no=rvaToOffset(d->Name);if(no>=b.size()){Err(error,"invalid PE import table");return false;}const auto end=std::find(b.begin()+no,b.end(),uint8_t{0});if(end==b.end()){Err(error,"unterminated PE import name");return false;}names.emplace_back(reinterpret_cast<const char*>(b.data()+no),static_cast<size_t>(end-(b.begin()+no)));off+=sizeof(*d);}return true;}
bool SystemDll(const std::string& n){const std::string x=Lower(n);if(x.rfind("api-ms-",0)==0||x.rfind("ext-ms-",0)==0)return true;static const std::unordered_set<std::string>s={"kernel32.dll","user32.dll","gdi32.dll","advapi32.dll","shell32.dll","ole32.dll","oleaut32.dll","imm32.dll","winmm.dll","version.dll","setupapi.dll","ws2_32.dll","bcrypt.dll","ntdll.dll","dxgi.dll","d3d11.dll","d3d12.dll","d3dcompiler_47.dll","vulkan-1.dll","shlwapi.dll","comdlg32.dll","rpcrt4.dll","secur32.dll","crypt32.dll","cfgmgr32.dll","hid.dll","powrprof.dll","uxtheme.dll","dwmapi.dll","dbghelp.dll","psapi.dll","wintrust.dll","normaliz.dll","msimg32.dll","ucrtbased.dll","vcruntime140d.dll","vcruntime140_1d.dll","msvcp140d.dll","concrt140d.dll"};return s.count(x)>0;}
fs::path FindInTree(const fs::path& root,const std::string& name) {
    std::error_code ec;
    std::vector<fs::path> matches;
    if(!fs::is_directory(root,ec)||ec) return {};
    for(fs::recursive_directory_iterator it(root,fs::directory_options::skip_permission_denied,ec),end;
        it!=end&&!ec;it.increment(ec)) {
        if(!it->is_regular_file(ec)||ec||Lower(it->path().filename().string())!=Lower(name)) continue;
        const std::string path=Lower(it->path().generic_string());
        if(path.find("/x64/")!=std::string::npos && path.find("/onecore/")==std::string::npos &&
           (path.find("/vc/redist/msvc/")!=std::string::npos ||
            Lower(root.generic_string()).find("redist")!=std::string::npos))
            matches.push_back(it->path());
    }
    std::sort(matches.begin(),matches.end(),std::greater<fs::path>());
    return matches.empty()?fs::path{}:matches.front();
}
fs::path FindDll(const std::string& name,const fs::path& binaries){
    std::error_code ec;
    if(fs::is_regular_file(binaries/name,ec)&&!ec)return fs::absolute(binaries/name);
    if(const char* redist=std::getenv("VCToolsRedistDir"))
        if(auto found=FindInTree(redist,name);!found.empty())return found;
    if(const char* vc=std::getenv("VCINSTALLDIR"))
        if(auto found=FindInTree(fs::path(vc)/"Redist/MSVC",name);!found.empty())return found;
    for(const char* variable:{"ProgramFiles","ProgramFiles(x86)"})
        if(const char* base=std::getenv(variable))
            if(auto found=FindInTree(fs::path(base)/"Microsoft Visual Studio",name);!found.empty())return found;
    return{};
}
}
#endif
bool WindowsRuntimeDependencyCollector::Collect(const fs::path& binaries,const fs::path& staging,RuntimeDependencyManifest& manifest,const std::vector<std::string>& executableNames,std::string* error){
#ifndef _WIN32
    Err(error,"Windows runtime dependency collection is unavailable");return false;
#else
    manifest={};std::vector<fs::path> queue;queue.reserve(executableNames.size()+2);for(const auto& executableName:executableNames)queue.push_back(binaries/executableName);queue.push_back(binaries/"runtime.dll");queue.push_back(binaries/"SDL3.dll");std::unordered_set<std::string> done;
    for(size_t i=0;i<queue.size();++i){fs::path src=queue[i];std::string key=Lower(src.filename().string());if(!done.insert(key).second)continue;if(!fs::is_regular_file(src)){src=FindDll(src.filename().string(),binaries);if(src.empty()){Err(error,"runtime dependency cannot be resolved: "+key);return false;}}std::vector<std::string> imports;if(!Imports(src,imports,error))return false;for(const auto& name:imports)if(!SystemDll(name)&&!done.count(Lower(name))){fs::path found=FindDll(name,binaries);if(found.empty()){Err(error,"transitive runtime dependency cannot be resolved: "+name+" imported by "+src.filename().string());return false;}queue.push_back(found);}fs::path dst=staging/src.filename();std::error_code ec;fs::copy_file(src,dst,fs::copy_options::overwrite_existing,ec);if(ec){Err(error,"failed to copy runtime dependency: "+src.string());return false;}std::string he;std::string hash=Sha256::HashFile(dst,&he);if(!he.empty()){Err(error,he);return false;}manifest.files.push_back({dst.filename().string(),"x64",fs::file_size(dst),hash});}
    std::sort(manifest.files.begin(),manifest.files.end(),[](const auto&a,const auto&b){return Lower(a.file)<Lower(b.file);});return true;
#endif
}

bool WindowsRuntimeDependencyCollector::Collect(const fs::path& binaries,const fs::path& staging,RuntimeDependencyManifest& manifest,const std::string& executableName,std::string* error){
    return Collect(binaries, staging, manifest, std::vector<std::string>{executableName}, error);
}

bool WindowsRuntimeDependencyCollector::Collect(const fs::path& binaries,const fs::path& staging,RuntimeDependencyManifest& manifest,std::string* error){
    return Collect(binaries, staging, manifest, "MyEnginePlayer.exe", error);
}

bool HostRuntimeDependencyCollector::Collect(const fs::path& binaries,
                                             const fs::path& staging,
                                             RuntimeDependencyManifest& manifest,
                                             const std::vector<std::string>& fileNames,
                                             std::string* error)
{
    manifest = {};
    const std::string architecture = HostArchitecture();
    std::unordered_set<std::string> copied;
    for (const auto& fileName : fileNames) {
        if (!copied.insert(Lower(fileName)).second) continue;
        const fs::path source = binaries / fileName;
        std::error_code ec;
        if (!fs::is_regular_file(source, ec) || ec) {
            Err(error, "required runtime file is missing: " + source.string());
            return false;
        }
        const fs::path destination = staging / fileName;
        fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            Err(error, "failed to copy runtime dependency: " + source.string());
            return false;
        }
        std::string hashError;
        const std::string hash = Sha256::HashFile(destination, &hashError);
        if (!hashError.empty()) {
            Err(error, hashError);
            return false;
        }
        const auto size = fs::file_size(destination, ec);
        if (ec) {
            Err(error, "failed to stat runtime dependency: " + destination.string());
            return false;
        }
        manifest.files.push_back({destination.filename().string(),
                                  architecture,
                                  size,
                                  hash});
    }
    std::sort(manifest.files.begin(), manifest.files.end(),
              [](const auto& a, const auto& b) { return Lower(a.file) < Lower(b.file); });
    return true;
}

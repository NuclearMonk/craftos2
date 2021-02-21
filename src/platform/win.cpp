/*
 * platform/win.cpp
 * CraftOS-PC 2
 * 
 * This file implements functions specific to Windows.
 * 
 * This code is licensed under the MIT license.
 * Copyright (c) 2019-2021 JackMacWindows.
 */

#ifdef _WIN32
//#include <Windows.h>
#include <vector>
#include <string>
#include <fstream>
#include <cstring>
#include <codecvt>
#include <Poco/SHA2Engine.h>
#include <Poco/URI.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/HTTPSClientSession.h>
#include <processenv.h>
#include <Shlwapi.h>
#include <dirent.h>
#include <SDL2/SDL_syswm.h>
#include <sys/stat.h>
#include <zlib.h>
#include "../platform.hpp"
#include "../util.hpp"

const wchar_t * base_path = L"%appdata%\\CraftOS-PC";
std::wstring base_path_expanded;
std::wstring rom_path_expanded;
wchar_t expand_tmp[32767];
static std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

path_t wstr(std::string str) {
    try {return converter.from_bytes(str);}
    catch (std::exception &e) {return L"";}
}

std::string astr(path_t str) {
    return converter.to_bytes(str);
}

FILE* platform_fopen(const wchar_t* path, const char * mode) { return _wfopen(path, wstr(mode).c_str()); }

void setBasePath(const char * path) {
    base_path_expanded = wstr(path);
}

void setROMPath(const char * path) {
    rom_path_expanded = wstr(path);
}

std::wstring getBasePath() {
    if (!base_path_expanded.empty()) return base_path_expanded;
    ExpandEnvironmentStringsW(base_path, expand_tmp, 32767);
    base_path_expanded = expand_tmp;
    return base_path_expanded;
}

std::wstring getROMPath() {
    if (!rom_path_expanded.empty()) return rom_path_expanded;
    GetModuleFileNameW(NULL, expand_tmp, 32767);
    rom_path_expanded = expand_tmp;
    rom_path_expanded = rom_path_expanded.substr(0, rom_path_expanded.find_last_of('\\'));
    return rom_path_expanded;
}

std::wstring getPlugInPath() { return getROMPath() + L"\\plugins\\"; }

std::wstring getMCSavePath() {
    ExpandEnvironmentStringsW(L"%appdata%\\.minecraft\\saves\\", expand_tmp, 32767);
    return std::wstring(expand_tmp);
}

void* kernel32handle = NULL;
HRESULT(*_SetThreadDescription)(HANDLE, PCWSTR) = NULL;

void setThreadName(std::thread &t, const std::string& name) {
    if (kernel32handle == NULL) {
        kernel32handle = SDL_LoadObject("kernel32");
        _SetThreadDescription = (HRESULT(*)(HANDLE, PCWSTR))SDL_LoadFunction(kernel32handle, "SetThreadDescription");
    }
    if (_SetThreadDescription != NULL) _SetThreadDescription((HANDLE)t.native_handle(), std::wstring(name.begin(), name.end()).c_str());
}

int createDirectory(const std::wstring& path) {
    struct_stat st;
    if (platform_stat(path.c_str(), &st) == 0) return !S_ISDIR(st.st_mode);
    if (CreateDirectoryExW(path.substr(0, path.find_last_of('\\', path.size() - 2)).c_str(), path.c_str(), NULL) == 0) {
        if ((GetLastError() == ERROR_PATH_NOT_FOUND || GetLastError() == ERROR_FILE_NOT_FOUND) && path != L"\\" && !path.empty()) {
            if (createDirectory(path.substr(0, path.find_last_of('\\', path.size() - 2)))) return 1;
            CreateDirectoryExW(path.substr(0, path.find_last_of('\\', path.size() - 2)).c_str(), path.c_str(), NULL);
        }
        else if (GetLastError() != ERROR_ALREADY_EXISTS) return 1;
    }
    return 0;
}

char* basename(char* path) {
    char* filename = strrchr(path, '/');
    if (filename == NULL)
        filename = path;
    else
        filename++;
    return filename;
}

char* dirname(char* path) {
    if (path[0] == '/') strcpy(path, &path[1]);
    char tch;
    if (strrchr(path, '/') != NULL) tch = '/';
    else if (strrchr(path, '\\') != NULL) tch = '\\';
    else return path;
    path[strrchr(path, tch) - path] = '\0';
    return path;
}

unsigned long long getFreeSpace(const std::wstring& path) {
    ULARGE_INTEGER retval;
    if (GetDiskFreeSpaceExW(path.substr(0, path.find_last_of('\\', path.size() - 2)).c_str(), &retval, NULL, NULL) == 0) {
        if (path.find_last_of('\\') == std::string::npos || path.substr(0, path.find_last_of('\\')-1).empty()) return 0;
        else return getFreeSpace(path.substr(0, path.find_last_of('\\')-1));
    }
    return retval.QuadPart;
}

unsigned long long getCapacity(const std::wstring& path) {
    ULARGE_INTEGER retval;
    if (GetDiskFreeSpaceExW(path.substr(0, path.find_last_of('\\', path.size() - 2)).c_str(), NULL, &retval, NULL) == 0) {
        if (path.find_last_of('\\') == std::string::npos || path.substr(0, path.find_last_of('\\')-1).empty()) return 0;
        else return getCapacity(path.substr(0, path.find_last_of('\\')-1));
    }
    return retval.QuadPart;
}

int removeDirectory(const std::wstring& path) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return GetLastError();
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        WIN32_FIND_DATAW find;
        std::wstring s = path;
        if (path[path.size() - 1] != '\\') s += L"\\";
        s += L"*";
        const HANDLE h = FindFirstFileW(s.c_str(), &find);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(find.cFileName[0] == '.' && (wcslen(find.cFileName) == 1 || (find.cFileName[1] == '.' && wcslen(find.cFileName) == 2)))) {
                    std::wstring newpath = path;
                    if (path[path.size() - 1] != '\\') newpath += L"\\";
                    newpath += find.cFileName;
                    const int res = removeDirectory(newpath);
                    if (res) {
                        FindClose(h);
                        return res;
                    }
                }
            } while (FindNextFileW(h, &find));
            FindClose(h);
        }
        return RemoveDirectoryW(path.c_str()) ? 0 : (int)GetLastError();
    } else return DeleteFileW(path.c_str()) ? 0 : (int)GetLastError();
}

void updateNow(const std::string& tagname) {
    HTTPDownload("https://github.com/MCJack123/craftos2/releases/download/" + tagname + "/sha256-hashes.txt", [tagname](std::istream * shain, Poco::Exception * e){
        if (e != NULL) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Update Error", std::string("An error occurred while downloading the update: " + e->displayText()).c_str(), NULL);
            return;
        }
        std::string line;
        bool found = false;
        while (!shain->eof()) {
            std::getline(*shain, line);
            if (line.find("CraftOS-PC-Setup.exe") != std::string::npos) {found = true; break;}
        }
        if (!found) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Update Error", "A file required for verification could not be downloaded sucessfully. Please download the installer manually.", NULL);
            return;
        }
        std::string hash = line.substr(0, 64);
        HTTPDownload("https://github.com/MCJack123/craftos2/releases/download/" + tagname + "/CraftOS-PC-Setup.exe", [hash](std::istream * in, Poco::Exception * e) {
            if (e != NULL) {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Update Error", std::string("An error occurred while downloading the update: " + e->displayText()).c_str(), NULL);
                return;
            }
            std::stringstream ss;
            ss << in->rdbuf();
            std::string data = ss.str();
            Poco::SHA2Engine engine;
            engine.update(data);
            std::string myhash = Poco::SHA2Engine::digestToHex(engine.digest());
            if (hash != myhash) {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Update Error", "The installer file could not be verified. Please try again later. If this issue persists, please download the installer manually.", NULL);
                return;
            }
            char str[261];
            GetTempPathA(261, str);
            const std::string path = std::string(str) + "\\setup.exe";
            std::ofstream out(path, std::ios::binary);
            out << data;
            out.close();
            STARTUPINFOA info;
            memset(&info, 0, sizeof(info));
            info.cb = sizeof(info);
            PROCESS_INFORMATION process;
            //CreateProcessA(path.c_str(), (char*)(path + " /SILENT").c_str(), NULL, NULL, FALSE, 0, NULL, NULL, &info, &process);
            //CloseHandle(process.hProcess);
            //CloseHandle(process.hThread);
            exit(0);
        });
    });
}

std::vector<std::wstring> failedCopy;

static int recursiveMove(const std::wstring& path, const std::wstring& toPath) {
    const DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return GetLastError();
    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        if (CreateDirectoryExW(toPath.substr(0, toPath.find_last_of('\\', toPath.size() - 2)).c_str(), toPath.c_str(), NULL) == 0) return GetLastError();
        WIN32_FIND_DATAW find;
        std::wstring s = path;
        if (path[path.size() - 1] != '\\') s += L"\\";
        s += L"*";
        const HANDLE h = FindFirstFileW(s.c_str(), &find);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(find.cFileName[0] == '.' && (wcslen(find.cFileName) == 1 || (find.cFileName[1] == '.' && wcslen(find.cFileName) == 2)))) {
                    std::wstring newpath = path;
                    if (path[path.size() - 1] != '\\') newpath += L"\\";
                    newpath += find.cFileName;
                    const int res = recursiveMove(newpath, toPath + L"\\" + std::wstring(find.cFileName));
                    if (res) failedCopy.push_back(toPath + L"\\" + std::wstring(find.cFileName));
                }
            } while (FindNextFileW(h, &find));
            FindClose(h);
        }
        return RemoveDirectoryW(path.c_str()) ? 0 : (int)GetLastError();
    } else return MoveFileW(path.c_str(), toPath.c_str()) ? 0 : (int)GetLastError();
}

void migrateOldData() {
    ExpandEnvironmentStringsW(L"%USERPROFILE%\\.craftos", expand_tmp, 32767);
    const std::wstring oldpath = expand_tmp;
    struct_stat st;
    if (platform_stat(oldpath.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && platform_stat(getBasePath().c_str(), &st) != 0)
        recursiveMove(oldpath, getBasePath());
    if (!failedCopy.empty())
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Migration Failure", "Some files were unable to be moved while migrating the user data directory. These files have been left in place, and they will not appear inside the computer. You can copy them over from the old directory manually.", NULL);
}

void copyImage(SDL_Surface* surf) {
    char * bmp = new char[surf->w*surf->h*surf->format->BytesPerPixel + 128];
    SDL_RWops * rw = SDL_RWFromMem(bmp, surf->w*surf->h*surf->format->BytesPerPixel + 128);
    SDL_SaveBMP_RW(surf, rw, false);
    const HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, rw->seek(rw, 0, RW_SEEK_CUR) - sizeof(BITMAPFILEHEADER));
    if (hMem == NULL) { delete[] bmp; return; }
    memcpy(GlobalLock(hMem), bmp + sizeof(BITMAPFILEHEADER), rw->seek(rw, 0, RW_SEEK_CUR) - sizeof(BITMAPFILEHEADER));
    GlobalUnlock(hMem);
    OpenClipboard(0);
    EmptyClipboard();
    SetClipboardData(CF_DIB, hMem);
    CloseClipboard();
    delete[] bmp;
}

LONG WINAPI exceptionHandler(PEXCEPTION_POINTERS pExceptionInfo) {
    if (!loadingPlugin.empty()) MessageBoxA(NULL, std::string("Uh oh, CraftOS-PC has crashed! It appears the plugin \"" + loadingPlugin + "\" may have been responsible for this. Please remove it and try again. CraftOS-PC will now close.").c_str(), "Application Error", MB_OK | MB_ICONSTOP);
    else if (config.snooperEnabled) MessageBoxA(NULL, std::string("Uh oh, CraftOS-PC has crashed! A crash log has been saved and will be uploaded on next launch. CraftOS-PC will now close.").c_str(), "Application Error", MB_OK | MB_ICONSTOP);
    else MessageBoxA(NULL, std::string("Uh oh, CraftOS-PC has crashed! Please report this to https://www.craftos-pc.cc/bugreport. When writing the report, attach the latest CraftOS-PC.exe .dmp file located here (you can type this into the File Explorer): '%LOCALAPPDATA%\\CrashDumps'. Add this text to the report as well: \"Last C function: " + std::string(lastCFunction) + "\". CraftOS-PC will now close.").c_str(), "Application Error", MB_OK | MB_ICONSTOP);
    return EXCEPTION_CONTINUE_SEARCH;
}

// Do nothing. We definitely don't want to crash when there's only an invalid parameter, and I assume functions affected will return some value that won't cause problems. (I know strftime, used in os.date, will be fine.)
void invalidParameterHandler(const wchar_t * expression, const wchar_t * function, const wchar_t * file, unsigned int line, uintptr_t pReserved) {}

#ifdef CRASHREPORT_API_KEY
#include "../apikey.cpp" // if you get an error here, please go into Project Properties => C/C++ => Preprocessor => Preprocessor Defines and remove "CRASHREPORT_API_KEY" from the list

static void pushCrashDump(const char * data, const size_t size, const path_t& path, const std::string& url = "https://www.craftos-pc.cc/api/uploadCrashDump", const std::string& method = "POST") {
    Poco::URI uri(url);
    Poco::Net::HTTPSClientSession session(uri.getHost(), uri.getPort(), new Poco::Net::Context(Poco::Net::Context::CLIENT_USE, "", Poco::Net::Context::VERIFY_NONE, 9, true, "ALL:!ADH:!LOW:!EXP:!MD5:@STRENGTH"));
    if (!config.http_proxy_server.empty()) session.setProxy(config.http_proxy_server, config.http_proxy_port);
    Poco::Net::HTTPRequest request(method, uri.getPathAndQuery(), Poco::Net::HTTPMessage::HTTP_1_1);
    Poco::Net::HTTPResponse response;
    session.setTimeout(Poco::Timespan(5000000));
    request.add("User-Agent", "CraftOS-PC/" CRAFTOSPC_VERSION " ComputerCraft/" CRAFTOSPC_CC_VERSION);
    request.add("X-API-Key", getAPIKey());
    request.add("x-amz-server-side-encryption", "AES256");
    request.setContentType("application/gzip");
    request.setContentLength(size);
    try {
        session.sendRequest(request).write(data, size);
        std::istream& stream = session.receiveResponse(response);
        if (response.getStatus() / 100 == 3 && response.has("Location")) 
            return pushCrashDump(data, size, path, response.get("Location"));
        else if (response.getStatus() == 200 && method == "POST") {
            Value root;
            Poco::JSON::Object::Ptr p = root.parse(stream);
            if (root.isMember("uploadURL")) {
                return pushCrashDump(data, size, path, root["uploadURL"].asString(), "PUT");
            } else if (root.isMember("error")) {
                fprintf(stderr, "Warning: Couldn't upload crash dump at %s: %s\n", astr(path).c_str(), root["error"].asString().c_str());
            } else if (root.isMember("message")) {
                fprintf(stderr, "Warning: Couldn't upload crash dump at %s: %s\n", astr(path).c_str(), root["message"].asString().c_str());
            }
        }
    } catch (Poco::Exception &e) {
        fprintf(stderr, "Warning: Couldn't upload crash dump at %s: %s\n", astr(path).c_str(), e.displayText().c_str());
    }
}
#endif

// We're relying on WER to automatically generate a minidump here.
// If this is an official build with an API key, we'll automatically upload the dump on next start
void setupCrashHandler() {
    SetUnhandledExceptionFilter(exceptionHandler);
    _set_invalid_parameter_handler(invalidParameterHandler);
}

void uploadCrashDumps() {
#ifdef CRASHREPORT_API_KEY
    if (config.snooperEnabled) {
        WIN32_FIND_DATAW find;
        wchar_t path[32767];
        ExpandEnvironmentStringsW(L"%LOCALAPPDATA%\\CrashDumps\\", path, 32767);
        std::wstring searchpath = std::wstring(path) + L"CraftOS-PC.exe.*.dmp";
        const HANDLE h = FindFirstFileW(searchpath.c_str(), &find);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::wstring newpath = std::wstring(path) + find.cFileName;
                std::stringstream ss;
                FILE * source = platform_fopen(newpath.c_str(), "rb");

                int ret, flush;
                unsigned have;
                z_stream strm;
                unsigned char in[16384];
                unsigned char out[16384];

                /* allocate deflate state */
                strm.zalloc = Z_NULL;
                strm.zfree = Z_NULL;
                strm.opaque = Z_NULL;
                ret = deflateInit2(&strm, 7, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY);
                if (ret != Z_OK) {
                    fclose(source);
                    continue;
                }

                /* compress until end of file */
                do {
                    strm.avail_in = fread(in, 1, 16384, source);
                    if (ferror(source)) {
                        (void)deflateEnd(&strm);
                        fclose(source);
                        continue;
                    }
                    flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
                    strm.next_in = in;

                    /* run deflate() on input until output buffer not full, finish
                    compression if all of source has been read in */
                    do {
                        strm.avail_out = 16384;
                        strm.next_out = out;
                        ret = deflate(&strm, flush);    /* no bad return value */
                        assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
                        have = 16384 - strm.avail_out;
                        ss.write((const char*)out, have);
                    } while (strm.avail_out == 0);

                    /* done when last data in file processed */
                } while (flush != Z_FINISH);

                /* clean up and return */
                (void)deflateEnd(&strm);
                fclose(source);
                std::string data = ss.str();
                pushCrashDump(data.c_str(), data.size(), newpath);
                DeleteFileW(newpath.c_str());
            } while (FindNextFileW(h, &find));
            FindClose(h);
        }
    }
#endif
}

void setFloating(SDL_Window* win, bool state) {
    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    SDL_GetWindowWMInfo(win, &info);
    if (info.subsystem != SDL_SYSWM_WINDOWS) return; // should always be true
    SetWindowPos(info.info.win.window, state ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

#endif
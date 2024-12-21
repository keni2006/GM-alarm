#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <sstream>
#include <string>
#include <algorithm>
#include <commctrl.h>
#include <set>

#define ID_LISTBOX 101
#define ID_BUTTON 103
#define ID_RESULT 104
#define TH32CS_PROCESS 0x00000002
#define WM_USER_UPDATE 1001
#define MAX_LINES 20

std::vector<BYTE> stringToBytes(const std::wstring& str) {
    std::vector<BYTE> bytes;
    bytes.resize(str.length() * sizeof(wchar_t));
    memcpy(bytes.data(), str.c_str(), str.length() * sizeof(wchar_t));
    return bytes;
}

std::vector<BYTE> hexStringToBytes(const std::string& hex) {
    std::vector<BYTE> bytes;
    if (hex.length() % 2 != 0) {
        return bytes;
    }
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteStr = hex.substr(i, 2);
        try {
            bytes.push_back(std::stoul(byteStr, nullptr, 16));
        }
        catch (const std::invalid_argument& e) {
            return bytes;
        }
        catch (const std::out_of_range& e) {
            return bytes;
        }
    }
    return bytes;
}

void UpdateLog(HWND hResult, const std::wstring& newEntry, std::set<std::wstring>& uniqueEntries) {
    if (uniqueEntries.insert(newEntry).second) {
        int length = GetWindowTextLength(hResult);
        std::wstring currentText(length, L'\0');
        GetWindowText(hResult, &currentText[0], length + 1);

        std::vector<std::wstring> lines;
        std::wistringstream stream(currentText);
        std::wstring line;
        while (std::getline(stream, line)) {
            lines.push_back(line);
        }

        if (lines.size() >= MAX_LINES) {
            lines.erase(lines.begin(), lines.begin() + (lines.size() - MAX_LINES + 1));
        }

        lines.push_back(newEntry);

        std::wstring updatedText;
        for (const auto& l : lines) {
            updatedText += l + L"\r\n";
        }
        SetWindowTextW(hResult, updatedText.c_str());
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    static HWND hListBox, hButton, hResult;
    static std::vector<std::pair<std::wstring, DWORD>> processes;
    static DWORD processId = 0;
    static std::set<std::wstring> uniqueEntries;

    switch (uMsg) {
    case WM_CREATE:
        hListBox = CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_SORT | WS_VSCROLL, 10, 10, 200, 200, hwnd, (HMENU)ID_LISTBOX, NULL, NULL);
        hButton = CreateWindowW(L"BUTTON", L"Scan", WS_CHILD | WS_VISIBLE, 220, 40, 100, 30, hwnd, (HMENU)ID_BUTTON, NULL, NULL);
        hResult = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL, 10, 220, 310, 360, hwnd, (HMENU)ID_RESULT, NULL, NULL);

        HANDLE hProcessSnap;
        PROCESSENTRY32 pe32;
        hProcessSnap = CreateToolhelp32Snapshot(TH32CS_PROCESS, 0);
        if (hProcessSnap == INVALID_HANDLE_VALUE) return 0;

        pe32.dwSize = sizeof(PROCESSENTRY32);

        if (Process32First(hProcessSnap, &pe32)) {
            do {
                std::wstring processName = pe32.szExeFile;
                processes.emplace_back(processName, pe32.th32ProcessID);
            } while (Process32Next(hProcessSnap, &pe32));
        }
        CloseHandle(hProcessSnap);

        std::sort(processes.begin(), processes.end(), [](const auto& a, const auto& b) {
            return a.first < b.first;
            });

        for (const auto& process : processes) {
            SendMessage(hListBox, LB_ADDSTRING, 0, (LPARAM)process.first.c_str());
            SendMessage(hListBox, LB_SETITEMDATA, SendMessage(hListBox, LB_GETCOUNT, 0, 0) - 1, process.second);
        }

        SetTimer(hwnd, 1, 2000, NULL);

        return 0;

    case WM_TIMER:
        if (processId != 0) {
            CreateThread(NULL, 0, [](LPVOID param) -> DWORD {
                DWORD pid = *(DWORD*)param;
                std::wstring textToFind = L"You see:";
                std::vector<BYTE> searchBytes = stringToBytes(textToFind);

                HANDLE hProcess = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
                if (hProcess) {
                    SIZE_T bytesRead;
                    BYTE* buffer = new BYTE[1024 * 1024];
                    SYSTEM_INFO sysInfo;
                    GetSystemInfo(&sysInfo);
                    LPVOID baseAddress = sysInfo.lpMinimumApplicationAddress;
                    LPVOID maxAddress = sysInfo.lpMaximumApplicationAddress;

                    for (LPVOID address = baseAddress; address < maxAddress; address = (LPVOID)((BYTE*)address + 1024 * 1024)) {
                        if (ReadProcessMemory(hProcess, address, buffer, 1024 * 1024, &bytesRead)) {
                            size_t pos = 0;
                            while ((pos = std::search(buffer, buffer + bytesRead, searchBytes.begin(), searchBytes.end()) - buffer) < bytesRead) {
                                std::wstring foundName(reinterpret_cast<wchar_t*>(buffer + pos + searchBytes.size() / sizeof(BYTE)), 70);
                                foundName = foundName.substr(0, foundName.find(L'\0'));

                                foundName.erase(std::remove_if(foundName.begin(), foundName.end(), [](wchar_t c) {
                                    return c < 32 || c > 126;
                                    }), foundName.end());

                                if (!foundName.empty()) {
                                    std::wstring output = L"You see: " + foundName;

                                    UpdateLog(hResult, output, uniqueEntries);

                                    if (foundName.find(L"GM") != std::wstring::npos) {
                                        MessageBeep(MB_ICONWARNING);
                                        UpdateLog(hResult, L"Character with GM found: " + foundName, uniqueEntries);
                                    }
                                    if (foundName.find(L"LittleFOOT") != std::wstring::npos) {
                                        MessageBeep(MB_ICONWARNING);
                                        UpdateLog(hResult, L"Vendor found: LittleFOOT", uniqueEntries);
                                    }
                                }
                                pos += searchBytes.size() / sizeof(BYTE);
                            }
                        }
                        Sleep(5);
                    }

                    delete[] buffer;
                    CloseHandle(hProcess);
                }
                return 0;
                }, &processId, 0, NULL);
        }
        else {
            SetWindowTextW(hButton, L"Scan");
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == ID_BUTTON) {
            int index = SendMessage(hListBox, LB_GETCURSEL, 0, 0);
            if (index != LB_ERR) {
                processId = SendMessage(hListBox, LB_GETITEMDATA, index, 0);
                if (processId != 0) {
                    SetWindowTextW(hButton, L"Stop");
                    SetWindowTextW(hResult, L"Scanning...");
                }
                else {
                    SetWindowTextW(hButton, L"Scan");
                }
            }
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"MemoryScannerClass";

    WNDCLASSW wc = { };
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"GM-ALARM", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 650, 600, NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, nCmdShow);

    MSG msg = { };
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}

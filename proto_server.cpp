// server.cpp
#include <iostream>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

int main() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET server_fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_fd, 1);

    std::cout << "Esperando conexión...\n";

    SOCKET client_socket = accept(server_fd, nullptr, nullptr);

    std::cout << "Cliente conectado.\n";

    char c;

    while (true) {
        int bytes = recv(client_socket, &c, 1, 0);

        if (bytes <= 0)
            break;

        std::cout << "Recibido: " << c << std::endl;
    }

    closesocket(client_socket);
    closesocket(server_fd);
    WSACleanup();
}
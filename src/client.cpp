#include <iostream>
#include <thread>
#include <string>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

const int PORT = 5000;
const char* SERVER_IP = "127.0.0.1";

void receive_loop(int sock) {
    char buffer[1024];
    while (true) {
        ssize_t bytes = recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        std::cout << "\n" << buffer << "\n>";
        std::cout.flush();
    }

}

int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    if (connect(sock, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) { 
        std::cerr << "Failed to connect to server.\n";
        return 1;
    }

    std::thread(receive_loop, sock).detach();

    std::string input;
    std::cout << "> ";
    while (getline(std::cin, input)) {
        send(sock, input.c_str(), input.length(), 0);
        std::cout << "> ";
    }

    close(sock);
    return 0;
}
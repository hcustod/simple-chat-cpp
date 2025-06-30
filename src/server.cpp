#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <mutex>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

const int PORT = 8080;
std::vector<int> clients;
std::mutex m;

void handle_client(int client_fd) {
    char buffer[1028];
    while (true) {
        ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        broadcast(buffer, client_fd);
    }

    close(client_fd);
    std::lock_guard<std::mutex> lock(m);
    clients.erase(std::remove(clients.begin(), clients.end(), client_fd), clients.end());
}

void broadcast(const std::string& message, int sender_fd) {
    std::lock_guard<std::mutex> lock(m);
    for (int client_fd : clients) {
        if (client_fd != sender_fd) {
            send(client_fd, message.c_str(), message.length(), 0);
        }
    }
}

int main() {
    // Server port
    int server_conn = socket(AF_INET, SOCK_STREAM, 0);

    // Set server addr
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    // Attach server to IP:Port and wait for incoming conns
    bind(server_conn, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_conn, 10);

    std::cout << "Server listening on port " << PORT << std::endl;

    //
    while (true) {
        int client_conn = accept(server_conn, nullptr, nullptr);
        {
            std::lock_guard<std::mutex> lock(m);
            clients.push_back(client_conn);
        }
        std::thread(handle_client, client_conn).detach();
    }

    close(server_conn);
    return 0;
}





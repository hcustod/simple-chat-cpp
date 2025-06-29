#include "iostream"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <thread>
#include <string>
#include <mutex>
#include <sys/socket.h>

const int PORT = 8080;
std::vector<int> clients;
std::mutex m;

void handle_client(int client_fd) {


}

void broadcast(const std::string& message, int client_fd) {


}

int main() {
    // Server port
    int server_conn = socket(AF_INET, SOCK_STREAM, 0);

    // Set server addr
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    // Attach server to IP:Port and wait for incoming conns
    bind(server_conn, (struct sockaddr*)&addr, sizeof(addr));
    listen(server_conn, 10);

    std::cout << "Server listening on port " << port << std::endl;

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





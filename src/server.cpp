#include <iostream>
#include <thread>
#include <vector>
#include <string>
#include <mutex>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unordered_map>
#include <ctime>

const int PORT = 5000;

std::vector<int> clients;
std::mutex m;
std::unordered_map<int, std::string> client_names;

void broadcast(const std::string& message, int sender_fd) {
    std::lock_guard<std::mutex> lock(m);
    for (int client_fd : clients) {
        if (client_fd != sender_fd) {
            send(client_fd, message.c_str(), message.length(), 0);
        }
    }
}

std::string get_time() {
    time_t now = time(nullptr);
    struct tm* local_time = localtime(&now);
    char buffer[80];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", local_time);
    return std::string(buffer);
}

void handle_client(int client_fd) {
    char buffer[1024];

    size_t name_bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (name_bytes <= 0) {
        close(client_fd);
        return;
    }
    buffer[name_bytes] = '\0';
    std::string client_name(buffer);

    {
        std::lock_guard<std::mutex> lock(m);
        client_names[client_fd] = client_name;
    }

    std::string welcome_message = client_name + " has joined the chat.\n";
    broadcast(welcome_message, client_fd);
    std::cout << welcome_message;

    while (true) {
        ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';

        std::string full_msg = get_time() + " " + client_name + ":" + buffer + "\n";

        // Replace above with: 
        std::string msg(buffer);
        if (msg.rfind("/name ", 0) == 0) {
            std::string new_name = msg.substr(6);
            std::lock_guard<std::mutex> lock(m);
            std::string old_name = client_names[client_fd];
            client_names[client_fd] = new_name;
            std::string notice = old_name + " changed name to " + new_name + "\n";
            broadcast(notice, client_fd);
            std::cout << notice;
            continue;
        } else if (msg == "/exit") {
            break;
        } else {
            std::string full_msg = get_time() + " " + client_names[client_fd] + ": " + msg + "\n";
            std::cout << full_msg;
            broadcast(full_msg, client_fd);
        }

        std::cout << full_msg << std::endl;
        broadcast(full_msg, client_fd);
    }

    close(client_fd);
    {
        std::lock_guard<std::mutex> lock(m);
        clients.erase(std::remove(clients.begin(), clients.end(), client_fd), clients.end());
        std::string leave_message = client_name + " has left the chat.\n";
        client_names.erase(client_fd);
        std::cout << leave_message;
        broadcast(leave_message, client_fd);
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





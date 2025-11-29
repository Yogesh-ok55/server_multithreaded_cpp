#include <iostream>
#include <vector>
#include <string>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <cstring>
#include <algorithm>
#include <mutex>

#define PORT 8080
#define MAX_EVENTS 1000
#define BUFFER_SIZE 1024

std::vector<int> clients;
std::mutex clients_mutex;

// Make socket non-blocking
void setNonBlocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

// Broadcast message to all clients except sender
void broadcastMessage(const std::string& msg, int sender) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    for (int client : clients) {
        if (client != sender) {
            send(client, msg.c_str(), msg.size(), 0);
        }
    }
}

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        perror("bind");
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        return 1;
    }

    // Create epoll instance
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        return 1;
    }

    // Add server socket to epoll
    epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = server_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_fd, &event) == -1) {
        perror("epoll_ctl");
        return 1;
    }

    epoll_event events[MAX_EVENTS];
    char buffer[BUFFER_SIZE];

    std::cout << "Server running on port " << PORT << std::endl;

    while (true) {
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n == -1) {
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;

            if (fd == server_fd) {
                // New client connection
                sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                int client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
                if (client_socket == -1) {
                    perror("accept");
                    continue;
                }

                setNonBlocking(client_socket);

                // Add client socket to epoll
                epoll_event client_event;
                client_event.events = EPOLLIN | EPOLLET; // Edge-triggered
                client_event.data.fd = client_socket;
                epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &client_event);

                {
                    std::lock_guard<std::mutex> lock(clients_mutex);
                    clients.push_back(client_socket);
                }

                std::cout << "New client connected: " << client_socket << std::endl;
            } else {
                // Existing client sent data
                int bytes_read = recv(fd, buffer, BUFFER_SIZE - 1, 0);
                if (bytes_read <= 0) {
                    // Client disconnected
                    std::cout << "Client disconnected: " << fd << std::endl;
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
                    close(fd);
                    {
                        std::lock_guard<std::mutex> lock(clients_mutex);
                        clients.erase(std::remove(clients.begin(), clients.end(), fd), clients.end());
                    }
                } else {
                    buffer[bytes_read] = '\0';
                    std::string msg = "Client " + std::to_string(fd) + ": " + buffer;
                    std::cout << msg;
                    broadcastMessage(msg, fd);
                }
            }
        }
    }

    close(server_fd);
    close(epoll_fd);
    return 0;
}

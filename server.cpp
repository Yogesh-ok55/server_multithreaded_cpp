#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <algorithm>

#define PORT 8080

std::vector<int> clients;
std::mutex clients_mutex;

void broadcastMessage(const std::string& msg, int sender_fd) {
    std::lock_guard<std::mutex> lock(clients_mutex);

    for (int client : clients) {
        if (client != sender_fd) {
            send(client, msg.c_str(), msg.size(), 0);
        }
    }
}

// Each client runs in its own thread
void handleClient(int client_socket) {
    char buffer[1024];

    while (true) {
        int bytes_read = read(client_socket, buffer, sizeof(buffer));// read will be blocked untill a data packet is sent here

        if (bytes_read <= 0) {//in case of disconnection it will be 0
            std::cout << "Client disconnected: " << client_socket << std::endl;

            // Remove client safely
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                clients.erase(std::remove(clients.begin(), clients.end(), client_socket), clients.end());
            }

            close(client_socket);
            return;
        }

        buffer[bytes_read] = '\0';
        std::string msg = "Client " + std::to_string(client_socket) + ": " + buffer;

        std::cout << msg;
        broadcastMessage(msg, client_socket);
    }
}

int main() {
    int server_fd, new_socket;// server_fd is main socket which handle everything from connection to reading new connection , new_socket is temp for  assigning new socket to new connection
    struct sockaddr_in address;//address for networking from where connection allowed etc
    socklen_t addr_len = sizeof(address);

    // Create server socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);//creating socket
    if (server_fd < 0) {
        std::cerr << "Socket creation failed\n";
        return 1;
    }

    // Allow reuse of address/port
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    address.sin_family = AF_INET;// for IPv4
    address.sin_addr.s_addr = INADDR_ANY;//tcp 
    address.sin_port = htons(PORT);// changing format as per network requirement

    // Bind
    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) { // binding socket with port
        std::cerr << "Bind failed\n";
        return 1;
    }

    // Listen
    if (listen(server_fd, 10) < 0) { // start listening on socket
        std::cerr << "Listen failed\n";
        return 1;
    }

    std::cout << "Multithreaded Server started on port " << PORT << std::endl;

    while (true) {// main loop
        new_socket = accept(server_fd, (struct sockaddr*)&address, &addr_len); // for new connection 

        if (new_socket < 0) {
            std::cerr << "Accept failed\n";
            continue;
        }

        std::cout << "New client connected: " << new_socket << std::endl;

        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients.push_back(new_socket);
        }

        // Start a thread for this client
        std::thread(handleClient, new_socket).detach(); // seperate thread after connection is made
    }

    return 0;
}

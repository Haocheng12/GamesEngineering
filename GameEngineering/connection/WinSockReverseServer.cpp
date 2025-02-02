#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <thread>
#include <vector>
#include <algorithm>
#include <mutex>
#include <string>
#include <sstream>

#pragma comment(lib, "Ws2_32.lib")

// Simple struct to hold client info
struct ClientInfo {
    SOCKET socket;
    std::string username;
};

static std::vector<ClientInfo> g_clients;
static std::mutex g_clientsMutex;

// Send a message to a single client
void SendToClient(SOCKET sock, const std::string& message)
{
    send(sock, message.c_str(), static_cast<int>(message.size()), 0);
}

// Broadcast a message to all clients 
void BroadcastMessage(const std::string& message, SOCKET excludeSocket = INVALID_SOCKET)
{
    std::lock_guard<std::mutex> lock(g_clientsMutex);
    for (auto& c : g_clients) {
        if (c.socket != excludeSocket) {
            SendToClient(c.socket, message);
        }
    }
}

// Build and send the updated user list as a special "[USERLIST]" message
void BroadcastUserList()
{
    std::lock_guard<std::mutex> lock(g_clientsMutex);

    // Build a line like "[USERLIST]Alice,Fox,Rabbit"
    std::ostringstream ss;
    ss << "[USERLIST]";
    for (auto& client : g_clients) {
        ss << client.username << ",";
    }
    // Remove trailing comma
    // (only if there's at least one user)
    std::string userList = ss.str();
    if (!g_clients.empty()) {
        userList.pop_back(); 
    }
    userList += "\n";

    // Send to client
    for (auto& client : g_clients) {
        SendToClient(client.socket, userList);
    }
}

void RemoveClient(SOCKET clientSocket)
{
    std::lock_guard<std::mutex> lock(g_clientsMutex);
    auto it = std::remove_if(g_clients.begin(), g_clients.end(),
        [clientSocket](const ClientInfo& ci) { return ci.socket == clientSocket; });
    if (it != g_clients.end()) {
        g_clients.erase(it, g_clients.end());
    }
}

// Each client is handled here
void HandleClient(SOCKET client_socket)
{
    char buffer[1024];
    bool running = true;
    bool userNameSet = false;
    std::string userName;

    // 1) Expect first message from client to be the user name
    while (!userNameSet && running) {
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            running = false;
            break;
        }
        buffer[bytes_received] = '\0';
        userName = buffer;
        if (!userName.empty()) {
            userNameSet = true;
        }
        else {
            userName = "UnnamedUser";
            userNameSet = true;
        }
    }

    if (!running) {
        closesocket(client_socket);
        return;
    }

    // Insert the client into the global list
    {
        std::lock_guard<std::mutex> lock(g_clientsMutex);
        g_clients.push_back({ client_socket, userName });
    }

    // Announce join in chat
    {
        std::ostringstream ss;
        ss << "[SERVER]: " << userName << " joined the chat!\n";
        BroadcastMessage(ss.str());
    }

    // broadcast the updated user list
    BroadcastUserList();

    // Main loop
    while (running) {
        int bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received <= 0) {
            // disconnected or error
            running = false;
            break;
        }

        buffer[bytes_received] = '\0';
        std::string message(buffer);

        if (message.rfind("PRIVATE|", 0) == 0)
        {
            // Remove the prefix "PRIVATE|"
            std::string payload = message.substr(8);
            

            // Separate the payload into <target> and <text>
            size_t pipePos = payload.find('|');
            if (pipePos != std::string::npos)
            {
                std::string targetUser = payload.substr(0, pipePos);
                std::string privateMsg = payload.substr(pipePos + 1);

                // find target user in g_clients
                SOCKET targetSocket = INVALID_SOCKET;
                {
                    std::lock_guard<std::mutex> lock(g_clientsMutex);
                    for (auto& ci : g_clients)
                    {
                        if (ci.username == targetUser)
                        {
                            targetSocket = ci.socket;
                            break;
                        }
                    }
                }

                if (targetSocket != INVALID_SOCKET)
                {
                    // Construct a new line to send
                    std::ostringstream ss;
                    ss << userName << " (private): " << privateMsg << "\n";
                    SendToClient(targetSocket, ss.str());

                }
            }
          
        }
        else {
            std::ostringstream ss;
            ss << userName << ": " << message << "\n";
            BroadcastMessage(ss.str(), client_socket);
        }

        
       
    }

    //vremove client
    RemoveClient(client_socket);

    // Broadcast user leaving
    {
        std::ostringstream ss;
        ss << "[SERVER]: " << userName << " left the chat.\n";
        BroadcastMessage(ss.str());
    }

    // Update user list
    BroadcastUserList();

    closesocket(client_socket);
}

int main()
{
    WSADATA wsaData;
    int wsaErr = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaErr != 0) {
        std::cerr << "WSAStartup failed: " << wsaErr << std::endl;
        return 1;
    }

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (server_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(65432);
    server_addr.sin_addr.s_addr = INADDR_ANY; // 0.0.0.0

    if (bind(server_socket, (sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    if (listen(server_socket, SOMAXCONN) == SOCKET_ERROR) {
        std::cerr << "Listen failed: " << WSAGetLastError() << std::endl;
        closesocket(server_socket);
        WSACleanup();
        return 1;
    }

    std::cout << "Chatroom server listening on 65432...\n";

    while (true) {
        sockaddr_in client_addr;
        int client_size = sizeof(client_addr);
        SOCKET client_socket = accept(server_socket, (sockaddr*)&client_addr, &client_size);
        if (client_socket == INVALID_SOCKET) {
            std::cerr << "Accept failed: " << WSAGetLastError() << std::endl;
            break;
        }

        std::thread t(HandleClient, client_socket);
        t.detach();
    }

    closesocket(server_socket);
    WSACleanup();
    return 0;
}

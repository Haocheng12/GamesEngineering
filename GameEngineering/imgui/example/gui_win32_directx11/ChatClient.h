#pragma once

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <vector>
#include <iostream>
#pragma comment(lib, "ws2_32.lib")
#define DEFAULT_BUFFER_SIZE 1024
class ChatClient {
public:
    ChatClient();
    ~ChatClient();

    bool Connect(const char* host, unsigned short port);
    void Disconnect();

    //  Send message to server
    bool SendMessageToServer(const std::string& msg);
    std::vector<std::string> GetReceivedMessages();
    std::vector<std::string> GetConnectedUsers();


    

private:
    void ReceiverThreadFunc();
    SOCKET m_socket = INVALID_SOCKET;
    std::atomic<bool> m_running{ false };
    std::thread m_receiveThread;

 
    // For normal chat messages
    std::mutex m_mutexMessages;
    std::queue<std::string> m_messageQueue;

    // For user list
    std::mutex m_mutexUserList;
    std::vector<std::string> m_connectedUsers;
};






ChatClient::ChatClient() {
    // Initialize the WinSock 
    WSADATA wsaData;
    int wsaErr = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaErr != 0) {
        std::cerr << "WSAStartup failed. Error: " << wsaErr << std::endl;
    }
}

ChatClient::~ChatClient() {
    Disconnect();
    WSACleanup();
}

bool ChatClient::Connect(const char* host, unsigned short port) {
    // Create the socket
    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed. Error: " << WSAGetLastError() << std::endl;
        return false;
    }

    // Setup the server address
    sockaddr_in serverAddress = {};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port);

    if (inet_pton(AF_INET, host, &serverAddress.sin_addr) <= 0) {
        std::cerr << "Invalid address / Address not supported.\n";
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    // Connect to server
    if (connect(m_socket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cerr << "Connection failed. Error: " << WSAGetLastError() << std::endl;
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    std::cout << "Connected to server.\n";
    m_running = true;

    // Start the background receiving thread
    m_receiveThread = std::thread(&ChatClient::ReceiverThreadFunc, this);

    return true;
}

void ChatClient::Disconnect() {
    // Signal the thread to stop
    m_running = false;

    // Close the socket if open
    if (m_socket != INVALID_SOCKET) {
        shutdown(m_socket, SD_BOTH);
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }

    // Join the thread
    if (m_receiveThread.joinable()) {
        m_receiveThread.join();
    }
}
// Send messaage to server
bool ChatClient::SendMessageToServer(const std::string& msg) {
    if (!m_running || m_socket == INVALID_SOCKET) {
        return false;
    }

    int result = send(m_socket, msg.c_str(), static_cast<int>(msg.size()), 0);
    if (result == SOCKET_ERROR) {
        std::cerr << "Send failed. Error: " << WSAGetLastError() << std::endl;
        return false;
    }
    return true;
}
// Receive message from server
std::vector<std::string> ChatClient::GetReceivedMessages()
{
    std::vector<std::string> messages;
    {
        std::lock_guard<std::mutex> lock(m_mutexMessages);
        while (!m_messageQueue.empty()) {
            messages.push_back(m_messageQueue.front());
            m_messageQueue.pop();
        }
    }
    return messages;
}

// Receive userlist from server
std::vector<std::string> ChatClient::GetConnectedUsers()
{
    std::lock_guard<std::mutex> lock(m_mutexUserList);
    return m_connectedUsers; 
}

void ChatClient::ReceiverThreadFunc()
{
    char buffer[DEFAULT_BUFFER_SIZE];
    while (m_running) {
        int bytesRecv = recv(m_socket, buffer, DEFAULT_BUFFER_SIZE - 1, 0);
        if (bytesRecv <= 0) {
            // disconnected or error
            m_running = false;
            break;
        }

        buffer[bytesRecv] = '\0';
        std::string msg(buffer);

        // Check if it is a userlist message
        if (msg.rfind("[USERLIST]", 0) == 0) {
            // parse the substring after "[USERLIST]"
            std::string listPart = msg.substr(10);  // skip the prefix
            // remove trailing newline if present
            if (!listPart.empty() && listPart.back() == '\n') {
                listPart.pop_back();
            }

            // Split by commas
            std::vector<std::string> usersParsed;
            size_t start = 0;
            while (true) {
                size_t commaPos = listPart.find(',', start);
                if (commaPos == std::string::npos) {
                    // last element
                    std::string name = listPart.substr(start);
                    if (!name.empty()) {
                        usersParsed.push_back(name);
                    }
                    break;
                }
                else {
                    std::string name = listPart.substr(start, commaPos - start);
                    if (!name.empty()) {
                        usersParsed.push_back(name);
                    }
                    start = commaPos + 1;
                }
            }

            // Store in m_connectedUsers
            {
                std::lock_guard<std::mutex> lock(m_mutexUserList);
                m_connectedUsers = std::move(usersParsed);
            }
        }
        else {
            // Normal chat line -> push to m_messageQueue
            std::lock_guard<std::mutex> lock(m_mutexMessages);
            m_messageQueue.push(msg);
        }
    }

    m_running = false;
}


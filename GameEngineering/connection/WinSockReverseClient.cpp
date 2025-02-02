#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <thread>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")

#define DEFAULT_BUFFER_SIZE 1024




void sender_thread(SOCKET client_socket, std::atomic<bool>& running) {
    std::string sentence;

    while (running) {
        std::cout << "Send to server: ";
        std::getline(std::cin, sentence);

        if (send(client_socket, sentence.c_str(), static_cast<int>(sentence.size()), 0) == SOCKET_ERROR) {
            std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;
            running = false;
            break;
        }

        if (sentence == "!bye") {
            running = false;
            break;
        }
    }
}

void receiver_thread(SOCKET client_socket, std::atomic<bool>& running) {
    char buffer[DEFAULT_BUFFER_SIZE];

    while (running) {
        int bytes_received = recv(client_socket, buffer, DEFAULT_BUFFER_SIZE - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0'; // Null-terminate the received data
            std::cout << "\nReceived from server: " << buffer << std::endl;
        }
        else if (bytes_received == 0) {
            std::cout << "\nConnection closed by server." << std::endl;
            running = false;
            break;
        }
        else {
            std::cerr << "\nReceive failed with error: " << WSAGetLastError() << std::endl;
            running = false;
            break;
        }
    }
}

void run_client_loop() {
    const char* host = "127.0.0.1"; // Server IP address
    unsigned int port = 65432;
    std::string sentence = "Hello, server!";

    // Initialize WinSock
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed with error: " << WSAGetLastError() << std::endl;
        return;
    }

    // Create a socket
    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return;
    }

    // Resolve the server address and port
    sockaddr_in server_address = {};
    server_address.sin_family = AF_INET;
    //server_address.sin_port = htons(std::stoi(port));
    server_address.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &server_address.sin_addr) <= 0) {
        std::cerr << "Invalid address/ Address not supported" << std::endl;
        closesocket(client_socket);
        WSACleanup();
        return;
    }

    // Connect to the server
    if (connect(client_socket, reinterpret_cast<sockaddr*>(&server_address), sizeof(server_address)) == SOCKET_ERROR) {
        std::cerr << "Connection failed with error: " << WSAGetLastError() << std::endl;
        closesocket(client_socket);
        WSACleanup();
        return;
    }

    std::cout << "Connected to the server." << std::endl;
    //while (true) {
    //    //std::cin >> sentence; 
    //    std::cout << "Send to server: ";
    //    std::getline(std::cin, sentence);


    //    // Send the sentence to the server
    //    if (send(client_socket, sentence.c_str(), static_cast<int>(sentence.size()), 0) == SOCKET_ERROR) {
    //        std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;
    //        closesocket(client_socket);
    //        WSACleanup();
    //        return;
    //    }

    //    if (sentence == "!bye") break; 

    //    std::cout << "Sent: " << sentence << std::endl;

    //    // Receive the reversed sentence from the server
    //    char buffer[DEFAULT_BUFFER_SIZE] = { 0 };
    //    int bytes_received = recv(client_socket, buffer, DEFAULT_BUFFER_SIZE - 1, 0);
    //    if (bytes_received > 0) {
    //        buffer[bytes_received] = '\0'; // Null-terminate the received data
    //        std::cout << "Received from server: " << buffer << std::endl;
    //    }
    //    else if (bytes_received == 0) {
    //        std::cout << "Connection closed by server." << std::endl;
    //    }
    //    else {
    //        std::cerr << "Receive failed with error: " << WSAGetLastError() << std::endl;
    //    }

    //}

     // Multithreading for send and receive
    std::atomic<bool> running(true);
    std::thread sender(sender_thread, client_socket, std::ref(running));
    std::thread receiver(receiver_thread, client_socket, std::ref(running));

    // Wait for threads to finish
    sender.join();
    receiver.join();

    // Cleanup
    closesocket(client_socket);
    WSACleanup();
}



//int main() {
//    //run_client();
//    run_client_loop(); 
//}
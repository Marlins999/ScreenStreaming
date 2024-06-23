#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <vector>
#include <chrono>
#include <thread>

#pragma comment(lib, "Ws2_32.lib")

constexpr auto SERVER_PORT = 6699;
constexpr auto BUFFER_SIZE = 1024;
SOCKET clientSocket;

bool initializeWinsock() {
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

SOCKET createClientSocket() {
    SOCKET sock = INVALID_SOCKET;
    while (sock == INVALID_SOCKET) {
        sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET) {
            std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
            continue;
        }

        sockaddr_in serverAddress;
        ZeroMemory(&serverAddress, sizeof(serverAddress));
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(SERVER_PORT);
        InetPton(AF_INET, L"127.0.0.1", &serverAddress.sin_addr);

        if (connect(sock, (SOCKADDR*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
            std::cerr << "Connection failed: " << WSAGetLastError() << std::endl;
            closesocket(sock);
            sock = INVALID_SOCKET;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    return sock;
}

void sendScreenFrame(SOCKET clientSocket) {
    int width = 800;
    int height = 600;
    int bmpSize = width * height * 3;

    std::vector<char> bmpData(bmpSize);

    HDC hdc = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdc);
    HBITMAP hbm = CreateCompatibleBitmap(hdc, width, height);
    SelectObject(hdcMem, hbm);
    BitBlt(hdcMem, 0, 0, width, height, hdc, 0, 0, SRCCOPY);

    BITMAPINFOHEADER bi;
    ZeroMemory(&bi, sizeof(BITMAPINFOHEADER));
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = height;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;

    GetDIBits(hdcMem, hbm, 0, height, bmpData.data(), reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);

    DeleteObject(hbm);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdc);

    int header[2] = { bmpSize, width };
    int headerSize = sizeof(header);

    int bytesSent = send(clientSocket, reinterpret_cast<char*>(header), headerSize, 0);
    if (bytesSent != headerSize) {
        std::cerr << "Failed to send header: " << WSAGetLastError() << std::endl;
        return;
    }

    bytesSent = 0;
    while (bytesSent < bmpSize) {
        int result = send(clientSocket, bmpData.data() + bytesSent, bmpSize - bytesSent, 0);
        if (result == SOCKET_ERROR) {
            std::cerr << "Failed to send image data: " << WSAGetLastError() << std::endl;
            return;
        }
        bytesSent += result;
    }

    if (bytesSent != bmpSize) {
        std::cerr << "Failed to send complete image data. Sent: " << bytesSent << ", Expected: " << bmpSize << std::endl;
    }
}

void clientHandler(SOCKET clientSocket) {
    while (true) {
        sendScreenFrame(clientSocket);
        std::this_thread::sleep_for(std::chrono::milliseconds(1000 / 30));  // 30 FPS
    }
    closesocket(clientSocket);
}


int main() {
    if (!initializeWinsock()) {
        std::cerr << "Failed to initialize Winsock." << std::endl;
        return 1;
    }

    while (true) {
        clientSocket = createClientSocket();
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Failed to create client socket." << std::endl;
            WSACleanup();
            continue;
        }

        std::thread commandThread(clientHandler, clientSocket);
        commandThread.join();
    }

    WSACleanup();
    return 0;
}

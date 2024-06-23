#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <tchar.h>

#pragma comment(lib, "Ws2_32.lib")

constexpr auto SERVER_PORT = 6699;
constexpr auto BUFFER_SIZE = 1024;

std::mutex bmpMutex;
std::vector<char> bmpData[2];  // Double buffer for bmp data
int bmpWidth = 800, bmpHeight = 600;
int currentBuffer = 0;
bool dataReady = false;

bool initializeWinsock() {
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

SOCKET createServerSocket() {
    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        return INVALID_SOCKET;
    }

    sockaddr_in serverAddress;
    ZeroMemory(&serverAddress, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(SERVER_PORT);
    serverAddress.sin_addr.s_addr = INADDR_ANY;

    if (bind(serverSocket, (SOCKADDR*)&serverAddress, sizeof(serverAddress)) == SOCKET_ERROR) {
        closesocket(serverSocket);
        return INVALID_SOCKET;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(serverSocket);
        return INVALID_SOCKET;
    }

    return serverSocket;
}

void displayImage(HDC hdc) {
    std::lock_guard<std::mutex> lock(bmpMutex);

    if (!dataReady) return;

    BITMAPINFOHEADER bi;
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bmpWidth;
    bi.biHeight = bmpHeight;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = 0;
    bi.biXPelsPerMeter = 0;
    bi.biYPelsPerMeter = 0;
    bi.biClrUsed = 0;
    bi.biClrImportant = 0;

    BITMAPINFO bInfo;
    bInfo.bmiHeader = bi;

    StretchDIBits(hdc, 0, 0, bmpWidth, bmpHeight, 0, 0, bmpWidth, bmpHeight, bmpData[currentBuffer].data(), &bInfo, DIB_RGB_COLORS, SRCCOPY);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        displayImage(hdc);
        EndPaint(hwnd, &ps);
    }
    break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

void networkHandler(SOCKET clientSocket, HWND hwnd) {
    int header[2];
    int result;

    while (true) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(clientSocket, &readfds);

        timeval timeout;
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        int activity = select(0, &readfds, NULL, NULL, &timeout);

        if (activity == SOCKET_ERROR) {
            std::cerr << "select failed: " << WSAGetLastError() << "\n";
            break;
        }

        if (FD_ISSET(clientSocket, &readfds)) {
            result = recv(clientSocket, reinterpret_cast<char*>(header), sizeof(header), 0);
            if (result > 0) {
                int bmpSize = header[0];
                int width = header[1];
                int height = bmpSize / (width * 3);

                if (width <= 0 || height <= 0 || bmpSize <= 0) {
                    std::cerr << "Invalid header received.\n";
                    break;
                }

                std::vector<char> bmpDataLocal(bmpSize);
                int bytesReceived = 0;
                while (bytesReceived < bmpSize) {
                    result = recv(clientSocket, bmpDataLocal.data() + bytesReceived, bmpSize - bytesReceived, 0);
                    if (result > 0) {
                        bytesReceived += result;
                    }
                    else {
                        std::cerr << "Failed to receive image data. Error: " << WSAGetLastError() << "\n";
                        break;
                    }
                }

                if (bytesReceived == bmpSize) {
                    {
                        std::lock_guard<std::mutex> lock(bmpMutex);
                        bmpData[1 - currentBuffer] = std::move(bmpDataLocal);
                        bmpWidth = width;
                        bmpHeight = height;
                        dataReady = true;
                    }
                    currentBuffer = 1 - currentBuffer;
                    InvalidateRect(hwnd, NULL, FALSE); 
                }
                else {
                    std::cerr << "Incomplete image data received.\n";
                }
            }
            else if (result == 0) {
                std::cout << "Connection closed by client.\n";
                break;
            }
            else {
                std::cerr << "recv failed: " << WSAGetLastError() << "\n";
                break;
            }
        }
    }
    closesocket(clientSocket);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    if (!initializeWinsock()) {
        std::cerr << "Failed to initialize Winsock." << std::endl;
        return 1;
    }

    SOCKET serverSocket = createServerSocket();
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "Failed to create server socket." << std::endl;
        WSACleanup();
        return 1;
    }

    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WindowProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("Screen Streamer"), NULL };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindow(wc.lpszClassName, _T("Screen Streamer"), WS_OVERLAPPEDWINDOW, 100, 100, 800, 600, NULL, NULL, wc.hInstance, NULL);
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    std::thread acceptThread([serverSocket, hwnd]() {
        SOCKET clientSocket;
        sockaddr_in clientAddress;
        int clientAddressSize = sizeof(clientAddress);

        while (true) {
            clientSocket = accept(serverSocket, (SOCKADDR*)&clientAddress, &clientAddressSize);
            if (clientSocket == INVALID_SOCKET) {
                std::cerr << "Failed to accept client connection." << std::endl;
                continue;
            }
            std::thread handlerThread(networkHandler, clientSocket, hwnd);
            handlerThread.detach();
        }
        });

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    acceptThread.join();

    closesocket(serverSocket);
    WSACleanup();
    return 0;
}

#include <iostream>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <stack>

#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <WIndowsx.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <wincodec.h>
#include <zlib.h>

#include "Message.hpp"

#define DEFAULT_PORT "27015"

std::mutex g_hBitmapMutex;
std::stack<HBITMAP> g_hBitmaps;
std::queue<Message> g_messages;
std::mutex g_messagesMutex;

char* DecompressData(const char* compressedData, uLong compressedSize, uLong decompressedSize) {
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;

    int ret = inflateInit(&stream);
    if (ret != Z_OK) {
        std::cerr << "inflateInit failed with error code " << ret << std::endl;
        return nullptr;
    }

    char* decompressedData = new char[decompressedSize];

    stream.avail_in = compressedSize;
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressedData));
    stream.avail_out = decompressedSize;
    stream.next_out = reinterpret_cast<Bytef*>(decompressedData);

    ret = inflate(&stream, Z_FINISH);
    if (ret != Z_STREAM_END) {
        std::cerr << "inflate failed with error code " << ret << std::endl;
        delete[] decompressedData;
        inflateEnd(&stream);
        return nullptr;
    }

    inflateEnd(&stream);
    return decompressedData;
}

float mapValue(float value, float in_min, float in_max, float out_min, float out_max)
{
    return (value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void drawBitmap(const HBITMAP& hBitmap, HDC hDC)
{
    // Create a compatible device context for the bitmap
	HDC hBitmapDC = CreateCompatibleDC(hDC);

	// Select the bitmap into the memory DC
	HGDIOBJ hOldBitmap = SelectObject(hBitmapDC, hBitmap);

	// Get the dimensions of the bitmap
	BITMAP bm;
	GetObject(hBitmap, sizeof(BITMAP), &bm);

	// Draw the bitmap onto the window's HDC
	BitBlt(hDC, 0, 0, bm.bmWidth, bm.bmHeight, hBitmapDC, 0, 0, SRCCOPY);

	// Clean up
	SelectObject(hBitmapDC, hOldBitmap);
	DeleteDC(hBitmapDC);
}

void QueueMessage(const Message& message)
{
    g_messagesMutex.lock();
    g_messages.push(message);
    g_messagesMutex.unlock();
}

void SendMessagesThread(SOCKET socket)
{
    Message message;
    while (true)
    {
        bool send = false;

        g_messagesMutex.lock();
        if (!g_messages.empty())
        {
            message = g_messages.front();
            send = true;
            g_messages.pop();
        }
        g_messagesMutex.unlock();

        if (send)
        {
			SendAll(socket, (char*)&message, sizeof(Message));
            send = false;
        }
    }
}

HBITMAP CreateHBitmapFromDIBBits(BYTE* dibBits, BITMAPINFO* bmi)
{
    HDC hdcScreen = GetDC(NULL);

    // Create a compatible DC to work with
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    if (!hdcMem)
    {
        ReleaseDC(NULL, hdcScreen);
        return NULL;
    }

    HBITMAP hBitmap = CreateDIBitmap(hdcScreen, &(bmi->bmiHeader), CBM_INIT, dibBits, bmi, DIB_RGB_COLORS);

    // Clean up
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    return hBitmap;
}

void ReceiveCapture(SOCKET socket)
{
    BITMAPINFO bitmapInfo;
    int bytesReceived = ReceiveAll(socket, (char*)&bitmapInfo, sizeof(BITMAPINFO));

    uLong compressedSize;
    bytesReceived = ReceiveAll(socket, (char*)&compressedSize, sizeof(uLong));

    char* bitmapDataCompressed = new char[compressedSize];
	bytesReceived = ReceiveAll(socket, bitmapDataCompressed, compressedSize);

    BYTE* bitmapData = (BYTE*)DecompressData(bitmapDataCompressed, compressedSize, bitmapInfo.bmiHeader.biSizeImage);

    HBITMAP hbmp;
    hbmp = CreateHBitmapFromDIBBits(bitmapData, &bitmapInfo);

    delete[] bitmapDataCompressed;
    delete[] bitmapData;

    g_hBitmapMutex.lock();
    g_hBitmaps.push(hbmp);
    g_hBitmapMutex.unlock();

}

void ReceiveCaptureThread(SOCKET socket)
{
    while (true)
    {
        ReceiveCapture(socket);
    }
}

void DestroyBitmaps()
{
    while (!g_hBitmaps.empty())
    {
        DeleteObject(g_hBitmaps.top());
        g_hBitmaps.pop();
    }
}

// Function to handle messages sent to the window
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg)
    {
        case WM_CLOSE:
        {
            PostQuitMessage(0);
            return 0;
        }
        case WM_PAINT:
        {
            if (!g_hBitmaps.empty())
            {

                g_hBitmapMutex.lock();
                HBITMAP hbmp = g_hBitmaps.top();
                g_hBitmaps.pop();
                DestroyBitmaps();
                g_hBitmapMutex.unlock();

				PAINTSTRUCT ps;
				HDC hdc = BeginPaint(hwnd, &ps);
                drawBitmap(hbmp, hdc);
                EndPaint(hwnd, &ps);
                DeleteObject(hbmp);
            }

            break;
        }
        case WM_MOUSEMOVE:
        {
            int xPos = GET_X_LPARAM(lParam);
            int yPos = GET_Y_LPARAM(lParam);
            QueueMessage(Message{ EventMouseMove,
                static_cast<int>(mapValue(xPos, 0, 1920, 0, 65535)), // should use window size of server
                static_cast<int>(mapValue(yPos, 0, 1080, 0, 65535))
                });
            break;
        }
        case WM_LBUTTONDOWN:
        {
            int xPos = GET_X_LPARAM(lParam);
            int yPos = GET_Y_LPARAM(lParam);
            QueueMessage(Message{ EventMouseLeftDown });
            break;
        }
        case WM_LBUTTONUP:
        {
            int xPos = GET_X_LPARAM(lParam);
            int yPos = GET_Y_LPARAM(lParam);
            QueueMessage(Message{ EventMouseLeftUp });
            break;
        }
        case WM_MOUSEWHEEL:
		{
			int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            QueueMessage(Message{ EventMouseScroll, delta });
            break;
		}
       default:
        {
            InvalidateRect(hwnd, NULL, TRUE);
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
        }
    }
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Define the window class
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;  // Set the window procedure
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MyWindowClass";

    // Register the window class
    RegisterClass(&wc);

    // Create the window
    HWND hwnd = CreateWindowEx(
        0,                      // Extended window style
        L"MyWindowClass",       // Class name
        L"My Window",           // Window title
        WS_OVERLAPPEDWINDOW,    // Window style
        CW_USEDEFAULT,          // X position
        CW_USEDEFAULT,          // Y position
        CW_USEDEFAULT,          // Width
        CW_USEDEFAULT,          // Height
        NULL,                   // Parent window
        NULL,                   // Menu
        hInstance,              // Instance handle
        NULL                    // Additional application data
    );

    if (hwnd == NULL)
    {
        MessageBox(NULL, L"Window creation failed!", L"Error", MB_ICONERROR);
        return 1;
    }

    // Show the window
    ShowWindow(hwnd, nCmdShow);


	WSADATA wsaData;
    SOCKET ConnectSocket = INVALID_SOCKET;
    struct addrinfo *result = NULL,
                    *ptr = NULL,
                    hints;
    int iResult;

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return 1;
    }

    ZeroMemory( &hints, sizeof(hints) );
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    // Resolve the server address and port
    static const char* serverAdress = "192.168.0.100";
    iResult = getaddrinfo(serverAdress, DEFAULT_PORT, &hints, &result);
    if ( iResult != 0 ) {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    // Attempt to connect to an address until one succeeds
    for(ptr=result; ptr != NULL ;ptr=ptr->ai_next) {

        // Create a SOCKET for connecting to server
        ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, 
            ptr->ai_protocol);
        if (ConnectSocket == INVALID_SOCKET) {
            printf("socket failed with error: %ld\n", WSAGetLastError());
            WSACleanup();
            return 1;
        }

        // Connect to server.
        iResult = connect( ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
        if (iResult == SOCKET_ERROR) {
            closesocket(ConnectSocket);
            ConnectSocket = INVALID_SOCKET;
            continue;
        }
        break;
    }

    freeaddrinfo(result);

    if (ConnectSocket == INVALID_SOCKET) {
        printf("Unable to connect to server!\n");
        WSACleanup();
        return 1;
    }

    std::thread sendMessagesThread{ SendMessagesThread, ConnectSocket };
    std::thread receiveCaptureThread{ ReceiveCaptureThread, ConnectSocket };

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // cleanup
    closesocket(ConnectSocket);
    WSACleanup();

    return 0;
}

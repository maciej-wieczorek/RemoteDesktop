#include <iostream>
#include <queue>
#include <thread>
#include <mutex>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <zlib.h>

#include "Message.hpp"

#define DEFAULT_PORT "27015"
#define COMPRESSION_RATE 3

char* CompressData(const char* inputData, uLong inputSize, uLong* compressedSize) {
    z_stream stream;
    stream.zalloc = Z_NULL;
    stream.zfree = Z_NULL;
    stream.opaque = Z_NULL;

    int ret = deflateInit(&stream, COMPRESSION_RATE);
    if (ret != Z_OK) {
        std::cerr << "deflateInit failed with error code " << ret << std::endl;
        return nullptr;
    }

    uLong bufferSize = compressBound(inputSize);
    char* compressedData = new char[bufferSize];

    stream.avail_in = inputSize;
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(inputData));
    stream.avail_out = bufferSize;
    stream.next_out = reinterpret_cast<Bytef*>(compressedData);

    ret = deflate(&stream, Z_FINISH);
    if (ret != Z_STREAM_END) {
        std::cerr << "deflate failed with error code " << ret << std::endl;
        delete[] compressedData;
        deflateEnd(&stream);
        return nullptr;
    }

    *compressedSize = stream.total_out;

    deflateEnd(&stream);
    return compressedData;
}

HBITMAP TakeCapture()
{
    // Get the screen dimensions
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Create a device context for the entire screen
    HDC hScreenDC = GetDC(NULL);

    // Create a compatible device context for the captured image
    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);

    // Create a bitmap to hold the captured image
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, screenWidth, screenHeight);

    // Select the bitmap into the memory DC
    SelectObject(hMemoryDC, hBitmap);

    // Perform the screen capture
    BitBlt(hMemoryDC, 0, 0, screenWidth, screenHeight, hScreenDC, 0, 0, SRCCOPY);

    DeleteDC(hMemoryDC);
    ReleaseDC(NULL, hScreenDC);

    return hBitmap;
}

BYTE* CopyBitmapToCharArray(HBITMAP hBitmap, BITMAPINFO& bitmapInfo)
{
    HDC hdc = GetDC(NULL);
    bitmapInfo.bmiHeader.biSize = sizeof(bitmapInfo.bmiHeader); 

    // Get the BITMAPINFO structure from the bitmap
    if(0 == GetDIBits(hdc, hBitmap, 0, 0, NULL, &bitmapInfo, DIB_RGB_COLORS)) {
        std::cout << "error: GetDIBits\n";
    }

    // create the bitmap buffer
    BYTE* lpPixels = new BYTE[bitmapInfo.bmiHeader.biSizeImage];

    // Better do this here - the original bitmap might have BI_BITFILEDS, which makes it
    // necessary to read the color table - you might not want this.
    bitmapInfo.bmiHeader.biCompression = BI_RGB;

    // get the actual bitmap buffer
    if(0 == GetDIBits(hdc, hBitmap, 0, bitmapInfo.bmiHeader.biHeight, (LPVOID)lpPixels, &bitmapInfo, DIB_RGB_COLORS)) {
        std::cout << "error: GetDIBits2\n";
    }

    ReleaseDC(NULL, hdc);

    return lpPixels;
}

void SendCapture(SOCKET socket, HBITMAP hBitmap)
{
    int width, height;
    BITMAPINFO bitmapInfo{0};
    BYTE* bitmapData = CopyBitmapToCharArray(hBitmap, bitmapInfo);
    uLong compressedSize;
    char* compressedBitmapData = CompressData((char*)bitmapData, bitmapInfo.bmiHeader.biSizeImage, &compressedSize);

    std::cout << "compression rate: " << compressedSize / (double)bitmapInfo.bmiHeader.biSizeImage  << "     " << '\r';

    SendAll(socket, (char*)&bitmapInfo, sizeof(BITMAPINFO));
    SendAll(socket, (char*)&compressedSize, sizeof(uLong));
    SendAll(socket, (char*)compressedBitmapData, compressedSize);

    delete[] bitmapData;
    delete[] compressedBitmapData;
}

void MouseMove(int x, int y)
{
    INPUT input = { 0 };
	input.type = INPUT_MOUSE;
    input.mi.dx = x;
    input.mi.dy = y;
	input.mi.dwFlags = MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE;

    SendInput(1, &input, sizeof(INPUT));
}

void MouseClickLeftDown()
{
    INPUT input = { 0 };

	input.type = INPUT_MOUSE;
	input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;

	SendInput(1, &input, sizeof(INPUT));
}

void MouseClickLeftUp()
{
    INPUT input = { 0 };

	input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_LEFTUP;

	SendInput(1, &input, sizeof(INPUT));
   
}

void MouseScroll(int delta)
{
	INPUT input = { 0 };

    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    input.mi.mouseData = delta;

    SendInput(1, &input, sizeof(INPUT));
}

void ProcessMessage(const Message& message)
{
    switch (message.event)
    {
        case EventMouseMove:
        {
            int xPos = message.data1;
            int yPos = message.data2;
            MouseMove(xPos, yPos);
            break;
        }
        case EventMouseLeftDown:
        {
            MouseClickLeftDown();
            break;
        }
        case EventMouseLeftUp:
        {
            MouseClickLeftUp();
            break;
        }
        case EventMouseScroll:
		{
            int delta = message.data1;
            MouseScroll(delta);
            break;
		}
    }
}

void ProcessMessages(std::queue<Message>& messages, std::mutex* mutex)
{
	while (!messages.empty())
	{
        mutex->lock();
        Message message = messages.front();
		messages.pop();
        mutex->unlock();

		ProcessMessage(message);
	}
}

Message ReceiveMessage(SOCKET socket)
{
	Message message;
	int iResult = ReceiveAll(socket, (char*)&message, sizeof(Message));
    if (iResult != 0 && iResult != sizeof(Message))
    {
        std::cerr << "error: received faulty message";
    }

    return message;
}

void SendCaptureThread(SOCKET socket)
{
    while (true)
    {
		HBITMAP hBitmap = TakeCapture();
		SendCapture(socket, hBitmap);
        DeleteObject(hBitmap);
    }
}

void ReceiveCommandsThread(SOCKET socket, std::queue<Message>* messages, std::mutex* mutex)
{
    while (true)
    {
        Message message = ReceiveMessage(socket);
        mutex->lock();
        messages->push(message);
        mutex->unlock();
    }
}

int main()
{
    WSADATA wsaData;
    int iResult;

    SOCKET ListenSocket = INVALID_SOCKET;
    SOCKET ClientSocket = INVALID_SOCKET;

    struct addrinfo *result = NULL;
    struct addrinfo hints;
    
    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return 1;
    }

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    // Resolve the server address and port
    iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
    if ( iResult != 0 ) {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    // Create a SOCKET for the server to listen for client connections.
    ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (ListenSocket == INVALID_SOCKET) {
        printf("socket failed with error: %ld\n", WSAGetLastError());
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    // Setup the TCP listening socket
    iResult = bind( ListenSocket, result->ai_addr, (int)result->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
        printf("bind failed with error: %d\n", WSAGetLastError());
        freeaddrinfo(result);
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(result);

    iResult = listen(ListenSocket, SOMAXCONN);
    if (iResult == SOCKET_ERROR) {
        printf("listen failed with error: %d\n", WSAGetLastError());
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    // Accept a client socket
    ClientSocket = accept(ListenSocket, NULL, NULL);
    if (ClientSocket == INVALID_SOCKET) {
        printf("accept failed with error: %d\n", WSAGetLastError());
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    // No longer need server socket
    closesocket(ListenSocket);


    std::queue<Message> messages;
    std::mutex messagesMutex;
	std::thread sendCaptureThread{ SendCaptureThread, ClientSocket };
    std::thread receiveCommandsThread{ ReceiveCommandsThread, ClientSocket, &messages, &messagesMutex };
    while (true)
    {
        ProcessMessages(messages, &messagesMutex);
    }

    // shutdown the connection since we're done
    iResult = shutdown(ClientSocket, SD_SEND);
    if (iResult == SOCKET_ERROR) {
        printf("shutdown failed with error: %d\n", WSAGetLastError());
        closesocket(ClientSocket);
        WSACleanup();
        return 1;
    }

    // cleanup
    closesocket(ClientSocket);
    WSACleanup();

    return 0;
}
enum Event
{
	EventMouseMove,
	EventMouseLeftDown,
	EventMouseLeftUp,
	EventMouseRightDown,
	EventMouseRightUp,
	EventMouseScroll
};

struct Message
{
    Event event;
    int data1;
    int data2;
};

int ReceiveAll(SOCKET socket, char* buffer, int totalBytes)
{
    int bytesReceived = 0;
    while (bytesReceived < totalBytes)
    {
        int bytes = recv(socket, buffer + bytesReceived, totalBytes - bytesReceived, 0);

        if (bytes <= 0)
        {
            exit(0);
        }
        else
        {
            bytesReceived += bytes;
        }
    }

    return bytesReceived;
}

int SendAll(SOCKET socket, const char* buffer, int totalBytes) {
    int totalSent = 0;

    while (totalSent < totalBytes)
    {
        int sent = send(socket, buffer + totalSent, totalBytes - totalSent, 0);

        if (sent == SOCKET_ERROR) {
            exit(0);
        }

        totalSent += sent;
    }

    return totalSent;
}

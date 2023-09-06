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

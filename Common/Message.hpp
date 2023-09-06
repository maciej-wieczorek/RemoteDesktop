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

        if (bytes == -1)
        {
            // Handle error (e.g., check errno for error code)
            std::cerr << "recv error: " << errno << std::endl;
            return -1;
        }
        else if (bytes == 0)
        {
            // Connection closed by the other side
            return bytesReceived;
        }
        else
        {
            // Increment the total bytes received
            bytesReceived += bytes;
        }
    }

    return bytesReceived;
}

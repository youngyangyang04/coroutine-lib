#include "ioscheduler.h"
#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <netinet/in.h>
#include <fcntl.h>
#include "hook.h"

using namespace sylar;

void func()
{

	int sock = socket(AF_INET, SOCK_STREAM, 0);
    
    sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(80);  // HTTP 标准端口
    server.sin_addr.s_addr = inet_addr("103.235.46.96");

    int rt = connect(sock, (struct sockaddr *)&server, sizeof(server));
    if(rt)
    {
    	std::cout << "connect() failed:" << strerror(errno) << std::endl;
    	return;
    }
    std::cout << "connected\n";

    const char data[] = "GET / HTTP/1.0\r\n\r\n";
    rt = send(sock, data, sizeof(data), 0);
    if(rt<=0)
    {
    	std::cout << "send() failed\n";
    }
    std::cout << "send success\n";

    std::string buff;
    buff.resize(4096);
    rt = recv(sock, &buff[0], buff.size(), 0);
    if(rt<=0)
    {
        std::cout << "recv() failed\n";
    }
    std::cout << "recv success\n";

    buff.resize(10);
    std::cout << buff << std::endl;
}

int main(int argc, char const *argv[])
{
	IOManager manager(2);
	for(int i=0;i<4;i++)
	{
		manager.scheduleLock(&func);
	}
	return 0;
}
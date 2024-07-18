#include "ioscheduler.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <cstring>
#include <cerrno>

using namespace sylar;

char recv_data[4096];

const char data[] = "GET / HTTP/1.0\r\n\r\n"; 

int sock;

void func()
{
    recv(sock, recv_data, 4096, 0);
    std::cout << recv_data << std::endl << std::endl;
}

void func2()
{
    send(sock, data, sizeof(data), 0);
}

int main(int argc, char const *argv[])
{
	IOManager manager(2);


	sock = socket(AF_INET, SOCK_STREAM, 0);

	sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(80);  // HTTP 标准端口
    server.sin_addr.s_addr = inet_addr("103.235.46.96");

    fcntl(sock, F_SETFL, O_NONBLOCK);

    connect(sock, (struct sockaddr *)&server, sizeof(server));
  	
  	
    
    manager.addEvent(sock, IOManager::WRITE, &func2);
    manager.addEvent(sock, IOManager::READ, &func);

    std::cout << "event has been posted\n\n";
    
	return 0;
}
#ifndef HTTP_CONNECTION_H
#define HTTP_CONNECTION_H

#include<sys/epoll.h>
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<signal.h>
#include<sys/types.h>
#include<fcntl.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<sys/stat.h>
#include<sys/mman.h>
#include<errno.h>
#include<sys/uio.h>

#include "../Thread/locker.h"

//本项目采用proactor的模式来实现服务器
//在主线程中完成对数据的读写操作后将数据封装到一个类中，将这个类交给工作线程去处理
//这个类即为下面的任务类

//任务类
class HttpConnection{
    public:
    HttpConnection();
    ~HttpConnection();

    //处理客户端请求以及服务器的响应
    void process();

    //初始化新接收的客户端连接信息
    void init(int socketfd,const sockaddr_in &addr);

    //关闭连接
    void closeConnection();

    //非阻塞 一次性 读取数据
    bool read();

    //非阻塞 一次性 写入数据
    bool write();

    //所有socket上的事件都被注册到同一个epoll实例上
    static int m_epollfd;

    //统计用户的数量
    static int m_user_count;

    private:
    int m_socketfd;//该http连接的socket
    sockaddr_in m_address;//用于通信的socket的地址

};


#endif
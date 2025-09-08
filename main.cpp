#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<unistd.h>
#include<errno.h>
#include<fcntl.h>
#include<sys/epoll.h>
#include<signal.h>
#include<iostream>

#include "./Thread/locker.h"
#include "./Thread/thread_pool.h"
#include "./Task/http_connection.h"

//最大客户端数量
#define MAX_FD 65535  

//监听的最大的数量
#define MAX_EVENT_NUM 10000

// 数据库配置
#define MYSQL_HOST "localhost"
#define MYSQL_USER "webuser"   
#define MYSQL_PASSWORD "23456789"
#define MYSQL_DATABASE "bzk11_db"

//项目的入口  主线程  

//添加信号捕捉
void addSignal(int signal,void(handler)(int)){
    //sa为信号注册的参数
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));//清空sa

    //设置信号处理函数为我们传入的函数
    sa.sa_handler=handler;

    //将所有信号添加到阻塞集合中，处理当前信号时其余信号均阻塞
    sigfillset(&sa.sa_mask);

    //注册信号捕捉
    sigaction(signal,&sa,NULL);
}

//添加指定文件描述符到epoll实例
extern void addfd(int epollfd,int fd,bool one_shot);

//从epoll实例中去除指定文件描述符
extern void removefd(int epollfd,int fd);

//修改指定的文件描述符
extern void modifyfd(int epollfd,int fd,int ev);

int main(int argc,char *argv[]){
    //参数个数小于等于1说明用户没有传入端口号，参数只有命令，需要重新启动
    if(argc<=1){
        printf("按照如下格式运行：%s port_number\n",basename(argv[0]));
        exit(-1);
    }

    //获取端口号  （需要将命令参数中字符串格式的端口号转为整数）
    int port=atoi(argv[1]);

    //对SIGPIPE信号进行处理
    //由于该信号发生后程序将直接终止，服务器不应这样，出现一些错误应该通过自身程序处理
    //而不是直接终止  因此在网络编程中常常将这个信号忽略掉
    addSignal(SIGPIPE,SIG_IGN);

    // 初始化数据库连接
    std::cout << "正在初始化数据库连接..." << std::endl;
    if (!HttpConnection::initDatabase(MYSQL_HOST, MYSQL_USER, MYSQL_PASSWORD, MYSQL_DATABASE)) {
        std::cerr << "数据库初始化失败！请检查数据库配置和连接状态。" << std::endl;
        exit(-1);
    }
    std::cout << "数据库连接初始化成功！" << std::endl;

    //创建线程池，初始化线程池  HttpConnection即为任务类
    ThreadPool<HttpConnection>*pool=NULL;
    try{
        pool=new ThreadPool<HttpConnection>;
    }
    catch(...){
        //捕捉到异常说明线程池都没有建好，无法运行，直接退出
        std::cerr << "线程池创建失败！" << std::endl;
        delete pool;
        exit(-1);
    }

    //创建一个数组用于保存所有的客户端信息
    HttpConnection *users=new HttpConnection[MAX_FD];

    //创建用于监听的套接字
    int listenfd=socket(PF_INET,SOCK_STREAM,0);
    if(listenfd==-1){
        perror("创建套接字错误！");
        delete[] users;
        delete pool;
        exit(-1);
    }

    //设置端口复用  (必须绑定前设置)
    int reuse=1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    //绑定
    struct sockaddr_in address;
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=INADDR_ANY;
    address.sin_port=htons(port);//port为在命令参数中获取到的端口号

    int ret=bind(listenfd,(struct sockaddr*)&address,sizeof(address));
    if(ret==-1){
        perror("绑定错误！");
        close(listenfd);
        delete[] users;
        delete pool;
        exit(-1);
    }

    //监听
    ret=listen(listenfd,5);
    if(ret==-1){
        perror("监听错误");
        close(listenfd);
        delete[] users;
        delete pool;
        exit(-1);
    }

    //创建epoll实例 事件数组
    epoll_event events[MAX_EVENT_NUM];
    int epollfd=epoll_create(1);
    if(epollfd == -1){
        perror("创建epoll实例失败");
        close(listenfd);
        delete[] users;
        delete pool;
        exit(-1);
    }

    //将用于监听的文件描述符添加到epoll实例中
    //注意添加操作封装成了一个addfd()函数
    addfd(epollfd,listenfd,false);

    //设置用于事件注册的静态成员m_epollfd
    HttpConnection::m_epollfd=epollfd;

    std::cout << "服务器启动成功！监听端口: " << port << std::endl;
    std::cout << "等待客户端连接..." << std::endl;

    while(1){
        int num=epoll_wait(epollfd,events,MAX_EVENT_NUM,-1);
        if((num==-1)&&(errno != EINTR)){
            printf("epoll执行失败！\n");
            break;
        }

        //循环遍历事件数组
        for(int i=0;i<num;i++){
            int sockfd=events[i].data.fd;
            if(sockfd==listenfd){
                //由客户端连接请求
                struct sockaddr_in clientAddress;
                socklen_t clientAddressLen=sizeof(clientAddress);

                int connectfd=accept(listenfd,(struct sockaddr*)&clientAddress,&clientAddressLen);
                if(connectfd == -1){
                    perror("接受连接失败");
                    continue;
                }

                if(HttpConnection::m_user_count>=MAX_FD){
                    //目前的连接数已满
                    std::cout << "连接数已满，拒绝新连接" << std::endl;
                    
                    //给客户端发送服务器繁忙信息
                    const char* busy_msg = "HTTP/1.1 503 Service Unavailable\r\n"
                                          "Content-Type: text/plain\r\n"
                                          "Connection: close\r\n"
                                          "\r\n"
                                          "服务器繁忙，请稍后再试";
                    send(connectfd, busy_msg, strlen(busy_msg), 0);
                    
                    close(connectfd);
                    continue;
                }

                //将新的客户端数据放到数组中
                users[connectfd].init(connectfd,clientAddress);
                
                std::cout << "新客户端连接: " << inet_ntoa(clientAddress.sin_addr) 
                          << ":" << ntohs(clientAddress.sin_port) 
                          << "，连接ID: " << connectfd << std::endl;
            }
            else if(events[i].events & (EPOLLRDHUP | EPOLLHUP |EPOLLERR)){
                //对方异常断开或错误
                std::cout << "客户端异常断开，连接ID: " << sockfd << std::endl;
                users[sockfd].closeConnection();//关闭连接
            }
            else if(events[i].events & EPOLLIN){
                //可读事件发生
                if(users[sockfd].read()){
                    //一次性将所有数据读完
                    pool->addTask(users+sockfd);
                }
                else{
                    //读取失败
                    std::cout << "读取数据失败，关闭连接ID: " << sockfd << std::endl;
                    users[sockfd].closeConnection();//关闭连接
                }
            }
            else if(events[i].events & EPOLLOUT){
                if(!users[sockfd].write()){
                    //写(一次性)失败
                    std::cout << "写入数据失败，关闭连接ID: " << sockfd << std::endl;
                    users[sockfd].closeConnection();//关闭连接
                }
            }
        }

    }

    // 清理资源
    std::cout << "服务器正在关闭..." << std::endl;
    close(epollfd);
    close(listenfd);
    delete []users;
    delete pool;
    
    std::cout << "服务器已关闭" << std::endl;
    return 0;
}
#include "http_connection.h"

//初始化静态成员
int HttpConnection::m_epollfd=-1;
int HttpConnection::m_user_count=0;

//设置指定的文件描述符非阻塞
void setNonBlock(int fd){
    int old_flag=fcntl(fd,F_GETFL);
    int new_flag=old_flag | O_NONBLOCK;
    fcntl(fd,F_SETFL,new_flag);
}

//添加指定文件描述符到epoll实例
void addfd(int epollfd,int fd,bool one_shot){
    epoll_event event;
    event.data.fd=fd;
    //EPOLLRDHUP 精确检测对端关闭，支持半关闭状态	需要 Linux 2.6.17+ 内核支持
    event.events=EPOLLIN | EPOLLRDHUP;   //检测读事件  暂时使用水平触发模式的epoll

    //EPOLLONESHOT确保一个事件仅能够被一个线程所处理
    if(one_shot){
        event.events |= EPOLLONESHOT;
    }

    //将指定的文件描述符fd添加到epoll实例中
    epoll_ctl(epollfd,EPOLL_CTL_ADD,fd,&event);

    //设置文件描述符非阻塞
    setNonBlock(fd);

}

//从epoll实例中去除指定的文件描述符
void removefd(int epollfd,int fd){
    epoll_ctl(epollfd,EPOLL_CTL_DEL,fd,0);
    close(fd);
}

//修改指定的文件描述符
//重置socket上的EPOLLONESHOT事件，以确保下一次可读时，EPOLLIN事件能够被触发
void modifyfd(int epollfd,int fd,int ev){
    epoll_event event;
    event.data.fd=fd;
    //注意修改时需要加上EPOLLONESHOT和EPOLLRDHUP事件
    event.events=ev | EPOLLONESHOT | EPOLLRDHUP;

    //修改指定的文件描述符fd
    epoll_ctl(epollfd,EPOLL_CTL_MOD,fd,&event);
}

//构造函数
HttpConnection::HttpConnection(){

}

//析构函数
HttpConnection::~HttpConnection(){
    
}


//初始化最新连接的客户端信息
void HttpConnection::init(int socketfd,const sockaddr_in &addr){
    this->m_socketfd=socketfd;
    this->m_address=addr;

    //设置端口复用
    int reuse=1;
    setsockopt(m_socketfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    //添加到epoll对象中
    addfd(m_epollfd,m_socketfd,true);
    m_user_count++;//总用户数加1
}

//关闭连接
void HttpConnection::closeConnection(){
    if(m_socketfd != -1){
        removefd(m_epollfd,m_socketfd);
        m_socketfd=-1;
        m_user_count--;//关闭连接，客户数量减1
    }
}

//非阻塞 一次性 读取所有数据
bool HttpConnection::read(){
    printf("一次性读取数据\n");
    return true;
}

//非阻塞 一次性 写入数据
bool HttpConnection::write(){
    printf("一次性写入数据\n");
    return true;
}

//由线程池中的工作线程调用，是处理HTTP请求的入口函数  业务逻辑
void HttpConnection::process(){
    //解析HTTP请求
    printf("解析http请求，生成http响应\n");

    //生成HTTP响应
}
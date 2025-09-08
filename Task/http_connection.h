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
#include<string.h>
#include<regex>
#include <stdarg.h>
#include<pthread.h>
#include <string>
#include <iostream>

#include "../Thread/locker.h"
#include "../DataBaseModule/mysql_connection.h"  // 包含数据库连接头文件

//本项目采用proactor的模式来实现服务器
//在主线程中完成对数据的读写操作后将数据封装到一个类中，将这个类交给工作线程去处理
//这个类即为下面的任务类

//任务类
class HttpConnection{
    public:
    HttpConnection();
    ~HttpConnection();

     //HTTP请求方法
    enum METHOD {GET=0,POST,HEAD,PUT,DELETE,TRACE,OPTIONS,CONNECT};

    /*解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE：当前正在分析请求行
        CHECK_STATE_HEADER：当前正在分析请求头的字段
        CHECK_STATE_CONTENT：当前正在解析请求体
    */
    enum CHECK_STATE{CHECK_STATE_REQUESTLINE=0,CHECK_STATE_HEADER,CHECK_STATE_CONTENT};

    //从状态机的三种可能的状态，即行的读取状态：
    //1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS{LINE_OK=0,LINE_BAD,LINE_OPEN};


    /*服务器处理http请求的可能结果，报文解析的结果
        NO_REQUEST          :    请求不完整，需要继续读取客户端数据
        GET_REQUEST         :    表示获得了一个完成的客户请求
        BAD_REQUEST         :    表示客户请求语法错误
        NO_RESOURCE         :    表示服务器没有资源
        FORBIDDEN_REQUEST   :    表示客户端对资源没有足够的访问权限
        FILE_REQUEST        :    文件请求，表示获取文件成功
        INTERNAL_ERROR      :    表示服务器内部错误
        CLOSED_CONNECTION   :    表示客户端已经关闭了连接
    */
    enum HTTP_CODE {
        NO_REQUEST,GET_REQUEST,BAD_REQUEST,NO_RESOURCE,
        FORBIDDEN_REQUEST,FILE_REQUEST,INTERNAL_ERROR,
        CLOSED_CONNECTION,JSON_RESPONSE       
    };

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

    //读缓冲区的大小
    static const int READ_BUFFER_SIZE=2048;

    //写缓冲区的大小
    static const int WRITE_BUFFER_SIZE=1024;

    // 文件名的最大长度
    static const int FILENAME_LEN = 200;        

    // 初始化数据库连接（静态方法，在程序启动时调用一次）
    static bool initDatabase(const std::string& host, const std::string& user, 
                           const std::string& password, const std::string& database);

    private:
     //解析http请求
    HTTP_CODE processRead();

    //http响应
    bool processWrite(HTTP_CODE result);

    //解析http请求首行
    HTTP_CODE parseRequestLine(char *text);

    //解析请求头
    HTTP_CODE parseHeaders(char *text);

    //解析请求体
    HTTP_CODE parseContent(char *text);

    //解析具体的某一行
    LINE_STATUS parseLine();

    //具体的请求逻辑操作
    HTTP_CODE doRequest();

    // 获取Content-Type的函数
    const char* get_content_type(const char* filename);

    //以下是processWrite()函数封装http响应时所采用的函数
    void unmap();//内存映射
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type(const char* content_type = "text/html");
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length, const char* content_type = "text/html" );
    bool add_content_length( int content_length );
    bool add_keep();//客户端是否保持连接
    bool add_blank_line();


    //初始化连接其余的信息
    void init();

    //获取一行数据
    char* getLine(){return m_readBuf+m_start_line;};

    // 处理登录和注册请求
    HTTP_CODE handleLoginRequest();
    HTTP_CODE handleRegisterRequest();
    
    // 解析JSON请求体（简单实现）
    bool parseJsonBody();
    
    // 创建JSON响应
    std::string createJsonResponse(bool success, const std::string& message);

    // 数据库连接实例（静态）
    static MySQLConnection* m_db_connection;

    // 登录相关成员变量
    std::string m_post_content; // 存储POST请求体
    std::string m_json_username;
    std::string m_json_password;
    std::string m_json_email;

    int m_socketfd;//该http连接的socket

    sockaddr_in m_address;//用于通信的socket的地址

    char m_readBuf[READ_BUFFER_SIZE];//读缓冲区
    char m_writeBuf[WRITE_BUFFER_SIZE];//写缓冲区

    int m_read_index;//标识读缓冲区中以及读入的客户端数据的最后一个字节的下一个位置

    int m_checked_index;//当前正在分析的字符在读缓冲区的位置
    int m_start_line;//当前正在解析的行的起始位置

    CHECK_STATE m_check_state;//主状态机当前所处的状态

    char *m_url;//请求目标url
    char *m_version;//协议版本  此项目只支持http1.1
    METHOD m_method;//请求方法
    char* m_host;//主机名
    bool m_keep;//http请求是否要保持连接
    int m_content_length;// HTTP请求的消息总长度

    char m_real_file[ FILENAME_LEN ];// 客户请求的目标文件的完整路径，其内容等于 doc_root + m_url, doc_root是网站根目录
    int m_write_index;//写缓冲区中待发送的字节数
    char *m_file_address;//客户端请求的目标文件使用内存映射mmap后的内存中的首地址
    struct stat m_file_stat;//目标文件的状态  可用于判断文件是否存在，是否为目录，是否可读，获取文件大小等信息
    struct iovec m_iv[2];//采用writeev（分散写）来执行写操作
    int m_iv_count;//被写内存块的数量

};


#endif
#include "http_connection.h"
#include <iostream>
#include <cstring>

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

// 数据库连接静态变量初始化
MySQLConnection* HttpConnection::m_db_connection = nullptr;

// 网站的根目录
const char* doc_root = "/home/bz/webserver";

//初始化静态成员
int HttpConnection::m_epollfd=-1;
int HttpConnection::m_user_count=0;

// 初始化数据库连接
bool HttpConnection::initDatabase(const std::string& host, const std::string& user, 
                                const std::string& password, const std::string& database) {
    m_db_connection = MySQLConnection::getInstance();
    return m_db_connection->init(host, user, password, database);
}

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
    init();
}

//析构函数
HttpConnection::~HttpConnection(){
    if (m_url) {
        free(m_url);
        m_url = nullptr;
    }
    if (m_version) {
        free(m_version);
        m_version = nullptr;
    }
    // 确保关闭连接和取消内存映射
    closeConnection();
    unmap();
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

    init();
}

//初始化连接的其余信息
void HttpConnection::init(){
    m_check_state=CHECK_STATE_REQUESTLINE;//初始状态为解析请求首行
    m_checked_index=0;
    m_start_line=0;
    m_read_index=0;
    m_write_index=0;

    m_method=GET;
    // 确保指针初始化为nullptr
    m_url = nullptr;
    m_version = nullptr;
    m_keep=false;//默认不保持连接
    m_content_length=0;
    m_host=nullptr;

    bzero(m_readBuf,READ_BUFFER_SIZE);
    bzero(m_writeBuf,WRITE_BUFFER_SIZE);
    bzero(m_real_file,FILENAME_LEN);
    
    // 确保文件地址初始化为nullptr
    m_file_address = nullptr;
    m_iv_count = 0;
    
    // 清空POST相关数据
    m_post_content.clear();
    m_json_username.clear();
    m_json_password.clear();
    m_json_email.clear();
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
    if(m_read_index >= READ_BUFFER_SIZE){
        return false;
    }

    //读取到的字节
    int bytesRead=0;
    while(1){
        //注意需要从上一次读取到的字节的下一个位置开始读取
        bytesRead=recv(m_socketfd,m_readBuf+m_read_index,READ_BUFFER_SIZE-m_read_index,0);
        if(bytesRead==-1){
            if(errno == EAGAIN || errno ==EWOULDBLOCK){
                //没有数据
                break;
            }
            return false;
        }
        else if(bytesRead == 0){
            //客户端关闭连接
            return false;
        }
        m_read_index+=bytesRead;//更新最新的字节位置
    }
    printf("读取到了数据：\n%s\n",m_readBuf);
    return true;
}

// 对内存映射区执行munmap操作
void HttpConnection::unmap() {
    if (m_file_address && m_file_address != MAP_FAILED) {
        if (munmap(m_file_address, m_file_stat.st_size) == -1) {
            perror("munmap失败");
        }
        m_file_address = nullptr;
    }
}

//非阻塞 一次性 写入数据
bool HttpConnection::write(){
    printf("开始发送数据，总大小: %ld bytes\n", (long)(m_write_index + m_file_stat.st_size));
    printf("HTTP头大小: %d bytes\n", m_write_index);
    printf("文件大小: %ld bytes\n", (long)m_file_stat.st_size);
    
    int temp = 0;
    int bytes_have_send = 0;
    
    // 计算总共要发送的字节数
    int bytes_to_send = 0;
    for (int i = 0; i < m_iv_count; i++) {
        bytes_to_send += m_iv[i].iov_len;
    }

    if (bytes_to_send == 0) {
        modifyfd(m_epollfd, m_socketfd, EPOLLIN);
        init();
        return true;
    }

    while (true) {
        temp = writev(m_socketfd, m_iv, m_iv_count);
        
        if (temp < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // TCP缓冲区已满，等待下一次可写事件
                modifyfd(m_epollfd, m_socketfd, EPOLLOUT);
                return true;
            }
            // 其他错误，关闭连接
            unmap();
            return false;
        } else if (temp == 0) {
            // 连接已关闭
            unmap();
            return false;
        }

        printf("本次发送: %d bytes, 剩余: %d bytes\n", temp, bytes_to_send - bytes_have_send);
        bytes_have_send += temp;
        
        // 更新IO向量，处理部分发送的情况
        int remaining = temp;
        for (int i = 0; i < m_iv_count && remaining > 0; i++) {
            if (remaining >= (int)m_iv[i].iov_len) {
                // 当前向量已完全发送
                remaining -= m_iv[i].iov_len;
                m_iv[i].iov_len = 0;
                m_iv[i].iov_base = nullptr;
            } else {
                // 当前向量部分发送
                m_iv[i].iov_base = (char*)m_iv[i].iov_base + remaining;
                m_iv[i].iov_len -= remaining;
                remaining = 0;
            }
        }

        // 移除已完全发送的向量
        int new_count = 0;
        for (int i = 0; i < m_iv_count; i++) {
            if (m_iv[i].iov_len > 0) {
                m_iv[new_count] = m_iv[i];
                new_count++;
            }
        }
        m_iv_count = new_count;

        if (bytes_have_send >= bytes_to_send) {
            // 所有数据已发送完毕
            unmap();
            if (m_keep) {
                init();
                modifyfd(m_epollfd, m_socketfd, EPOLLIN);
            } else {
                closeConnection();
            }
            return true;
        }
    }
}

// 往写缓冲中写入待发送的数据
bool HttpConnection::add_response( const char* format, ... ) {
    if( m_write_index >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_writeBuf + m_write_index, WRITE_BUFFER_SIZE - 1 - m_write_index, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_index ) ) {
        return false;
    }
    m_write_index += len;
    va_end( arg_list );
    return true;
}

bool HttpConnection::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool HttpConnection::add_headers(int content_len, const char* content_type) {
    add_content_length(content_len);
    add_content_type(content_type);
    add_keep();
    add_blank_line();
    return true;
}

bool HttpConnection::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool HttpConnection::add_keep()
{
    return add_response( "Connection: %s\r\n", ( m_keep == true ) ? "keep-alive" : "close" );
}

bool HttpConnection::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool HttpConnection::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool HttpConnection::add_content_type(const char* content_type) {
    return add_response("Content-Type: %s\r\n", content_type);
}

// 获取Content-Type的函数
const char* HttpConnection::get_content_type(const char* filename) {
    const char* dot = strrchr(filename, '.');
    if (dot) {
        if (strcasecmp(dot, ".png") == 0) return "image/png";
        if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0) return "image/jpeg";
        if (strcasecmp(dot, ".gif") == 0) return "image/gif";
        if (strcasecmp(dot, ".bmp") == 0) return "image/bmp";
        if (strcasecmp(dot, ".ico") == 0) return "image/x-icon";
        if (strcasecmp(dot, ".css") == 0) return "text/css";
        if (strcasecmp(dot, ".js") == 0) return "application/javascript";
        if (strcasecmp(dot, ".json") == 0) return "application/json";
    }
    return "text/html";
}

//由线程池中的工作线程调用，是处理HTTP请求的入口函数  业务逻辑
void HttpConnection::process(){
    // 初始化MySQL连接（如果需要）
    if (m_db_connection == nullptr) {
        printf("Database connection is null\n");
    }
    
    //解析HTTP请求
    HTTP_CODE read_ret=processRead();
    if(read_ret==NO_REQUEST){
        //请求不完整，需要继续获取客户端数据
        modifyfd(m_epollfd,m_socketfd,EPOLLIN);
        return;
    }

    printf("解析http请求，生成http响应，结果码: %d\n", read_ret);

    //生成HTTP响应
    bool write_ret = processWrite( read_ret );
    if ( !write_ret ) {
        closeConnection();
    }
    modifyfd( m_epollfd, m_socketfd, EPOLLOUT);
}

//主状态机 解析请求
HttpConnection::HTTP_CODE HttpConnection::processRead(){
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;
    
    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK))
                || ((line_status = parseLine()) == LINE_OK)) {
        // 获取一行数据
        text = getLine();
        m_start_line = m_checked_index;
        printf("got 1 http line: %s\n", text);

        switch (m_check_state) {
            case CHECK_STATE_REQUESTLINE: {
                ret = parseRequestLine(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER: {
                ret = parseHeaders(text);
                if (ret == BAD_REQUEST) {
                    return BAD_REQUEST;
                } else if (ret == GET_REQUEST) {
                    // 检查是否是POST请求的特殊处理
                    if (m_method == POST) {
                        if (strcmp(m_url, "/login") == 0) {
                            printf("-------login request detected-------\n");
                            // 继续读取内容
                            m_check_state = CHECK_STATE_CONTENT;
                            return NO_REQUEST;
                        } else if (strcmp(m_url, "/register") == 0) {
                            printf("-------register request detected-------\n");
                            // 继续读取内容
                            m_check_state = CHECK_STATE_CONTENT;
                            return NO_REQUEST;
                        }
                    }
                    return doRequest();
                }
                break;
            }
            case CHECK_STATE_CONTENT: {
                ret = parseContent(text);
                if (ret == GET_REQUEST) {
                    // 检查是否是登录或注册请求
                    if (m_method == POST && strcmp(m_url, "/login") == 0) {
                        printf("Processing login request\n");
                        return handleLoginRequest();
                    } else if (m_method == POST && strcmp(m_url, "/register") == 0) {
                        printf("Processing register request\n");
                        return handleRegisterRequest();
                    }
                    return doRequest();
                }
                line_status = LINE_OPEN;
                break;
            }
            default: {
                return INTERNAL_ERROR;
            }
        }
    }
    return NO_REQUEST;
}

bool HttpConnection::processWrite(HTTP_CODE result){
    const char* content_type = "text/html"; // 默认值
    
    switch (result)
    {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ), content_type);
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ), content_type);
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ), content_type);
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title );
            add_headers(strlen( error_403_form), content_type);
            if ( ! add_content( error_403_form ) ) {
                return false;
            }
            break;
        case FILE_REQUEST:
            // 根据文件扩展名设置正确的Content-Type
            content_type = get_content_type(m_real_file);
            add_status_line(200, ok_200_title );
            add_headers(m_file_stat.st_size, content_type);
            
            m_iv[ 0 ].iov_base = m_writeBuf;
            m_iv[ 0 ].iov_len = m_write_index;
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        case JSON_RESPONSE:  //处理JSON响应
            // JSON响应已经在handle函数中构建好了，直接使用
            m_iv[0].iov_base = m_writeBuf;
            m_iv[0].iov_len = m_write_index;
            m_iv_count = 1;
            printf("准备发送JSON响应，长度: %d\n", m_write_index);
            printf("响应内容: %s\n", m_writeBuf);
            return true;
        case NO_REQUEST:
        case GET_REQUEST:
        case CLOSED_CONNECTION:
            // 这些状态不需要处理响应
            return false;
        default:
            return false;
    }

    m_iv[ 0 ].iov_base = m_writeBuf;
    m_iv[ 0 ].iov_len = m_write_index;
    m_iv_count = 1;
    return true;
}

//具体的请求逻辑操作
HttpConnection::HTTP_CODE HttpConnection::doRequest(){
    // "/home/bz/webserver"
    strcpy( m_real_file, doc_root );
    int len = strlen( doc_root );
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );
    
    // 获取文件状态信息
    if ( stat( m_real_file, &m_file_stat ) < 0 ) {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if ( ! ( m_file_stat.st_mode & S_IROTH ) ) {
        return FORBIDDEN_REQUEST;
    }

    // 判断是否是目录
    if ( S_ISDIR( m_file_stat.st_mode ) ) {
        return BAD_REQUEST;
    }

    // 以只读方式打开文件
    int fd = open( m_real_file, O_RDONLY );
    if (fd < 0) {
        return NO_RESOURCE;
    }

    // 创建内存映射 - 确保使用正确的文件大小
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );
    close( fd );
    
    if ( m_file_address == MAP_FAILED ) {
        m_file_address = nullptr;
        return INTERNAL_ERROR;
    }
    
    return FILE_REQUEST;
}

//解析HTTP请求，获得请求方法，目标URL，HTTP版本
HttpConnection::HTTP_CODE HttpConnection::parseRequestLine(char *text){
    // 检查并释放之前可能分配的内存
    if (m_url) {
        free(m_url);
        m_url = nullptr;
    }
    if (m_version) {
        free(m_version);
        m_version = nullptr;
    }
    
    //例如：GET /index.html HTTP/1.1
     // 定义正则表达式模式，匹配请求方法、URL和HTTP版本
    std::regex requestLinePattern(R"(^([A-Z]+)\s+([^\s]+)\s+HTTP/(\d\.\d)\s*$)");
    std::cmatch matches;
    
    // 尝试匹配请求行
    if (!std::regex_match(text, matches, requestLinePattern)) {
        return BAD_REQUEST;  // 格式不符合要求，返回错误
    }
    
    // 提取并设置请求方法
    std::string methodStr = matches[1].str();
    if (methodStr == "GET") {
        m_method = GET;
    } else if (methodStr == "POST") {
        m_method = POST;
        printf("POST method detected\n");
    } else if (methodStr == "HEAD") {
        m_method = HEAD;
    } else if (methodStr == "PUT") {
        m_method = PUT;
    } else if (methodStr == "DELETE") {
        m_method = DELETE;
    } else if (methodStr == "TRACE") {
        m_method = TRACE;
    } else if (methodStr == "OPTIONS") {
        m_method = OPTIONS;
    } else if (methodStr == "CONNECT") {
        m_method = CONNECT;
    } else {
        return BAD_REQUEST;  // 不支持的请求方法
    }
    
    // 设置URL和版本
    m_url = strdup(matches[2].str().c_str());  // 分配内存并复制URL
    std::string versionStr = matches[3].str();
    m_version = strdup(("HTTP/" + versionStr).c_str());  // 分配内存并复制版本
    
    // 检查HTTP版本是否为1.1或1.0
    if (versionStr != "1.1" && versionStr != "1.0") {
        return BAD_REQUEST;
    }
    
    // 检查URL是否以/开头
    if (m_url[0] != '/') {
        return BAD_REQUEST;
    }
    
    // 解析成功，改变主状态机的状态为解析请求头
    m_check_state = CHECK_STATE_HEADER;
    
    return NO_REQUEST;
}

//获取一行数据  判断依据 \r\n
HttpConnection::LINE_STATUS HttpConnection::parseLine(){
    char temp;
    for ( ; m_checked_index < m_read_index; ++m_checked_index ) {
        temp = m_readBuf[ m_checked_index ];
        if ( temp == '\r' ) {
            if ( ( m_checked_index + 1 ) == m_read_index ) {
                return LINE_OPEN;
            } else if ( m_readBuf[ m_checked_index + 1 ] == '\n' ) {
                m_readBuf[ m_checked_index++ ] = '\0';
                m_readBuf[ m_checked_index++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        } else if( temp == '\n' )  {
            if( ( m_checked_index > 1) && ( m_readBuf[ m_checked_index - 1 ] == '\r' ) ) {
                m_readBuf[ m_checked_index-1 ] = '\0';
                m_readBuf[ m_checked_index++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;
}

HttpConnection::HTTP_CODE HttpConnection::parseHeaders(char *text){
    // 遇到空行，表示头部字段解析完毕
    if (text[0] == '\0') {
        // 对于POST请求，必须有Content-Length
        if (m_method == POST) {
            if (m_content_length <= 0) {
                printf("POST request without Content-Length\n");
                return BAD_REQUEST;
            }
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    
    // 使用更清晰的解析逻辑
    char* key = text;
    char* value = strchr(text, ':');
    
    if (value == nullptr) {
        // 无效的头部格式
        return BAD_REQUEST;
    }
    
    // 分割键值对
    *value = '\0';  // 在冒号处截断
    value++;        // 移动到值部分
    
    // 跳过值前面的空白字符
    while (*value == ' ' || *value == '\t') {
        value++;
    }
    
    // 处理已知的头部字段
    if (strcasecmp(key, "Connection") == 0) {
        if (strcasecmp(value, "keep-alive") == 0) {
            m_keep = true;
        } else {
            m_keep = false;
        }
    } else if (strcasecmp(key, "Content-Length") == 0) {
        m_content_length = atol(value);
        printf("Content-Length: %d\n", m_content_length);
    } else if (strcasecmp(key, "Content-Type") == 0) {
        // 记录Content-Type，用于判断是否是JSON
        if (strstr(value, "application/json") != nullptr) {
            printf("JSON content detected\n");
        }
    } else if (strcasecmp(key, "Host") == 0) {
        m_host = value;
    }
    // 其他头部字段可以忽略
    
    return NO_REQUEST;
}

HttpConnection::HTTP_CODE HttpConnection::parseContent(char *text){
    if (m_read_index >= (m_content_length + m_checked_index)) {
        // 确保不会越界
        if (m_content_length > 0 && m_content_length < READ_BUFFER_SIZE - m_checked_index) {
            text[m_content_length] = '\0';
            
            // 保存POST请求体内容
            m_post_content = std::string(text, m_content_length);
            printf("POST content: %s\n", m_post_content.c_str());
        }
        
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

// 处理登录请求
HttpConnection::HTTP_CODE HttpConnection::handleLoginRequest() {
    printf("Handling login request\n");
    
    // 解析JSON请求体
    if (!parseJsonBody()) {
        printf("JSON parsing failed for login\n");
        return BAD_REQUEST;
    }
    
    if (m_json_username.empty() || m_json_password.empty()) {
        printf("Username or password is empty\n");
        return BAD_REQUEST;
    }
    
    if (m_db_connection == nullptr) {
        printf("Database connection is null\n");
        m_post_content = "{\"success\":false,\"message\":\"数据库连接未初始化\"}";
    } else {
        std::string errorMsg;
        bool success = false;
        
        try {
            success = m_db_connection->userLogin(m_json_username, m_json_password, errorMsg);
            printf("Login result: %s, message: %s\n", success ? "success" : "failed", errorMsg.c_str());
        } catch (const std::exception& e) {
            errorMsg = "数据库操作异常: ";
            errorMsg += e.what();
            success = false;
        }
        
        m_post_content = createJsonResponse(success, errorMsg);
    }
    
    add_status_line(200, ok_200_title);
    add_headers(m_post_content.length(), "application/json;charset=utf-8");
    add_content(m_post_content.c_str());
    
    m_iv[0].iov_base = m_writeBuf;
    m_iv[0].iov_len = m_write_index;
    m_iv_count = 1;
    
    return JSON_RESPONSE;
}

// 处理注册请求
HttpConnection::HTTP_CODE HttpConnection::handleRegisterRequest() {
    printf("Handling register request\n");
    
    // 解析JSON请求体
    if (!parseJsonBody()) {
        printf("JSON parsing failed for register\n");
        return BAD_REQUEST;
    }
    
    if (m_json_username.empty() || m_json_password.empty() || m_json_email.empty()) {
        printf("Username, password or email is empty\n");
        return BAD_REQUEST;
    }
    
    if (m_db_connection == nullptr) {
        printf("Database connection is null\n");
        m_post_content = "{\"success\":false,\"message\":\"数据库连接未初始化\"}";
    } else {
        std::string errorMsg;
        bool success = false;
        
        try {
            success = m_db_connection->userRegister(m_json_username, m_json_password, m_json_email, errorMsg);
            printf("Register result: %s, message: %s\n", success ? "success" : "failed", errorMsg.c_str());
        } catch (const std::exception& e) {
            errorMsg = "数据库操作异常: ";
            errorMsg += e.what();
            success = false;
        }
        
        m_post_content = createJsonResponse(success, errorMsg);
    }
    
    add_status_line(200, ok_200_title);
    add_headers(m_post_content.length(), "application/json");
    add_content(m_post_content.c_str());
    
    m_iv[0].iov_base = m_writeBuf;
    m_iv[0].iov_len = m_write_index;
    m_iv_count = 1;
    
    return JSON_RESPONSE;
}

// 解析JSON请求体
bool HttpConnection::parseJsonBody() {
    if (m_post_content.empty()) {
        printf("Empty POST content\n");
        return false;
    }
    
    printf("Raw JSON: %s\n", m_post_content.c_str());
    
    // 清空之前的数据
    m_json_username.clear();
    m_json_password.clear();
    m_json_email.clear();
    
    // 简单的JSON解析
    try {
        // 查找username字段
        size_t pos = m_post_content.find("\"username\"");
        if (pos != std::string::npos) {
            pos = m_post_content.find(':', pos);
            if (pos != std::string::npos) {
                size_t start = m_post_content.find('"', pos + 1);
                if (start != std::string::npos) {
                    size_t end = m_post_content.find('"', start + 1);
                    if (end != std::string::npos) {
                        m_json_username = m_post_content.substr(start + 1, end - start - 1);
                    }
                }
            }
        }
        
        // 查找password字段
        pos = m_post_content.find("\"password\"");
        if (pos != std::string::npos) {
            pos = m_post_content.find(':', pos);
            if (pos != std::string::npos) {
                size_t start = m_post_content.find('"', pos + 1);
                if (start != std::string::npos) {
                    size_t end = m_post_content.find('"', start + 1);
                    if (end != std::string::npos) {
                        m_json_password = m_post_content.substr(start + 1, end - start - 1);
                    }
                }
            }
        }
        
        // 查找email字段（注册时使用）
        pos = m_post_content.find("\"email\"");
        if (pos != std::string::npos) {
            pos = m_post_content.find(':', pos);
            if (pos != std::string::npos) {
                size_t start = m_post_content.find('"', pos + 1);
                if (start != std::string::npos) {
                    size_t end = m_post_content.find('"', start + 1);
                    if (end != std::string::npos) {
                        m_json_email = m_post_content.substr(start + 1, end - start - 1);
                    }
                }
            }
        }
        
        printf("Parsed - username: %s, password: %s, email: %s\n", 
               m_json_username.c_str(), m_json_password.c_str(), m_json_email.c_str());
        
        return !m_json_username.empty() && !m_json_password.empty();
    } catch (...) {
        printf("JSON parsing failed\n");
        return false;
    }
}

// 添加JSON字符串转义函数
std::string escapeJsonString(const std::string& input) {
    std::string output;
    output.reserve(input.length() * 2); // 预留足够空间
    
    for (char c : input) {
        switch (c) {
            case '"':  output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
                    // 控制字符，使用Unicode转义
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned char>(c));
                    output += buf;
                } else {
                    output += c;
                }
                break;
        }
    }
    return output;
}

// 生成json响应
std::string HttpConnection::createJsonResponse(bool success, const std::string& message) {
    std::string response = "{";
    response += "\"success\":" + std::string(success ? "true" : "false") + ",";
    response += "\"message\":\"" + escapeJsonString(message) + "\"";
    
    // 如果是登录成功，添加额外信息
    if (success && m_method == POST && strcmp(m_url, "/login") == 0) {
        response += ",\"username\":\"" + escapeJsonString(m_json_username) + "\"";
        response += ",\"redirect\":\"http://192.168.188.128:9090/login/personalProjectShow.html\"";
        response += ",\"timestamp\":" + std::to_string(time(nullptr));
    }
    
    response += "}";

    std::cout << "生成的JSON响应: " << response << std::endl; 
    return response;
}
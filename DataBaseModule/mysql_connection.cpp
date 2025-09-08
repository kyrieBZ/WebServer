#include "mysql_connection.h"


MySQLConnection* MySQLConnection::getInstance() {
    static MySQLConnection instance;
    return &instance;
}

MySQLConnection::MySQLConnection() : m_conn(nullptr) {}

MySQLConnection::~MySQLConnection() {
    close();
}

bool MySQLConnection::init(const std::string& host, const std::string& user, 
                         const std::string& password, const std::string& database) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_host = host;
    m_user = user;
    m_password = password;
    m_database = database;
    
    // 初始化MySQL连接
    m_conn = mysql_init(nullptr);
    if (m_conn == nullptr) {
        std::cerr << "MySQL初始化失败: " << mysql_error(m_conn) << std::endl;
        return false;
    }
    
    // 设置连接选项 - 启用自动重连
    my_bool reconnect = 1;
    mysql_options(m_conn, MYSQL_OPT_RECONNECT, &reconnect);
    
    // 连接到数据库
    if (!mysql_real_connect(m_conn, host.c_str(), user.c_str(), 
                          password.c_str(), database.c_str(), 0, nullptr, 0)) {
        std::cerr << "MySQL连接失败: " << mysql_error(m_conn) << std::endl;
        mysql_close(m_conn);
        m_conn = nullptr;
        return false;
    }
    
    // 设置字符集为UTF8
    if (mysql_set_character_set(m_conn, "utf8")) {
        std::cerr << "设置字符集失败: " << mysql_error(m_conn) << std::endl;
    }
    
    std::cout << "MySQL连接成功建立" << std::endl;
    return true;
}

void MySQLConnection::close() {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_conn != nullptr) {
        mysql_close(m_conn);
        m_conn = nullptr;
        std::cout << "MySQL连接已关闭" << std::endl;
    }
}

bool MySQLConnection::userLogin(const std::string& username, const std::string& password, std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_conn == nullptr) {
        errorMsg = "数据库连接未初始化";
        return false;
    }
    
    // 使用预处理语句防止SQL注入
    const char* query = "SELECT password FROM users WHERE username = ?";
    MYSQL_STMT* stmt = mysql_stmt_init(m_conn);
    
    if (stmt == nullptr) {
        errorMsg = "初始化预处理语句失败";
        return false;
    }
    
    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        errorMsg = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }
    
    // 绑定参数
    MYSQL_BIND bind_params[1];
    memset(bind_params, 0, sizeof(bind_params));
    
    bind_params[0].buffer_type = MYSQL_TYPE_STRING;
    bind_params[0].buffer = (void*)username.c_str();
    bind_params[0].buffer_length = username.length();
    
    if (mysql_stmt_bind_param(stmt, bind_params)) {
        errorMsg = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }
    
    // 执行查询
    if (mysql_stmt_execute(stmt)) {
        errorMsg = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }
    
    // 绑定结果
    char stored_password[256];
    MYSQL_BIND bind_result;
    memset(&bind_result, 0, sizeof(bind_result));
    
    bind_result.buffer_type = MYSQL_TYPE_STRING;
    bind_result.buffer = stored_password;
    bind_result.buffer_length = sizeof(stored_password);
    
    if (mysql_stmt_bind_result(stmt, &bind_result)) {
        errorMsg = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }
    
    // 获取结果
    if (mysql_stmt_fetch(stmt) != 0) {
        errorMsg = "用户不存在";
        mysql_stmt_close(stmt);
        return false;
    }
    
    mysql_stmt_close(stmt);
    
    // 验证密码（实际应用中应该使用密码哈希验证）
    if (password == stored_password) {
        std::cout<<"---------------bzbzbz---------------"<<std::endl;
        errorMsg = "登录成功";
        return true;
    } else {
        errorMsg = "密码错误";
        return false;
    }
}

bool MySQLConnection::userRegister(const std::string& username, const std::string& password, 
                                 const std::string& email, std::string& errorMsg) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_conn == nullptr) {
        errorMsg = "数据库连接未初始化";
        return false;
    }
    
    // 检查用户名是否已存在
    if (usernameExists(username)) {
        errorMsg = "用户名已存在";
        return false;
    }
    
    // 使用预处理语句防止SQL注入
    const char* query = "INSERT INTO users (username, password, email) VALUES (?, ?, ?)";
    MYSQL_STMT* stmt = mysql_stmt_init(m_conn);
    
    if (stmt == nullptr) {
        errorMsg = "初始化预处理语句失败";
        return false;
    }
    
    if (mysql_stmt_prepare(stmt, query, strlen(query))) {
        errorMsg = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }
    
    // 绑定参数
    MYSQL_BIND bind_params[3];
    memset(bind_params, 0, sizeof(bind_params));
    
    bind_params[0].buffer_type = MYSQL_TYPE_STRING;
    bind_params[0].buffer = (void*)username.c_str();
    bind_params[0].buffer_length = username.length();
    
    bind_params[1].buffer_type = MYSQL_TYPE_STRING;
    bind_params[1].buffer = (void*)password.c_str();
    bind_params[1].buffer_length = password.length();
    
    bind_params[2].buffer_type = MYSQL_TYPE_STRING;
    bind_params[2].buffer = (void*)email.c_str();
    bind_params[2].buffer_length = email.length();
    
    if (mysql_stmt_bind_param(stmt, bind_params)) {
        errorMsg = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }
    
    // 执行插入
    if (mysql_stmt_execute(stmt)) {
        errorMsg = mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }
    
    mysql_stmt_close(stmt);
    return true;
}

bool MySQLConnection::usernameExists(const std::string& username) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_conn == nullptr) {
        return false;
    }
    
    std::string query = "SELECT id FROM users WHERE username = '" + username + "'";
    
    if (mysql_query(m_conn, query.c_str())) {
        return false;
    }
    
    MYSQL_RES* result = mysql_store_result(m_conn);
    if (result == nullptr) {
        return false;
    }
    
    bool exists = (mysql_num_rows(result) > 0);
    mysql_free_result(result);
    
    return exists;
}

bool MySQLConnection::executeQuery(const std::string& query, MYSQL_RES** result) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_conn == nullptr || mysql_query(m_conn, query.c_str())) {
        return false;
    }
    
    *result = mysql_store_result(m_conn);
    return (*result != nullptr);
}

bool MySQLConnection::executeUpdate(const std::string& query) {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_conn == nullptr || mysql_query(m_conn, query.c_str())) {
        return false;
    }
    
    return true;
}

std::string MySQLConnection::getError() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    
    if (m_conn == nullptr) {
        return "数据库连接未初始化";
    }
    
    return mysql_error(m_conn);
}
#ifndef MYSQL_CONNECTION_H
#define MYSQL_CONNECTION_H

#include <mysql/mysql.h>
#include <string.h>
#include <mutex>
#include <iostream>

class MySQLConnection {
public:
    // 获取单例实例
    static MySQLConnection* getInstance();
    
    // 初始化数据库连接
    bool init(const std::string& host, const std::string& user, 
              const std::string& password, const std::string& database);
    
    // 关闭数据库连接
    void close();
    
    // 用户登录验证
    bool userLogin(const std::string& username, const std::string& password, std::string& errorMsg);
    
    // 用户注册
    bool userRegister(const std::string& username, const std::string& password, 
                     const std::string& email, std::string& errorMsg);
    
    // 检查用户名是否存在
    bool usernameExists(const std::string& username);
    
    // 执行查询语句
    bool executeQuery(const std::string& query, MYSQL_RES** result);
    
    // 执行更新语句
    bool executeUpdate(const std::string& query);
    
    // 获取错误信息
    std::string getError() const;

private:
    MySQLConnection();
    ~MySQLConnection();
    MySQLConnection(const MySQLConnection&) = delete;
    MySQLConnection& operator=(const MySQLConnection&) = delete;
    
    MYSQL* m_conn;
    std::string m_host;
    std::string m_user;
    std::string m_password;
    std::string m_database;
    mutable std::mutex m_mutex; // 用于线程安全
};

#endif // MYSQL_CONNECTION_H
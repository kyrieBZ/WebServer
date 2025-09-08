一、项目简介
  •使用C/C++语言开发，使用同步方式的proactor模式来设计，综合使用多线程，I/O多路复用，socket网络通信等技术。使用webbench工具进行压力测试，能够支持上万级并发客户的请求的响应。

二、项目文件说明
  Task文件夹中存放与http通信有关的源码实现，包括对http报文的封装以及解析等内容
  Thread文件夹中存放与线程有关的文件，线程池即由里面的thread_pool.h来实现
  NonActive中为持超时自动断开连接功能的实现
  testpressure中为压力测试相关代码
  DataBaseModule为数据库模块
  Login中存放登录功能对应的html页面
  main.cpp为项目入口，实现了Epoll监听文件描述符，套接字通信等功能

三、环境说明
  1.Linux环境：Ubuntu18 镜像文件：ubuntu-18.04.6-desktop-amd64.iso
  2.vscode编译器：1.84.2 x64
  3.虚拟机管理工具：VMware 16

四、项目编译运行
  项目中编写了makefile文件，编译时可直接make，但事先需要配置数据库
  以下为main.cpp中配置的数据库信息
#define MYSQL_HOST "localhost"
#define MYSQL_USER "webuser"   
#define MYSQL_PASSWORD "23456789"
#define MYSQL_DATABASE "bzk11_db"
  配置数据库首先需要创建一个名为MYSQL_DATABASE的数据库，项目中仅实现了登录功能，数据库中有一张表users，用于用户的username为testuser,password为test123
  上面的MYSQL_USER "webuser"是特别设置的用于远程访问数据库服务器的用户，需要使用sql创建并赋予其所有的权限。
  MYSQL_PASSWORD为安装mysql时设置的密码，MYSQL_HOST设置为本机即可
  设置远程访问数据库服务器用户的方法可看这篇文章：
  

  

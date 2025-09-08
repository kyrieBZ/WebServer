一、项目简介
  •使用C/C++语言开发，使用同步方式的proactor模式来设计，综合使用多线程，I/O多路复用，socket网络通信等技术。使用webbench工具进行压力测试，能够支持上万级并发客户的请求的响应。目前支持GET和POST这两种请求。
  项目中交互页面使用纯html编写，服务器处理逻辑由c/c++语言编写。
  项目架构：四层结构（I/O处理单元，逻辑单元，网络存储单元，请求队列）
    其中I/O处理单元负责处理客户连接，读写网络数据（主线程中实现）
        逻辑单元负责分析并处理客户数据（业务线程中实现）
        网络存储单元负责存储一些客户数据，例如项目中用户的账号密码
        请求队列负责进行单元间的通信

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
  设置远程访问数据库服务器用户的方法可看这篇文章：https://blog.csdn.net/2303_76152639/article/details/151322830?fromshare=blogdetail&sharetype=blogdetail&sharerId=151322830&sharerefer=PC&sharesource=2303_76152639&sharefrom=from_link
  数据库配置完，编译好后项目中会生成server可执行程序，执行./server 端口号 命令即可启动服务器
  启动服务器后在本机输入网址：http://服务器ip:端口号/resource/index.html即可访问。
  

  

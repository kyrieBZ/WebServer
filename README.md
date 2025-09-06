一、项目简介
  •使用C/C++语言开发，使用同步方式的proactor模式来设计，综合使用多线程，I/O多路复用，socket网络通信等技术。使用webbench工具进行压力测试，能够支持上万级并发客户的请求的响应。

二、项目文件说明
  Task文件夹中存放与http通信有关的源码实现，包括对http报文的封装以及解析等内容
  Thread文件夹中存放与线程有关的文件，线程池即由里面的thread_pool.h来实现

三、环境说明
  1.Linux环境：Ubuntu18 镜像文件：ubuntu-18.04.6-desktop-amd64.iso
  2.vscode编译器：1.84.2 x64
  3.虚拟机管理工具：VMware 16

四、项目编译运行
  项目中提供了makefile文件，仅需要将源码拷到虚拟机上直接make即可编译
  

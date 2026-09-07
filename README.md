#                                                聊天室项目

## 🗂️总况

基于 **Qt6 客户端 + Linux Epoll C 服务端**的TCP聊天室项目，支持群聊、私聊、在线用户列表、上下限广播，学习网络编程、TCP粘包处理、Qt 网络编程。

⚠本项目为**学习演示项目**，仅供教学练习，请勿直接用于生产环境。

## 📖项目简介

- **服务端**：纯 C 语言实现，基于 `epoll` IO 多路复用，Linux 平台运行。采用链表管理客户端连接，自定义文本协议，每条报文以 `\n` 换行作为分隔符，处理 TCP 粘包半包。
- **客户端**：Qt6 (C++) 图形界面程序，Windows 平台运行；QTcpSocket 完成 TCP 通信，图形界面实现登录连接、群聊、私聊、在线用户列表展示。
- 通信协议为自定义简单文本协议，全部报文以换行符 `\n` 结尾。

### ✨功能特性

1. TCP 长连接，客户端登录，本机 IP 作为聊天室用户名
2. **群聊**：向全部在线用户广播消息
3. **私聊**：指定单个用户点对点发送消息
4. 在线用户列表实时刷新
5. 用户上线、下线系统广播提示
6. 异常网络断开提示
7. 处理 TCP 粘包 / 半包问题（服务端 + 客户端均做缓冲区处理）

### 📁项目目录说明

```shell
.
├── Server
│   ├── chat_server_new.c      # Linux epoll聊天室服务端(C语言)
│   └── chat_client_new.c      # 聊天室客户端(C语言 用于测试)
├── Client
│   ├── CMakeLists.txt          # Qt6客户端CMake构建脚本
│   ├── main.cpp
│   ├── logindialog.h / logindialog.cpp    # 登录连接对话框
│   ├── mymain.h / mymain.cpp              # 聊天室主窗口
│   └── mymain.ui               # Qt UI界面文件
└── README.md
```

## 🛠环境依赖

### 服务端（Linux）

- GCC 编译器
- Linux 内核支持 epoll（主流 Ubuntu / CentOS 均可）
- pthread 线程库

### 客户端（Windows）

- **Qt 6.11+**
- CMake >=3.16
- MinGW‑64 编译套件

## 🚀编译与运行

### 1. 编译运行 Linux 服务端

```shell
# 进入server目录
cd server
# 编译，链接pthread库
gcc chart_server_new.c -o chat_server -lpthread

# 前台测试运行
./chat_server

# 后台守护运行（可选，用于云服务器）
nohup ./chat_server > server.log 2>&1 &

# 查看是否监听6666端口
ss -tulnp | grep 6666

```

默认监听端口：`6666`，监听地址 `0.0.0.0`，支持内网 / 公网访问。

云服务器部署注意：

1. 云平台安全组放行 TCP 6666 入站端口
2. Linux 防火墙放行 6666/tcp
3. 不要长期对外开放端口，本项目仅用于学习。

### 2. Qt6 客户端编译运行

1. 使用 Qt Creator 打开`client`目录 CMake 项目
2. Kit 选择 **Qt6‑MinGW‑64**
3. 删除旧构建缓存，Reload CMake Project，构建项目
4. 运行程序：
   - 服务端 IP：填写你的 Linux 服务器 IP（内网 IP / 云服务器公网 IP）
   - 端口：`6666`
   - 点击登录连接，进入聊天室。

## 📜自定义通信协议

> 每条报文末尾必须带换行符 `\n`

**客户端 → 服务端**

表格

| 报文                | 说明                      |
| ------------------- | ------------------------- |
| `login:name`        | 用户登录，name 为用户名   |
| `chat:message`      | 公聊群聊消息              |
| `to:target:message` | 私聊，target 为目标用户名 |

**服务端 → 客户端**

表格

| 报文                      | 说明                             |
| ------------------------- | -------------------------------- |
| `sys:xxx`                 | 系统消息，上线 / 下线 / 错误提示 |
| `users:user1,user2,user3` | 在线用户列表                     |
| `chat:sender:msg`         | 群聊转发消息                     |
| `to:sender:msg`           | 私聊转发消息                     |

## 📝技术要点

1. Linux epoll IO 多路复用 ET 边缘触发
2. 非阻塞 socket 编程
3. 链表管理在线客户端
4. TCP 粘包半包，使用换行作为报文分隔符，自定义缓冲区解析报文
5. Qt6 QTcpSocket 异步网络编程


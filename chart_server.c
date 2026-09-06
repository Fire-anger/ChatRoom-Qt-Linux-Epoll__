#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>

#define SERVER_PORT 6666
#define BUFFER_SIZE 1024
#define MAX_EVENTS 10

#define handle_error(cmd, result) \
    if (result < 0)               \
    {                             \
        perror(cmd);              \
        exit(EXIT_FAILURE);       \
    }

/*
 * 每个客户端连接结构体，每个fd对应一个实例
 * buf:私有接收缓冲区；   buf_len：缓冲区当前有效字节数
 * next：链表指针，管理全部在线客户端
 */
typedef struct Client
{
    int fd;
    char buf[BUFFER_SIZE];
    int buf_len;
    struct Client *next;
}Client;

Client *g_client_head = NULL;   // 全局客户端链表头(裸头指针)，只存链表指针，不存数据(和第一个有数据的链表节点一样的地址)

// 将 socketfd 设置为非阻塞模式
void set_nonblocking(int socketfd)
{
    int opts = fcntl(socketfd, F_GETFL);
    if (opts < 0)
    {
        perror("fcntl(F_GETFL)");
        exit(EXIT_FAILURE);
    }
    opts |= O_NONBLOCK;
    int res = fcntl(socketfd, F_SETFL, opts);
    if (res < 0)
    {
        perror("fcntl(F_SETFL)");
        exit(EXIT_FAILURE);
    }
}

// 创建新的Client节点
Client *client_create(int fd)
{
    Client *client = (Client *)malloc(sizeof(Client));
    if (!client)
    {
        perror("malloc Client");
        exit(EXIT_FAILURE);
    }
    client->fd = fd;
    memset(client->buf, 0, BUFFER_SIZE);
    client->buf_len = 0;
    client->next = NULL;

    // 头插法加入链表
    client->next = g_client_head;
    g_client_head = client;
    return client;
}

// 根据 fd 查找 Client 节点
Client *client_find_by_fd(int fd)
{
    Client *p = g_client_head;
    while (p)
    {
        if (p->fd == fd)
        {
            return p;
        }
        
        p = p->next;
    }
    return NULL;
}

// 删除并释放指定 fd 的 Client 节点     使用二级指针，处理起来会更方便，且逻辑清晰；如果是一级指针还要进行头结点的特殊处理
void client_destory(int fd, int epollfd)
{
    Client **pp = &g_client_head;  // 二级指针
    while (*pp != NULL)
    {
        Client *cur = *pp;
        if (cur->fd == fd)
        {
            *pp = cur->next;   // 链表摘除
            // 将该客户端 fd 从 epoll 监听集合中删除
            epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
            shutdown(fd, SHUT_WR);
            close(fd);
            free(cur);   // 释放堆区资源
            printf("客户端 fd=%d 已经从链表移除并释放资源\n", fd);
            return;
        }
        // 二级指针 (*pp)->next 指向的下一个节点的地址，还需要取址获取该指针的地址
        pp = &(*pp)->next;
    }
}

/*
 * 解析缓冲区报文：按换行符'\n'分割完整报文    这种情况：你好啊\nhello\n
 * 目前demo：每拿到一条完整报文直接回显给客户端
 * 后期聊天室在这里扩展：解析登录/公聊/私聊协议
 */
void parse_packet(Client *client)
{
    // 循环找换行符，处理多条报文粘在一起
    while (1)
    {
        // 在内存块中查找指定字符的第一次出现；成功：返回指向找到的字符的指针，失败：NULL
        char *p_nl = memchr(client->buf, '\n', client->buf_len);
        if (p_nl == NULL)
        {
            // 没有找到换行，说明是半包，留在buffer等下次数据到达
            break;
        }
        int pkt_len = p_nl - client->buf + 1;  // 包含 \n
        // 得到一条完整的报文 pkt_len 字节
        // %.*s：用于精确控制打印字符串的长度； % 格式说明符起始  . 精度修饰符  * 动态取值  s 字符串类型
        printf("fd=%d 收到完整的报文：%.*s", client->fd, pkt_len, client->buf);

        // ============ 此处后期替换为聊天室业务逻辑 ============
        // demo回显：把原消息发回客户端
        int ret = send(client->fd, client->buf, pkt_len, 0);
        if (ret <= 0)
        {
            perror("send fail, will close client");
            return;
        }
        // =====================================================

        // 将剩余未处理数据向前移动
        int remain = client->buf_len - pkt_len;
        // 内存移动函数  目标内存地址  源内存地址  要移动的字节数
        memmove(client->buf, client->buf + pkt_len, remain);
        client->buf_len = remain;
    }
}


int main()
{
    signal(SIGPIPE, SIG_IGN);

    // 声明sockfd、clientfd和函数返回状态变量
    int socketfd, client_fd, temp_result;
    // 声明服务端和客户端地址
    struct sockaddr_in server_addr, client_addr;
    // 清除，防止脏数据
    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));

    // 声明IPv4通信协议
    server_addr.sin_family = AF_INET;
    // 需要绑定0.0.0.0地址，转换成网络字节序后完成设置
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    // 端口随便一个，不用使用特权端口
    server_addr.sin_port = htons(SERVER_PORT);

    // 创建server socket
    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    handle_error("socket", socketfd);

    // 绑定地址
    temp_result = bind(socketfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    handle_error("bind", temp_result);

    // 将 socket 设置为监听模式，最大连接数为128
    temp_result = listen(socketfd, 128);
    handle_error("listen", temp_result);

    // 设置为非阻塞模式
    set_nonblocking(socketfd);
    // -------------------------------------epoll池设置------------------------------------------
    // 1.定义epoll池相关变量
    int epollfd, nfds;
    struct epoll_event ev, events[MAX_EVENTS];

    // 2.创建 epoll 池
    epollfd = epoll_create1(0);
    handle_error("epoll_create1", epollfd);

    // 3.将 socketfd 的 有数据 读 添加到感兴趣列表中
    ev.data.fd = socketfd;
    ev.events = EPOLLIN; // 设置为感兴趣的事为 有数据 可读
    // EPOLL_CTL_ADD 向 epoll 中添加的感兴趣信息的标记  socketfd 感兴趣的文件描述符  &ev 文件描述符的时间(有数据可读事件)
    temp_result = epoll_ctl(epollfd, EPOLL_CTL_ADD, socketfd, &ev);
    handle_error("epoll_ctl", temp_result);

    socklen_t cliaddr_len = sizeof(client_addr);

    while (1)
    {
        // 4.等待epoll中有具体的可读信息
        // 第一个循环 -> 只等待客户端连接进来 -> nfds表示有多少个客户端连接
        // 第n次循环 -> 既有新的客户端连接 还有 旧的客户端发过来消息 -> nfds此时表示有客户端连接和之前连接好的客户端发消息过来
        /**
         * epoll_wait阻塞等待事件产生
         * epollfd：epoll实例
         * events：输出参数，存放就绪事件数组
         * MAX_EVENTS：数组最大容量
         * -1：无限阻塞等待事件
         * 返回nfds：本次就绪事件的数量
         */
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        handle_error("epoll_wait", nfds);

        for (int i = 0; i < nfds; i++)
        {
            // 第一次循环 -> 只会走这个逻辑
            // 第n次循环 -> 有新的客户端连接也会走这个逻辑
            // 就绪fd是监听socket，代表有新客户端连接到达
            if (events[i].data.fd == socketfd)
            {
                // ET模式accept循环读取全部待接纳连接
                while (1)
                {
                    // 因为里面有客户端连接了，所以可以直接获取连接accept
                    client_fd = accept(socketfd, (struct sockaddr *)&client_addr, &cliaddr_len);
                    if (client_fd < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            break;
                        }
                        handle_error("accept", client_fd);
                    }

                    // 将获取的文件描述符也设置为非阻塞状态，这是ET模式硬性的要求，新连接fd必须为非阻塞
                    set_nonblocking(client_fd);
                    printf("与客户端 from %s at Port %d 文件描述符 %d 建立连接\n", 
                        inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port),client_fd);

                    // 创建客户端节点加入链表
                    client_create(client_fd);

                    // 设置新的客户端 fd 事件：可读 + 边缘触发EPOLLET  可以和客户端进行读写操作
                    ev.data.fd = client_fd;
                    ev.events = EPOLLIN | EPOLLET; // EPOLLIN 有数据 可读事件  EPOLLET 上升沿触发(server_fd没有这个)
                    // 将客户端 fd 加入 epoll 监听组合
                    // EPOLL_CTL_ADD 向 epoll 中添加的感兴趣信息的标记  socketfd 感兴趣的文件描述符  &ev 文件描述符的时间(有数据可读事件)
                    temp_result = epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &ev);
                    handle_error("epoll_ctl", temp_result);
                }
            }
            // 第n次循环 -> 旧的连接，有数据过来了  EPOLLIN 有数据可读事件
            // 普通客户端fd触发EPOLLIN：socket有数据可读
            else if (events[i].events & EPOLLIN)
            {
                int fd = events[i].data.fd;
                Client *client = client_find_by_fd(fd);
                if (client == NULL)
                {
                    continue;
                }

                /**
                 * EPOLLET边缘触发，必须循环recv直到EAGAIN，一次性读完缓冲区全部数据
                 * count>0 读到有效数据
                 * count=0 对端正常关闭连接
                 * count=-1 && errno==EAGAIN，内核缓冲区数据读完
                 */
                int count;
                while ((count = recv(fd, client->buf + client->buf_len, BUFFER_SIZE - client->buf_len, 0)) > 0)
                {
                    client->buf_len += count;
                    parse_packet(client);   // 尝试解析已经收到的字节
                }
                
                if (count == -1 && errno == EAGAIN)
                {
                    // 数据读完，等待下一次事件
                    continue;
                }
                else if (count == 0)
                {
                    // 对端正常关闭
                    printf("客户端fd=%d主动关闭连接\n", fd);
                    client_destory(fd, epollfd);
                }
                else
                {
                    // recv 异常，直接销毁客户端
                    perror("recv error");
                    client_destory(fd, epollfd);
                }
            }   
        }
    }
    close(epollfd);
    close(socketfd);
    return 0;
}

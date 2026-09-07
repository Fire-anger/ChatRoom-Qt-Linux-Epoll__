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
#define NAME_SIZE 64          // 用户名最大长度

#define handle_error(cmd, result) \
    if (result < 0)               \
    {                             \
        perror(cmd);              \
        exit(EXIT_FAILURE);       \
    }

/*
 * 聊天室协议（每条报文以 '\n' 结尾）
 * 客户端 -> 服务端：
 *   login:名字           登录，名字作为唯一用户名
 *   chat:内容            公聊，服务端广播给所有在线用户
 *   to:目标用户:内容      私聊，服务端只转发给目标用户
 * 服务端 -> 客户端：
 *   sys:内容             系统消息（上线/下线提示、错误提示）
 *   users:用户1,用户2    在线用户列表（变化时全量推送）
 *   chat:发送者:内容      公聊消息（转发给除发送者外的所有人）
 *   to:发送者:内容        私聊消息（只转发给目标用户）
 */

/*
 * 每个客户端连接结构体，每个fd对应一个实例
 * name:登录用户名； buf:私有接收缓冲区； buf_len：缓冲区当前有效字节数
 * next：链表指针，管理全部在线客户端
 */
typedef struct Client
{
    int fd;
    char name[NAME_SIZE];
    char buf[BUFFER_SIZE];
    int buf_len;
    struct Client *next;
}Client;

Client *g_client_head = NULL;   // 全局客户端链表头(裸头指针)

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
    memset(client->name, 0, NAME_SIZE);
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

// 根据用户名查找已登录的 Client 节点
Client *client_find_by_name(const char *name)
{
    Client *p = g_client_head;
    while (p)
    {
        if (p->name[0] != '\0' && strcmp(p->name, name) == 0)
        {
            return p;
        }
        p = p->next;
    }
    return NULL;
}

// 尽力把 data 的 len 字节全部发送出去（非阻塞socket，缓冲满则放弃剩余，演示级实现）
void send_all(int fd, const char *data, int len)
{
    int total = 0;
    while (total < len)
    {
        int ret = send(fd, data + total, len - total, 0);
        if (ret < 0)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;          // 发送缓冲已满，丢弃剩余（演示级处理）
            }
            else if (errno == EINTR)
            {
                continue;       // 被信号打断，重试
            }
            else
            {
                perror("send");
                break;
            }
        }
        else if (ret == 0)
        {
            break;
        }
        total += ret;
    }
}

// 广播给所有已登录客户端；exclude 不为 NULL 时跳过该客户端（用于跳过发送者/已下线者）
void broadcast(const char *data, int len, Client *exclude)
{
    Client *p = g_client_head;
    while (p)
    {
        if (p != exclude && p->name[0] != '\0')
        {
            send_all(p->fd, data, len);
        }
        p = p->next;
    }
}

// 把当前所有已登录用户名拼成 users:xxx,yyy 广播给所有人
void send_user_list(void)
{
    char buf[4096];
    int len = snprintf(buf, sizeof(buf), "users:");

    Client *p = g_client_head;
    int first = 1;
    while (p && len < (int)sizeof(buf) - 2)
    {
        if (p->name[0] != '\0')
        {
            int n = snprintf(buf + len, sizeof(buf) - len, "%s%s",
                             first ? "" : ",", p->name);
            if (n < 0) n = 0;
            len += n;
            first = 0;
        }
        p = p->next;
    }
    if (len < (int)sizeof(buf) - 1)
    {
        buf[len++] = '\n';
    }
    broadcast(buf, len, NULL);
}

// 删除并释放指定 fd 的 Client 节点
void client_destory(int fd, int epollfd)
{
    Client **pp = &g_client_head;  // 二级指针
    while (*pp != NULL)
    {
        Client *cur = *pp;
        if (cur->fd == fd)
        {
            // 如果该客户端已登录，先广播下线提示（排除它自己）
            if (cur->name[0] != '\0')
            {
                char sys[BUFFER_SIZE];
                int slen = snprintf(sys, sizeof(sys), "sys:%s 离开了聊天室\n", cur->name);
                broadcast(sys, slen, cur);
            }
            *pp = cur->next;   // 链表摘除
            // 将该客户端 fd 从 epoll 监听集合中删除
            epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL);
            shutdown(fd, SHUT_WR);
            close(fd);
            free(cur);   // 释放堆区资源
            printf("客户端 fd=%d 已经从链表移除并释放资源\n", fd);
            // 用户列表发生变化，推送最新列表
            send_user_list();
            return;
        }
        pp = &(*pp)->next;
    }
}

// 处理一条完整报文（不含结尾换行符）
void handle_message(Client *client, const char *line, int line_len)
{
    // 拷贝成以 '\0' 结尾的字符串，方便用字符串函数解析
    char msg[BUFFER_SIZE];
    int len = line_len;
    if (len >= (int)sizeof(msg)) len = sizeof(msg) - 1;
    memcpy(msg, line, len);
    msg[len] = '\0';

    // ---------- 登录：login:名字 ----------
    if (strncmp(msg, "login:", 6) == 0)
    {
        const char *name = msg + 6;
        if (name[0] == '\0') name = "anonymous";
        strncpy(client->name, name, NAME_SIZE - 1);
        client->name[NAME_SIZE - 1] = '\0';

        // 广播上线提示（包括它自己，让它也看到欢迎语）
        char sys[BUFFER_SIZE];
        int slen = snprintf(sys, sizeof(sys), "sys:%s 加入了聊天室\n", client->name);
        broadcast(sys, slen, NULL);

        // 推送最新在线用户列表
        send_user_list();
        printf("客户端 %s (fd=%d) 登录\n", client->name, client->fd);
    }
    // ---------- 公聊：chat:内容 ----------
    else if (strncmp(msg, "chat:", 5) == 0)
    {
        if (client->name[0] == '\0')
        {
            send_all(client->fd, "sys:请先登录再聊天\n", sizeof("sys:请先登录再聊天\n") - 1);
            return;
        }
        const char *text = msg + 5;
        char out[BUFFER_SIZE];
        int olen = snprintf(out, sizeof(out), "chat:%s:%s\n", client->name, text);
        // 广播给除发送者以外的所有人
        broadcast(out, olen, client);
        printf("fd=%d (%s) 公聊: %s\n", client->fd, client->name, text);
    }
    // ---------- 私聊：to:目标用户:内容 ----------
    else if (strncmp(msg, "to:", 3) == 0)
    {
        if (client->name[0] == '\0')
        {
            send_all(client->fd, "sys:请先登录再聊天\n", sizeof("sys:请先登录再聊天\n") - 1);
            return;
        }
        const char *rest = msg + 3;
        const char *colon = strchr(rest, ':');
        if (colon == NULL)
        {
            send_all(client->fd, "sys:私聊格式错误，应为 to:用户:内容\n",
                     sizeof("sys:私聊格式错误，应为 to:用户:内容\n") - 1);
            return;
        }

        char target[NAME_SIZE];
        int tlen = colon - rest;
        if (tlen >= (int)sizeof(target)) tlen = sizeof(target) - 1;
        memcpy(target, rest, tlen);
        target[tlen] = '\0';
        const char *text = colon + 1;

        Client *tgt = client_find_by_name(target);
        if (tgt == NULL)
        {
            char err[BUFFER_SIZE];
            int elen = snprintf(err, sizeof(err), "sys:用户[%s]不在线或不存在\n", target);
            send_all(client->fd, err, elen);
            return;
        }

        char out[BUFFER_SIZE];
        int olen = snprintf(out, sizeof(out), "to:%s:%s\n", client->name, text);
        // 只发送给目标用户
        send_all(tgt->fd, out, olen);
        printf("fd=%d (%s) 私聊 %s: %s\n", client->fd, client->name, target, text);
    }
    else
    {
        // 未知协议，提示一下
        send_all(client->fd, "sys:未知协议报文\n", sizeof("sys:未知协议报文\n") - 1);
    }
}

/*
 * 解析缓冲区报文：按换行符'\n'分割完整报文
 * 每拿到一条完整报文交给 handle_message 做业务处理
 */
void parse_packet(Client *client)
{
    // 循环找换行符，处理多条报文粘在一起
    while (1)
    {
        char *p_nl = memchr(client->buf, '\n', client->buf_len);
        if (p_nl == NULL)
        {
            // 没有找到换行，说明是半包，留在buffer等下次数据到达
            break;
        }
        int pkt_len = p_nl - client->buf + 1;  // 包含 \n
        printf("fd=%d 收到完整报文：%.*s", client->fd, pkt_len, client->buf);

        // 处理一条完整报文（去掉结尾换行）
        handle_message(client, client->buf, pkt_len - 1);

        // 将剩余未处理数据向前移动
        int remain = client->buf_len - pkt_len;
        memmove(client->buf, client->buf + pkt_len, remain);
        client->buf_len = remain;
    }
}


int main()
{
    signal(SIGPIPE, SIG_IGN);

    int socketfd, client_fd, temp_result;
    struct sockaddr_in server_addr, client_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    memset(&client_addr, 0, sizeof(client_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(SERVER_PORT);

    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    handle_error("socket", socketfd);

    temp_result = bind(socketfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    handle_error("bind", temp_result);

    temp_result = listen(socketfd, 128);
    handle_error("listen", temp_result);

    set_nonblocking(socketfd);

    int epollfd, nfds;
    struct epoll_event ev, events[MAX_EVENTS];

    epollfd = epoll_create1(0);
    handle_error("epoll_create1", epollfd);

    ev.data.fd = socketfd;
    ev.events = EPOLLIN;
    temp_result = epoll_ctl(epollfd, EPOLL_CTL_ADD, socketfd, &ev);
    handle_error("epoll_ctl", temp_result);

    socklen_t cliaddr_len = sizeof(client_addr);

    while (1)
    {
        nfds = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        handle_error("epoll_wait", nfds);

        for (int i = 0; i < nfds; i++)
        {
            // 监听socket就绪：有新客户端连接
            if (events[i].data.fd == socketfd)
            {
                while (1)
                {
                    client_fd = accept(socketfd, (struct sockaddr *)&client_addr, &cliaddr_len);
                    if (client_fd < 0)
                    {
                        if (errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            break;
                        }
                        handle_error("accept", client_fd);
                    }

                    set_nonblocking(client_fd);
                    printf("与客户端 from %s at Port %d 文件描述符 %d 建立连接\n",
                        inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), client_fd);

                    client_create(client_fd);

                    ev.data.fd = client_fd;
                    ev.events = EPOLLIN | EPOLLET;
                    temp_result = epoll_ctl(epollfd, EPOLL_CTL_ADD, client_fd, &ev);
                    handle_error("epoll_ctl", temp_result);
                }
            }
            // 普通客户端fd触发EPOLLIN：socket有数据可读
            else if (events[i].events & EPOLLIN)
            {
                int fd = events[i].data.fd;
                Client *client = client_find_by_fd(fd);
                if (client == NULL)
                {
                    continue;
                }

                int count = 0;
                // 留出至少1字节空间，避免缓冲区满时 recv 传 0 被误判为连接关闭
                while (client->buf_len < BUFFER_SIZE &&
                       (count = recv(fd, client->buf + client->buf_len,
                                     BUFFER_SIZE - client->buf_len, 0)) > 0)
                {
                    client->buf_len += count;
                    parse_packet(client);
                }

                if (count == -1)
                {
                    if (errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        continue;   // 数据读完，等待下一次事件
                    }
                    perror("recv error");
                    client_destory(fd, epollfd);
                }
                else if (count == 0 && client->buf_len < BUFFER_SIZE)
                {
                    // 对端正常关闭连接
                    printf("客户端fd=%d主动关闭连接\n", fd);
                    client_destory(fd, epollfd);
                }
                else if (client->buf_len >= BUFFER_SIZE)
                {
                    // 缓冲区已满却仍没有换行符，报文异常，直接断开
                    printf("客户端fd=%d报文过长(超过%d字节无换行)，断开连接\n", fd, BUFFER_SIZE);
                    client_destory(fd, epollfd);
                }
            }
        }
    }
    close(epollfd);
    close(socketfd);
    return 0;
}

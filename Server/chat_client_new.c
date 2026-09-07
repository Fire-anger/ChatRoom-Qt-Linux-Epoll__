#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

/**
 * @brief 错误处理宏，系统调用返回小于0时打印错误并返回 -1
 * @param cmd 发生错误的函数名称字符串
 * @param result 系统调用返回值
 */
#define handle_error(cmd, result) \
    if (result < 0)               \
    {                             \
        perror(cmd);              \
        return -1;                \
    }

#define BUF_SIZE 1024

/*
 * 聊天协议（与服务端 chart_server.c 保持一致，每条报文以 '\n' 结尾）
 * 客户端 -> 服务端：
 *   login:名字            登录（名字作为唯一用户名，用于私聊寻址）
 *   chat:内容             大群公聊，服务端广播给所有在线用户
 *   to:目标用户:内容       私聊，服务端只转发给目标用户
 * 服务端 -> 客户端：
 *   sys:内容              系统消息（上线/下线/错误提示）
 *   users:用户1,用户2      在线用户列表
 *   chat:发送者:内容       公聊消息
 *   to:发送者:内容         私聊消息
 */

// 全局登录用户名（读线程打印时可能用到，这里先保留方便扩展）
static char g_name[64] = "client";

/**
 * @brief 尽力把 data 的 len 字节全部发送出去（处理部分发送）
 */
static void send_all(int fd, const char *data, size_t len)
{
    size_t total = 0;
    while (total < len)
    {
        ssize_t ret = send(fd, data + total, len - total, 0);
        if (ret < 0)
        {
            perror("send");
            break;
        }
        total += (size_t)ret;
    }
}

/**
 * @brief 把服务端发来的一条完整报文（不含换行）按协议格式化打印
 */
static void display_server_msg(const char *line)
{
    if (strncmp(line, "sys:", 4) == 0)
    {
        printf("[系统] %s\n", line + 4);
    }
    else if (strncmp(line, "users:", 6) == 0)
    {
        printf("[在线用户] %s\n", line + 6);
    }
    else if (strncmp(line, "chat:", 5) == 0)
    {
        // chat:发送者:内容
        const char *p = line + 5;
        const char *colon = strchr(p, ':');
        if (colon)
        {
            int sender_len = (int)(colon - p);
            printf("[%.*s](群聊) %s\n", sender_len, p, colon + 1);
        }
        else
        {
            printf("%s\n", line);
        }
    }
    else if (strncmp(line, "to:", 3) == 0)
    {
        // to:发送者:内容
        const char *p = line + 3;
        const char *colon = strchr(p, ':');
        if (colon)
        {
            int sender_len = (int)(colon - p);
            printf("[%.*s](私聊) %s\n", sender_len, p, colon + 1);
        }
        else
        {
            printf("%s\n", line);
        }
    }
    else
    {
        printf("%s\n", line);
    }
}

/**
 * @brief 线程函数：接收服务端数据，按 '\n' 拆包（解决粘包/半包）后格式化打印
 * @param arg 客户端socket文件描述符指针
 */
void *read_from_server(void *arg)
{
    int sock_fd = *(int *)arg;
    char buf[BUF_SIZE];       // recv 临时缓冲
    char line[BUF_SIZE];      // 累计一条完整报文
    int line_len = 0;
    ssize_t count = 0;

    while ((count = recv(sock_fd, buf, BUF_SIZE - 1, 0)) > 0)
    {
        buf[count] = '\0';
        for (int i = 0; i < count; i++)
        {
            char c = buf[i];
            if (c == '\n')
            {
                line[line_len] = '\0';
                display_server_msg(line);
                line_len = 0;
            }
            else if (line_len < (int)sizeof(line) - 1)
            {
                line[line_len++] = c;
            }
        }
    }

    printf("\n[系统] 与服务端的连接已断开\n");
    return NULL;
}

/**
 * @brief 线程函数：读取控制台输入，解析命令后发送给服务端
 *  直接输入文字           -> 群聊 chat:文字
 *  @用户名 文字           -> 私聊 to:用户名:文字
 *  /quit 或 Ctrl+D        -> 退出
 * @param arg 客户端socket文件描述符指针
 */
void *write_to_server(void *arg)
{
    int sock_fd = *(int *)arg;
    char buf[BUF_SIZE];
    char pkt[BUF_SIZE + 16];

    printf("--------------------------------------------------\n");
    printf("  直接输入文字      = 大群公聊\n");
    printf("  @用户名 文字      = 给某用户私聊\n");
    printf("  /quit             = 退出聊天室\n");
    printf("--------------------------------------------------\n");

    while (fgets(buf, BUF_SIZE, stdin) != NULL)
    {
        // 去掉末尾换行符
        size_t len = strlen(buf);
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r'))
            buf[--len] = '\0';
        if (len == 0) continue;

        // 退出命令
        if (strcmp(buf, "/quit") == 0 || strcmp(buf, "/exit") == 0)
        {
            break;
        }
        // 私聊：@用户名 内容
        else if (buf[0] == '@')
        {
            char *space = strchr(buf, ' ');
            if (space == NULL || space[1] == '\0')
            {
                printf("[提示] 私聊格式: @用户名 消息内容\n");
                continue;
            }
            *space = '\0';
            const char *target = buf + 1;
            const char *text = space + 1;
            int n = snprintf(pkt, sizeof(pkt), "to:%s:%s\n", target, text);
            send_all(sock_fd, pkt, (size_t)n);
            printf("[我→%s](私聊) %s\n", target, text);
        }
        // 群聊：chat:内容
        else
        {
            int n = snprintf(pkt, sizeof(pkt), "chat:%s\n", buf);
            send_all(sock_fd, pkt, (size_t)n);
            printf("[我](群聊) %s\n", buf);
        }
    }

    printf("[系统] 退出聊天室，关闭连接\n");
    // SHUT_WR 关闭写半连接，向服务端发送EOF
    shutdown(sock_fd, SHUT_WR);
    return NULL;
}

/**
 * @brief 主函数：连接服务端 -> 登录 -> 启动读写线程
 * 用法: ./chart_client [服务端IP] [端口] [用户名]
 * 例如: ./chart_client 192.168.88.137 6666 张三
 */
int main(int argc, char *argv[])
{
    const char *ip   = (argc > 1) ? argv[1] : "192.168.88.137";
    int         port = (argc > 2) ? atoi(argv[2]) : 6666;
    const char *name = (argc > 3) ? argv[3] : "client";

    // 保存登录名（用户名，用于私聊寻址，多开时务必不同）
    strncpy(g_name, name, sizeof(g_name) - 1);
    g_name[sizeof(g_name) - 1] = '\0';

    int socketfd;
    int temp_result;
    struct sockaddr_in serv_addr;
    pthread_t pid_read, pid_write;

    memset(&serv_addr, 0, sizeof(serv_addr));

    // 填充远端服务端地址信息
    serv_addr.sin_family = AF_INET;
    // 指定要连接的服务端IP
    inet_pton(AF_INET, ip, &serv_addr.sin_addr);
    // 端口必须转换为网络大端字节序 htons
    serv_addr.sin_port = htons((uint16_t)port);

    // 创建TCP流式socket
    socketfd = socket(AF_INET, SOCK_STREAM, 0);
    handle_error("socket", socketfd);

    // 主动向远端服务器发起TCP连接
    temp_result = connect(socketfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    handle_error("connect", temp_result);

    printf("连接上服务器 %s : %d，登录名 = %s\n",
           inet_ntoa(serv_addr.sin_addr), port, g_name);

    // 连接成功后发送登录报文
    char login[BUF_SIZE];
    int n = snprintf(login, sizeof(login), "login:%s\n", g_name);
    send_all(socketfd, login, (size_t)n);

    // 创建读线程：接收服务端数据并打印
    pthread_create(&pid_read, NULL, read_from_server, (void *)&socketfd);
    // 创建写线程：读取控制台输入发送给服务端
    pthread_create(&pid_write, NULL, write_to_server, (void *)&socketfd);

    // 主线程阻塞，等待两个子线程执行完毕
    pthread_join(pid_read, NULL);
    pthread_join(pid_write, NULL);

    printf("释放资源\n");
    close(socketfd);
    return 0;
}

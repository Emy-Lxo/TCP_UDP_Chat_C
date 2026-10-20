/* ============================================================================
 * tcp_client.c —— TCP 聊天室客户端（全志 T113 / Linux 版）
 * ----------------------------------------------------------------------------
 * 编译： 在电脑上执行 make（详见同目录 Makefile）
 *
 * 用法：
 *   ./tcp_client                 连接默认服务器（本机 127.0.0.1）
 *   ./tcp_client 192.168.1.100   连接指定 IP 的服务器（板子连电脑时用这种）
 *
 * 交互：
 *   输入昵称回车进入，然后输入消息回车发送，输入 quit 退出。
 *
 * 为什么要有两个线程？
 *   fgets（读键盘）和 recv（收消息）都是"阻塞"的：
 *   一个线程只能同时干一件事，所以必须一个线程收、一个线程发。
 * ============================================================================
 */

#include <stdio.h>          /* printf / fgets                      */
#include <stdlib.h>         /* exit                                */
#include <string.h>         /* memset / strcspn                    */
#include <unistd.h>         /* close                               */
#include <pthread.h>        /* pthread_create                      */
#include <arpa/inet.h>      /* inet_pton / htons                   */
#include <netinet/in.h>     /* struct sockaddr_in                  */
#include <sys/socket.h>     /* socket / connect / send / recv      */

/* ---------------------------- 参数配置 ----------------------------------- */
#define DEFAULT_SERVER_IP   "127.0.0.1"     /* 默认服务器 IP（板子上要改成电脑的 IP） */
#define SERVER_PORT         9999            /* 必须和服务器端口一致 */
#define BUF_SIZE            1024            /* 输入/接收缓冲区大小 */
#define NICK_SIZE           32              /* 昵称最大长度 */

/* ---------------------------- 全局变量 ----------------------------------- */
/* 接收线程也要用这个 socket，所以放全局而不是局部变量 */
static int g_iSock = -1;

/* ---------------------------- 函数实现 ----------------------------------- */

/**
 * 把字符串完整发送给服务器
 * @NOTE   一次 send() 不一定能发完，必须循环发送到发完为止
 * @param  i_fd    连接服务器的 socket
 * @param  p_cMsg  要发送的字符串（以 '\0' 结尾）
 * @return 0 成功；-1 失败（连接已断开）
 */
static int i_sendAll(int i_fd, const char *p_cMsg)
{
    int i_left = (int)strlen(p_cMsg);
    int i_sent;
    const char *p_cPos = p_cMsg;

    while (i_left > 0) {
        i_sent = (int)send(i_fd, p_cPos, (size_t)i_left, 0);
        if (i_sent <= 0) {
            return -1;
        }
        p_cPos += i_sent;
        i_left -= i_sent;
    }
    return 0;
}

/**
 * 接收线程函数：不断接收服务器转发来的消息并打印
 * @NOTE   线程参数这里用不到，用 (void)p_vArg 消除编译告警
 * @param  p_vArg 未使用
 * @return 只会在进程退出时结束（正常不会返回）
 */
static void *p_recvThread(void *p_vArg)
{
    char c_buf[BUF_SIZE];
    int  i_recvLen;

    (void)p_vArg;

    while (1) {
        i_recvLen = (int)recv(g_iSock, c_buf, sizeof(c_buf) - 1, 0);
        if (i_recvLen <= 0) {
            /* recv 返回 0 = 服务器正常关闭了连接；<0 = 出错 */
            printf("\n[系统] 与服务器断开连接，程序退出\n");
            exit(0);
        }
        c_buf[i_recvLen] = '\0';
        printf("%s", c_buf);        /* 服务器发来的消息已自带换行符 */
    }

    return NULL;
}

/**
 * 客户端主流程
 * @param  argc 参数个数
 * @param  argv argv[1] 可以是服务器 IP（不填就用默认值）
 * @return 0 正常退出；-1 出错退出
 */
int main(int argc, char *argv[])
{
    const char *p_cServerIp = DEFAULT_SERVER_IP;
    struct sockaddr_in st_serverAddr;
    pthread_t st_tid;
    char c_nickname[NICK_SIZE];
    char c_buf[BUF_SIZE];
    char c_line[BUF_SIZE];

    if (argc > 1) {
        p_cServerIp = argv[1];      /* 命令行给了 IP 就用命令行的 */
    }

    /* ---------- 第 1 步：读昵称 ---------- */
    /* 用 fgets 比 scanf 安全：能限制长度、能读空格、不残留垃圾字符 */
    printf("请输入你的昵称：");
    fflush(stdout);                 /* 刷新输出，保证提示语先显示出来 */
    if (fgets(c_nickname, sizeof(c_nickname), stdin) == NULL) {
        return 0;
    }
    c_nickname[strcspn(c_nickname, "\r\n")] = '\0';     /* 去掉结尾换行 */
    if (c_nickname[0] == '\0') {
        snprintf(c_nickname, sizeof(c_nickname), "%s", "无名氏");
    }

    /* ---------- 第 2 步：连接服务器 ---------- */
    g_iSock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_iSock < 0) {
        perror("socket");
        return -1;
    }

    memset(&st_serverAddr, 0, sizeof(st_serverAddr));
    st_serverAddr.sin_family = AF_INET;
    st_serverAddr.sin_port   = htons(SERVER_PORT);
    if (inet_pton(AF_INET, p_cServerIp, &st_serverAddr.sin_addr) <= 0) {
        printf("服务器 IP 格式不正确：%s\n", p_cServerIp);
        close(g_iSock);
        return -1;
    }

    /* TCP 是"先接通再说话"：连接不上就没法聊天 */
    if (connect(g_iSock, (struct sockaddr *)&st_serverAddr, sizeof(st_serverAddr)) < 0) {
        perror("connect");
        printf("连接失败，请检查：1) 服务器程序是否已启动 2) IP/端口是否正确 3) 防火墙是否放行\n");
        close(g_iSock);
        return -1;
    }
    printf("已连接到服务器 %s:%d\n", p_cServerIp, SERVER_PORT);

    /* ---------- 第 3 步：把昵称作为第一条消息发出去（约定） ---------- */
    snprintf(c_line, sizeof(c_line), "%s\n", c_nickname);
    i_sendAll(g_iSock, c_line);

    /* ---------- 第 4 步：开接收线程，主线程负责读键盘发送 ---------- */
    if (pthread_create(&st_tid, NULL, p_recvThread, NULL) != 0) {
        perror("pthread_create");
        close(g_iSock);
        return -1;
    }
    pthread_detach(st_tid);         /* 分离线程，结束后自动回收 */

    printf("已进入聊天室！输入消息回车发送，输入 quit 退出。\n");

    while (1) {
        if (fgets(c_buf, sizeof(c_buf) - 1, stdin) == NULL) {
            break;                  /* 输入结束（Ctrl+D 或输入流关闭） */
        }
        c_buf[strcspn(c_buf, "\r\n")] = '\0';   /* 去掉结尾换行 */
        if (c_buf[0] == '\0') {
            continue;               /* 空消息不发送 */
        }

        snprintf(c_line, sizeof(c_line), "%s\n", c_buf);    /* 补上换行，服务器按行读取 */
        if (i_sendAll(g_iSock, c_line) < 0) {
            break;                  /* 发送失败，说明连接已断 */
        }
        if (strcmp(c_buf, "quit") == 0) {
            break;                  /* 主动退出 */
        }
    }

    close(g_iSock);
    printf("已退出聊天室\n");
    return 0;
}

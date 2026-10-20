/* ============================================================================
 * udp_client.c —— UDP 聊天室客户端（全志 T113 / Linux 版）
 * ----------------------------------------------------------------------------
 * 编译： 在电脑上执行 make（详见同目录 Makefile）
 *
 * 用法：
 *   ./udp_client                 连接默认服务器（本机 127.0.0.1）
 *   ./udp_client 192.168.1.100   连接指定 IP 的服务器（板子连电脑时用这种）
 *
 * 和 TCP 客户端的区别（重点体会）：
 *   1. 没有 connect()，也不需要自己 bind 端口；
 *      第一次 sendto 时系统会自动给这个 socket 分配一个临时端口；
 *   2. 每发一条消息都要带上服务器地址（用 sendto）；
 *   3. UDP 不可靠：可能丢包、可能乱序，所以聊天室这种"丢了也无所谓"的场景才合适。
 * ============================================================================
 */

#include <stdio.h>          /* printf / fgets                      */
#include <stdlib.h>         /* exit                                */
#include <string.h>         /* memset / strcspn                    */
#include <unistd.h>         /* close                               */
#include <pthread.h>        /* pthread_create                      */
#include <arpa/inet.h>      /* inet_pton / htons                   */
#include <netinet/in.h>     /* struct sockaddr_in                  */
#include <sys/socket.h>     /* socket / sendto / recvfrom          */

/* ---------------------------- 参数配置 ----------------------------------- */
#define DEFAULT_SERVER_IP   "127.0.0.1"     /* 默认服务器 IP（板子上要改成电脑的 IP） */
#define SERVER_PORT         8888            /* 必须和 UDP 服务器端口一致 */
#define BUF_SIZE            1024            /* 输入/接收缓冲区大小 */
#define NICK_SIZE           32              /* 昵称最大长度 */

/* ---------------------------- 全局变量 ----------------------------------- */
static int g_iSock = -1;                    /* 接收线程也要用，所以放全局 */

/* ---------------------------- 函数实现 ----------------------------------- */

/**
 * 接收线程函数：打印服务器转发来的消息
 * @NOTE   第二个返回值（对方地址）用不上，用下划线占位忽略
 * @param  p_vArg 未使用
 * @return 只会在进程退出时结束（正常不会返回）
 */
static void *p_recvThread(void *p_vArg)
{
    char c_buf[BUF_SIZE];
    int  i_recvLen;
    struct sockaddr_in st_fromAddr;
    socklen_t u32_addrLen;

    (void)p_vArg;

    while (1) {
        u32_addrLen = sizeof(st_fromAddr);
        i_recvLen = (int)recvfrom(g_iSock, c_buf, sizeof(c_buf) - 1, 0,
                                  (struct sockaddr *)&st_fromAddr, &u32_addrLen);
        if (i_recvLen <= 0) {
            /* UDP 上出错一般是本机 socket 被关闭了，直接退出进程 */
            printf("\n[系统] 接收出错，程序退出\n");
            exit(0);
        }
        c_buf[i_recvLen] = '\0';
        printf("%s", c_buf);        /* 服务器发来的消息已自带换行符 */
    }

    return NULL;
}

/**
 * 把一条消息发给服务器
 * @NOTE   UDP 没有连接，所以每次发送都必须写明收件人地址
 * @param  p_stServer 服务器地址
 * @param  p_cMsg     要发送的字符串（以 '\0' 结尾）
 * @return 无
 */
static void v_sendToServer(const struct sockaddr_in *p_stServer, const char *p_cMsg)
{
    sendto(g_iSock, p_cMsg, strlen(p_cMsg), 0,
           (struct sockaddr *)p_stServer, sizeof(*p_stServer));
}

/**
 * 客户端主流程
 * @param  argc 参数个数
 * @param  argv argv[1] 可以是服务器 IP（不填就用默认值）
 * @return 0 正常退出
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
        p_cServerIp = argv[1];
    }

    /* ---------- 第 1 步：读昵称 ---------- */
    printf("请输入你的昵称：");
    fflush(stdout);
    if (fgets(c_nickname, sizeof(c_nickname), stdin) == NULL) {
        return 0;
    }
    c_nickname[strcspn(c_nickname, "\r\n")] = '\0';
    if (c_nickname[0] == '\0') {
        snprintf(c_nickname, sizeof(c_nickname), "%s", "无名氏");
    }

    /* ---------- 第 2 步：创建 UDP socket ----------
     * @NOTE 没有 connect，也不用 bind 端口 */
    g_iSock = socket(AF_INET, SOCK_DGRAM, 0);
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

    /* ---------- 第 3 步：先发昵称，让服务器认识我们 ----------
     * @NOTE UDP 无连接：服务器只有收到过我们的数据报，
     *       才知道我们的地址，之后才能把别人的消息发给我们 */
    snprintf(c_line, sizeof(c_line), "%s\n", c_nickname);
    v_sendToServer(&st_serverAddr, c_line);
    printf("已进入聊天室！输入消息回车发送，输入 quit 退出。\n");

    /* ---------- 第 4 步：开接收线程，主线程负责读键盘发送 ---------- */
    if (pthread_create(&st_tid, NULL, p_recvThread, NULL) != 0) {
        perror("pthread_create");
        close(g_iSock);
        return -1;
    }
    pthread_detach(st_tid);

    while (1) {
        if (fgets(c_buf, sizeof(c_buf) - 1, stdin) == NULL) {
            break;                  /* 输入结束 */
        }
        c_buf[strcspn(c_buf, "\r\n")] = '\0';
        if (c_buf[0] == '\0') {
            continue;
        }

        snprintf(c_line, sizeof(c_line), "%s\n", c_buf);
        v_sendToServer(&st_serverAddr, c_line);

        if (strcmp(c_buf, "quit") == 0) {
            break;                  /* 通知服务器自己走了，然后退出 */
        }
    }

    close(g_iSock);
    printf("已退出聊天室\n");
    return 0;
}

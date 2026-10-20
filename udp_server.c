#include <stdio.h>          /* printf / perror                     */
#include <string.h>         /* memset / strcspn / strcmp           */
#include <unistd.h>         /* close                               */
#include <arpa/inet.h>      /* htons / ntohs                       */
#include <netinet/in.h>     /* struct sockaddr_in                  */
#include <sys/socket.h>     /* socket / bind / recvfrom / sendto   */

/* ---------------------------- 参数配置 ----------------------------------- */
#define PORT            8888        /* 监听端口（和 TCP 的 9999 不冲突） */
#define MAX_CLIENTS     32          /* 最多同时在线人数 */
#define BUF_SIZE        1024        /* 单个数据报最大长度 */

/* ---------------------------- 数据结构 ----------------------------------- */

/* 一个在线客户端：UDP 没有连接对象，只能靠"地址"区分用户 */
typedef struct {
    struct sockaddr_in st_addr;         /* 客户端地址（IP + 端口） */
    char c_nickname[32];                /* 昵称 */
} st_client;

/* ---------------------------- 全局变量 ----------------------------------- */
static st_client g_astClients[MAX_CLIENTS];     /* 在线名单 */
static int       g_iClientCount = 0;            /* 在线人数 */
/* @NOTE UDP 版是单线程的，同一时刻只有 main 在改这个名单，所以不需要线程锁 */

/* ---------------------------- 函数实现 ----------------------------------- */

/**
 * 在在线名单里查一个地址
 * @NOTE   比较 IP 和端口两项都相同才算同一个人
 * @param  p_stAddr 要查找的客户端地址
 * @return >=0 在名单中的下标；-1 表示没找到（是新用户）
 */
static int i_findClient(const struct sockaddr_in *p_stAddr)
{
    int i;

    for (i = 0; i < g_iClientCount; i++) {
        if (g_astClients[i].st_addr.sin_addr.s_addr == p_stAddr->sin_addr.s_addr &&
            g_astClients[i].st_addr.sin_port == p_stAddr->sin_port) {
            return i;
        }
    }
    return -1;
}

/**
 * 把新客户端登记进在线名单
 * @param  p_stAddr    客户端地址
 * @param  p_cNickname 昵称
 * @return >=0 新的下标；-1 名单已满
 */
static int i_addClient(const struct sockaddr_in *p_stAddr, const char *p_cNickname)
{
    int i_idx;

    if (g_iClientCount >= MAX_CLIENTS) {
        return -1;
    }

    i_idx = g_iClientCount;
    g_astClients[i_idx].st_addr = *p_stAddr;        /* 整个地址结构直接拷贝 */

    snprintf(g_astClients[i_idx].c_nickname,
             sizeof(g_astClients[i_idx].c_nickname),
             "%s", p_cNickname);
    g_iClientCount++;

    return i_idx;
}

/**
 * 把客户端从在线名单移除
 * @NOTE   用"最后一个元素填到空位"的方式删除，不用搬动整段数组
 * @param  i_idx 要移除的下标
 * @return 无
 */
static void v_removeClient(int i_idx)
{
    if (i_idx < 0 || i_idx >= g_iClientCount) {
        return;
    }
    g_astClients[i_idx] = g_astClients[g_iClientCount - 1];
    g_iClientCount--;
}

/**
 * 给所有在线客户端群发一条消息
 * @NOTE   UDP 每个数据报都要指定收件人，所以必须在循环里逐个 sendto
 * @param  i_sockFd     服务器 socket
 * @param  p_cMsg       要群发的消息（已带换行符）
 * @param  i_exceptIdx  跳过这个下标的客户端（一般是发送者本人），传 -1 发给所有人
 * @return 无
 */
static void v_broadcast(int i_sockFd, const char *p_cMsg, int i_exceptIdx)
{
    int i;

    for (i = 0; i < g_iClientCount; i++) {
        if (i == i_exceptIdx) {
            continue;
        }
        sendto(i_sockFd, p_cMsg, strlen(p_cMsg), 0,
               (struct sockaddr *)&g_astClients[i].st_addr,
               sizeof(g_astClients[i].st_addr));
    }
}

/**
 * UDP 服务器主流程
 * @NOTE   只有三步：socket -> bind -> 循环 recvfrom（没有 listen / accept）
 * @param  无
 * @return 0 正常结束；-1 启动失败
 */
int main(void)
{
    int i_serverFd;
    int i_recvLen;
    int i_idx;
    char c_buf[BUF_SIZE];
    char c_msg[BUF_SIZE + 32];
    struct sockaddr_in st_serverAddr;
    struct sockaddr_in st_clientAddr;    /* recvfrom 会把发信人地址填进来 */
    socklen_t u32_addrLen;

    /* SOCK_DGRAM = UDP（TCP 用的是 SOCK_STREAM） */
    i_serverFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (i_serverFd < 0) {
        perror("socket");
        return -1;
    }

    memset(&st_serverAddr, 0, sizeof(st_serverAddr));
    st_serverAddr.sin_family      = AF_INET;
    st_serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);   /* 监听所有网卡 */
    st_serverAddr.sin_port        = htons(PORT);

    if (bind(i_serverFd, (struct sockaddr *)&st_serverAddr, sizeof(st_serverAddr)) < 0) {
        perror("bind");
        printf("绑定端口 %d 失败：端口可能被别的程序占用\n", PORT);
        close(i_serverFd);
        return -1;
    }

    printf("UDP 聊天室服务器已启动，监听 0.0.0.0:%d\n", PORT);
    printf("等待客户端加入（按 Ctrl+C 关闭服务器）\n");

    while (1) {
        /* recvfrom：收一个数据报，同时得到"寄信人"地址（UDP 最核心的函数） */
        u32_addrLen = sizeof(st_clientAddr);
        i_recvLen = (int)recvfrom(i_serverFd, c_buf, sizeof(c_buf) - 1, 0,
                                  (struct sockaddr *)&st_clientAddr, &u32_addrLen);
        if (i_recvLen <= 0) {
            continue;               /* 收到空报或出错，忽略继续 */
        }
        c_buf[i_recvLen] = '\0';
        c_buf[strcspn(c_buf, "\r\n")] = '\0';   /* 去掉结尾换行 */
        if (c_buf[0] == '\0') {
            continue;
        }

        i_idx = i_findClient(&st_clientAddr);

        /* ---------- 情况 1：没见过这个地址 —— 新用户上线，第一条消息当昵称 ---------- */
        if (i_idx < 0) {
            i_idx = i_addClient(&st_clientAddr, c_buf);
            if (i_idx < 0) {
                continue;           /* 名单满了，忽略 */
            }
            printf("[系统] %s 进入聊天室，当前在线 %d 人\n",
                   g_astClients[i_idx].c_nickname, g_iClientCount);
            snprintf(c_msg, sizeof(c_msg), "*** %s 进入聊天室 ***\n",
                     g_astClients[i_idx].c_nickname);
            v_broadcast(i_serverFd, c_msg, i_idx);      /* 通知别人，跳过自己 */
            continue;
        }

        /* ---------- 情况 2：已知用户发来 quit —— 退出 ---------- */
        if (strcmp(c_buf, "quit") == 0) {
            printf("[系统] %s 离开聊天室，当前在线 %d 人\n",
                   g_astClients[i_idx].c_nickname, g_iClientCount - 1);
            snprintf(c_msg, sizeof(c_msg), "*** %s 离开聊天室 ***\n",
                     g_astClients[i_idx].c_nickname);
            v_broadcast(i_serverFd, c_msg, i_idx);      /* 先通知别人（此时自己还在名单里） */
            v_removeClient(i_idx);
            continue;
        }

        /* ---------- 情况 3：普通消息 —— 转发给其他所有人 ---------- */
        printf("[%s] %s\n", g_astClients[i_idx].c_nickname, c_buf);
        snprintf(c_msg, sizeof(c_msg), "[%s] %s\n", g_astClients[i_idx].c_nickname, c_buf);
        v_broadcast(i_serverFd, c_msg, i_idx);
    }

    close(i_serverFd);
    return 0;
}

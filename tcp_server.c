/* ============================================================================
 * tcp_server.c —— TCP 聊天室服务器（全志 T113 / Linux 版）
 * ----------------------------------------------------------------------------
 * 编译： 在电脑上执行 make（详见同目录 Makefile）
 * 运行： 把可执行文件拷到 T113 板子上，执行 ./tcp_server
 *
 * 功能：
 *   1. 等待多个客户端连接（每个客户端一个线程，互不阻塞）；
 *   2. 把任一客户端发来的消息转发（广播）给其他所有客户端；
 *   3. 有人进入 / 离开时，通知其他所有人。
 *
 * TCP 四步曲： socket -> bind -> listen -> accept
 *   TCP 面向连接、可靠不丢包，像打电话：先接通，再说话。
 *
 * 命名习惯（本工程统一，方便对照）：
 *   变量：类型前缀 + 名字     i_=int   c_=char/字符串   p_=指针   st_=结构体
 *   函数：返回值前缀 + 名字   i_=返回int   v_=void无返回   p_=返回指针
 *   常量：全大写宏定义（PORT / MAX_CLIENTS 等）
 * ============================================================================
 */

#include <stdio.h>          /* printf / perror                     */
#include <stdlib.h>         /* malloc / free                       */
#include <string.h>         /* memset / strncpy / strcspn          */
#include <unistd.h>         /* close                               */
#include <pthread.h>        /* pthread_create：每个客户端一个线程  */
#include <arpa/inet.h>      /* inet_ntop / htons / ntohs           */
#include <netinet/in.h>     /* struct sockaddr_in                  */
#include <sys/socket.h>     /* socket / bind / listen / accept     */

/* ---------------------------- 参数配置 ----------------------------------- */
#define PORT            9999        /* 监听端口，客户端必须填同一个 */
#define MAX_CLIENTS     32          /* 最多同时在线人数 */
#define BUF_SIZE        1024        /* 单条消息最大长度（字节） */
#define NICK_SIZE       32          /* 昵称最大长度（字节） */

/* ---------------------------- 数据结构 ----------------------------------- */

/* 一个在线客户端的信息 */
typedef struct {
    int  i_fd;                      /* 与这个客户端通信的 socket 描述符 */
    char c_nickname[NICK_SIZE];     /* 昵称 */
} st_client;

/* 传给新线程的参数
 * @NOTE 线程函数的参数必须"活"到线程用完为止，
 *       所以要 malloc 单独申请一份，不能传局部变量地址 */
typedef struct {
    int  i_fd;                      /* 新连接的 socket */
    char c_ip[INET_ADDRSTRLEN];     /* 客户端 IP，如 "192.168.1.5" */
    int  i_port;                    /* 客户端端口 */
} st_threadArg;

/* ---------------------------- 全局变量 ----------------------------------- */
static st_client       g_astClients[MAX_CLIENTS];            /* 在线列表 */
static int             g_iClientCount = 0;                   /* 在线人数 */
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;  /* 保护在线列表的锁 */

/* ---------------------------- 函数实现 ----------------------------------- */

/**
 * 把字符串完整发送给某个客户端
 * @NOTE   一次 send() 不一定能发完所有字节，必须循环发送直到发完
 * @param  i_fd    目标客户端 socket
 * @param  p_cMsg  要发送的字符串（以 '\0' 结尾）
 * @return 0 成功；-1 失败（通常表示对方已经掉线）
 */
static int i_sendAll(int i_fd, const char *p_cMsg)
{
    int i_left = (int)strlen(p_cMsg);   /* 还剩多少字节没发出去 */
    int i_sent;                         /* 本次实际发出的字节数   */
    const char *p_cPos = p_cMsg;        /* 下一个要发送的位置     */

    while (i_left > 0) {
        i_sent = (int)send(i_fd, p_cPos, (size_t)i_left, 0);
        if (i_sent <= 0) {
            return -1;                  /* 发送失败，对方可能已断开 */
        }
        p_cPos += i_sent;
        i_left -= i_sent;
    }
    return 0;
}

/**
 * 向所有在线客户端广播一条消息
 * @param  p_cMsg     要广播的消息（已带换行符）
 * @param  i_exceptFd 跳过这个 socket（一般是消息发送者本人），传 -1 发给所有人
 * @return 无
 */
static void v_broadcast(const char *p_cMsg, int i_exceptFd)
{
    int i;

    pthread_mutex_lock(&g_mutex);           /* 加锁：安全地遍历在线列表 */
    for (i = 0; i < g_iClientCount; i++) {
        if (g_astClients[i].i_fd == i_exceptFd) {
            continue;                       /* 跳过发送者本人 */
        }
        if (i_sendAll(g_astClients[i].i_fd, p_cMsg) < 0) {
            /* 这里不直接删除，交给该客户端的线程在退出时统一清理 */
            printf("[系统] 向 %s 发送失败（可能已掉线）\n", g_astClients[i].c_nickname);
        }
    }
    pthread_mutex_unlock(&g_mutex);
}

/**
 * 把客户端登记到在线列表
 * @param  i_fd        客户端 socket
 * @param  p_cNickname 昵称
 * @return >=0 该客户端在列表中的下标；-1 表示已满
 */
static int i_addClient(int i_fd, const char *p_cNickname)
{
    int i_ret;

    pthread_mutex_lock(&g_mutex);
    if (g_iClientCount >= MAX_CLIENTS) {
        i_ret = -1;
    } else {
        i_ret = g_iClientCount;
        g_astClients[i_ret].i_fd = i_fd;
        /* strncpy 不保证补 '\0'，所以手动补一个，防止打印越界 */
        strncpy(g_astClients[i_ret].c_nickname, p_cNickname, NICK_SIZE - 1);
        g_astClients[i_ret].c_nickname[NICK_SIZE - 1] = '\0';
        g_iClientCount++;
    }
    pthread_mutex_unlock(&g_mutex);

    return i_ret;
}

/**
 * 把客户端从在线列表移除
 * @NOTE   用"把最后一个元素搬到空位再减少人数"的办法删除，简单且不用整段搬数组
 * @param  i_fd 要移除的客户端 socket
 * @return 无
 */
static void v_removeClient(int i_fd)
{
    int i;

    pthread_mutex_lock(&g_mutex);
    for (i = 0; i < g_iClientCount; i++) {
        if (g_astClients[i].i_fd == i_fd) {
            g_astClients[i] = g_astClients[g_iClientCount - 1];
            g_iClientCount--;
            break;
        }
    }
    pthread_mutex_unlock(&g_mutex);
}

/**
 * 单个客户端的服务线程：先收昵称，再循环收消息并广播
 * @NOTE   约定：客户端连接成功后发出的第一条消息就是它的昵称
 * @param  p_vArg 指向 st_threadArg（malloc 申请，本函数负责 free）
 * @return 线程固定返回 NULL
 */
static void *p_handleClient(void *p_vArg)
{
    st_threadArg *p_stArg = (st_threadArg *)p_vArg;
    int  i_fd = p_stArg->i_fd;
    int  i_recvLen;
    char c_nickname[NICK_SIZE];
    char c_buf[BUF_SIZE];
    char c_msg[BUF_SIZE + NICK_SIZE];   /* 组装后的广播消息，留够昵称的位置 */

    /* 默认昵称用 "IP:端口"，万一对方没发昵称也能显示 */
    snprintf(c_nickname, sizeof(c_nickname), "%s:%d", p_stArg->c_ip, p_stArg->i_port);
    free(p_stArg);                      /* 参数已取出，及时释放，避免内存泄漏 */

    /* ---------- 第 1 步：接收昵称（第一条消息） ---------- */
    i_recvLen = (int)recv(i_fd, c_buf, sizeof(c_buf) - 1, 0);
    if (i_recvLen <= 0) {               /* 连上就断开，直接结束线程 */
        close(i_fd);
        return NULL;
    }
    c_buf[i_recvLen] = '\0';
    c_buf[strcspn(c_buf, "\r\n")] = '\0';       /* 去掉结尾的换行符 */
    if (c_buf[0] != '\0') {
        snprintf(c_nickname, sizeof(c_nickname), "%s", c_buf);
    }

    /* ---------- 第 2 步：登记上线并通知其他人 ---------- */
    if (i_addClient(i_fd, c_nickname) < 0) {
        i_sendAll(i_fd, "*** 服务器已满，请稍后再试 ***\n");
        close(i_fd);
        return NULL;
    }
    printf("[系统] %s 进入聊天室，当前在线 %d 人\n", c_nickname, g_iClientCount);
    snprintf(c_msg, sizeof(c_msg), "*** %s 进入聊天室 ***\n", c_nickname);
    v_broadcast(c_msg, i_fd);

    /* ---------- 第 3 步：循环收消息并转发 ---------- */
    while ((i_recvLen = (int)recv(i_fd, c_buf, sizeof(c_buf) - 1, 0)) > 0) {
        c_buf[i_recvLen] = '\0';
        c_buf[strcspn(c_buf, "\r\n")] = '\0';   /* 去掉结尾的 \r \n */
        if (c_buf[0] == '\0') {
            continue;                           /* 空消息不处理 */
        }
        if (strcmp(c_buf, "quit") == 0) {
            break;                              /* 客户端主动退出 */
        }
        printf("[%s] %s\n", c_nickname, c_buf); /* 服务器后台也打印一份 */
        snprintf(c_msg, sizeof(c_msg), "[%s] %s\n", c_nickname, c_buf);
        v_broadcast(c_msg, i_fd);               /* 转发给其他所有人 */
    }

    /* ---------- 第 4 步：无论怎么退出，都要清理下线 ---------- */
    v_removeClient(i_fd);
    close(i_fd);
    printf("[系统] %s 离开聊天室，当前在线 %d 人\n", c_nickname, g_iClientCount);
    snprintf(c_msg, sizeof(c_msg), "*** %s 离开聊天室 ***\n", c_nickname);
    v_broadcast(c_msg, -1);

    return NULL;
}

/**
 * 服务器主流程
 * @NOTE   固定四步：socket 创建 -> bind 绑定端口 -> listen 开始监听 -> accept 等连接
 * @param  无
 * @return 0 正常结束；-1 启动失败
 */
int main(void)
{
    int i_serverFd;                 /* 监听用的 socket */
    int i_clientFd;                 /* 每个新连接的 socket */
    int i_on = 1;                   /* setsockopt 用 */
    struct sockaddr_in st_serverAddr;   /* 服务器地址（本机） */
    struct sockaddr_in st_clientAddr;   /* 客户端地址（accept 时填进来） */
    socklen_t u32_addrLen;              /* 地址结构长度 */
    pthread_t st_tid;
    st_threadArg *p_stArg;

    /* AF_INET = IPv4；SOCK_STREAM = TCP；0 = 自动选协议 */
    i_serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (i_serverFd < 0) {
        perror("socket");
        return -1;
    }

    /* 允许端口被立即重用，避免重启程序时报 "Address already in use" */
    if (setsockopt(i_serverFd, SOL_SOCKET, SO_REUSEADDR, &i_on, sizeof(i_on)) < 0) {
        perror("setsockopt");
    }

    /* 填写本机地址：INADDR_ANY 表示监听所有网卡（本机和局域网都能连进来） */
    memset(&st_serverAddr, 0, sizeof(st_serverAddr));
    st_serverAddr.sin_family      = AF_INET;
    st_serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);   /* htonl：主机字节序 -> 网络字节序 */
    st_serverAddr.sin_port        = htons(PORT);         /* htons：端口同样要转成网络字节序 */

    if (bind(i_serverFd, (struct sockaddr *)&st_serverAddr, sizeof(st_serverAddr)) < 0) {
        perror("bind");
        printf("绑定端口 %d 失败：端口可能被别的程序占用\n", PORT);
        close(i_serverFd);
        return -1;
    }

    /* listen 的第二个参数是"已完成连接但还没被 accept 的排队上限" */
    if (listen(i_serverFd, 5) < 0) {
        perror("listen");
        close(i_serverFd);
        return -1;
    }

    printf("TCP 聊天室服务器已启动，监听 0.0.0.0:%d\n", PORT);
    printf("等待客户端加入（按 Ctrl+C 关闭服务器）\n");

    while (1) {
        /* accept 会阻塞，直到有新客户端连接上来才返回 */
        u32_addrLen = sizeof(st_clientAddr);
        i_clientFd = accept(i_serverFd, (struct sockaddr *)&st_clientAddr, &u32_addrLen);
        if (i_clientFd < 0) {
            perror("accept");
            continue;               /* 单个连接出错不影响整个服务器，继续等下一个 */
        }

        /* 为新客户端准备线程参数（malloc 保证线程使用期间一直有效） */
        p_stArg = (st_threadArg *)malloc(sizeof(st_threadArg));
        if (p_stArg == NULL) {
            close(i_clientFd);
            continue;
        }
        p_stArg->i_fd   = i_clientFd;
        p_stArg->i_port = ntohs(st_clientAddr.sin_port);      /* ntohs：网络字节序 -> 主机字节序 */
        inet_ntop(AF_INET, &st_clientAddr.sin_addr, p_stArg->c_ip, sizeof(p_stArg->c_ip));
        printf("[系统] 新连接：%s:%d\n", p_stArg->c_ip, p_stArg->i_port);

        /* 每个客户端单独开一个线程，这样一个人发消息不会卡住其他人 */
        if (pthread_create(&st_tid, NULL, p_handleClient, p_stArg) != 0) {
            perror("pthread_create");
            free(p_stArg);
            close(i_clientFd);
        } else {
            pthread_detach(st_tid);     /* 分离线程：线程结束后系统自动回收资源 */
        }
    }

    close(i_serverFd);
    return 0;
}

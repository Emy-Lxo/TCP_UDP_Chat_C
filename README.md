# TCP / UDP 聊天室

纯 C 实现，socket + pthread，适用于 Ubuntu WSL / 全志 T113 Tina Linux。

## 文件清单

| 文件 | 端口 | 说明 |
|---|---|---|
| `tcp_server.c` | 9999 | TCP 服务器，广播消息 |
| `tcp_client.c` | 9999 | TCP 客户端 |
| `udp_server.c` | 8888 | UDP 服务器 |
| `udp_client.c` | 8888 | UDP 客户端 |
| `Makefile` | — | 构建脚本 |

## 编译

```bash
# Ubuntu 本机
make CROSS=

# T113 交叉编译（工具链前缀替换成你实际的）
make CROSS=arm-openwrt-linux-muslgnueabi-

# 静态链接（板子缺 libc.so.6 时用）
make CROSS=arm-openwrt-linux-muslgnueabi- STATIC=1
```

## 运行

开 3 个终端，都 cd 到源码目录：

```bash
# 终端 1：服务器
./tcp_server                      # UDP 版用 ./udp_server

# 终端 2：客户端 A（昵称 Alice）
./tcp_client 127.0.0.1            # UDP 版用 ./udp_client 127.0.0.1

# 终端 3：客户端 B（昵称 Bob）
./tcp_client 127.0.0.1
```

Alice 打字回车 → Bob 和服务器窗口都能看到。输入 `quit` 退出。

板子和 Ubuntu 跨设备只能用 TCP + `adb forward tcp:9999 tcp:9999`（UDP 不走 adb 隧道）。

## TCP vs UDP — 一句话

**TCP 像打电话**：先接通，对方掉线立刻知道，消息不丢。
**UDP 像寄信**：投进邮筒就算完，对方收没收到不知道。

## 注意事项

- 客户端发出的**第一条消息**就是自己的昵称。
- 服务器把消息发给除发送者以外的所有人，自己看不到自己发的。
- 服务器没开时：TCP 立刻 `Connection refused`；UDP **静默**（照样显示"已进入聊天室"）。

---

# TCP / UDP Chat Room

Pure C implementation, socket + pthread, for Ubuntu WSL / Allwinner T113 Tina Linux.

## File List

| File | Port | Description |
|---|---|---|
| `tcp_server.c` | 9999 | TCP server, broadcasts messages |
| `tcp_client.c` | 9999 | TCP client |
| `udp_server.c` | 8888 | UDP server |
| `udp_client.c` | 8888 | UDP client |
| `Makefile` | — | Build script |

## Compile

```bash
# On Ubuntu host
make CROSS=

# Cross-compile for T113 (replace prefix with your actual toolchain)
make CROSS=arm-openwrt-linux-muslgnueabi-

# Static link (for boards missing libc.so.6)
make CROSS=arm-openwrt-linux-muslgnueabi- STATIC=1
```

## Run

Open 3 terminals, all in the source directory:

```bash
# Terminal 1: server
./tcp_server                      # UDP: ./udp_server

# Terminal 2: client A (nickname: Alice)
./tcp_client 127.0.0.1            # UDP: ./udp_client 127.0.0.1

# Terminal 3: client B (nickname: Bob)
./tcp_client 127.0.0.1
```

Alice types and hits Enter → Bob's window + server window both show it. Type `quit` to exit.

Cross-device chat between board and Ubuntu works **only with TCP** + `adb forward tcp:9999 tcp:9999` (UDP cannot be tunneled over adb).

## TCP vs UDP — One Line

**TCP is a phone call**: connect first, peer hangup detected instantly, reliable.
**UDP is a mailbox**: drop and go, no confirmation.

## Notes

- The **first message** a client sends is its nickname.
- Server sends to everyone except the sender — you do NOT see your own message echoed.
- Server down: TCP → immediate `Connection refused`; UDP → silent, still says "entered chat room".

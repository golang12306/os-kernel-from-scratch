/*
 * TCP 三次握手演示
 *
 * 本程序通过观察 socket 状态变化，展示 TCP 三次握手过程
 *
 * 编译：gcc -o tcp_handshake tcp_handshake.c
 * 运行：
 *   终端1（服务端）：./tcp_handshake server
 *   终端2（客户端）：./tcp_handshake client
 *   终端3：ss -tn | grep 9998 观察连接状态
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/time.h>
#include <time.h>

#define SERVER_PORT 9998
#define MSG "Hello from client!\n"
#define SERVER_MSG "Hello from server!\n"

/* 获取当前时间戳（毫秒） */
long get_timestamp_ms() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* 打印 TCP 状态（通过 getsockopt） */
void print_tcp_state(int fd) {
    struct tcp_info info;
    socklen_t len = sizeof(info);
    getsockopt(fd, IPPROTO_TCP, TCP_INFO, &info, &len);

    const char *state_name = "?";
    switch (info.tcpi_state) {
        case 0:  state_name = "ESTABLISHED"; break;
        case 1:  state_name = "SYN_SENT"; break;
        case 2:  state_name = "SYN_RECEIVED"; break;
        case 3:  state_name = "FIN_WAIT_1"; break;
        case 4:  state_name = "FIN_WAIT_2"; break;
        case 5:  state_name = "TIME_WAIT"; break;
        case 6:  state_name = "CLOSED"; break;
        case 7:  state_name = "CLOSE_WAIT"; break;
        case 8:  state_name = "LAST_ACK"; break;
        case 9:  state_name = "LISTEN"; break;
        case 10: state_name = "CLOSING"; break;
        case 11: state_name = "NEW_SYN_RECV"; break;
        default: state_name = "UNKNOWN"; break;
    }
    printf("  [状态] TCP_STATE = %s\n", state_name);
}

/*
 * 服务端：创建 socket -> bind -> listen -> accept
 */
void run_server() {
    printf("=== TCP 三次握手 - 服务端 ===\n");
    printf("[%ldms] 创建 socket\n", get_timestamp_ms());

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(1);
    }

    /* 允许地址复用（调试用） */
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(SERVER_PORT);

    printf("[%ldms] bind(port=%d)\n", get_timestamp_ms(), SERVER_PORT);
    bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr));

    printf("[%ldms] listen(backlog=5)\n", get_timestamp_ms());
    listen(listen_fd, 5);

    printf("[%ldms] 服务端已启动，等待客户端连接...\n", get_timestamp_ms());
    printf("        （在另一终端运行: nc 127.0.0.1 %d）\n", SERVER_PORT);

    struct sockaddr_in cli_addr;
    socklen_t cli_len = sizeof(cli_addr);
    int conn_fd = accept(listen_fd, (struct sockaddr *)&cli_addr, &cli_len);

    printf("[%ldms] accept() 返回！客户端连接建立\n", get_timestamp_ms());
    printf("  客户端: %s:%d\n",
           inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
    print_tcp_state(conn_fd);

    /* 简单交互 */
    char buf[256];
    ssize_t n = read(conn_fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("[%ldms] 收到客户端消息: %s", get_timestamp_ms(), buf);
        write(conn_fd, SERVER_MSG, strlen(SERVER_MSG));
    }

    sleep(1);
    printf("[%ldms] 关闭连接\n", get_timestamp_ms());
    close(conn_fd);
    close(listen_fd);
    printf("=== 服务端退出 ===\n");
}

/*
 * 客户端：创建 socket -> connect（触发三次握手） -> 发送数据
 */
void run_client() {
    printf("=== TCP 三次握手 - 客户端 ===\n");
    printf("[%ldms] 创建 socket\n", get_timestamp_ms());

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        exit(1);
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(SERVER_PORT);

    printf("[%ldms] connect(127.0.0.1:%d) -- 触发三次握手\n",
           get_timestamp_ms(), SERVER_PORT);
    printf("  ↓ SYN 被发送\n");

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        exit(1);
    }

    printf("[%ldms] connect() 返回！连接建立完成\n", get_timestamp_ms());
    print_tcp_state(fd);

    printf("[%ldms] 发送数据: \"%s\"", get_timestamp_ms(), MSG);
    write(fd, MSG, strlen(MSG));

    /* 接收服务端响应 */
    char buf[256];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("[%ldms] 收到服务端消息: %s", get_timestamp_ms(), buf);
    }

    sleep(1);
    printf("[%ldms] 关闭连接\n", get_timestamp_ms());
    close(fd);
    printf("=== 客户端退出 ===\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("用法: %s [server|client]\n", argv[0]);
        printf("\nTCP 三次握手演示：\n");
        printf("  终端1: %s server\n", argv[0]);
        printf("  终端2: %s client\n", argv[0]);
        printf("  终端3: watch -n 0.1 'ss -tn | grep 9998'\n");
        return 1;
    }

    if (strcmp(argv[1], "server") == 0) {
        run_server();
    } else if (strcmp(argv[1], "client") == 0) {
        run_client();
    } else {
        printf("未知参数: %s\n", argv[1]);
        return 1;
    }

    return 0;
}

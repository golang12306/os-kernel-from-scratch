/* TCP Four-Way Handshake (Connection Teardown)
 * 演示主动关闭方的状态机
 * 编译: gcc -o tcp_teardown tcp_teardown.c
 * 运行: ./tcp_teardown
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>

#define SERVER_PORT 9999
#define BUF_SIZE 1024

/* TCP 关闭状态枚举 */
enum tcp_close_state {
    STATE_CLOSED = 0,
    STATE_FIN_WAIT_1,    /* 已发FIN，等待ACK */
    STATE_FIN_WAIT_2,    /* 已收ACK+对端FIN，等待对端ACK */
    STATE_CLOSING,       /* 同时关闭：收到FIN但ACK丢失 */
    STATE_TIME_WAIT,     /* 等待2MSL后彻底关闭 */
    STATE_CLOSE_WAIT,    /* 被动关闭方：收到FIN后等待应用关闭 */
    STATE_LAST_ACK       /* 被动关闭方：最后发送ACK */
};

const char *state_name(enum tcp_close_state s) {
    switch(s) {
        case STATE_CLOSED:      return "CLOSED";
        case STATE_FIN_WAIT_1:  return "FIN_WAIT_1";
        case STATE_FIN_WAIT_2:  return "FIN_WAIT_2";
        case STATE_CLOSING:     return "CLOSING";
        case STATE_TIME_WAIT:   return "TIME_WAIT";
        case STATE_CLOSE_WAIT:  return "CLOSE_WAIT";
        case STATE_LAST_ACK:    return "LAST_ACK";
        default:                return "UNKNOWN";
    }
}

/* 打印TCP连接状态（通过getsockopt获取）*/
void print_tcp_state(int fd) {
    struct tcp_info info;
    socklen_t len = sizeof(info);
    memset(&info, 0, len);
    
    if (getsockopt(fd, IPPROTO_TCP, TCP_INFO, &info, &len) == 0) {
        printf("  [TCP Info] state=%u (%s)\n", 
               info.tcpi_state, 
               info.tcpi_state == TCP_ESTABLISHED ? "ESTABLISHED" :
               info.tcpi_state == TCP_FIN_WAIT1 ? "FIN_WAIT_1" :
               info.tcpi_state == TCP_FIN_WAIT2 ? "FIN_WAIT_2" :
               info.tcpi_state == TCP_CLOSING ? "CLOSING" :
               info.tcpi_state == TCP_TIME_WAIT ? "TIME_WAIT" :
               info.tcpi_state == TCP_CLOSE ? "CLOSE" :
               info.tcpi_state == TCP_CLOSE_WAIT ? "CLOSE_WAIT" :
               "OTHER");
    }
}

/* ---------- 主动关闭方（客户端）演示 ---------- */
void run主动关闭方() {
    printf("\n======== 主动关闭方（客户端）演示 ========\n");
    
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in srv = {
        .sin_family = AF_INET,
        .sin_port = htons(SERVER_PORT),
        .sin_addr.s_addr = inet_addr("127.0.0.1")
    };
    
    /* 连接服务器 */
    printf("[1] 连接服务器...\n");
    connect(fd, (struct sockaddr *)&srv, sizeof(srv));
    print_tcp_state(fd);
    
    /* 发送数据 */
    send(fd, "Hello", 5, 0);
    printf("[2] 发送了数据: 'Hello'\n");
    
    /* 关闭连接 - 进入主动关闭流程 */
    printf("[3] 调用 close(fd)，进入主动关闭流程...\n");
    printf("    状态变化: ESTABLISHED -> FIN_WAIT_1\n");
    close(fd);
    
    /* 注意: 这里不打印状态了，因为fd已关闭
     * 真实流程需要用 poll/select 观察 socket 事件 */
    printf("    主动关闭方状态机:\n");
    printf("    ESTABLISHED -> FIN_WAIT_1 -> FIN_WAIT_2 -> TIME_WAIT -> CLOSED\n");
    printf("    或: ESTABLISHED -> FIN_WAIT_1 -> CLOSING -> TIME_WAIT -> CLOSED\n");
}

/* ---------- 被动关闭方（服务器）演示 ---------- */
void run被动关闭方() {
    printf("\n======== 被动关闭方（服务器）演示 ========\n");
    
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in srv = {
        .sin_family = AF_INET,
        .sin_port = htons(SERVER_PORT),
        .sin_addr.s_addr = INADDR_ANY
    };
    bind(listen_fd, (struct sockaddr *)&srv, sizeof(srv));
    listen(listen_fd, 1);
    
    printf("[1] 服务器监听端口 %d\n", SERVER_PORT);
    
    /* 接受连接 */
    struct sockaddr_in cli;
    socklen_t clilen = sizeof(cli);
    int conn_fd = accept(listen_fd, (struct sockaddr *)&cli, &clilen);
    printf("[2] 收到客户端连接\n");
    print_tcp_state(conn_fd);
    
    /* 读取数据 */
    char buf[BUF_SIZE];
    int n = recv(conn_fd, buf, BUF_SIZE, 0);
    if (n > 0) {
        buf[n] = '\0';
        printf("[3] 收到客户端数据: '%s'\n", buf);
    }
    
    /* 关闭连接 - 被动关闭 */
    printf("[4] 服务器调用 close(conn_fd)，进入被动关闭流程\n");
    printf("    状态变化: ESTABLISHED -> CLOSE_WAIT\n");
    close(conn_fd);
    printf("    继续 -> LAST_ACK -> CLOSED\n");
    
    close(listen_fd);
}

/* ---------- 四次挥手流程图 ---------- */
void print挥手流程() {
    printf("\n======== TCP 四次挥手流程 ========\n");
    printf("\n");
    printf("  客户端（主动关闭）              服务器（被动关闭）\n");
    printf("       |                              |\n");
    printf("       | <------ TCP 数据流 --------> |\n");
    printf("       |         (ESTABLISHED)         |\n");
    printf("       |                              |\n");
    printf("       | ----- FIN + ACK, seq=F ----> |  应用调用 close()\n");
    printf("       |     (FIN_WAIT_1)             |  (CLOSE_WAIT)\n");
    printf("       |                              |\n");
    printf("       | <---- ACK, ack=F+1 --------- |  确认收到FIN\n");
    printf("       |     (FIN_WAIT_2)             |  (对端进入半关闭)\n");
    printf("       |                              |\n");
    printf("       |     ... 应用可继续发送 ...   |\n");
    printf("       | <---- FIN + ACK, seq=G ----- |  应用也调用 close()\n");
    printf("       |     (CLOSING)                |  (LAST_ACK)\n");
    printf("       |                              |\n");
    printf("       | ----- ACK, ack=G+1 --------> |  确认收到FIN\n");
    printf("       |     (TIME_WAIT)              |  (CLOSED)\n");
    printf("       |                              |\n");
    printf("       |   等待 2MSL（约60秒）        |\n");
    printf("       |                              |\n");
    printf("       v     (最终 CLOSED)            v\n");
    printf("\n");
    printf("关键点:\n");
    printf("  1. 主动关闭方发FIN -> 立即进入半关闭状态\n");
    printf("  2. 每一方都必须独立确认对方的FIN，形成4个segment\n");
    printf("  3. TIME_WAIT 状态等待 2MSL（Maximum Segment Lifetime）\n");
    printf("     防止旧连接的延迟segment被新连接接收\n");
    printf("  4. SO_REUSEADDR 选项可以绕过 TIME_WAIT 立即重启\n");
}

int main(int argc, char *argv[]) {
    printf("TCP Four-Way Handshake (Teardown) 演示\n");
    printf("======================================\n");
    
    if (argc > 1 && strcmp(argv[1], "active") == 0) {
        run主动关闭方();
    } else if (argc > 1 && strcmp(argv[1], "passive") == 0) {
        run被动关闭方();
    } else {
        print挥手流程();
        printf("\n用法:\n");
        printf("  ./tcp_teardown              - 显示流程图\n");
        printf("  ./tcp_teardown active       - 演示主动关闭\n");
        printf("  ./tcp_teardown passive     - 演示被动关闭（需先运行active）\n");
    }
    
    return 0;
}

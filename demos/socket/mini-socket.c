// demos/socket/mini-socket.c
//
// 简化版 TCP socket 演示：
// socket() → bind() → listen() → accept() → read/write → close()
// 对应文章 ID: 236 从零写OS内核 | socket

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main() {
    // ① socket：创建 TCP socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket"); exit(1);
    }
    printf("socket fd = %d\n", listen_fd);

    // ② bind：绑定到 0.0.0.0:9999
    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(9999),
        .sin_addr.s_addr = INADDR_ANY,  // 0.0.0.0
    };
    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); exit(1);
    }
    printf("bind to 0.0.0.0:9999\n");

    // ③ listen：开始监听，backlog = 5
    if (listen(listen_fd, 5) < 0) {
        perror("listen"); exit(1);
    }
    printf("listening...\n");

    // ④ accept：阻塞等待连接
    struct sockaddr_in client;
    socklen_t client_len = sizeof(client);
    int conn_fd = accept(listen_fd, (struct sockaddr *)&client, &client_len);
    if (conn_fd < 0) {
        perror("accept"); exit(1);
    }
    printf("connection from %s:%d, conn_fd=%d\n",
           inet_ntoa(client.sin_addr), ntohs(client.sin_port), conn_fd);

    // ⑤ 读写
    char buf[1024];
    int n = read(conn_fd, buf, sizeof(buf));
    if (n > 0) {
        buf[n] = '\0';
        printf("received: %s\n", buf);
        write(conn_fd, "HTTP/1.0 200 OK\r\n\r\n", 19);
    }

    // ⑥ 关闭
    close(conn_fd);
    close(listen_fd);
    return 0;
}

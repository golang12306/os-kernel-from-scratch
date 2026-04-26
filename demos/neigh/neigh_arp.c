/*
 * neigh_arp.c — 纯用户态 ARP 解析：发 ARP 请求，收 ARP 响应
 *
 * 演示 neigh 子系统的核心操作：IP → MAC 的地址解析
 *
 * 编译：gcc -o neigh_arp neigh_arp.c
 * 运行：sudo ./neigh_arp <目标IP> <本机IP> <本机MAC> <网卡名>
 * 示例：sudo ./neigh_arp 192.168.1.1 192.168.1.100 00:11:22:33:44:55 eth0
 *
 * 观察：另一个终端运行 sudo tcpdump -i eth0 -n arp
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/ether.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <signal.h>
#include <errno.h>

/* ===================== ARP 头定义 ===================== */

struct arp_header {
    uint16_t hw_type;        /* 硬件类型：以太网 = 1 */
    uint16_t proto_type;     /* 协议类型：IPv4 = 0x0800 */
    uint8_t  hw_size;        /* 硬件地址长度：MAC = 6 */
    uint8_t  proto_size;     /* 协议地址长度：IP = 4 */
    uint16_t opcode;         /* 操作码：1=请求 2=响应 */
    uint8_t  sender_mac[6];  /* 发送者 MAC */
    uint8_t  sender_ip[4];   /* 发送者 IP (网络字节序) */
    uint8_t  target_mac[6]; /* 目标 MAC (请求时填 0) */
    uint8_t  target_ip[4];  /* 目标 IP (网络字节序) */
} __attribute__((packed));

/* Ethernet 帧头 */
struct eth_frame {
    uint8_t  dest_mac[6];
    uint8_t  src_mac[6];
    uint16_t ether_type;
} __attribute__((packed));

#define ARP_REQUEST 1
#define ARP_RESPONSE 2

/* ===================== 全局变量 ===================== */

static volatile int got_response = 0;

static void alarm_handler(int sig) {
    (void)sig;
    /* 超时，什么也不做，recvfrom 会返回 */
}

/* ===================== ARP 请求发送 ===================== */

static int send_arp_request(int sockfd, struct sockaddr_ll *dev,
                             uint8_t *sender_mac, uint32_t sender_ip,
                             uint32_t target_ip)
{
    uint8_t broadcast_mac[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

    /* Ethernet 头 (14B) + ARP 头 (28B) = 42B */
    uint8_t packet[sizeof(struct eth_frame) + sizeof(struct arp_header)];
    struct eth_frame *eth = (struct eth_frame *)packet;
    struct arp_header *arp = (struct arp_header *)(packet + sizeof(struct eth_frame));

    /* ── Ethernet 头 ── */
    memcpy(eth->dest_mac, broadcast_mac, 6);   /* 广播地址 */
    memcpy(eth->src_mac, sender_mac, 6);
    eth->ether_type = htons(ETH_P_ARP);         /* 0x0806 = ARP */

    /* ── ARP 头 ── */
    arp->hw_type    = htons(1);                  /* 以太网 */
    arp->proto_type = htons(ETH_P_IP);          /* IPv4 */
    arp->hw_size    = 6;
    arp->proto_size = 4;
    arp->opcode     = htons(ARP_REQUEST);       /* 请求 */
    memcpy(arp->sender_mac, sender_mac, 6);
    memcpy(arp->sender_ip, &sender_ip, 4);
    memset(arp->target_mac, 0, 6);               /* 请求时目标 MAC 未知，填 0 */
    memcpy(arp->target_ip, &target_ip, 4);

    /* ── 发送 ── */
    ssize_t ret = sendto(sockfd, packet, sizeof(packet), 0,
                          (struct sockaddr *)dev, sizeof(struct sockaddr_ll));
    if (ret < 0) {
        perror("sendto");
        return -1;
    }
    printf("[ARP] 已发送请求，等待响应...\n");
    return 0;
}

/* ===================== ARP 响应接收 ===================== */

static int recv_arp_response(int sockfd, uint32_t expected_ip, uint8_t *out_mac)
{
    uint8_t buf[1024];

    while (!got_response) {
        struct timeval tv = {1, 0};
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(sockfd, &rfds);

        int ready = select(sockfd + 1, &rfds, NULL, NULL, &tv);
        if (ready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            return -1;
        }
        if (ready == 0) {
            /* 超时，可以在这里重试 */
            printf("[ARP] 本轮超时，继续等待...\n");
            continue;
        }

        ssize_t len = recvfrom(sockfd, buf, sizeof(buf), 0, NULL, NULL);
        if (len < (ssize_t)(sizeof(struct eth_frame) + sizeof(struct arp_header)))
            continue;

        struct eth_frame *eth = (struct eth_frame *)buf;
        /* 跳过非 ARP 包 */
        if (ntohs(eth->ether_type) != ETH_P_ARP)
            continue;

        struct arp_header *arp = (struct arp_header *)(buf + sizeof(struct eth_frame));
        /* 只处理 ARP 响应 */
        if (ntohs(arp->opcode) != ARP_RESPONSE)
            continue;

        /* 检查是否是期望的 IP 的响应 */
        uint32_t resp_ip;
        memcpy(&resp_ip, arp->sender_ip, 4);
        if (resp_ip != expected_ip)
            continue;

        /* 成功！复制 MAC 地址 */
        memcpy(out_mac, arp->sender_mac, 6);
        got_response = 1;
        return 0;
    }
    return -1;
}

/* ===================== 主函数 ===================== */

int main(int argc, char *argv[])
{
    if (argc != 5) {
        fprintf(stderr, "用法: %s <目标IP> <本机IP> <本机MAC> <网卡名>\n", argv[0]);
        fprintf(stderr, "例:   %s 192.168.1.1 192.168.1.100 00:11:22:33:44:55 eth0\n", argv[0]);
        fprintf(stderr, "\n注意：需要 root 权限运行（原始套接字）\n");
        return 1;
    }

    uint32_t target_ip, sender_ip;
    uint8_t sender_mac[6];

    /* 解析参数 */
    if (inet_pton(AF_INET, argv[1], &target_ip) != 1) {
        fprintf(stderr, "目标 IP 格式错误: %s\n", argv[1]);
        return 1;
    }
    if (inet_pton(AF_INET, argv[2], &sender_ip) != 1) {
        fprintf(stderr, "本机 IP 格式错误: %s\n", argv[2]);
        return 1;
    }
    if (sscanf(argv[3], "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
               &sender_mac[0], &sender_mac[1], &sender_mac[2],
               &sender_mac[3], &sender_mac[4], &sender_mac[5]) != 6) {
        fprintf(stderr, "本机 MAC 格式错误（应如 00:11:22:33:44:55）: %s\n", argv[3]);
        return 1;
    }

    printf("=== Linux neigh 子系统演示：ARP 解析 ===\n");
    printf("目标 IP  : %s\n", argv[1]);
    printf("本机 IP  : %s\n", argv[2]);
    printf("本机 MAC : %02x:%02x:%02x:%02x:%02x:%02x\n",
           sender_mac[0], sender_mac[1], sender_mac[2],
           sender_mac[3], sender_mac[4], sender_mac[5]);
    printf("网卡     : %s\n\n", argv[4]);

    /* ── 创建原始套接字 ── */
    int sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ARP));
    if (sockfd < 0) {
        perror("socket (需要 root 权限)");
        return 1;
    }

    /* ── 获取网卡索引 ── */
    struct ifreq ifr;
    strncpy(ifr.ifr_name, argv[4], IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';
    if (ioctl(sockfd, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl SIOCGIFINDEX");
        close(sockfd);
        return 1;
    }

    struct sockaddr_ll dev = {0};
    dev.sll_ifindex = ifr.ifr_ifindex;
    dev.sll_family  = AF_PACKET;
    dev.sll_protocol = htons(ETH_P_ARP);

    /* ── 绑定到网卡 ── */
    if (bind(sockfd, (struct sockaddr *)&dev, sizeof(dev)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    /* ── 设置信号（超时 5 秒）── */
    signal(SIGALRM, alarm_handler);
    alarm(5);

    /* ── 发 ARP 请求 ── */
    printf("[主线程] 发送 ARP 请求：谁有 %s ?\n", argv[1]);
    if (send_arp_request(sockfd, &dev, sender_mac, sender_ip, target_ip) < 0) {
        close(sockfd);
        return 1;
    }

    /* ── 等 ARP 响应 ── */
    uint8_t resp_mac[6];
    int ret = recv_arp_response(sockfd, target_ip, resp_mac);

    alarm(0);  /* 取消闹钟 */

    if (ret == 0) {
        printf("\n[成功] %s 的 MAC 地址是: %02x:%02x:%02x:%02x:%02x:%02x\n",
               argv[1],
               resp_mac[0], resp_mac[1], resp_mac[2],
               resp_mac[3], resp_mac[4], resp_mac[5]);
        printf("\n现在这个 MAC 已经被缓存到内核 neigh 表了，\n");
        printf("可以用 'ip neigh show' 查看。\n");
    } else {
        printf("\n[失败] ARP 解析失败，邻居不可达或超时\n");
        printf("可能原因：\n");
        printf("  - 目标 IP 不在同一网络\n");
        printf("  - 防火墙拦截了 ARP\n");
        printf("  - 目标机器不存在或离线\n");
    }

    close(sockfd);
    return ret;
}

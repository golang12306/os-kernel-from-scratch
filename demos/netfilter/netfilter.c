/*
 * netfilter.c — Linux netfilter HOOK 机制演示
 * 演示数据包在 5 个 HOOK 点的流转、规则匹配、连接跟踪
 *
 * 编译：gcc -o netfilter_demo netfilter.c
 * 运行：./netfilter_demo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// ============================================================
// 网络协议头（简化版）
// ============================================================

// IPv4 头（20字节）
typedef struct {
    uint8_t  version_ihl;    // 版本(4) + 头部长度(4)
    uint8_t  tos;
    uint16_t total_len;
    uint16_t id;
    uint16_t frag_offset;
    uint8_t  ttl;
    uint8_t  protocol;        // 6=TCP, 17=UDP, 1=ICMP
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} __attribute__((packed)) ip_header;

// TCP 头（20字节）
typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_offset;    // 数据偏移
    uint8_t  flags;           // SYN/FIN/ACK 等
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent;
} __attribute__((packed)) tcp_header;

// UDP 头（8字节）
typedef struct {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed)) udp_header;

// ============================================================
// netfilter HOOK 点定义（与内核一致）
// ============================================================

typedef enum nf_hook_stage {
    NF_PRE_ROUTING = 0,  // 路由判决之前（所有入站包）
    NF_LOCAL_IN,         // 路由判决后，目标本地（ INPUT 链）
    NF_FORWARD,          // 路由判决后，转发（FORWARD 链）
    NF_LOCAL_OUT,        // 本地生成（OUTPUT 链）
    NF_POST_ROUTING      // 外出前，最后机会（POSTROUTING 链）
} nf_hook_stage;

const char *hook_names[] = {
    "NF_PRE_ROUTING", "NF_LOCAL_IN", "NF_FORWARD",
    "NF_LOCAL_OUT", "NF_POST_ROUTING"
};

// HOOK 返回值
typedef enum nf_verdict {
    NF_DROP   = 0,    // 丢弃数据包
    NF_ACCEPT = 1,    // 放行
    NF_STOLEN = 2,    // 劫持（HOOK 处理，不再往下走）
    NF_QUEUE  = 3,    // 放入队列（用户空间处理）
    NF_REPEAT = 4,     // 重新调用此 HOOK
    NF_STOP   = 5     // 停止后续 HOOK 检查
} nf_verdict;

const char *verdict_names[] = {
    "NF_DROP", "NF_ACCEPT", "NF_STOLEN", "NF_QUEUE", "NF_REPEAT", "NF_STOP"
};

// ============================================================
// 模拟数据包
// ============================================================

typedef struct packet {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint8_t  protocol;   // 6=TCP, 17=UDP, 1=ICMP
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  flags;      // TCP flags
    int      indev;      // 入接口（0=环回, 1=eth0, 2=eth1）
    int      outdev;     // 出接口
    bool     routed;      // 是否已路由判决
    bool     local;       // 目标是否是本机
    bool     dropped;
    char     hook_path[64];  // 经过的 HOOK 点记录
} packet;

const char *ip_str(uint32_t ip) {
    static char buf[4][32];
    static int idx;
    idx = (idx + 1) % 4;
    snprintf(buf[idx], 32, "%d.%d.%d.%d",
             (ip >> 0) & 0xFF, (ip >> 8) & 0xFF,
             (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    return buf[idx];
}

packet *packet_create(uint32_t src, uint32_t dst, uint8_t proto,
                     uint16_t sport, uint16_t dport) {
    packet *p = calloc(1, sizeof(packet));
    p->src_ip = src;
    p->dst_ip = dst;
    p->protocol = proto;
    p->src_port = sport;
    p->dst_port = dport;
    p->indev = 1;   // 默认从 eth0 进入
    return p;
}

// ============================================================
// 模拟 iptables 规则
// ============================================================

typedef enum {
    PROTOCOL_TCP = 6,
    PROTOCOL_UDP = 17,
    PROTOCOL_ICMP = 1
} protocol_num;

typedef struct rule {
    nf_hook_stage stage;   // 应用在哪个 HOOK 点
    const char *chain;     // INPUT / OUTPUT / FORWARD 等
    uint8_t  protocol;     // 协议（0=任意）
    uint16_t src_port;     // 源端口（0=任意）
    uint16_t dst_port;     // 目标端口（0=任意）
    uint32_t src_ip;       // 源 IP（0=任意）
    uint32_t dst_ip;       // 目标 IP（0=任意）
    int      verdict;      // 返回值
    const char *comment;
    int matches;           // 命中计数
} rule;

rule rules[] = {
    // SSH 允许（TCP 22 端口）
    { NF_LOCAL_IN, "INPUT", PROTOCOL_TCP, 0, 22, 0, 0, NF_ACCEPT, "允许 SSH 入站" },
    // HTTP 允许
    { NF_LOCAL_IN, "INPUT", PROTOCOL_TCP, 0, 80, 0, 0, NF_ACCEPT, "允许 HTTP 入站" },
    // HTTPS 允许
    { NF_LOCAL_IN, "INPUT", PROTOCOL_TCP, 0, 443, 0, 0, NF_ACCEPT, "允许 HTTPS 入站" },
    // 允许已建立连接（连接跟踪）
    { NF_LOCAL_IN, "INPUT", 0, 0, 0, 0, 0, NF_ACCEPT, "允许已建立连接" },
    // 丢弃 ping（ICMP）
    { NF_LOCAL_IN, "INPUT", PROTOCOL_ICMP, 0, 0, 0, 0, NF_DROP, "丢弃 ICMP" },
    // 默认拒绝 INPUT
    { NF_LOCAL_IN, "INPUT", 0, 0, 0, 0, 0, NF_DROP, "默认拒绝 INPUT" },
    // OUTPUT 默认允许
    { NF_LOCAL_OUT, "OUTPUT", 0, 0, 0, 0, 0, NF_ACCEPT, "允许 OUTPUT" },
    // FORWARD 默认丢弃
    { NF_FORWARD, "FORWARD", 0, 0, 0, 0, 0, NF_DROP, "默认丢弃 FORWARD" },
};
int num_rules = sizeof(rules) / sizeof(rules[0]);

// 连接跟踪表（简化版）
typedef struct conn_entry {
    uint32_t src_ip, dst_ip;
    uint16_t src_port, dst_port;
    uint8_t  protocol;
    bool     established;
} conn_entry;

#define MAX_CONN 256
conn_entry conn_table[MAX_CONN];
int conn_count = 0;

void conn_track_add(packet *p) {
    if (conn_count < MAX_CONN) {
        conn_table[conn_count++] = (conn_entry){
            p->src_ip, p->dst_ip,
            p->src_port, p->dst_port,
            p->protocol, true
        };
    }
}

bool conn_track_lookup(packet *p) {
    for (int i = 0; i < conn_count; i++) {
        conn_entry *c = &conn_table[i];
        // 检查反向连接（已建立连接的双向流量）
        if (c->src_ip == p->dst_ip && c->dst_ip == p->src_ip &&
            c->src_port == p->dst_port && c->dst_port == p->src_port &&
            c->protocol == p->protocol && c->established) {
            return true;
        }
    }
    return false;
}

// ============================================================
// HOOK 模拟
// ============================================================

// 路由判决（简化：同网段直连，否则走网关）
void routing_decision(packet *p) {
    // 简化：目标 IP 最后一段 > 100 认为是本机
    p->local = ((p->dst_ip >> 0) & 0xFF) > 100;
    p->routed = true;
}

// 遍历所有 HOOK 点（按数据包流向）
int traverse_hooks(packet *p, nf_hook_stage first, nf_hook_stage last) {
    for (int stage = first; stage <= last; stage++) {
        printf("  → %s\n", hook_names[stage]);
        
        for (int i = 0; i < num_rules; i++) {
            rule *r = &rules[i];
            if (r->stage != stage) continue;
            
            // 协议匹配
            if (r->protocol && r->protocol != p->protocol) continue;
            // 目标端口匹配
            if (r->dst_port && r->dst_port != p->dst_port) continue;
            // 源端口匹配
            if (r->src_port && r->src_port != p->src_port) continue;
            // IP 匹配
            if (r->src_ip && r->src_ip != p->src_ip) continue;
            if (r->dst_ip && r->dst_ip != p->dst_ip) continue;
            
            // 连接跟踪例外
            if (strstr(r->comment, "已建立连接") && !conn_track_lookup(p)) continue;
            
            r->matches++;
            printf("    ★ 命中规则: %s [%s]\n", r->chain, r->comment);
            printf("    → verdict: %s\n", verdict_names[r->verdict]);
            
            return r->verdict;
        }
    }
    return NF_ACCEPT; // 默认放行
}

// 处理数据包（模拟 netfilter 核心处理流程）
void process_packet(packet *p) {
    printf("\n处理数据包：%s:%d → %s:%d [%s]\n",
           ip_str(p->src_ip), p->src_port,
           ip_str(p->dst_ip), p->dst_port,
           p->protocol == PROTOCOL_TCP ? "TCP" :
           p->protocol == PROTOCOL_UDP ? "UDP" : "ICMP");
    printf("  入接口: %d\n", p->indev);
    
    // ===== 入站数据包路径 =====
    if (!p->routed) routing_decision(p);
    
    if (p->local) {
        // ===== 本机接收 =====
        printf("  路由判决: 目标本机\n");
        printf("  HOOK 路径: PRE_ROUTING → LOCAL_IN\n");
        
        int v = traverse_hooks(p, NF_PRE_ROUTING, NF_LOCAL_IN);
        p->dropped = (v == NF_DROP);
        
        // 新建连接记录（三次握手完成后）
        if (!p->dropped && p->protocol == PROTOCOL_TCP &&
            (p->flags & 0x10)) { // ACK
            conn_track_add(p);
        }
    } else {
        // ===== 转发 =====
        printf("  路由判决: 需转发\n");
        printf("  HOOK 路径: PRE_ROUTING → FORWARD → POST_ROUTING\n");
        
        int v = traverse_hooks(p, NF_PRE_ROUTING, NF_FORWARD);
        if (v == NF_ACCEPT) {
            printf("  → 转发前经过 POST_ROUTING\n");
            int v2 = traverse_hooks(p, NF_POST_ROUTING, NF_POST_ROUTING);
            p->dropped = (v2 == NF_DROP);
        } else {
            p->dropped = (v == NF_DROP);
        }
    }
}

// ============================================================
// 演示
// ============================================================

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║      netfilter HOOK 机制演示 — 从零写 OS 内核系列       ║\n");
    printf("║      5 个 HOOK 点  ·  iptables 规则匹配  ·  连接跟踪    ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    
    // IP: 192.168.1.xx
    uint32_t local_ip   = (192 << 24) | (168 << 16) | (1 << 8) | 100;
    uint32_t remote_ip  = (192 << 24) | (168 << 16) | (1 << 8) | 50;
    uint32_t external_ip = (8 << 24) | (8 << 16) | (8 << 8) | 8;  // 8.8.8.8
    
    printf("\n【场景 1】外部主机访问本机 Web 服务（80端口）\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    packet *p1 = packet_create(remote_ip, local_ip, PROTOCOL_TCP, 12345, 80);
    p1->flags = 0x02; // SYN
    process_packet(p1);
    printf("\n结果: %s\n", p1->dropped ? "❌ 丢弃（包未到达服务）" : "✅ 放行（到达 nginx）");
    free(p1);
    
    printf("\n【场景 2】外部 ping 本机（ICMP）\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    packet *p2 = packet_create(remote_ip, local_ip, PROTOCOL_ICMP, 0, 0);
    process_packet(p2);
    printf("\n结果: %s\n", p2->dropped ? "❌ 丢弃（ping 无响应）" : "✅ 放行");
    free(p2);
    
    printf("\n【场景 3】本机访问外部 DNS（UDP 53）\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    packet *p3 = packet_create(local_ip, external_ip, PROTOCOL_UDP, 53, 53);
    printf("  本机生成数据包：%s:%d → %s:%d [UDP]\n",
           ip_str(p3->src_ip), p3->src_port,
           ip_str(p3->dst_ip), p3->dst_port);
    printf("  路由判决: 目标非本机（外出）\n");
    printf("  HOOK 路径: LOCAL_OUT → POST_ROUTING\n");
    int v3 = traverse_hooks(p3, NF_LOCAL_OUT, NF_LOCAL_OUT);
    printf("  → verdict: %s\n", verdict_names[v3]);
    if (v3 == NF_ACCEPT) {
        int v3b = traverse_hooks(p3, NF_POST_ROUTING, NF_POST_ROUTING);
        printf("  → POSTROUTING verdict: %s\n", verdict_names[v3b]);
        p3->dropped = (v3b == NF_DROP);
    }
    printf("\n结果: %s\n", p3->dropped ? "❌ 丢弃" : "✅ 放行（DNS 请求外出）");
    free(p3);
    
    printf("\n【场景 4】外部攻击者扫描本机端口（SYN 扫描）\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    packet *p4 = packet_create(remote_ip, local_ip, PROTOCOL_TCP, 20000, 22);
    p4->flags = 0x02; // SYN
    process_packet(p4);
    printf("\n结果: %s\n", p4->dropped ? "❌ 丢弃" : "✅ 放行（SSH 端口开放）");
    free(p4);
    
    printf("\n【场景 5】本机已建立连接的回程包（连接跟踪）\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("前提：场景1的连接已建立（三次握手完成）\n");
    // 先处理一次场景1建立连接
    packet *p5a = packet_create(remote_ip, local_ip, PROTOCOL_TCP, 12345, 80);
    p5a->flags = 0x02;
    process_packet(p5a);
    // SYN+ACK
    packet *p5b = packet_create(local_ip, remote_ip, PROTOCOL_TCP, 80, 12345);
    p5b->flags = 0x12; // SYN+ACK
    process_packet(p5b);
    // ACK
    packet *p5c = packet_create(remote_ip, local_ip, PROTOCOL_TCP, 12345, 80);
    p5c->flags = 0x10; // ACK
    conn_track_add(p5a); conn_track_add(p5b); conn_track_add(p5c);
    
    // 回程数据（服务端 → 客户端）
    packet *p5d = packet_create(local_ip, remote_ip, PROTOCOL_TCP, 80, 12345);
    p5d->flags = 0x10; // ACK+PUSH
    printf("\n处理回程数据包（服务端→客户端）：\n");
    routing_decision(p5d);
    printf("  路由判决: 目标非本机（外出）\n");
    printf("  HOOK 路径: LOCAL_OUT → POST_ROUTING\n");
    int v5 = traverse_hooks(p5d, NF_LOCAL_OUT, NF_LOCAL_OUT);
    printf("  → verdict: %s（连接跟踪表匹配）\n", verdict_names[v5]);
    printf("\n结果: %s\n", p5d->dropped ? "❌ 丢弃" : "✅ 放行（已建立连接的回程数据）");
    free(p5a); free(p5b); free(p5c); free(p5d);
    
    printf("\n【场景 6】家用路由转发（内网 → 外网）\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    uint32_t lan_ip  = (192 << 24) | (168 << 16) | (1 << 8) | 50;
    packet *p6 = packet_create(lan_ip, external_ip, PROTOCOL_TCP, 40000, 443);
    p6->indev = 2; // 内网口 eth1
    p6->outdev = 3; // 外网口 eth2
    printf("  家用路由处理内网主机访问外网 HTTPS：\n");
    printf("  %s:%d → %s:%d [TCP]\n",
           ip_str(p6->src_ip), p6->src_port,
           ip_str(p6->dst_ip), p6->dst_port);
    printf("  接口: eth1(内网) → eth2(外网)\n");
    routing_decision(p6);
    printf("  路由判决: 需转发\n");
    printf("  HOOK 路径: PRE_ROUTING → FORWARD → POST_ROUTING\n");
    int v6 = traverse_hooks(p6, NF_PRE_ROUTING, NF_FORWARD);
    if (v6 == NF_ACCEPT) {
        traverse_hooks(p6, NF_POST_ROUTING, NF_POST_ROUTING);
    }
    printf("\n结果: %s（NAT 之后包到达外网）\n", p6->dropped ? "❌ 丢弃" : "✅ 放行");
    free(p6);
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║              netfilter 5 大 HOOK 点                      ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║                                                              ║\n");
    printf("║   数据包                               HOOK 点              ║\n");
    printf("║   ─────────────────────────────────────────────           ║\n");
    printf("║   进入网卡 ─→ [PRE_ROUTING]  路由判决前                  ║\n");
    printf("║                    ↓                                       ║\n");
    printf("║              路由判决 → 本机？                             ║\n");
    printf("║                    ↓                                       ║\n");
    printf("║   ┌──────────────┴──────────────┐                          ║\n");
    printf("║   ↓                              ↓                          ║\n");
    printf("║ [LOCAL_IN]                 [FORWARD]                        ║\n");
    printf("║ 本机接收                        ↓                          ║\n");
    printf("║ (INPUT 链)           路由判决后转发                        ║\n");
    printf("║   ↓                    (FORWARD 链)                        ║\n");
    printf("║ [本机进程]               ↓                                  ║\n");
    printf("║   ↓              [POST_ROUTING]                           ║\n");
    printf("║ [LOCAL_OUT]       外出前（NAT 机会）                        ║\n");
    printf("║ 本机生成                                               ║\n");
    printf("║ (OUTPUT 链)                                           ║\n");
    printf("║   ↓                                                      ║\n");
    printf("║ [POST_ROUTING]                                          ║\n");
    printf("║   ↓                                                      ║\n");
    printf("║   发送到网卡                                             ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  关注公众号「必收」并 star 本仓库                       ║\n");
    printf("║  GitHub: github.com/golang12306/os-kernel-from-scratch  ║\n");
    printf("║  对应 Demo: demos/netfilter/netfilter.c                 ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    
    return 0;
}

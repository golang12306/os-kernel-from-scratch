/*
 * conntrack.c — Linux 连接跟踪（conntrack）机制演示
 * 演示 TCP 三次握手建立、UDP 单向/双向会话、连接状态机
 *
 * 编译：gcc -o conntrack_demo conntrack.c
 * 运行：./conntrack_demo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// ============================================================
// 网络协议
// ============================================================

const char *ip_str(uint32_t ip) {
    static char buf[4][32];
    static int idx;
    idx = (idx + 1) % 4;
    snprintf(buf[idx], 32, "%d.%d.%d.%d",
             (ip>>0)&0xFF, (ip>>8)&0xFF,
             (ip>>16)&0xFF, (ip>>24)&0xFF);
    return buf[idx];
}

const char *proto_name(uint8_t p) {
    return p == 6 ? "TCP" : p == 17 ? "UDP" : "ICMP";
}

// TCP flags
#define TCP_SYN  0x02
#define TCP_ACK  0x10
#define TCP_FIN  0x01
#define TCP_RST  0x04
#define TCP_PSH  0x08

const char *tcp_flags_str(uint8_t f) {
    static char s[64];
    s[0] = 0;
    if (f & TCP_SYN) strcat(s, "SYN ");
    if (f & TCP_ACK) strcat(s, "ACK ");
    if (f & TCP_FIN) strcat(s, "FIN ");
    if (f & TCP_RST) strcat(s, "RST ");
    if (f & TCP_PSH) strcat(s, "PSH ");
    if (!s[0]) strcpy(s, "-");
    return s;
}

// ============================================================
// 连接状态（TCP 状态机）
// ============================================================

typedef enum {
    TCP_CLOSED,        // 关闭
    TCP_SYN_SENT,      // 已发 SYN
    TCP_SYN_RECV,      // 收到 SYN（half-open）
    TCP_ESTABLISHED,   // 已建立
    TCP_FIN_WAIT,      // 已发 FIN
    TCP_CLOSE_WAIT,    // 收到 FIN（对端关闭）
    TCP_LAST_ACK,      // 最后确认
    TCP_TIME_WAIT      // 等待 2MSL
} tcp_state;

const char *tcp_state_names[] = {
    "CLOSED", "SYN_SENT", "SYN_RECV", "ESTABLISHED",
    "FIN_WAIT", "CLOSE_WAIT", "LAST_ACK", "TIME_WAIT"
};

// ============================================================
// 连接跟踪条目
// ============================================================

typedef enum { CONN_TCP, CONN_UDP, CONN_ICMP } conn_type;

typedef enum {
    // TCP 状态
    CT_TCP_SYN_SENT,
    CT_TCP_SYN_RECV,
    CT_TCP_ESTABLISHED,
    CT_TCP_FIN_WAIT,
    CT_TCP_CLOSE_WAIT,
    CT_TCP_LAST_ACK,
    CT_TCP_TIME_WAIT,
    // UDP 状态
    CT_UDP_UNREPLIED,   // UDP 一方发了包，还没收到对端回复
    CT_UDP_REPLIED,     // UDP 双向有包
    // ICMP
    CT_ICMP,
    // 通用
    CT_NONE
} conn_state;

const char *conn_state_names[] = {
    "TCP_SYN_SENT", "TCP_SYN_RECV", "TCP_ESTABLISHED",
    "TCP_FIN_WAIT", "TCP_CLOSE_WAIT", "TCP_LAST_ACK", "TCP_TIME_WAIT",
    "UDP_UNREPLIED", "UDP_REPLIED", "ICMP", "NONE"
};

typedef struct conn_entry {
    uint32_t orig_src, orig_dst;
    uint16_t orig_sport, orig_dport;
    uint8_t  protocol;
    conn_type type;
    conn_state state;
    uint8_t  dir; // 0=orig, 1=reply
    uint32_t reply_src, reply_dst;  // NAT 后可能改变
    uint16_t reply_sport, reply_dport;
    int      timeout;
    struct conn_entry *next;
} conn_entry;

#define MAX_CONN 512
conn_entry *conn_hash[MAX_CONN];
int conn_total = 0;

// 哈希（简化五元组）
int conn_hash_key(uint32_t s, uint32_t d, uint16_t sp, uint16_t dp, uint8_t proto) {
    return ((s ^ d ^ sp ^ dp ^ proto) * 0x9e3779b1) % MAX_CONN;
}

conn_entry *conn_lookup(uint32_t s, uint32_t d, uint16_t sp, uint16_t dp,
                        uint8_t proto, uint8_t flags, bool *is_reply) {
    int k = conn_hash_key(s, d, sp, dp, proto);
    for (conn_entry *e = conn_hash[k]; e; e = e->next) {
        // 精确五元组匹配
        if (e->orig_src == s && e->orig_dst == d &&
            e->orig_sport == sp && e->orig_dport == dp &&
            e->protocol == proto) {
            *is_reply = false;
            return e;
        }
        // 反向匹配（reply 方向）
        if (e->orig_src == d && e->orig_dst == s &&
            e->orig_sport == dp && e->orig_dport == sp &&
            e->protocol == proto) {
            *is_reply = true;
            return e;
        }
    }
    return NULL;
}

conn_entry *conn_create(uint32_t s, uint32_t d, uint16_t sp, uint16_t dp,
                        uint8_t proto, conn_type t, conn_state st) {
    int k = conn_hash_key(s, d, sp, dp, proto);
    conn_entry *e = calloc(1, sizeof(conn_entry));
    e->orig_src = s; e->orig_dst = d;
    e->orig_sport = sp; e->orig_dport = dp;
    e->protocol = proto;
    e->type = t;
    e->state = st;
    e->reply_src = d; e->reply_dst = s;
    e->reply_sport = dp; e->reply_dport = sp;
    e->next = conn_hash[k];
    conn_hash[k] = e;
    conn_total++;
    return e;
}

void conn_update_reply(conn_entry *e, uint32_t rs, uint32_t rd,
                       uint16_t rsp, uint16_t rdp) {
    e->reply_src = rs; e->reply_dst = rd;
    e->reply_sport = rsp; e->reply_dport = rdp;
}

// ============================================================
// iptables state 匹配模拟
// ============================================================

// 模拟 iptables -m state --state ESTABLISHED
const char *match_state(conn_entry *e, bool is_reply) {
    if (!e) return "UNKNOWN";

    switch (e->state) {
        case CT_TCP_ESTABLISHED:
            return "ESTABLISHED";
        case CT_UDP_REPLIED:
            return "ESTABLISHED";
        case CT_TCP_SYN_SENT:
        case CT_TCP_SYN_RECV:
            return "RELATED"; // 某些情况下
        default:
            return "NEW";
    }
}

bool is_established(conn_entry *e) {
    if (!e) return false;
    return e->state == CT_TCP_ESTABLISHED || e->state == CT_UDP_REPLIED;
}

// ============================================================
// TCP 状态机
// ============================================================

void tcp_step(conn_entry *e, uint8_t flags_in, bool is_reply, bool *dropped) {
    *dropped = false;
    conn_state old = e->state;

    if (!is_reply) {
        // 原始方向
        switch (e->state) {
            case CT_TCP_SYN_SENT:
                if (flags_in == (TCP_SYN | TCP_ACK)) {
                    e->state = CT_TCP_SYN_RECV;
                }
                break;
            case CT_TCP_SYN_RECV:
                if (flags_in == TCP_ACK) {
                    e->state = CT_TCP_ESTABLISHED;
                }
                break;
            case CT_TCP_ESTABLISHED:
                if (flags_in == TCP_FIN) {
                    e->state = CT_TCP_FIN_WAIT;
                } else if (flags_in == TCP_RST) {
                    e->state = CT_TCP_SYN_SENT; // 重置
                }
                break;
            case CT_TCP_FIN_WAIT:
                if (flags_in == TCP_ACK) {
                    e->state = CT_TCP_TIME_WAIT;
                }
                break;
            case CT_TCP_TIME_WAIT:
                // 2MSL 后变 CLOSED（超时处理）
                break;
            default:
                break;
        }
    } else {
        // 回复方向
        switch (e->state) {
            case CT_TCP_SYN_SENT:
                if (flags_in == (TCP_SYN | TCP_ACK)) {
                    e->state = CT_TCP_SYN_RECV; // 对端收到 SYN+ACK
                } else if (flags_in == TCP_ACK) {
                    e->state = CT_TCP_ESTABLISHED;
                }
                break;
            case CT_TCP_SYN_RECV:
                if (flags_in == TCP_FIN) {
                    e->state = CT_TCP_CLOSE_WAIT;
                }
                break;
            case CT_TCP_ESTABLISHED:
                if (flags_in == TCP_FIN) {
                    e->state = CT_TCP_CLOSE_WAIT;
                }
                break;
            case CT_TCP_CLOSE_WAIT:
                if (flags_in == TCP_FIN) {
                    e->state = CT_TCP_LAST_ACK;
                }
                break;
            default:
                break;
        }
    }

    if (old != e->state) {
        printf("    TCP状态: %s → %s\n", tcp_state_names[old], tcp_state_names[e->state]);
    }
}

// ============================================================
// 演示：TCP 三次握手 + 四次挥手
// ============================================================

void demo_tcp_handshake(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     TCP 三次握手 + 连接跟踪                        ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    uint32_t cli = (192<<24)|(168<<16)|(1<<8)|50;
    uint32_t srv = (192<<24)|(168<<16)|(1<<8)|100;
    uint16_t sport = 12345, dport = 80;

    printf("\n[连接 1] 客户端 %s:%d → 服务器 %s:%d [TCP]\n",
           ip_str(cli), sport, ip_str(srv), dport);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    // ===== TCP 三次握手 =====
    printf("\n① 客户端发送 SYN（主动打开）\n");
    conn_entry *e = conn_create(cli, srv, sport, dport, 6, CONN_TCP, CT_TCP_SYN_SENT);
    printf("  conntrack: 新建条目 state=SYN_SENT\n");
    printf("  iptables -m state --state NEW → %s\n", match_state(e, false));

    printf("\n② 服务器回复 SYN+ACK（被动打开）\n");
    bool dropped = false;
    tcp_step(e, TCP_SYN | TCP_ACK, false, &dropped);
    printf("  conntrack: state → SYN_RECV\n");
    printf("  服务器发送 SYN+ACK，客户端收到后:\n");
    bool cli_dropped = false;
    tcp_step(e, TCP_SYN | TCP_ACK, true, &cli_dropped);
    printf("  客户端 TCP: state → SYN_RECV\n");

    printf("\n③ 客户端发送 ACK\n");
    tcp_step(e, TCP_ACK, false, &dropped);
    printf("  conntrack: state → ESTABLISHED\n");
    printf("  iptables -m state --state ESTABLISHED → %s\n", match_state(e, false));
    printf("  ✅ 连接建立完成！后续所有包命中 ESTABLISHED 规则\n");

    // ===== HTTP 请求/响应（已建立连接）=====
    printf("\n④ 客户端发送 HTTP GET（PSH+ACK）\n");
    tcp_step(e, TCP_PSH | TCP_ACK, false, &dropped);
    printf("  state=ESTABLISHED → %s\n", match_state(e, false));
    printf("  iptables -m state --state ESTABLISHED,RELATED → ACCEPT（自动放行）\n");

    printf("\n⑤ 服务器回复 HTTP 200 + FIN（数据完了要关闭）\n");
    tcp_step(e, TCP_PSH | TCP_ACK | TCP_FIN, true, &dropped);
    printf("  服务器: ESTABLISHED → CLOSE_WAIT\n");

    // ===== TCP 四次挥手 =====
    printf("\n⑥ 客户端确认关闭（ACK）\n");
    tcp_step(e, TCP_ACK, false, &dropped);
    printf("  客户端: CLOSE_WAIT → FIN_WAIT\n");

    printf("\n⑦ 客户端发送 FIN（我要关了）\n");
    tcp_step(e, TCP_FIN, false, &dropped);
    printf("  客户端: FIN_WAIT → TIME_WAIT\n");

    printf("\n⑧ 服务器确认（ACK）\n");
    tcp_step(e, TCP_ACK, true, &dropped);
    printf("  服务器: LAST_ACK（等待最后 ACK）\n");

    printf("\n⑨ 客户端最后 ACK\n");
    tcp_step(e, TCP_ACK, false, &dropped);
    printf("  客户端: TIME_WAIT（等 2MSL）→ 最终 CLOSED\n");
}

// ============================================================
// 演示：UDP + NAT 场景
// ============================================================

void demo_udp_nat(void) {
    printf("\n\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     UDP 单向会话 + NAT 下的连接跟踪                  ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    uint32_t lan = (192<<24)|(168<<16)|(1<<8)|50;
    uint32_t pub = (8<<24)|(8<<16)|(8<<8)|8;  // 8.8.8.8
    uint16_t cport = 33333, dport = 53;

    printf("\n[连接 2] 内网主机 %s:%d → DNS服务器 %s:%d [UDP]\n",
           ip_str(lan), cport, ip_str(pub), dport);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    printf("\n① 内网主机发 DNS 请求\n");
    conn_entry *e = conn_create(lan, pub, cport, dport, 17, CONN_UDP, CT_UDP_UNREPLIED);
    printf("  conntrack: 新建条目 state=UDP_UNREPLIED\n");
    printf("  iptables -m state --state NEW → %s\n", match_state(e, false));

    printf("\n② 家用路由做 SNAT（POSTROUTING）\n");
    printf("  源IP %s → 公网IP（MASQUERADE）\n", ip_str(lan));
    conn_update_reply(e, pub, lan, dport, cport);
    printf("  reply 方向自动填充: %s:%d\n", ip_str(e->reply_src), e->reply_sport);

    printf("\n③ DNS 服务器回复\n");
    // DNS 回复：pub:53 → 公网IP:随机端口（假设NAT后端口还是33333）
    conn_state old = e->state;
    e->state = CT_UDP_REPLIED;
    printf("  conntrack: state UDP_UNREPLIED → UDP_REPLIED\n");
    printf("  conntrack 查表：reply 方向匹配 NAT 转换\n");
    printf("  iptables -m state --state ESTABLISHED → %s\n", match_state(e, true));
    printf("  ✅ DNS 回复直接路由回内网主机（无需内网显式放行）\n");

    printf("\n④ 家用路由查 conntrack 表做 DNAT\n");
    printf("  目标IP: 公网IP → %s（查表得原始内网IP）\n", ip_str(e->orig_src));
    printf("  目标端口: 随机 → %d（查表得原始端口）\n", e->orig_sport);
    printf("  ✅ 包到达内网主机，应用收到 DNS 响应\n");
}

// ============================================================
// 演示：ICMP ping 跟踪
// ============================================================

void demo_icmp(void) {
    printf("\n\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     ICMP 连接跟踪（ping 往返）                         ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    uint32_t src = (192<<24)|(168<<16)|(1<<8)|50;
    uint32_t dst = (192<<24)|(168<<16)|(1<<8)|100;

    printf("\n[连接 3] %s ping %s [ICMP]\n", ip_str(src), ip_str(dst));
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    printf("\n① 发送 ICMP Echo Request\n");
    conn_entry *e = conn_create(src, dst, 0, 0, 1, CONN_ICMP, CT_ICMP);
    printf("  conntrack: 新建 ICMP 条目\n");
    printf("  iptables -m state --state NEW → %s\n", match_state(e, false));

    printf("\n② 收到 ICMP Echo Reply\n");
    // ICMP reply：用 ICMP ID 做匹配（简化用原始 src/dst）
    conn_entry *r = conn_lookup(dst, src, 0, 0, 1, 0, &(bool){0});
    if (r) {
        printf("  conntrack: 查表匹配（ICMP ID / 五元组）\n");
        printf("  iptables -m state --state ESTABLISHED/RELATED → ACCEPT\n");
        printf("  ✅ Echo Reply 被自动放行\n");
    } else {
        printf("  模拟 ICMP reply 查找...\n");
    }
}

// ============================================================
// 演示：家用路由完整路径
// ============================================================

void demo_router_forward(void) {
    printf("\n\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     家用路由：内网主机通过 NAT 访问外网 HTTPS         ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    uint32_t lan = (192<<24)|(168<<16)|(1<<8)|50;
    uint32_t wan = (10<<24)|(0<<16)|(0<<8)|1;  // 家用路由 WAN 口 IP
    uint32_t ext = (142<<24)|(250<<16)|(50<<8)|10; // 外网服务器
    uint16_t sport = 45678, dport = 443;

    printf("\n[连接 4] 内网 %s:%d → 外网 %s:%d [TCP]\n",
           ip_str(lan), sport, ip_str(ext), dport);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    printf("\n① NF_PRE_ROUTING — conntrack 记录新连接\n");
    conn_entry *e = conn_create(lan, ext, sport, dport, 6, CONN_TCP, CT_TCP_SYN_SENT);
    printf("  conntrack: 新建条目五元组 (%s:%d → %s:%d)\n",
           ip_str(lan), sport, ip_str(ext), dport);
    printf("  state = TCP_SYN_SENT\n");

    printf("\n② NAT PREROUTING — 此场景无 DNAT\n");
    printf("  跳过\n");

    printf("\n③ 路由判决 → FORWARD（转发）\n");

    printf("\n④ NF_FORWARD — filter FORWARD 链\n");
    printf("  iptables -A FORWARD -m state --state NEW -j ACCEPT\n");
    printf("  state=NEW → ACCEPT\n");

    printf("\n⑤ NAT POSTROUTING — MASQUERADE（SNAT）\n");
    printf("  源IP: %s → %s\n", ip_str(lan), ip_str(wan));
    conn_update_reply(e, ext, lan, dport, sport);
    printf("  conntrack: reply 方向写入 (%s:%d → %s:%d)\n",
           ip_str(ext), dport, ip_str(lan), sport);

    printf("\n⑥ 三次握手完成，连接建立\n");
    e->state = CT_TCP_ESTABLISHED;
    printf("  conntrack: state → ESTABLISHED\n");

    printf("\n⑦ 后续所有包（数据/ACK）\n");
    printf("  NF_FORWARD: -m state --state ESTABLISHED → ACCEPT\n");
    printf("  ✅ 无需任何额外规则，自动放行已建立连接的双向流量\n");
}

// ============================================================
// 总结
// ============================================================

void print_summary(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║              连接跟踪（conntrack）总结                ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║                                                            ║\n");
    printf("║  conntrack 在 PRE_ROUTING 记录每个新连接的五元组          ║\n");
    printf("║                                                            ║\n");
    printf("║  TCP: SYN_SENT → SYN_RECV → ESTABLISHED → FIN_WAIT...    ║\n");
    printf("║  UDP: UNREPLIED → REPLIED                                 ║\n");
    printf("║  ICMP: 记录后 reply 自动 RELATED                          ║\n");
    printf("║                                                            ║\n");
    printf("║  ESTABLISHED 的包：                                       ║\n");
    printf("║    iptables -m state --state ESTABLISHED → ACCEPT        ║\n");
    printf("║    无需逐包检查防火墙规则                                  ║\n");
    printf("║                                                            ║\n");
    printf("║  NAT 依赖 conntrack 表：                                   ║\n");
    printf("║    SNAT: 记录 reply 方向 → 回来的包自动逆向转换            ║\n");
    printf("║    DNAT: PREROUTING 查表 → 修改目标 IP/端口               ║\n");
    printf("║                                                            ║\n");
    printf("║  conntrack 表大小：/proc/sys/net/netfilter/nf_conntrack_max║\n");
    printf("║  查看当前连接：conntrack -L 或 cat /proc/net/nf_conntrack ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n当前跟踪连接数: %d\n", conn_total);
}

// ============================================================
// 主程序
// ============================================================

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  conntrack 连接跟踪机制演示 — 从零写 OS 内核系列    ║\n");
    printf("║  TCP状态机  ·  UDP会话  ·  NAT配合  ·  状态防火墙 ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    demo_tcp_handshake();
    demo_udp_nat();
    demo_icmp();
    demo_router_forward();
    print_summary();

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  关注公众号「必收」并 star 本仓库                       ║\n");
    printf("║  GitHub: github.com/golang12306/os-kernel-from-scratch  ║\n");
    printf("║  对应 Demo: demos/conntrack/conntrack.c                ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    return 0;
}

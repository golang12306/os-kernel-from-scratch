/*
 * route.c — Linux 路由表机制演示
 * 演示最长前缀匹配、路由查找、多路径路由、网关路由
 *
 * 编译：gcc -o route_demo route.c
 * 运行：./route_demo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <arpa/inet.h>

// ============================================================
// 网络工具
// ============================================================

const char *ip_str(uint32_t ip) {
    static char b[8][64];
    static int i;
    i = (i + 1) % 8;
    snprintf(b[i], 64, "%d.%d.%d.%d",
             (ip>>24)&0xFF, (ip>>16)&0xFF,
             (ip>>8)&0xFF, (ip>>0)&0xFF);
    return b[i];
}

void ip_hex(uint32_t *ip) {
    for (int i = 0; i < 4; i++) {
        unsigned char c = (unsigned char)((ip[i] >> (8*i)) & 0xFF);
        printf("%d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
        (void)c;
    }
}

// ============================================================
// 路由表条目
// ============================================================

typedef struct {
    uint32_t dst;       // 目的网络（网络字节序）
    uint32_t mask;      // 子网掩码（网络字节序）
    uint32_t gw;        // 下一跳网关（0 表示直连）
    uint32_t interface; // 出接口 IP（直连时为本机接口 IP）
    const char *iface_name;
    int metric;         // 路由度量值（越小越优先）
    const char *comment;
} route_entry;

route_entry kernel_routes[] = {
    // 直连路由（接口直接相连的网络）
    { .dst = 0xC0A80100U, .mask = 0xFFFFFF00U, .gw = 0,           .interface = 0xC0A80101U, .iface_name = "eth0:192.168.1.1", .metric = 0,   .comment = "直连: 192.168.1.0/24（内网）" },
    { .dst = 0x0A000000U, .mask = 0xFF000000U, .gw = 0,           .interface = 0x0A00000AU, .iface_name = "eth1:10.0.0.10",  .metric = 0,   .comment = "直连: 10.0.0.0/8（办公网络）" },
    { .dst = 0x00000000U, .mask = 0x00000000U, .gw = 0xC0A80101U, .interface = 0xC0A80101U, .iface_name = "eth0:192.168.1.1", .metric = 100, .comment = "默认路由 → 192.168.1.1（网关）" },
};
int num_routes = sizeof(kernel_routes) / sizeof(kernel_routes[0]);

// ============================================================
// 路由查找（最长前缀匹配）
// ============================================================

// 检查路由是否匹配目标 IP
bool route_matches(uint32_t route_dst, uint32_t route_mask, uint32_t target_ip) {
    return (target_ip & route_mask) == (route_dst & route_mask);
}

// 计算前缀长度（mask 中 1 的位数）
int prefix_len(uint32_t mask) {
    int len = 0;
    uint32_t m = mask;
    while (m) { len += m & 1; m >>= 1; }
    return len;
}

typedef struct {
    route_entry *entry;
    int match_len; // 匹配的前缀长度
} route_result;

route_result lookup_route(uint32_t dst_ip) {
    route_result best = { NULL, -1 };

    for (int i = 0; i < num_routes; i++) {
        route_entry *r = &kernel_routes[i];
        if (route_matches(r->dst, r->mask, dst_ip)) {
            int ml = prefix_len(r->mask);
            if (ml > best.match_len) {
                best.entry = r;
                best.match_len = ml;
            }
        }
    }

    return best;
}

// ============================================================
// 路由决策流程
// ============================================================

typedef enum {
    RT_DELIVER,    // 直连交付（本机）
    RT_FORWARD,    // 转发
    RT_DROP,       // 丢弃（无路由）
    RT_LOCAL       // 本机生成
} route_action;

const char *action_names[] = { "直连交付", "转发", "丢弃", "本机生成" };

typedef struct {
    route_action action;
    route_entry *route;
    uint32_t next_hop;
    const char *reason;
} route_decision;

route_decision route_packet(uint32_t src_ip, uint32_t dst_ip, bool is_local) {
    route_decision dec = { RT_DROP, NULL, 0, "无匹配路由" };

    if (!is_local) {
        // 查询到目标网络的路由
        route_result r = lookup_route(dst_ip);
        if (!r.entry) {
            dec.action = RT_DROP;
            dec.reason = "无到目标网络的路由";
            return dec;
        }

        if (r.entry->gw == 0) {
            // 直连：直接 ARP 对方 MAC（同一网络）
            dec.action = RT_DELIVER;
            dec.route = r.entry;
            dec.next_hop = dst_ip; // 同一网络，下一跳是目标 IP
            dec.reason = "直连网络，直接 ARP";
        } else {
            // 非直连：通过网关
            dec.action = RT_FORWARD;
            dec.route = r.entry;
            dec.next_hop = r.entry->gw;
            dec.reason = "非直连，走网关";
        }
    } else {
        // 本机生成的包
        dec.action = RT_LOCAL;
        dec.reason = "本机生成，查路由表决定出口";
        route_result r = lookup_route(dst_ip);
        if (r.entry) {
            dec.route = r.entry;
            dec.next_hop = r.entry->gw ? r.entry->gw : dst_ip;
        }
    }

    return dec;
}

// ============================================================
// 演示场景
// ============================================================

void demo_local_delivery(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     场景 1：同网络直连交付（ARP）                     ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    uint32_t dst = (192<<24)|(168<<16)|(1<<8)|80; // 同 192.168.1.0/24
    printf("\n目标 IP: %s\n", ip_str(dst));
    printf("本机接口: eth0 = %s\n", ip_str(0xC0A80101U));

    route_decision d = route_packet(0xC0A80101U, dst, false);
    printf("\n路由决策:\n");
    printf("  操作: %s\n", action_names[d.action]);
    printf("  匹配路由: %s\n", d.route ? d.route->comment : "无");
    printf("  下一跳: %s\n", ip_str(d.next_hop));
    printf("  理由: %s\n", d.reason);
    printf("  出口接口: %s\n", d.route ? d.route->iface_name : "无");
    printf("\n→ 查路由表 → 同网络 → 直接 ARP %s 的 MAC → 封装 eth0 发出\n",
           ip_str(dst));
}

void demo_gateway_forward(void) {
    printf("\n\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     场景 2：通过网关转发                                 ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    uint32_t dst = (8<<24)|(8<<16)|(8<<8)|8; // 8.8.8.8（外网）
    printf("\n目标 IP: %s\n", ip_str(dst));
    printf("本机接口: eth0 = %s\n", ip_str(0xC0A80101U));

    printf("\n查路由表:\n");
    route_result r = lookup_route(dst);
    if (r.entry) {
        printf("  匹配: %s (/%d)\n", ip_str(r.entry->dst), r.match_len);
        printf("  网关: %s\n", ip_str(r.entry->gw));
    }

    route_decision d = route_packet(0xC0A80101U, dst, false);
    printf("\n路由决策:\n");
    printf("  操作: %s\n", action_names[d.action]);
    printf("  匹配路由: %s\n", d.route ? d.route->comment : "无");
    printf("  下一跳（网关）: %s\n", ip_str(d.next_hop));
    printf("  出口接口: %s\n", d.route ? d.route->iface_name : "无");
    printf("\n→ 查表 → 命中默认路由 → 下一跳 %s\n", ip_str(d.next_hop));
    printf("→ 封装帧: ETH(本机MAC → 网关MAC) + IP(%s → %s)\n",
           ip_str(0xC0A80101U), ip_str(dst));
}

void demo_no_route(void) {
    printf("\n\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     场景 3：无路由 → 丢弃（ICMP 不可达）              ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    uint32_t dst = (192<<24)|(168<<16)|(2<<8)|99; // 192.168.2.99（未知网络）
    printf("\n目标 IP: %s\n", ip_str(dst));

    route_decision d = route_packet(0xC0A80101U, dst, false);
    printf("\n路由决策:\n");
    printf("  操作: %s\n", action_names[d.action]);
    printf("  理由: %s\n", d.reason);
    printf("\n→ 查表无匹配 → ICMP Destination Unreachable\n");
    printf("→ 路由表里没有 192.168.2.0/24 的路由，包被丢弃\n");
}

void demo_local_generated(void) {
    printf("\n\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     场景 4：本机生成包，查路由表决定出口                ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    uint32_t dst = (8<<24)|(8<<16)|(8<<8)|8; // 8.8.8.8
    uint32_t src = 0xC0A80101U;
    printf("\n本机: %s → 目标: %s\n", ip_str(src), ip_str(dst));

    route_decision d = route_packet(src, dst, true);
    printf("\n路由决策:\n");
    printf("  操作: %s\n", action_names[d.action]);
    printf("  匹配路由: %s\n", d.route ? d.route->comment : "无");
    printf("  出口接口: %s\n", d.route ? d.route->iface_name : "无");
    printf("  下一跳: %s\n", ip_str(d.next_hop));
    printf("\n→ 本机生成 → 查路由表 → 命中默认路由 → eth0 → %s\n",
           ip_str(d.next_hop));
}

void demo_multi_network(void) {
    printf("\n\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     场景 5：多网络环境 + 精确匹配优先                   ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    printf("\n本机路由表:\n");
    for (int i = 0; i < num_routes; i++) {
        route_entry *r = &kernel_routes[i];
        printf("  %s/%d via %s dev %s metric %d  (%s)\n",
               ip_str(r->dst), prefix_len(r->mask),
               r->gw ? ip_str(r->gw) : "直连",
               r->iface_name, r->metric, r->comment);
    }

    uint32_t targets[] = {
        (192<<24)|(168<<16)|(1<<8)|100,  // 192.168.1.100 — 直连
        (192<<24)|(168<<16)|(2<<8)|1,     // 192.168.2.1 — 默认路由
        (10<<24)|(5<<16)|(1<<8)|1,        // 10.5.1.1 — 10.0.0.0/8
        (142<<24)|(250<<16)|(50<<8)|10,   // 142.250.50.10 — 默认路由
    };
    const char *names[] = {
        "内网主机 192.168.1.100",
        "未知网络 192.168.2.1",
        "办公网络 10.5.1.1",
        "外网服务器 142.250.50.10（Google）",
    };

    printf("\n查表测试:\n");
    for (int i = 0; i < 4; i++) {
        route_result r = lookup_route(targets[i]);
        printf("\n  → %s\n", names[i]);
        if (r.entry) {
            printf("    匹配: %s/%d via %s\n",
                   ip_str(r.entry->dst), r.match_len,
                   r.entry->gw ? ip_str(r.entry->gw) : "直连");
            printf("    出口: %s\n", r.entry->iface_name);
        } else {
            printf("    无路由 → 丢弃\n");
        }
    }
}

void demo_real_ip_route(void) {
    printf("\n\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║     场景 6：真实 Linux 系统路由表                        ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    printf("\n在真实 Linux 系统上查看路由表:\n\n");
    printf("  $ ip route show\n");
    printf("  default via 192.168.1.1 dev eth0 proto dhcp ...\n");
    printf("  192.168.1.0/24 dev eth0 proto kernel scope link src 192.168.1.100\n");
    printf("  10.0.0.0/8 dev eth1 proto kernel scope link src 10.0.0.5\n\n");
    printf("  $ route -n\n");
    printf("  Destination     Gateway         Genmask         Flags Metric Ref  Use Iface\n");
    printf("  0.0.0.0          192.168.1.1    0.0.0.0         UG    100    0      0 eth0\n");
    printf("  192.168.1.0      0.0.0.0        255.255.255.0   U     0      0      0 eth0\n");
    printf("  10.0.0.0         0.0.0.0        255.0.0.0        U     0      0      0 eth1\n\n");
    printf("关键字段说明:\n");
    printf("  Destination: 目标网络（0.0.0.0 = 默认路由）\n");
    printf("  Gateway: 下一跳网关（0.0.0.0 = 直连）\n");
    printf("  Genmask: 子网掩码\n");
    printf("  Flags: U=启用, G=网关, H=主机\n");
    printf("  Metric: 路由优先级（越小越优先）\n\n");
    printf("最长前缀匹配原则:\n");
    printf("  访问 192.168.1.50 → 匹配 192.168.1.0/24（/24 > /0）\n");
    printf("  访问 10.1.2.3 → 匹配 10.0.0.0/8（/8 > /0）\n");
    printf("  访问 8.8.8.8 → 匹配默认路由（/0）\n");
}

// ============================================================
// 主程序
// ============================================================

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║      Linux 路由表机制演示 — 从零写 OS 内核系列       ║\n");
    printf("║      最长前缀匹配  ·  网关转发  ·  默认路由        ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    printf("\n本机路由表:\n");
    for (int i = 0; i < num_routes; i++) {
        route_entry *r = &kernel_routes[i];
        printf("  %s/%d via %s dev %s metric %d  (%s)\n",
               ip_str(r->dst), prefix_len(r->mask),
               r->gw ? ip_str(r->gw) : "直连",
               r->iface_name, r->metric, r->comment);
    }

    demo_local_delivery();
    demo_gateway_forward();
    demo_no_route();
    demo_local_generated();
    demo_multi_network();
    demo_real_ip_route();

    printf("\n\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║               路由表速查                              ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  查表算法: 最长前缀匹配 (Longest Prefix Match, LPM)   ║\n");
    printf("║  默认路由: 0.0.0.0/0 （匹配所有无特定路由的包）        ║\n");
    printf("║  直连网络: gw = 0.0.0.0 （同一网络，直接 ARP）        ║\n");
    printf("║  网关路由: gw = x.x.x.x （封装到网关 MAC，IP 不变）   ║\n");
    printf("║  查看路由: ip route / route -n / cat /proc/net/route ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  路由判决流程:                                         ║\n");
    printf("║    1. 查路由表（最长前缀匹配）                        ║\n");
    printf("║    2. gw=0 → 直连 → ARP 目标 IP → 封装 eth 发出       ║\n");
    printf("║    3. gw=x → 网关 → ARP 网关 IP → 封装 eth 发出       ║\n");
    printf("║    4. 无匹配 → ICMP Destination Unreachable           ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    printf("\n关注公众号「必收」 · star: github.com/golang12306/os-kernel-from-scratch\n");
    return 0;
}

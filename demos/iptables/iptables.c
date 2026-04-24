/*
 * iptables.c — iptables 规则表与链机制演示
 * 演示 filter/nat 表、5条内置链、规则匹配顺序、端口转发
 *
 * 编译：gcc -o iptables_demo iptables.c
 * 运行：./iptables_demo
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// ============================================================
// 网络协议头
// ============================================================

const char *ip_str(uint32_t ip) {
    static char buf[4][32];
    static int idx;
    idx = (idx + 1) % 4;
    snprintf(buf[idx], 32, "%d.%d.%d.%d",
             (ip >> 0) & 0xFF, (ip >> 8) & 0xFF,
             (ip >> 16) & 0xFF, (ip >> 24) & 0xFF);
    return buf[idx];
}

const char *proto_name(uint8_t p) {
    return p == 6 ? "TCP" : p == 17 ? "UDP" : p == 1 ? "ICMP" : "OTHER";
}

// ============================================================
// iptables 核心结构
// ============================================================

typedef enum { TABLE_FILTER, TABLE_NAT, TABLE_MANGLE, TABLE_RAW } iptables_table;
typedef enum { TARGET_ACCEPT, TARGET_DROP, TARGET_REJECT, TARGET_LOG,
               TARGET_DNAT, TARGET_SNAT, TARGET_MASQUERADE, TARGET_RETURN } target_type;

const char *target_names[] = {
    "ACCEPT", "DROP", "REJECT", "LOG", "DNAT", "SNAT", "MASQUERADE", "RETURN"
};

const char *chain_names[] = { "PREROUTING", "INPUT", "FORWARD", "OUTPUT", "POSTROUTING" };

typedef struct {
    uint8_t  protocol;          // 0 = any
    uint16_t dport_lo, dport_hi; // 0 = any
    uint16_t sport_lo, sport_hi;
    uint32_t src_ip, src_mask;   // 0 = any
    uint32_t dst_ip, dst_mask;
    bool     established;        // -m state --state ESTABLISHED,RELATED
    bool     new_conn;           // -m state --state NEW
} match_t;

bool match_packet(const match_t *m, uint8_t proto, uint16_t sport, uint16_t dport,
                 uint32_t sip, uint32_t dip, uint8_t tcp_flags, bool established) {
    if (m->protocol && m->protocol != proto) return false;
    if (m->dport_lo && (dport < m->dport_lo || dport > m->dport_hi)) return false;
    if (m->sport_lo && (sport < m->sport_lo || sport > m->sport_hi)) return false;
    if (m->src_ip && (sip & m->src_mask) != (m->src_ip & m->src_mask)) return false;
    if (m->dst_ip && (dip & m->dst_mask) != (m->dst_ip & m->dst_mask)) return false;
    if (m->established && !established) return false;
    if (m->new_conn && !(tcp_flags == 0x02)) return false; // SYN only
    return true;
}

typedef struct {
    target_type type;
    uint32_t nat_ip;
    uint16_t nat_port;
    char log_prefix[64];
} target_t;

typedef struct rule {
    match_t match;
    target_t target;
    const char *comment;
    struct rule *next;
} rule_t;

typedef struct chain {
    char name[32];
    iptables_table table;
    rule_t *rules;
    target_t default_policy;
    int packet_count;
    struct chain *next;
} chain_t;

typedef struct {
    uint8_t  protocol;
    uint16_t sport, dport;
    uint32_t sip, dip;
    uint8_t  tcp_flags;
    bool     established;
} pkt_t;

// ============================================================
// iptables 模拟器
// ============================================================

chain_t *filter_chains = NULL;
chain_t *nat_chains = NULL;

chain_t *find_chain(chain_t *c, const char *name) {
    while (c) {
        if (strcmp(c->name, name) == 0) return c;
        c = c->next;
    }
    return NULL;
}

void add_rule(chain_t *ch, match_t m, target_t t, const char *cmt) {
    rule_t *r = calloc(1, sizeof(rule_t));
    r->match = m;
    r->target = t;
    r->comment = cmt;
    r->next = ch->rules;
    ch->rules = r;
}

chain_t *make_chain(const char *name, iptables_table tbl, target_type def) {
    chain_t *c = calloc(1, sizeof(chain_t));
    strncpy(c->name, name, 31);
    c->table = tbl;
    c->default_policy.type = def;
    return c;
}

// 构建典型服务器防火墙规则
void build_iptables(void) {
    // ===== filter 表 =====
    chain_t *input = make_chain("INPUT", TABLE_FILTER, TARGET_DROP);
    chain_t *output = make_chain("OUTPUT", TABLE_FILTER, TARGET_ACCEPT);
    chain_t *forward = make_chain("FORWARD", TABLE_FILTER, TARGET_DROP);
    filter_chains = input;
    input->next = output;
    output->next = forward;

    // INPUT 链规则（顺序很重要！）
    // 注意：-i lo 条件在真实 iptables 中限制只匹配回环接口
    //      本 demo 简化为：任何包都可以匹配 lo，这里我们去掉这条规则，
    //      或者把 lo 改为只允许 127.0.0.1（由于没有模拟接口，移除此规则避免干扰）
    add_rule(input, (match_t){ .established = true }, (target_t){ TARGET_ACCEPT }, "允许已建立连接");
    add_rule(input, (match_t){ .protocol = 6, .dport_lo = 22, .dport_hi = 22, .new_conn = true },
             (target_t){ TARGET_ACCEPT }, "允许 SSH 新连接");
    add_rule(input, (match_t){ .protocol = 6, .dport_lo = 80, .dport_hi = 80 },
             (target_t){ TARGET_ACCEPT }, "允许 HTTP");
    add_rule(input, (match_t){ .protocol = 6, .dport_lo = 443, .dport_hi = 443 },
             (target_t){ TARGET_ACCEPT }, "允许 HTTPS");
    add_rule(input, (match_t){ .protocol = 1 },
             (target_t){ TARGET_ACCEPT }, "允许 ICMP");
    add_rule(input, (match_t){ .protocol = 6, .dport_lo = 23, .dport_hi = 23 },
             (target_t){ TARGET_LOG, .log_prefix = "FIREWALL TELNET " }, "LOG telnet");

    // FORWARD 链规则
    add_rule(forward, (match_t){ .established = true }, (target_t){ TARGET_ACCEPT }, "允许已建立转发");
    add_rule(forward, (match_t){ .protocol = 6, .dport_lo = 80, .dport_hi = 80 },
             (target_t){ TARGET_ACCEPT }, "允许转发 HTTP");
    add_rule(forward, (match_t){ .protocol = 6, .dport_lo = 443, .dport_hi = 443 },
             (target_t){ TARGET_ACCEPT }, "允许转发 HTTPS");

    // ===== nat 表 =====
    chain_t *prerouting = make_chain("PREROUTING", TABLE_NAT, TARGET_ACCEPT);
    chain_t *postrouting = make_chain("POSTROUTING", TABLE_NAT, TARGET_ACCEPT);
    chain_t *output_nat = make_chain("OUTPUT", TABLE_NAT, TARGET_ACCEPT);
    nat_chains = prerouting;
    prerouting->next = postrouting;
    postrouting->next = output_nat;

    // DNAT: 8080 → 80（端口转发）
    add_rule(prerouting, (match_t){ .protocol = 6, .dport_lo = 8080, .dport_hi = 8080 },
             (target_t){ TARGET_DNAT, .nat_port = 80 }, "DNAT 8080→80");
    add_rule(output_nat, (match_t){ .protocol = 6, .dport_lo = 8080, .dport_hi = 8080 },
             (target_t){ TARGET_DNAT, .nat_port = 80 }, "本地 DNAT 8080→80");

    // SNAT/MASQUERADE: 内网主机外出
    uint32_t lan_net = (192<<24)|(168<<16)|(1<<8);
    add_rule(postrouting, (match_t){ .src_ip = lan_net, .src_mask = 0xFFFFFF00U },
             (target_t){ TARGET_MASQUERADE }, "内网 MASQUERADE");
}

// 遍历一条链，返回最终判决
target_type eval_chain(chain_t *ch, pkt_t *p) {
    printf("    链 [%s]:\n", ch->name);
    for (rule_t *r = ch->rules; r; r = r->next) {
        if (match_packet(&r->match, p->protocol, p->sport, p->dport,
                         p->sip, p->dip, p->tcp_flags, p->established)) {
            printf("      ★ %s\n", r->comment);
            printf("      → %s\n", target_names[r->target.type]);
            ch->packet_count++;
            if (r->target.type == TARGET_LOG) {
                printf("      [LOG: %s]\n", r->target.log_prefix);
                continue; // LOG 不终止，继续匹配
            }
            return r->target.type;
        }
    }
    printf("    无匹配 → 默认 [%s]\n", target_names[ch->default_policy.type]);
    return ch->default_policy.type;
}

// 处理完整数据包流程
void process(pkt_t *p, int hook) {
    printf("\n包: %s:%d → %s:%d [%s] flags=0x%02x\n",
           ip_str(p->sip), p->sport, ip_str(p->dip), p->dport,
           proto_name(p->protocol), p->tcp_flags);
    printf("  HOOK: %d (%s)\n", hook, chain_names[hook < 5 ? hook : 0]);

    // 判断本机/转发/本机生成
    bool to_local   = ((p->dip >> 0) & 0xFF) > 100;
    bool from_local = ((p->sip >> 0) & 0xFF) > 100;

    // NAT PREROUTING (DNAT)
    if (hook == 0) {
        printf("\n[NAT PREROUTING — DNAT]:\n");
        chain_t *pr = find_chain(nat_chains, "PREROUTING");
        if (pr) {
            for (rule_t *r = pr->rules; r; r = r->next) {
                if (r->target.type == TARGET_DNAT &&
                    match_packet(&r->match, p->protocol, p->sport, p->dport,
                                 p->sip, p->dip, p->tcp_flags, p->established)) {
                    printf("  ★ DNAT: 端口 %d → %d\n", p->dport, r->target.nat_port);
                    p->dport = r->target.nat_port;
                    break;
                }
            }
        }
    }

    // 路由判决：根据 HOOK 点判断
    const char *path;
    target_type result;
    if (hook == 1) {
        path = "INPUT (本机接收)";
        result = eval_chain(find_chain(filter_chains, "INPUT"), p);
    } else if (hook == 3) {
        path = "OUTPUT (本机生成)";
        result = eval_chain(find_chain(filter_chains, "OUTPUT"), p);
    } else {
        path = to_local ? "INPUT (本机接收)" : (from_local ? "OUTPUT (本机生成)" : "FORWARD (转发)");
        if (to_local) {
            result = eval_chain(find_chain(filter_chains, "INPUT"), p);
        } else if (from_local) {
            result = eval_chain(find_chain(filter_chains, "OUTPUT"), p);
        } else {
            result = eval_chain(find_chain(filter_chains, "FORWARD"), p);
        }
    }
    printf("\n[路由判决]: %s (%s)\n", to_local ? "目标本机" : (from_local ? "本机生成" : "转发/路由"), path);

    // NAT POSTROUTING (SNAT/MASQUERADE)
    if (hook == 4 || hook == 3) {
        printf("\n[NAT POSTROUTING — SNAT]:\n");
        chain_t *po = find_chain(nat_chains, "POSTROUTING");
        if (po) {
            for (rule_t *r = po->rules; r; r = r->next) {
                if (r->target.type == TARGET_MASQUERADE &&
                    match_packet(&r->match, p->protocol, p->sport, p->dport,
                                 p->sip, p->dip, p->tcp_flags, p->established)) {
                    printf("  ★ MASQUERADE: 源IP → 公网IP\n");
                    break;
                }
            }
        }
    }

    printf("\n最终: %s\n", result == TARGET_ACCEPT ? "✅ ACCEPT" : "❌ DROP/REJECT");
}

// ============================================================
// 主程序
// ============================================================

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║   iptables 规则表与链机制演示 — 从零写 OS 内核系列      ║\n");
    printf("║   filter/nat 表  ·  5条内置链  ·  规则匹配顺序         ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");

    build_iptables();

    uint32_t local_ip  = (192<<24)|(168<<16)|(1<<8)|100;
    uint32_t remote_ip = (192<<24)|(168<<16)|(1<<8)|50;
    uint32_t dns_ip    = (8<<24)|(8<<16)|(8<<8)|8;
    uint32_t lan_ip    = (192<<24)|(168<<16)|(1<<8)|50;

    printf("\n【场景 1】外部访问本机 HTTP（80端口）\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    process(&(pkt_t){6, 12345, 80, remote_ip, local_ip, 0x02, false}, 1);

    printf("\n【场景 2】外部 ping 本机（ICMP）\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    process(&(pkt_t){1, 0, 0, remote_ip, local_ip, 0, false}, 1);

    printf("\n【场景 3】外部 telnet 入侵尝试（23端口）\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    process(&(pkt_t){6, 20000, 23, remote_ip, local_ip, 0x02, false}, 1);

    printf("\n【场景 4】外部访问 8080（NAT 端口转发 8080→80）\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("(经过 PREROUTING DNAT 转换后，命中 HTTP 规则)\n");
    process(&(pkt_t){6, 30000, 8080, remote_ip, local_ip, 0x02, false}, 0);

    printf("\n【场景 5】本机访问外网 DNS（UDP 53）\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    process(&(pkt_t){17, 53, 53, local_ip, dns_ip, 0, false}, 3);

    printf("\n【场景 6】内网主机通过家用路由访问外网（HTTPS/NAT）\n");
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("(家用路由: 内网 192.168.1.0/24 → 公网 NAT)\n");
    process(&(pkt_t){6, 40000, 443, lan_ip, dns_ip, 0x02, false}, 2);

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║           iptables 表与链速查                           ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  表（table）: filter（默认）/ nat / mangle / raw        ║\n");
    printf("║  内置链: INPUT OUTPUT FORWARD PREROUTING POSTROUTING     ║\n");
    printf("║  匹配: -p 协议 --dport 端口 -s/-d IP -m state --state   ║\n");
    printf("║  目标: ACCEPT DROP REJECT LOG DNAT SNAT MASQUERADE      ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  HOOK→链: NF_PRE_ROUTING→PREROUTING(nat)                ║\n");
    printf("║           NF_LOCAL_IN→INPUT(filter)                     ║\n");
    printf("║           NF_FORWARD→FORWARD(filter)                    ║\n");
    printf("║           NF_LOCAL_OUT→OUTPUT(filter+nat)               ║\n");
    printf("║           NF_POST_ROUTING→POSTROUTING(nat)              ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n关注公众号「必收」 · star: github.com/golang12306/os-kernel-from-scratch\n");
    return 0;
}

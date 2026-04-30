/*
 * TCP 拥塞控制状态机演示
 *
 * 演示慢启动（Slow Start）和拥塞避免（Congestion Avoidance）的工作原理。
 * 不涉及实际网络通信，纯状态机模拟。
 *
 * cwnd 单位说明：
 *   - 慢启动（SS）：cwnd 以 MSS 计数（整数）
 *   - 拥塞避免（CA）：cwnd 以字节计（累积到 >=1 MSS 时才加 1）
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define MSS 1460          /* Maximum Segment Size，单位：字节 */
#define INIT_CWND_MSS 2   /* 初始拥塞窗口：2 MSS */
#define INIT_SSTHRESH 64  /* 初始慢启动阈值：64 MSS（单位 MSS） */

typedef enum {
    SS,   /* Slow Start：每 ACK +1 MSS，指数增长 */
    CA    /* Congestion Avoidance：每 RTT +1 MSS，线性增长 */
} tcp_state_t;

static const char *state_name[] = { "Slow Start", "Congestion Avoid." };

typedef struct {
    uint32_t cwnd_bytes;     /* 拥塞窗口，单位：字节 */
    uint32_t ssthresh_bytes; /* 慢启动阈值，单位：字节 */
    tcp_state_t state;
    uint32_t dupack_count;
    uint32_t last_cwnd_mss;  /* 上次输出的 cwnd（MSS 为单位），用于检测切换 */
} congestion_control_t;

/* 初始化 */
static void cc_init(congestion_control_t *cc) {
    cc->cwnd_bytes     = INIT_CWND_MSS * MSS;
    cc->ssthresh_bytes = INIT_SSTHRESH * MSS;
    cc->state   = SS;
    cc->dupack_count = 0;
    cc->last_cwnd_mss = INIT_CWND_MSS;
}

/* 每次 ACK 时调用，更新 cwnd（SS） */
static void cc_on_ack_ss(congestion_control_t *cc) {
    /* 慢启动：每收到一个有效 ACK，cwnd += 1 MSS */
    cc->cwnd_bytes += MSS;
}

/* 每次 ACK 时调用，更新 cwnd（CA）
 * CA 阶段每 RTT 线性增加 1 MSS，这里每次 ACK 增量 = MSS * MSS / cwnd_bytes
 * （等价于 cwnd_bytes 个 ACK 累积 1 MSS） */
static void cc_on_ack_ca(congestion_control_t *cc) {
    cc->cwnd_bytes += (MSS * MSS) / cc->cwnd_bytes;
}

/* 判断是否进入拥塞避免（SS → CA） */
static void cc_check_threshold(congestion_control_t *cc) {
    if (cc->state == SS && cc->cwnd_bytes >= cc->ssthresh_bytes) {
        cc->state = CA;
        printf("[状态切换] SS → CA  (cwnd=%.0f MSS, ssthresh=%.0f MSS)\n",
               (double)cc->cwnd_bytes / MSS,
               (double)cc->ssthresh_bytes / MSS);
    }
}

/* 收到 ACK */
static void cc_on_ack(congestion_control_t *cc) {
    if (cc->state == SS) {
        cc_on_ack_ss(cc);
        cc_check_threshold(cc);
    } else {
        cc_on_ack_ca(cc);
    }
}

/* 收到重复 ACK */
static void cc_on_dupack(congestion_control_t *cc) {
    cc->dupack_count++;
    if (cc->dupack_count == 3) {
        /* 3 个重复 ACK：ssthresh = cwnd / 2，cwnd = ssthresh + 3 MSS */
        uint32_t cwnd_mss = (cc->cwnd_bytes + MSS - 1) / MSS;
        uint32_t new_ssthresh = cwnd_mss / 2;
        cc->ssthresh_bytes = new_ssthresh * MSS;
        cc->cwnd_bytes = cc->ssthresh_bytes + 3 * MSS;
        cc->state = CA;
        cc->dupack_count = 0;
        printf("[快速重传] dupack=3  →  ssthresh=%.0f MSS, cwnd=%.0f MSS\n",
               (double)cc->ssthresh_bytes / MSS,
               (double)cc->cwnd_bytes / MSS);
    }
}

/* RTO 超时 */
static void cc_on_timeout(congestion_control_t *cc) {
    uint32_t cwnd_mss = (cc->cwnd_bytes + MSS - 1) / MSS;
    cc->ssthresh_bytes = (cwnd_mss / 2) * MSS;
    cc->cwnd_bytes = MSS;   /* 最严厉惩罚 */
    cc->state = SS;
    cc->dupack_count = 0;
    printf("[RTO超时]  →  ssthresh=%.0f MSS, cwnd=1 MSS  (重新进入 SS)\n",
           (double)cc->ssthresh_bytes / MSS);
}

/* 打印当前 cwnd 状态（只在新 cwnd >= 1 MSS 整数变化时打印） */
static void cc_print(congestion_control_t *cc, const char *event) {
    uint32_t cwnd_mss = (cc->cwnd_bytes + MSS - 1) / MSS;
    printf("  %-24s  cwnd=%5.1f MSS  ssthresh=%5.1f MSS  [%s]\n",
           event,
           (double)cc->cwnd_bytes / MSS,
           (double)cc->ssthresh_bytes / MSS,
           state_name[cc->state]);
}

/* 模拟慢启动阶段（指数增长） */
static void simulate_slow_start(congestion_control_t *cc) {
    printf("\n=== 阶段 1：慢启动（指数增长） ===\n");

    int round = 0;
    while (cc->state == SS && round < 10) {
        round++;
        uint32_t acks_needed = (cc->cwnd_bytes + MSS - 1) / MSS;
        printf("  第 %2d 轮（需要 %3u 个 ACK）:\n", round, acks_needed);
        for (uint32_t i = 0; i < acks_needed; i++) {
            cc_on_ack(cc);
        }
        cc_print(cc, "→ cwnd 更新后");
    }
    if (cc->state == SS) {
        printf("  （10 轮后仍在 SS，超出演示范围）\n");
    }
}

/* 模拟拥塞避免（指定 RTT 数） */
static void simulate_congestion_avoidance(congestion_control_t *cc, int rtt_count) {
    printf("\n=== 阶段 2：拥塞避免（线性增长） ===\n");
    for (int i = 0; i < rtt_count; i++) {
        /* 一个 RTT：收到 cwnd_bytes / MSS 个 ACK */
        uint32_t acks_per_rtt = (cc->cwnd_bytes + MSS - 1) / MSS;
        printf("  RTT %2d（%u 个 ACK）:\n", i + 1, acks_per_rtt);
        for (uint32_t j = 0; j < acks_per_rtt; j++) {
            cc_on_ack(cc);
        }
        cc_print(cc, "→ cwnd 更新后");
    }
}

/* 模拟 3-ACK 快速重传 */
static void simulate_fast_retransmit(congestion_control_t *cc) {
    printf("\n=== 事件：收到 3 个重复 ACK（模拟丢包） ===\n");
    /* 发送 3 个 dupack，触发快速重传 */
    cc_on_dupack(cc);
    cc_on_dupack(cc);
    cc_on_dupack(cc);
}

/* 模拟 RTO 超时 */
static void simulate_rto_timeout(congestion_control_t *cc) {
    printf("\n=== 事件：RTO 超时（最严重丢包） ===\n");
    cc_on_timeout(cc);
}

int main(void) {
    printf("============================================\n");
    printf("  TCP 拥塞控制状态机演示\n");
    printf("  MSS=%u 字节, INIT_CWND=%u MSS, INIT_SSTHRESH=%u MSS\n",
           MSS, INIT_CWND_MSS, INIT_SSTHRESH);
    printf("============================================\n");

    congestion_control_t cc;
    cc_init(&cc);

    /* 阶段 1：慢启动 */
    simulate_slow_start(&cc);

    /* 阶段 2：拥塞避免，5 个 RTT */
    simulate_congestion_avoidance(&cc, 5);

    /* 事件：3-ACK 快速重传 */
    simulate_fast_retransmit(&cc);

    /* 继续 CA 3 个 RTT */
    simulate_congestion_avoidance(&cc, 3);

    /* 事件：RTO 超时 */
    simulate_rto_timeout(&cc);

    /* 重新 SS */
    simulate_slow_start(&cc);

    printf("\n============================================\n");
    printf("  演示结束\n");
    printf("  关键观察：\n");
    printf("  SS  阶段：cwnd 指数增长（每 ACK +1 MSS）\n");
    printf("  CA  阶段：cwnd 线性增长（每 RTT +1 MSS）\n");
    printf("  3ACK：ssthresh=cwnd/2, cwnd=ssthresh+3，直接进 CA\n");
    printf("  RTO ：cwnd=1 MSS，重新 SS（最严厉惩罚）\n");
    printf("============================================\n");
    return 0;
}

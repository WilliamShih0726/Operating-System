#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <getopt.h>
#include <errno.h>
#include <unistd.h>
#include <sched.h>
#include <time.h>

typedef enum { POL_NORMAL = 0, POL_FIFO = 1 } pol_t;

typedef struct {
    int id;
    double busy_sec;
    pol_t policy;
    int rt_prio;  // -1 for NORMAL
} thread_arg_t;

static int g_num_threads = 0;
static thread_arg_t *g_args = NULL;
static pthread_t *g_tids = NULL;
static pthread_barrier_t g_barrier;

static int parse_csv_policies(const char *s, pol_t *out, int n) {
    char *dup = strdup(s);
    if (!dup) return -1;
    int i = 0;
    char *tok, *save = NULL;
    for (tok = strtok_r(dup, ",", &save); tok && i < n; tok = strtok_r(NULL, ",", &save), ++i) {
        if (strcasecmp(tok, "NORMAL") == 0 || strcasecmp(tok, "SCHED_NORMAL") == 0 || strcasecmp(tok, "OTHER") == 0)
            out[i] = POL_NORMAL;
        else if (strcasecmp(tok, "FIFO") == 0 || strcasecmp(tok, "SCHED_FIFO") == 0)
            out[i] = POL_FIFO;
        else { free(dup); return -2; }
    }
    free(dup);
    return (i == n) ? 0 : -3;
}

static int parse_csv_ints(const char *s, int *out, int n) {
    char *dup = strdup(s);
    if (!dup) return -1;
    int i = 0;
    char *tok, *save = NULL;
    for (tok = strtok_r(dup, ",", &save); tok && i < n; tok = strtok_r(NULL, ",", &save), ++i) {
        out[i] = atoi(tok);
    }
    free(dup);
    return (i == n) ? 0 : -2;
}

static void busy_wait_seconds(double secs) {
    // Count per-thread CPU time (excludes preempted time).
    struct timespec t0, t1;
    double acc = 0.0;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t0);
    while (acc < secs) {
        for (volatile int i = 0; i < 1000; ++i) { }
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t1);
        acc = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    }
}

static void *worker(void *argp) {
    thread_arg_t *arg = (thread_arg_t *)argp;
    // 1) 同步同時開跑
    pthread_barrier_wait(&g_barrier);
    // 2) 三回合：列印 → 忙等
    for (int i = 0; i < 3; ++i) {
        printf("Thread %d is running\n", arg->id);
        fflush(stdout);
        busy_wait_seconds(arg->busy_sec);
    }
    return NULL;
}

static void pin_self_cpu0() {
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    sched_setaffinity(0, sizeof(set), &set);
}

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s -n <num_threads> -t <time_wait> -s <policies> -p <priorities>\n"
        "  policies   : CSV of NORMAL/FIFO (len = n)\n"
        "  priorities : CSV of ints (len = n), use -1 for NORMAL\n", prog);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0); // line-buffer for QEMU

    int opt, n = -1;
    double t = -1.0;
    const char *s_pol = NULL, *s_pri = NULL;

    while ((opt = getopt(argc, argv, "n:t:s:p:")) != -1) {
        switch (opt) {
            case 'n': n = atoi(optarg); break;
            case 't': t = atof(optarg); break;
            case 's': s_pol = optarg; break;
            case 'p': s_pri = optarg; break;
            default: usage(argv[0]); return 1;
        }
    }
    if (n <= 0 || t <= 0.0 || !s_pol || !s_pri) { usage(argv[0]); return 1; }

    g_num_threads = n;
    g_args = calloc(n, sizeof(*g_args));
    g_tids = calloc(n, sizeof(*g_tids));
    pol_t *pols = calloc(n, sizeof(*pols));
    int *pris = calloc(n, sizeof(*pris));
    if (!g_args || !g_tids || !pols || !pris) { perror("alloc"); return 1; }

    if (parse_csv_policies(s_pol, pols, n) != 0) { fprintf(stderr, "Bad -s\n"); return 1; }
    if (parse_csv_ints(s_pri, pris, n) != 0) { fprintf(stderr, "Bad -p\n"); return 1; }

    pthread_attr_t *attrs = calloc(n, sizeof(*attrs));
    if (!attrs) { perror("alloc attrs"); return 1; }

    // 主執行緒綁 CPU0；所有 worker 也會設 affinity 到 CPU0
    pin_self_cpu0();

    // 同步點：n 個 worker
    pthread_barrier_init(&g_barrier, NULL, n);

    for (int i = 0; i < n; ++i) {
        g_args[i].id = i;
        g_args[i].busy_sec = t;
        g_args[i].policy = pols[i];
        g_args[i].rt_prio = pris[i];

        pthread_attr_init(&attrs[i]);
        // 用顯式排程參數（不繼承）
        pthread_attr_setinheritsched(&attrs[i], PTHREAD_EXPLICIT_SCHED);

        int policy = (pols[i] == POL_FIFO) ? SCHED_FIFO : SCHED_OTHER;
        pthread_attr_setschedpolicy(&attrs[i], policy);

        struct sched_param sp = {0};
        if (policy == SCHED_FIFO) {
            sp.sched_priority = (g_args[i].rt_prio < 1) ? 1 : g_args[i].rt_prio; // 1..99
        } else {
            sp.sched_priority = 0;
        }
        pthread_attr_setschedparam(&attrs[i], &sp);

        if (pthread_create(&g_tids[i], &attrs[i], worker, &g_args[i]) != 0) {
            perror("pthread_create"); return 1;
        }

        // 每個 worker 綁到 CPU0
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(0, &set);
        if (pthread_setaffinity_np(g_tids[i], sizeof(set), &set) != 0) {
            perror("pthread_setaffinity_np"); return 1;
        }
    }

    for (int i = 0; i < n; ++i) {
        pthread_join(g_tids[i], NULL);
        pthread_attr_destroy(&attrs[i]);
    }
    pthread_barrier_destroy(&g_barrier);

    free(attrs);
    free(pols);
    free(pris);
    free(g_args);
    free(g_tids);
    return 0;
}

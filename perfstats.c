#ifdef HAVE_LINUX_PERF_EVENT_H
#ifdef __cplusplus
extern "C" {
#endif

#include "perfstats.h"

#include <linux/perf_event.h>
#include <linux/unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdio.h>
#include <sched.h>
#include <string.h>

int cpu = 0;
struct timeval time_start, time_end;
double elapsed_time;

enum {
    COUNTER_CPU_CYCLES,
    INSTRUCTIONS,
    L1D_ACCESSES,      // Native AMD Zen 3 Data Cache Accesses
    L1D_MISSES,        // Native AMD Zen 3 Demand Data Cache Refills
    LLC_MISSES,
};

static struct perf_event_attr attrs[] = {
    // 0: Cycles (Group Leader)
    { .type = PERF_TYPE_HARDWARE, .config = PERF_COUNT_HW_CPU_CYCLES },

    // 1: Instructions
    { .type = PERF_TYPE_HARDWARE, .config = PERF_COUNT_HW_INSTRUCTIONS },

    // 2: AMD Zen 3 DC Accesses (Event 0x40 - All Data Cache accesses by demand ops)
    { .type = PERF_TYPE_RAW,      .config = 0x0040 },

    // 3: AMD Zen 3 DC Misses (Event 0x44, Unit Mask 0x01 - Demand fills)
    { .type = PERF_TYPE_RAW,      .config = 0x0144 },

    // 4: Last Level Cache (LLC) Misses
    { .type = PERF_TYPE_HARDWARE, .config = PERF_COUNT_HW_CACHE_MISSES },
};

#define STAT_COUNT (sizeof(attrs) / sizeof(*attrs))
unsigned long long performance_counters[STAT_COUNT];
static int fds[STAT_COUNT];

static inline int sys_perf_event_open(struct perf_event_attr *attr,
                                     pid_t pid, int cpu, int group_fd,
                                     unsigned long flags) {
    attr->size = sizeof(*attr);
    return syscall(__NR_perf_event_open, attr, pid, cpu, group_fd, flags);
}

unsigned long long* flush_caches(void) {
    size_t num_elements = 16777216; // 128 MB buffer to blow out 16MB L3 cache
    unsigned long long* garbage = (unsigned long long*)malloc(sizeof(unsigned long long) * num_elements);
    if (!garbage) return NULL;
    for (size_t i = 0; i < num_elements; i++) {
        garbage[i] = 1;
    }
    return garbage;
}

int restore_cpufrequnecy(void) {
    char cmdline[1024];
    sprintf(cmdline, "/usr/sbin/changefreq 0 422334 4673823");
    return system(cmdline);
}

int change_cpufrequnecy(int MHz) {
    int KHz = MHz * 1000;
    char cmdline[1024];
    cpu = sched_getcpu();
    sprintf(cmdline, "/usr/sbin/changefreq 0 %d %d", KHz, KHz);
    return system(cmdline);
}

void perfstats_init(void) {
    int pid = 0; // Measure calling thread
    int group_fd = -1;

    for (size_t i = 0; i < STAT_COUNT; i++) {
        attrs[i].size = sizeof(struct perf_event_attr);
        attrs[i].inherit = 0;
        attrs[i].disabled = (i == 0) ? 1 : 0; // Leader is disabled initially; siblings inherit state
        attrs[i].exclude_kernel = 1; // Do NOT count kernel / page fault overhead
        attrs[i].exclude_hv = 1;     // Exclude hypervisor
        attrs[i].enable_on_exec = 0;
        attrs[i].pinned = (i == 0) ? 1 : 0;   // Pin group leader to prevent multiplexing

        // Event grouping: fds[0] acts as the group leader to keep all counters synchronized
        fds[i] = sys_perf_event_open(&attrs[i], pid, -1, group_fd, 0);
        if (fds[i] < 0) {
            fprintf(stderr, "Warning: Failed to open perf event %zu (config: 0x%llx)\n", 
                    i, (unsigned long long)attrs[i].config);
        } else if (i == 0) {
            group_fd = fds[0];
        }
    }
}

void perfstats_deinit(void) {
    for (size_t i = 0; i < STAT_COUNT; i++) {
        if (fds[i] > 0) {
            close(fds[i]);
            fds[i] = -1;
        }
    }
}

void perfstats_reset(void) {
    if (fds[0] > 0) {
        // Resetting the leader resets all counters in the group atomically
        ioctl(fds[0], PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
    }
}

void perfstats_enable(void) {
    perfstats_reset();
    gettimeofday(&time_start, NULL);
    if (fds[0] > 0) {
        // Enabling the leader enables all counters in the group synchronously
        ioctl(fds[0], PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);
    }
}

void perfstats_disable(void) {
    if (fds[0] > 0) {
        // Disabling the leader stops all counters in the group synchronously
        ioctl(fds[0], PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);
    }
    gettimeofday(&time_end, NULL);

    elapsed_time = ((time_end.tv_sec * 1000000.0 + time_end.tv_usec) -
                    (time_start.tv_sec * 1000000.0 + time_start.tv_usec)) / 1000000.0;
}

static void readall(void) {
    for (size_t i = 0; i < STAT_COUNT; i++) {
        if (fds[i] > 0) {
            unsigned long long val = 0;
            if (read(fds[i], &val, sizeof(val)) > 0) {
                performance_counters[i] = val;
            }
        }
    }
}

void perfstats_print_header(char *filename, char *header) {
    FILE *fout = fopen(filename, "w");
    if (fout) {
        fprintf(fout, "%s\n", header);
        fclose(fout);
    }
}

void perfstats_print(char *preamble, char *filename, char *epilogue) {
    FILE *fout = fopen(filename, "a");
    if (!fout) return;

    readall();

    unsigned long long ic         = performance_counters[INSTRUCTIONS];
    unsigned long long cycles     = performance_counters[COUNTER_CPU_CYCLES];
    unsigned long long accesses   = performance_counters[L1D_ACCESSES];
    unsigned long long misses     = performance_counters[L1D_MISSES];

    double cpi = (ic > 0) ? ((double)cycles / (double)ic) : 0.0;
    double ct  = (cycles > 0) ? (elapsed_time / (double)cycles) : 0.0;

    // Both numerator and denominator now track demand data cache operations directly
    double miss_rate = (accesses > 0) ? ((double)misses / (double)accesses) : 0.0;
    if (miss_rate > 1.0) {
        miss_rate = 1.0;
    }

    fprintf(fout, "%s%llu,%llu,%lf,%lf,%lf,%lf,%llu,%llu%s",
            preamble,
            ic,
            cycles,
            cpi,
            ct,
            elapsed_time,
            miss_rate,
            misses,
            accesses,
            epilogue);

    fclose(fout);
}

#ifdef __cplusplus
}
#endif
#endif
#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif //_GNU_SOURCE

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <execinfo.h>
#include <mutex>

#define TRACE_COUNT         (2048)
#define TRACE_LAYER         (8)

#define DBG(fmt, ...)       fprintf(stderr, "[alloc hook] " fmt, ##__VA_ARGS__)

static void add_trace(void *ptr, size_t size);
static void del_trace(void *ptr);

extern "C" {

void *__libc_malloc(size_t size);
void __libc_free(void *ptr);
void *__libc_calloc(size_t nmemb, size_t size);
void *__libc_realloc(void *ptr, size_t size);

void *malloc(size_t size)
{
    void *ptr = __libc_malloc(size);
    add_trace(ptr, size);
    return ptr;
}

void free(void *ptr)
{
    if (ptr == nullptr)
        return;
    __libc_free(ptr);
    del_trace(ptr);
}

void *calloc(size_t nmemb, size_t size)
{
    void *ptr = __libc_calloc(nmemb, size);
    add_trace(ptr, nmemb*size);
    return ptr;
}

void *realloc(void *ptr, size_t size)
{
    void *new_ptr = __libc_realloc(ptr, size);
    del_trace(ptr);
    add_trace(new_ptr, size);
    return new_ptr;
}

} //extern "C"

static int alloc_block = 0;

typedef struct {
    bool use;
    int call_times;
    int layer;
    void *call_traces[TRACE_LAYER];
}trace_item;

static std::mutex trace_mutex;
static bool trace_flag = false;
static trace_item trace_alloc[TRACE_COUNT] = {0};
static trace_item trace_free[TRACE_COUNT] = {0};

static bool is_traces_equal(void **t1, void **t2, int size)
{
    for (int i=0; i<size; i++) {
        if (t1[i] != t2[i])
            return false;
    }
    return true;
}

static void add_trace(void *ptr, size_t size)
{
    if (ptr == nullptr || size <= 0)
        return;
    std::lock_guard<std::mutex> lock(trace_mutex);
    if (!trace_flag)
        return;
    alloc_block++;
    trace_flag = false;
    void *call_traces[TRACE_LAYER] = {0};
    int layer = backtrace(call_traces, TRACE_LAYER);
    trace_flag = true;
    for (int i=0; i<TRACE_COUNT; i++) {
        if (trace_alloc[i].use) {
            if (is_traces_equal(call_traces, trace_alloc[i].call_traces, layer)) {
                trace_alloc[i].call_times++;
                return;
            }
        }
    }
    for (int i=0; i<TRACE_COUNT; i++) {
        if (!trace_alloc[i].use) {
            trace_alloc[i].use = true;
            trace_alloc[i].call_times++;
            trace_alloc[i].layer = layer;
            for (int j=0; j<layer; j++)
                trace_alloc[i].call_traces[j] = call_traces[j];
            return;
        }
    }
    DBG("trace alloc count %d not enough\n", TRACE_COUNT);
}

static void del_trace(void *ptr)
{
    if (ptr == nullptr)
        return;
    std::lock_guard<std::mutex> lock(trace_mutex);
    if (!trace_flag)
        return;
    alloc_block--;
    trace_flag = false;
    void *call_traces[TRACE_LAYER] = {0};
    int layer = backtrace(call_traces, TRACE_LAYER);
    trace_flag = true;
    for (int i=0; i<TRACE_COUNT; i++) {
        if (trace_free[i].use) {
            if (is_traces_equal(call_traces, trace_free[i].call_traces, layer)) {
                trace_free[i].call_times++;
                return;
            }
        }
    }
    for (int i=0; i<TRACE_COUNT; i++) {
        if (!trace_free[i].use) {
            trace_free[i].use = true;
            trace_free[i].call_times++;
            trace_free[i].layer = layer;
            for (int j=0; j<layer; j++)
                trace_free[i].call_traces[j] = call_traces[j];
            return;
        }
    }
    DBG("trace free count %d not enough\n", TRACE_COUNT);
}

class AllocStatistic {
public:
    AllocStatistic() {
        std::lock_guard<std::mutex> lock(trace_mutex);
        trace_flag = true;
    }

    ~AllocStatistic() {
        {
            std::lock_guard<std::mutex> lock(trace_mutex);
            trace_flag = false;
        }
        char cmd_line[128] = {0};
        int fd = open("/proc/self/cmdline", O_RDONLY);
        if (fd >= 0) {
            int size = read(fd, cmd_line, sizeof(cmd_line));
            for (int i=0; i<size-1; i++)
                if (cmd_line[i] == 0)
                    cmd_line[i] = ' ';
            close(fd);
        }
        DBG("alloc statistic [%s] ok\n", cmd_line);
        DBG("alloc buf not free block: %d\n", alloc_block);
        DBG("---- alloc info ----\n");
        for (int i=0; i<TRACE_COUNT; i++) {
            if (trace_alloc[i].use) {
                DBG("\tcall times: %d\n", trace_alloc[i].call_times);
                char **symbols = backtrace_symbols(trace_alloc[i].call_traces, trace_alloc[i].layer);
                for (int j=0; j<trace_alloc[i].layer; j++) {
                    DBG("\t\t%s\n", symbols[j]);
                }
            }
        }
        DBG("---- free info ----\n");
        for (int i=0; i<TRACE_COUNT; i++) {
            if (trace_free[i].use) {
                DBG("\tcall times: %d\n", trace_free[i].call_times);
                char **symbols = backtrace_symbols(trace_free[i].call_traces, trace_free[i].layer);
                for (int j=0; j<trace_free[i].layer; j++) {
                    DBG("\t\t%s\n", symbols[j]);
                }
            }
        }
    }
};

static AllocStatistic alloc_statistic;


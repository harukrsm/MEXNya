#include "engine/core/json.h"
#include "engine/platform/platform.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            s_failures++; \
        } \
    } while (0)

static void fill(unsigned char *p, size_t n, unsigned char seed)
{
    for (size_t i = 0; i < n; i++) p[i] = (unsigned char)(seed + (unsigned char)(i * 7u));
}

static int matches(const unsigned char *p, size_t n, unsigned char seed)
{
    for (size_t i = 0; i < n; i++)
        if (p[i] != (unsigned char)(seed + (unsigned char)(i * 7u))) return 0;
    return 1;
}

#if defined(_WIN32)

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    printf("atomic_test: skipped on Windows\n");
    return 0;
}

#else

#include <pthread.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    const char *path;
    unsigned char seed;
    int size;
    int times;
    volatile int *bad;
} Writer;

static void *write_thread(void *arg)
{
    Writer *w = (Writer *)arg;
    unsigned char *buf = (unsigned char *)malloc((size_t)w->size);
    if (!buf) return NULL;
    fill(buf, (size_t)w->size, w->seed);
    for (int i = 0; i < w->times; i++) {
        if (plat_write_file_atomic(w->path, buf, w->size) != 0) *w->bad = 0;
    }
    free(buf);
    return NULL;
}

typedef struct {
    const char *path;
    int size;
    unsigned char a, b;
    int times;
    volatile int *bad;
} ReaderArgs;

static void *read_thread(void *arg)
{
    ReaderArgs *r = (ReaderArgs *)arg;
    for (int i = 0; i < r->times; i++) {
        int size = 0;
        unsigned char *data = (unsigned char *)plat_read_file(r->path, &size, 0);
        if (!data) continue;
        if (size != r->size || (!matches(data, (size_t)size, r->a) && !matches(data, (size_t)size, r->b)))
            *r->bad = 0;
        free(data);
    }
    return NULL;
}

static void test_overwrite(const char *path)
{
    unsigned char a[4096], b[4096];
    fill(a, sizeof(a), 0x21);
    fill(b, sizeof(b), 0x63);
    CHECK(plat_write_file_atomic(path, a, (int)sizeof(a)) == 0);
    CHECK(plat_write_file_atomic(path, b, (int)sizeof(b)) == 0);
    int size = 0;
    unsigned char *data = (unsigned char *)plat_read_file(path, &size, 0);
    CHECK(data != NULL && size == (int)sizeof(b));
    CHECK(data && matches(data, (size_t)size, 0x63));
    free(data);
}

static void test_kill(const char *path)
{
    enum { SIZE = 32 * 1024 * 1024 };
    unsigned char *old = (unsigned char *)malloc(4096);
    CHECK(old != NULL);
    if (!old) return;
    fill(old, 4096, 0x11);
    CHECK(plat_write_file_atomic(path, old, 4096) == 0);
    free(old);

    pid_t pid = fork();
    if (pid == 0) {
        unsigned char *big = (unsigned char *)malloc(SIZE);
        if (!big) _exit(1);
        fill(big, SIZE, 0x7F);
        for (int i = 0; i < 100; i++) plat_write_file_atomic(path, big, SIZE);
        _exit(0);
    }
    CHECK(pid > 0);
    usleep(40000);
    if (pid > 0) {
        kill(pid, SIGKILL);
        int status = 0;
        waitpid(pid, &status, 0);
    }
    int size = 0;
    unsigned char *data = (unsigned char *)plat_read_file(path, &size, 0);
    CHECK(data != NULL);
    if (data) {
        CHECK(size == 4096 || size == SIZE);
        if (size == 4096) CHECK(matches(data, 4096, 0x11));
        else if (size == SIZE) CHECK(matches(data, SIZE, 0x7F));
        free(data);
    }
}

static void test_threads(const char *path)
{
    enum { SIZE = 256 * 1024, TIMES = 150 };
    unsigned char *first = (unsigned char *)malloc(SIZE);
    CHECK(first != NULL);
    if (!first) return;
    fill(first, SIZE, 0xA0);
    CHECK(plat_write_file_atomic(path, first, SIZE) == 0);
    free(first);

    volatile int okay = 1;
    Writer w1 = {path, 0xA0, SIZE, TIMES, &okay};
    Writer w2 = {path, 0x3C, SIZE, TIMES, &okay};
    ReaderArgs r = {path, SIZE, 0xA0, 0x3C, TIMES * 2, &okay};
    pthread_t t1, t2, t3;
    CHECK(pthread_create(&t1, NULL, write_thread, &w1) == 0);
    CHECK(pthread_create(&t2, NULL, write_thread, &w2) == 0);
    CHECK(pthread_create(&t3, NULL, read_thread, &r) == 0);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    pthread_join(t3, NULL);
    CHECK(okay == 1);
}

static void test_locks(const char *dir)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/test.lock", dir);
    remove(path);
    int fd = plat_try_lock(path);
    CHECK(fd >= 0);
    CHECK(plat_try_lock(path) < 0);
    plat_unlock(fd);
    int again = plat_try_lock(path);
    CHECK(again >= 0);
    plat_unlock(again);

    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) {
        fputs("999999\n", f);
        fclose(f);
    }
    int stale = plat_try_lock(path);
    CHECK(stale >= 0);
    plat_unlock(stale);

    f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f) {
        fprintf(f, "%ld\n", (long)getpid());
        fclose(f);
    }
    CHECK(plat_try_lock(path) < 0);
    remove(path);
}

static void test_json(const char *dir)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/data.json", dir);
    remove(path);
    Json *o = json_obj();
    json_sets(o, "name", "mexnya");
    json_setn(o, "version", 1.5);
    json_setb(o, "ready", 1);
    CHECK(json_save(path, o) == 0);
    json_setn(o, "version", 2.5);
    CHECK(json_save(path, o) == 0);
    json_free(o);
    char err[128];
    Json *back = json_load(path, err, sizeof(err));
    CHECK(back != NULL);
    if (back) {
        CHECK(strcmp(json_gets(back, "name", ""), "mexnya") == 0);
        CHECK(json_getn(back, "version", 0.0) == 2.5);
        CHECK(json_getb(back, "ready", 0) == 1);
        json_free(back);
    }
    remove(path);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    char path[512];
    snprintf(path, sizeof(path), "%s/atomic.bin", dir);
    test_overwrite(path);
    test_kill(path);
    test_threads(path);
    test_locks(dir);
    test_json(dir);
    remove(path);
    if (s_failures) {
        printf("atomic_test: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("atomic_test: all checks passed\n");
    return 0;
}

#endif

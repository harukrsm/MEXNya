#include "engine/core/error.h"
#include "engine/core/log.h"
#include "engine/core/map.h"
#include "engine/core/math.h"
#include "engine/core/mem.h"
#include "engine/core/path.h"
#include "engine/core/str.h"
#include "engine/core/vec.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int s_failures;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            s_failures++; \
        } \
    } while (0)

#define CHECK_NEAR(a, b, eps) CHECK(fabsf((a) - (b)) <= (eps))

static void test_mem(void)
{
    size_t base = mx_mem_live_blocks();
    MxArena *a = mx_arena_new(1024, "test");
    CHECK(a != NULL);
    void *p = mx_arena_alloc(a, 10, 64);
    CHECK(p != NULL);
    CHECK(((uintptr_t)p % 64) == 0);
    memset(p, 0x5A, 10);
    for (int i = 0; i < 1000; i++) {
        void *q = mx_arena_alloc(a, 32, 8);
        CHECK(q != NULL);
        if (!q) break;
    }
    CHECK(mx_arena_used(a) > 1024);
    CHECK(mx_arena_capacity(a) > 1024);
    CHECK(strcmp(mx_arena_name(a), "test") == 0);
    mx_arena_reset(a);
    CHECK(mx_arena_used(a) == 0);
    void *r = mx_arena_alloc(a, 8, 8);
    CHECK(r != NULL);
    mx_arena_destroy(a);

    CHECK(mx_frame_alloc(64) != NULL);
    mx_frame_reset();
    CHECK(mx_frame_alloc(256) != NULL);
    mx_frame_shutdown();
    CHECK(mx_mem_live_blocks() == base);
}

static void test_vec(void)
{
    MX_VEC(int) v = {0};
    for (int i = 0; i < 100000; i++) {
        CHECK(mx_vec_push(&v, i) == 1);
    }
    CHECK(v.len == 100000);
    CHECK(v.cap >= 100000);
    for (int i = 0; i < 100000; i += 9973) CHECK(v.data[i] == i);
    int last = v.data[v.len - 1];
    mx_vec_remove_swap(&v, 0);
    CHECK(v.len == 99999);
    CHECK(v.data[0] == last);
    mx_vec_clear(&v);
    CHECK(v.len == 0);
    CHECK(v.cap >= 100000);
    mx_vec_free(&v);
    CHECK(v.data == NULL && v.len == 0 && v.cap == 0);
}

static void test_map(void)
{
    enum { N = 100000 };
    int *naive = (int *)mx_mem_calloc(N, sizeof(int));
    MxMap *m = mx_map_new();
    CHECK(naive != NULL && m != NULL);
    for (int i = 0; i < N; i++) {
        uint64_t key = (uint64_t)i * 2654435761u;
        naive[i] = i + 1;
        CHECK(mx_map_put(m, key, (void *)(uintptr_t)(i + 1)));
    }
    CHECK(mx_map_count(m) == N);
    for (int i = 0; i < N; i++) {
        uint64_t key = (uint64_t)i * 2654435761u;
        int *got = (int *)mx_map_get(m, key);
        CHECK(got && (int)(uintptr_t)got == naive[i]);
    }
    for (int i = 0; i < N; i += 3) {
        uint64_t key = (uint64_t)i * 2654435761u;
        CHECK(mx_map_del(m, key) == 1);
        CHECK(mx_map_get(m, key) == NULL);
    }
    CHECK(mx_map_count(m) == N - (N + 2) / 3);
    for (int i = 0; i < N; i++) {
        uint64_t key = (uint64_t)i * 2654435761u;
        int *got = (int *)mx_map_get(m, key);
        if (i % 3 == 0) CHECK(got == NULL);
        else CHECK(got && (int)(uintptr_t)got == naive[i]);
    }
    int cursor = 0, seen = 0;
    uint64_t key;
    void *val;
    while (mx_map_next(m, &cursor, &key, &val)) {
        CHECK(val != NULL);
        seen++;
    }
    CHECK(seen == mx_map_count(m));
    CHECK(mx_hash_str("hello") == mx_hash_str("hello"));
    CHECK(mx_hash_str("hello") != mx_hash_str("world"));
    mx_map_free(m);
    mx_mem_free(naive);
}

static void test_str(void)
{
    const char *a = mx_str_intern("mexnya");
    const char *b = mx_str_intern("mexnya");
    CHECK(a && a == b);
    CHECK(mx_str_intern(NULL) == NULL);
    CHECK(strcmp(mx_str_intern(""), "") == 0);
    int base = mx_str_intern_count();
    for (int i = 0; i < 100000; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "name-%d", i);
        const char *s = mx_str_intern(buf);
        CHECK(s != NULL);
        if (i % 10000 == 0) CHECK(strcmp(s, buf) == 0);
    }
    CHECK(mx_str_intern_count() == base + 100000);
    mx_str_intern_shutdown();
    CHECK(mx_str_intern_count() == 0);
    CHECK(mx_str_intern("mexnya") != NULL);
    mx_str_intern_shutdown();
}

static void check_path(const char *in, const char *want)
{
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", in);
    mx_path_normalize(buf);
    if (strcmp(buf, want) != 0) {
        printf("FAIL path: '%s' -> '%s', want '%s'\n", in, buf, want);
        s_failures++;
    }
}

static void test_path(void)
{
    char buf[256];
    mx_path_join(buf, sizeof(buf), "/a", "b");
    CHECK(strcmp(buf, "/a/b") == 0);
    mx_path_join(buf, sizeof(buf), "/a/", "b");
    CHECK(strcmp(buf, "/a/b") == 0);
    mx_path_join(buf, sizeof(buf), "", "b");
    CHECK(strcmp(buf, "b") == 0);
    mx_path_join(buf, sizeof(buf), "/a", "/b");
    CHECK(strcmp(buf, "/b") == 0);

    check_path("/a/./b/../c", "/a/c");
    check_path("a//b/", "a/b");
    check_path("../a", "../a");
    check_path("/..", "/");
    check_path("a/..", ".");
    check_path("C:\\a\\..\\b", "C:/b");
    check_path("C:/", "C:/");
    check_path(".", ".");
    check_path("", ".");
    check_path("a/b/../../..", "..");
    check_path("/a/b/../../c", "/c");
    check_path("a/../..", "..");
    check_path("/a/b/../..", "/");

    CHECK(mx_path_is_subpath("/a", "/a/b"));
    CHECK(mx_path_is_subpath("/a", "/a"));
    CHECK(!mx_path_is_subpath("/a", "/ab"));
    CHECK(!mx_path_is_subpath("/a/b", "/a"));
    CHECK(mx_path_is_subpath("/a/./b", "/a/x/../b/c"));
    CHECK(mx_path_is_subpath("/", "/a"));
    CHECK(mx_path_is_subpath("C:\\Games", "C:\\Games\\save\\a.dat"));
    CHECK(!mx_path_is_subpath("/a", "/a/../b"));

    CHECK(strcmp(mx_path_ext("a.b/c"), "") == 0);
    CHECK(strcmp(mx_path_ext("a.b/c.txt"), "txt") == 0);
    CHECK(strcmp(mx_path_ext(".bashrc"), "") == 0);
    CHECK(strcmp(mx_path_ext("a.tar.gz"), "gz") == 0);
    CHECK(strcmp(mx_path_ext("dir/"), "") == 0);
    CHECK(mx_path_is_absolute("/a"));
    CHECK(mx_path_is_absolute("C:\\a"));
    CHECK(!mx_path_is_absolute("a"));
}

static void test_math(void)
{
    MxMat4 t, r, s, trs, inv, ident;
    MxTransform xf;
    xf.translation = mx_v3(1.0f, 2.0f, 3.0f);
    xf.rotation = mx_quat_axis_angle(mx_v3(0.0f, 0.0f, 1.0f), MX_PI * 0.5f);
    xf.scale = mx_v3(2.0f, 2.0f, 2.0f);
    mx_transform_compose(&trs, &xf);
    MxV3 p = mx_mat4_mul_point(&trs, mx_v3(1.0f, 0.0f, 0.0f));
    CHECK_NEAR(p.x, 1.0f, 1e-4f);
    CHECK_NEAR(p.y, 4.0f, 1e-4f);
    CHECK_NEAR(p.z, 3.0f, 1e-4f);

    MxTransform back;
    CHECK(mx_transform_decompose(&back, &trs));
    CHECK_NEAR(back.translation.x, 1.0f, 1e-4f);
    CHECK_NEAR(back.translation.y, 2.0f, 1e-4f);
    CHECK_NEAR(back.translation.z, 3.0f, 1e-4f);
    CHECK_NEAR(back.scale.x, 2.0f, 1e-4f);
    CHECK_NEAR(back.scale.y, 2.0f, 1e-4f);
    float dot = back.rotation.x * xf.rotation.x + back.rotation.y * xf.rotation.y +
                back.rotation.z * xf.rotation.z + back.rotation.w * xf.rotation.w;
    CHECK_NEAR(fabsf(dot), 1.0f, 1e-4f);

    CHECK(mx_mat4_invert(&inv, &trs));
    mx_mat4_mul(&ident, &trs, &inv);
    for (int i = 0; i < 16; i++) CHECK_NEAR(ident.m[i], (i % 5 == 0) ? 1.0f : 0.0f, 1e-4f);

    mx_mat4_translation(&t, mx_v3(4.0f, 0.0f, 0.0f));
    mx_mat4_rotation_y(&r, MX_PI * 0.5f);
    mx_mat4_mul(&trs, &t, &r);
    p = mx_mat4_mul_point(&trs, mx_v3(0.0f, 0.0f, -2.0f));
    CHECK_NEAR(p.x, 2.0f, 1e-4f);
    CHECK_NEAR(p.z, 0.0f, 1e-4f);

    s = r;
    mx_mat4_mul(&trs, &r, &s);
    p = mx_mat4_mul_point(&trs, mx_v3(1.0f, 0.0f, 0.0f));
    CHECK_NEAR(p.x, -1.0f, 1e-4f);
    CHECK_NEAR(p.z, 0.0f, 1e-4f);

    MxV3 v = mx_quat_rotate(mx_quat_axis_angle(mx_v3(0.0f, 0.0f, 1.0f), MX_PI * 0.5f),
                            mx_v3(1.0f, 0.0f, 0.0f));
    CHECK_NEAR(v.x, 0.0f, 1e-4f);
    CHECK_NEAR(v.y, 1.0f, 1e-4f);

    MxAabb box = mx_aabb_from_center(mx_v3(0.0f, 0.0f, 0.0f), mx_v3(1.0f, 1.0f, 1.0f));
    MxAabb box2 = mx_aabb_from_center(mx_v3(1.5f, 0.0f, 0.0f), mx_v3(1.0f, 1.0f, 1.0f));
    MxAabb box3 = mx_aabb_from_center(mx_v3(5.0f, 0.0f, 0.0f), mx_v3(1.0f, 1.0f, 1.0f));
    CHECK(mx_aabb_intersect(&box, &box2));
    CHECK(!mx_aabb_intersect(&box, &box3));
    CHECK(mx_aabb_contains(&box, mx_v3(1.0f, 0.0f, 0.0f)));

    MxMat4 move;
    mx_mat4_translation(&move, mx_v3(2.0f, 0.0f, 0.0f));
    MxAabb moved = mx_aabb_transform(&box, &move);
    CHECK_NEAR(moved.min.x, 1.0f, 1e-4f);
    CHECK_NEAR(moved.max.x, 3.0f, 1e-4f);

    MxRay ray;
    ray.origin = mx_v3(-5.0f, 0.0f, 0.0f);
    ray.dir = mx_v3(1.0f, 0.0f, 0.0f);
    MxRayHit hit;
    CHECK(mx_ray_aabb(&ray, &box, 100.0f, &hit));
    CHECK_NEAR(hit.t, 4.0f, 1e-4f);
    CHECK_NEAR(hit.normal.x, -1.0f, 1e-4f);
    MxAabb behind = mx_aabb_from_center(mx_v3(-10.0f, 0.0f, 0.0f), mx_v3(1.0f, 1.0f, 1.0f));
    CHECK(!mx_ray_aabb(&ray, &behind, 100.0f, &hit));
}

static void test_error(void)
{
    mx_clear_error();
    CHECK(strcmp(mx_last_error(), "") == 0);
    mx_set_error("value %d", 7);
    CHECK(strcmp(mx_last_error(), "value 7") == 0);
}

static int checked(int value)
{
    MX_CHECK(value > 0, MX_ERR_RANGE, "value %d out of range", value);
    return MX_OK;
}

static void test_check(void)
{
    CHECK(checked(3) == MX_OK);
    CHECK(checked(0) == MX_ERR_RANGE);
    CHECK(strcmp(mx_last_error(), "value 0 out of range") == 0);
}

static long file_size(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

static void test_log(const char *dir)
{
    char path[512], rotated[512];
    snprintf(path, sizeof(path), "%s/core.log", dir);
    snprintf(rotated, sizeof(rotated), "%s/core.log.1", dir);
    remove(path);
    remove(rotated);
    mx_log_init(path, 2000, 1);
    mx_log_set_level(MX_LOG_INFO);
    mx_log(MX_LOG_TRACE, "test", "hidden");
    CHECK(mx_log_count() == 0);
    for (int i = 0; i < 400; i++) mx_log(MX_LOG_INFO, "test", "line %d", i);
    CHECK(mx_log_count() == 400);
    mx_log_flush();
    long size = file_size(path);
    if (!(size > 0 && size <= 2064)) printf("log sizes: current=%ld rotated=%ld\n", size, file_size(rotated));
    CHECK(size > 0 && size <= 2064);
    CHECK(file_size(rotated) > 0);
    char *lines = (char *)mx_mem_alloc(1024 * 1024);
    CHECK(lines != NULL);
    if (lines) {
        int n = mx_log_lines(lines, 1024 * 1024);
        CHECK(n > 0);
        CHECK(strstr(lines, "line 399") != NULL);
        CHECK(strstr(lines, "line 0\n") != NULL);
        mx_mem_free(lines);
    }
    mx_log_set_level(MX_LOG_WARN);
    int before = mx_log_count();
    mx_log(MX_LOG_INFO, "test", "filtered");
    CHECK(mx_log_count() == before);
    mx_log(MX_LOG_ERROR, "test", "visible");
    CHECK(mx_log_count() == before);
    char *tail = (char *)mx_mem_alloc(65536);
    CHECK(tail != NULL);
    if (tail) {
        mx_log_lines(tail, 65536);
        CHECK(strstr(tail, "visible") != NULL);
        CHECK(strstr(tail, "filtered") == NULL);
        mx_mem_free(tail);
    }
    mx_log_shutdown();
    CHECK(mx_log_count() == 0);
    remove(path);
    remove(rotated);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
#ifdef MX_MEM_TRACK
    size_t base = mx_mem_live_blocks();
#endif
    test_mem();
    test_vec();
    test_map();
    test_str();
    test_path();
    test_math();
    test_error();
    test_check();
    test_log(dir);
#ifdef MX_MEM_TRACK
    CHECK(mx_mem_live_blocks() == base);
    if (mx_mem_live_blocks() != base) mx_mem_report();
#endif
    if (s_failures) {
        printf("core_test: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("core_test: all checks passed\n");
    return 0;
}

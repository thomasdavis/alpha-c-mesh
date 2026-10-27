/* programs.rs's heap_first_fit and heap_double_free. */
#include "mesh/check.h"
#include "mesh/view_alloc.h"

#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
    static mesh_view_heap h;
    mesh_view_heap_init(&h);
    CHECK_U(mesh_view_heap_free_pages(&h), MESH_VIEW_PAGES);
    uint32_t a, b, c, x;
    CHECK(mesh_view_heap_alloc(&h, 0, &x) && x == 0);
    CHECK(mesh_view_heap_alloc(&h, 0x1000, &a));
    CHECK(mesh_view_heap_alloc(&h, 0x1001, &b));
    CHECK(mesh_view_heap_alloc(&h, 1, &c));
    CHECK(a == 0x100000 && b == 0x101000 && c == 0x103000);
    CHECK_U(mesh_view_heap_free_pages(&h), MESH_VIEW_PAGES - 4);
    mesh_view_heap_free(&h, b, 0x1001);
    CHECK(mesh_view_heap_alloc(&h, 0x800, &x) && x == 0x101000);
    CHECK(mesh_view_heap_alloc(&h, 0x3000, &x) && x == 0x104000);
    CHECK(mesh_view_heap_alloc(&h, 0x1000, &x) && x == 0x102000);
    mesh_view_heap_init(&h);
    uint32_t all;
    CHECK(mesh_view_heap_alloc(&h, (uint32_t)(MESH_VIEW_PAGES * MESH_VIEW_PAGE), &all) && all == MESH_VIEW_HEAP);
    CHECK(!mesh_view_heap_alloc(&h, 1, &x));
    CHECK_U(mesh_view_heap_largest_run(&h), 0);
    mesh_view_heap_free(&h, all, (uint32_t)(MESH_VIEW_PAGES * MESH_VIEW_PAGE));
    CHECK(mesh_view_heap_alloc(&h, 1, &x) && x == MESH_VIEW_HEAP);
    CHECK_U(mesh_view_heap_largest_run(&h), MESH_VIEW_PAGES - 1);

    /* A double free aborts (the Rust panics "freeing a free page"). */
    pid_t pid = fork();
    if (pid == 0) {
        close(2);
        static mesh_view_heap d;
        mesh_view_heap_init(&d);
        uint32_t o;
        mesh_view_heap_alloc(&d, 1, &o);
        mesh_view_heap_free(&d, o, 1);
        mesh_view_heap_free(&d, o, 1);
        _exit(0);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGABRT);
    return mesh_check_done();
}

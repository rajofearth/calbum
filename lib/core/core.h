#ifndef CALBUM_CORE_H
#define CALBUM_CORE_H

// ── Target Windows 10+ ──────────────────────────────────────────────
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <stdint.h>
#include <wchar.h>

// ── COM interface keyword ───────────────────────────────────────────
#ifndef interface
#define interface struct
#endif

// ── Constants ────────────────────────────────────────────────────────
#define MAX_PATH_LEN 1024
#define ARENA_CAPACITY (16ULL * 1024 * 1024) // 16 MB
#define DEFAULT_CAPACITY 256
#define MIN_WINDOW_WIDTH 400
#define MIN_WINDOW_HEIGHT 300

// ── SAFE_RELEASE macro for COM cleanup ──────────────────────────────
#define SAFE_RELEASE(p)                                            \
    do                                                             \
    {                                                              \
        if (p)                                                     \
        {                                                          \
            ((IUnknown *) (p))->lpVtbl->Release((IUnknown *) (p)); \
            (p) = NULL;                                            \
        }                                                          \
    } while (0)

// ── Arena Allocator ──────────────────────────────────────────────────
typedef struct
{
    unsigned char *buf;
    size_t capacity;
    size_t offset;
} Arena;

static inline void arena_init(Arena *a, void *buf, size_t cap)
{
    a->buf = (unsigned char *) buf;
    a->capacity = cap;
    a->offset = 0;
}

static inline void *arena_alloc(Arena *a, size_t size)
{
    size_t align = sizeof(void *);
    size_t mask = align - 1;
    size_t off = (a->offset + mask) & ~mask;
    if (off + size > a->capacity)
        return NULL;
    a->offset = off + size;
    return a->buf + off;
}

static inline void arena_reset(Arena *a)
{
    a->offset = 0;
}

#define arena_alloc_type(a, T) (T *) arena_alloc(a, sizeof(T))
#define arena_alloc_array(a, T, n) (T *) arena_alloc(a, sizeof(T) * (n))

// ── Ring Buffer ──────────────────────────────────────────────────────
typedef struct
{
    int capacity;
    int head;
    int tail;
    CRITICAL_SECTION lock;
    HANDLE nonempty;
    void **slots;
} RingBuffer;

static inline void rb_init(RingBuffer *rb, void *slot_storage, int cap)
{
    rb->slots = (void **) slot_storage;
    rb->capacity = cap;
    rb->head = 0;
    rb->tail = 0;
    InitializeCriticalSection(&rb->lock);
    rb->nonempty = CreateEventW(NULL, FALSE, FALSE, NULL);
}

static inline void rb_destroy(RingBuffer *rb)
{
    DeleteCriticalSection(&rb->lock);
    if (rb->nonempty)
        CloseHandle(rb->nonempty);
}

static inline int rb_push(RingBuffer *rb, void *item)
{
    EnterCriticalSection(&rb->lock);
    int next = (rb->head + 1) % rb->capacity;
    int ok = (next != rb->tail);
    if (ok)
    {
        rb->slots[rb->head] = item;
        rb->head = next;
    }
    LeaveCriticalSection(&rb->lock);
    if (ok)
        SetEvent(rb->nonempty);
    return ok;
}

static inline void *rb_try_pop(RingBuffer *rb)
{
    EnterCriticalSection(&rb->lock);
    void *item = NULL;
    if (rb->tail != rb->head)
    {
        item = rb->slots[rb->tail];
        rb->tail = (rb->tail + 1) % rb->capacity;
    }
    LeaveCriticalSection(&rb->lock);
    return item;
}

// ── Math Helpers ─────────────────────────────────────────────────────
static inline float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}
static inline float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}
static inline int clamp_int(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}
static inline int ceil_div(int n, int d)
{
    return (n + d - 1) / d;
}
static inline float ease_out_factor(float speed, float dt)
{
    return 1.0F - expf(-speed * dt);
}

#endif // CALBUM_CORE_H

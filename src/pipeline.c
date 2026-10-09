#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int num_orders, T, P, M, N;
static int next_order_id;
static int *encoder_token_A, *encoder_token_B;
static sem_t *token_sem;
static pthread_mutex_t order_id_mutex;
static sem_t start_gate;
/* Written before posting the gate; workers read only after waiting on it. */
static int startup_aborted;

typedef struct {
    int order_id;
    int raw_value;
    int terminate;
} RawPacket;

typedef struct {
    int order_id;
    int encoded_value;
    int terminate;
} EPacket;

typedef struct {
    RawPacket *item;
    int size, in, out;
    sem_t empty, full;
    pthread_mutex_t mutex;
} BufferA;

typedef struct {
    EPacket *item;
    int size, in, out;
    sem_t empty, full;
    pthread_mutex_t mutex;
} BufferB;

static BufferA bufferA;
static BufferB bufferB;

static void fatal_error(const char *operation, int error)
{
    fprintf(stderr, "Error: %s: %s\n", operation, strerror(error));
    /* A broken runtime primitive cannot safely support continued processing or
       cleanup. Terminate the process instead of leaving blocked workers alive. */
    exit(EXIT_FAILURE);
}

static void check_pthread(int result, const char *operation)
{
    if (result != 0)
        fatal_error(operation, result);
}

static void wait_sem(sem_t *sem)
{
    while (sem_wait(sem) == -1) {
        if (errno != EINTR)
            fatal_error("sem_wait", errno);
    }
}

static void post_sem(sem_t *sem)
{
    if (sem_post(sem) == -1)
        fatal_error("sem_post", errno);
}

static void bufferA_push(BufferA *buf, RawPacket item)
{
    wait_sem(&buf->empty);
    check_pthread(pthread_mutex_lock(&buf->mutex), "buffer A lock");
    buf->item[buf->in] = item;
    buf->in = (buf->in + 1) % buf->size;
    check_pthread(pthread_mutex_unlock(&buf->mutex), "buffer A unlock");
    post_sem(&buf->full);
}

static RawPacket bufferA_pop(BufferA *buf)
{
    wait_sem(&buf->full);
    check_pthread(pthread_mutex_lock(&buf->mutex), "buffer A lock");
    RawPacket item = buf->item[buf->out];
    buf->out = (buf->out + 1) % buf->size;
    check_pthread(pthread_mutex_unlock(&buf->mutex), "buffer A unlock");
    post_sem(&buf->empty);
    return item;
}

static void bufferB_push(BufferB *buf, EPacket item)
{
    wait_sem(&buf->empty);
    check_pthread(pthread_mutex_lock(&buf->mutex), "buffer B lock");
    buf->item[buf->in] = item;
    buf->in = (buf->in + 1) % buf->size;
    check_pthread(pthread_mutex_unlock(&buf->mutex), "buffer B unlock");
    post_sem(&buf->full);
}

static EPacket bufferB_pop(BufferB *buf)
{
    wait_sem(&buf->full);
    check_pthread(pthread_mutex_lock(&buf->mutex), "buffer B lock");
    EPacket item = buf->item[buf->out];
    buf->out = (buf->out + 1) % buf->size;
    check_pthread(pthread_mutex_unlock(&buf->mutex), "buffer B unlock");
    post_sem(&buf->empty);
    return item;
}

static void *quantizer_thread(void *arg)
{
    (void)arg;
    wait_sem(&start_gate);
    if (startup_aborted)
        return NULL;
    for (;;) {
        check_pthread(pthread_mutex_lock(&order_id_mutex), "order ID lock");
        if (next_order_id >= num_orders) {
            check_pthread(pthread_mutex_unlock(&order_id_mutex), "order ID unlock");
            break;
        }
        int order_id = next_order_id++;
        /* rand() need not be thread-safe on every POSIX implementation. */
        int raw = rand() % 100;
        check_pthread(pthread_mutex_unlock(&order_id_mutex), "order ID unlock");
        RawPacket item = {order_id, raw, 0};
        bufferA_push(&bufferA, item);
    }
    return NULL;
}

static void *encoder_thread(void *arg)
{
    int encoder_id = *(int *)arg;
    wait_sem(&start_gate);
    if (startup_aborted)
        return NULL;
    for (;;) {
        RawPacket item = bufferA_pop(&bufferA);
        if (item.terminate) {
            EPacket end = {0, 0, 1};
            bufferB_push(&bufferB, end);
            break;
        }
        int a = encoder_token_A[encoder_id];
        int b = encoder_token_B[encoder_id];
        /* Distinct token IDs always follow one global resource order. */
        int first = a < b ? a : b;
        int second = a < b ? b : a;
        wait_sem(&token_sem[first]);
        wait_sem(&token_sem[second]);
        EPacket outcome = {item.order_id, item.raw_value * 2 + a + b, 0};
        post_sem(&token_sem[a]);
        post_sem(&token_sem[b]);
        /* Release tokens before waiting for space in the downstream buffer. */
        bufferB_push(&bufferB, outcome);
    }
    return NULL;
}

static void *logger(void *arg)
{
    (void)arg;
    int terminate_count = 0;
    wait_sem(&start_gate);
    if (startup_aborted)
        return NULL;
    for (;;) {
        EPacket item = bufferB_pop(&bufferB);
        if (item.terminate) {
            if (++terminate_count == P)
                break;
            continue;
        }
        if (printf("[Logger] order_id=%d, encoded_value=%d\n",
                   item.order_id, item.encoded_value) < 0)
            fatal_error("writing output", errno ? errno : EIO);
    }
    return NULL;
}

static void usage(const char *program)
{
    fprintf(stderr, "Usage: %s P M N num_orders T cnt_0 ... cnt_{T-1} "
            "tA_0 tB_0 ... tA_{P-1} tB_{P-1}\n", program);
    fprintf(stderr, "P, M, N and counts must be positive; T >= 2; "
            "num_orders >= 0; token IDs must be distinct and in [0, T-1].\n");
}

static int parse_int(const char *text, const char *name, int min, int max,
                     int *value)
{
    char *end;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno == ERANGE || end == text || *end != '\0' ||
        parsed < min || parsed > max) {
        fprintf(stderr, "Error: %s must be an integer in [%d, %d]: '%s'\n",
                name, min, max, text);
        return -1;
    }
    *value = (int)parsed;
    return 0;
}

static void *allocate(size_t count, size_t size)
{
    if (count > SIZE_MAX / size) {
        fprintf(stderr, "Error: allocation size overflow\n");
        return NULL;
    }
    void *memory = calloc(count, size);
    if (!memory)
        fprintf(stderr, "Error: memory allocation failed\n");
    return memory;
}

/* Track initialization independently, including partially initialized buffers. */
typedef struct {
    int a_empty, a_full, a_mutex;
    int b_empty, b_full, b_mutex;
    int order_mutex, gate, tokens;
} Initialized;

static int init_sem(sem_t *sem, unsigned int count, int *initialized)
{
    if (sem_init(sem, 0, count) == -1) {
        fprintf(stderr, "Error: sem_init: %s\n", strerror(errno));
        return -1;
    }
    *initialized = 1;
    return 0;
}

static int init_mutex(pthread_mutex_t *mutex, int *initialized)
{
    int result = pthread_mutex_init(mutex, NULL);
    if (result != 0) {
        fprintf(stderr, "Error: pthread_mutex_init: %s\n", strerror(result));
        return -1;
    }
    *initialized = 1;
    return 0;
}

static void destroy_sem(sem_t *sem)
{
    if (sem_destroy(sem) == -1)
        fatal_error("sem_destroy", errno);
}

static void cleanup(Initialized *init)
{
    if (init->gate) destroy_sem(&start_gate);
    if (init->order_mutex)
        check_pthread(pthread_mutex_destroy(&order_id_mutex), "order mutex destroy");
    for (int i = 0; i < init->tokens; ++i) destroy_sem(&token_sem[i]);
    if (init->a_empty) destroy_sem(&bufferA.empty);
    if (init->a_full) destroy_sem(&bufferA.full);
    if (init->a_mutex)
        check_pthread(pthread_mutex_destroy(&bufferA.mutex), "buffer A mutex destroy");
    if (init->b_empty) destroy_sem(&bufferB.empty);
    if (init->b_full) destroy_sem(&bufferB.full);
    if (init->b_mutex)
        check_pthread(pthread_mutex_destroy(&bufferB.mutex), "buffer B mutex destroy");
    free(bufferA.item);
    free(bufferB.item);
    free(token_sem);
    free(encoder_token_A);
    free(encoder_token_B);
}

int main(int argc, char *argv[])
{
    Initialized init = {0};
    pthread_t *q_threads = NULL, *e_threads = NULL, l_thread;
    int *encoder_ids = NULL, *token_counts = NULL;
    int q_created = 0, e_created = 0, l_created = 0;
    int status = EXIT_FAILURE;
    int sem_max = INT_MAX;
#ifdef SEM_VALUE_MAX
    if (SEM_VALUE_MAX < INT_MAX)
        sem_max = SEM_VALUE_MAX;
#endif
    if (argc < 6) {
        fprintf(stderr, "Error: missing pipeline configuration\n");
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (parse_int(argv[1], "P", 1, INT_MAX, &P) ||
        parse_int(argv[2], "M", 1, sem_max, &M) ||
        parse_int(argv[3], "N", 1, sem_max, &N) ||
        parse_int(argv[4], "num_orders", 0, INT_MAX, &num_orders) ||
        parse_int(argv[5], "T", 2, INT_MAX, &T)) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    /* Compute argument count before allocating, without overflowing int. */
    if (6ULL + (unsigned int)T + 2ULL * (unsigned int)P != (unsigned int)argc) {
        fprintf(stderr, "Error: expected T token counts and P token pairs; "
                "missing or extra arguments\n");
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    token_counts = allocate((size_t)T, sizeof(*token_counts));
    encoder_token_A = allocate((size_t)P, sizeof(*encoder_token_A));
    encoder_token_B = allocate((size_t)P, sizeof(*encoder_token_B));
    if (!token_counts || !encoder_token_A || !encoder_token_B)
        goto done;
    for (int i = 0; i < T; ++i) {
        if (parse_int(argv[6+i], "token count", 1, sem_max, &token_counts[i]))
            goto invalid;
    }
    int index = 6 + T;
    for (int i = 0; i < P; ++i) {
        if (parse_int(argv[index++], "token A ID", 0, T-1, &encoder_token_A[i]) ||
            parse_int(argv[index++], "token B ID", 0, T-1, &encoder_token_B[i]))
            goto invalid;
        if (encoder_token_A[i] == encoder_token_B[i]) {
            fprintf(stderr, "Error: encoder %d requires two distinct token IDs\n", i);
            goto invalid;
        }
        if ((int64_t)encoder_token_A[i] + encoder_token_B[i] + 198 > INT_MAX) {
            fprintf(stderr, "Error: encoder %d could overflow encoded_value\n", i);
            goto invalid;
        }
    }
    bufferA.item = allocate((size_t)M, sizeof(*bufferA.item));
    bufferB.item = allocate((size_t)N, sizeof(*bufferB.item));
    token_sem = allocate((size_t)T, sizeof(*token_sem));
    q_threads = allocate((size_t)P, sizeof(*q_threads));
    e_threads = allocate((size_t)P, sizeof(*e_threads));
    encoder_ids = allocate((size_t)P, sizeof(*encoder_ids));
    if (!bufferA.item || !bufferB.item || !token_sem ||
        !q_threads || !e_threads || !encoder_ids)
        goto done;
    bufferA.size = M;
    bufferB.size = N;
    if (init_sem(&bufferA.empty, (unsigned int)M, &init.a_empty) ||
        init_sem(&bufferA.full, 0, &init.a_full) ||
        init_mutex(&bufferA.mutex, &init.a_mutex) ||
        init_sem(&bufferB.empty, (unsigned int)N, &init.b_empty) ||
        init_sem(&bufferB.full, 0, &init.b_full) ||
        init_mutex(&bufferB.mutex, &init.b_mutex) ||
        init_mutex(&order_id_mutex, &init.order_mutex) ||
        init_sem(&start_gate, 0, &init.gate))
        goto done;
    for (int i = 0; i < T; ++i) {
        int initialized = 0;
        if (init_sem(&token_sem[i], (unsigned int)token_counts[i], &initialized))
            goto done;
        ++init.tokens;
    }
    srand((unsigned int)time(NULL));
    /* Workers stay behind the startup gate until the entire pipeline exists.
       On creation failure, release only created workers in abort mode and join
       them before destroying anything; no worker can be blocked in a buffer. */
    int result = pthread_create(&l_thread, NULL, logger, NULL);
    if (result != 0) goto creation_failed;
    l_created = 1;
    for (int i = 0; i < P; ++i) {
        encoder_ids[i] = i;
        result = pthread_create(&e_threads[i], NULL, encoder_thread, &encoder_ids[i]);
        if (result != 0) goto creation_failed;
        ++e_created;
    }
    for (int i = 0; i < P; ++i) {
        result = pthread_create(&q_threads[i], NULL, quantizer_thread, NULL);
        if (result != 0) goto creation_failed;
        ++q_created;
    }
    for (size_t i = 0; i < (size_t)q_created + (size_t)e_created + (size_t)l_created; ++i)
        post_sem(&start_gate);
    for (int i = 0; i < q_created; ++i)
        check_pthread(pthread_join(q_threads[i], NULL), "quantizer join");
    /* All real packets have entered A before any sentinel. Each encoder emits
       its own end packet only after its last result; the logger waits for P. */
    for (int i = 0; i < P; ++i) {
        RawPacket end = {0, 0, 1};
        bufferA_push(&bufferA, end);
    }
    for (int i = 0; i < e_created; ++i)
        check_pthread(pthread_join(e_threads[i], NULL), "encoder join");
    check_pthread(pthread_join(l_thread, NULL), "logger join");
    if (fflush(stdout) == EOF)
        fprintf(stderr, "Error: flushing output: %s\n", strerror(errno));
    else
        status = EXIT_SUCCESS;
    goto done;

creation_failed:
    fprintf(stderr, "Error: pthread_create: %s\n", strerror(result));
    startup_aborted = 1;
    for (size_t i = 0; i < (size_t)q_created + (size_t)e_created + (size_t)l_created; ++i)
        post_sem(&start_gate);
    for (int i = 0; i < q_created; ++i)
        check_pthread(pthread_join(q_threads[i], NULL), "aborted quantizer join");
    for (int i = 0; i < e_created; ++i)
        check_pthread(pthread_join(e_threads[i], NULL), "aborted encoder join");
    if (l_created)
        check_pthread(pthread_join(l_thread, NULL), "aborted logger join");
    goto done;
invalid:
    usage(argv[0]);
done:
    cleanup(&init);
    free(token_counts);
    free(q_threads);
    free(e_threads);
    free(encoder_ids);
    return status;
}

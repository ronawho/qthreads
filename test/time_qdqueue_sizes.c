#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <qthread/qthread.h>
#include <qthread/qloop.h>
#include <qthread/qdqueue.h>
#include <qthread/qpool.h>
#include "qtimer.h"

#define ELEMENT_COUNT 10000
#define THREAD_COUNT 128

qpool memory;
size_t objsize = 8;

aligned_t queuer (qthread_t *me, void *arg)
{
    qdqueue_t *q = (qdqueue_t*)arg;
    size_t i;

    for (i = 0; i < ELEMENT_COUNT; i++) {
	void * tmp = qpool_alloc(me, memory);
	memset(tmp, 1, objsize);
	if (qdqueue_enqueue(me, q, tmp) != QTHREAD_SUCCESS) {
	    fprintf(stderr, "qdqueue_enqueue(q, %p) failed!\n", me);
	    exit(-2);
	}
    }
    return 0;
}

aligned_t dequeuer (qthread_t *me, void *arg)
{
    qdqueue_t *q = (qdqueue_t*)arg;
    void *ret;
    size_t i;
    void * ref = qpool_alloc(me, memory);

    memset(ref, 1, objsize);
    for (i = 0; i < ELEMENT_COUNT; i++) {
	void * tmp;
	while ((tmp = qdqueue_dequeue(me, q)) == NULL) {
	    qthread_yield(me);
	}
	if (memcmp(ref, tmp, objsize)) {
	    fprintf(stderr, "memory was corrupted!\n");
	    exit(-3);
	}
	qpool_free(me, memory, tmp);
    }
    qpool_free(me, memory, ref);
    return 0;
}

void loop_queuer (qthread_t *me, const size_t startat, const size_t stopat, void *arg)
{
    size_t i;
    qdqueue_t *q = (qdqueue_t *)arg;

    for (i=startat; i<stopat; i++) {
	void * tmp = qpool_alloc(me, memory);
	memset(tmp, 1, objsize);
	if (qdqueue_enqueue(me, q, tmp) != QTHREAD_SUCCESS) {
	    fprintf(stderr, "qdqueue_enqueue(q, %p) failed!\n", me);
	    exit(-2);
	}
    }
}

void loop_dequeuer (qthread_t *me, const size_t startat, const size_t stopat, void *arg)
{
    size_t i;
    qdqueue_t *q = (qdqueue_t *)arg;
    void * ref = qpool_alloc(me, memory);

    memset(ref, 1, objsize);
    for (i=startat; i<stopat; i++) {
	void * tmp;
	if ((tmp = qdqueue_dequeue(me, q)) == NULL) {
	    fprintf(stderr, "qdqueue_dequeue(q, %p) failed!\n", me);
	    exit(-2);
	}
	if (memcmp(ref, tmp, objsize)) {
	    fprintf(stderr, "memory was corrupted!\n");
	    exit(-3);
	}
	qpool_free(me, memory, tmp);
    }
    qpool_free(me, memory, ref);
}

int main(int argc, char *argv[])
{
    qdqueue_t *q;
    int threads = 1, interactive = 0;
    qthread_t *me;
    size_t i;
    aligned_t *rets;
    qtimer_t timer = qtimer_new();

    if (argc >= 2) {
	threads = strtol(argv[1], NULL, 0);
	if (threads <= 0) {
	    threads = 1;
	    interactive = 0;
	} else {
	    interactive = 1;
	}
    }
    if (argc >= 3) {
	objsize = strtol(argv[2], NULL, 0);
    }

    assert(qthread_init(threads) == 0);
    me = qthread_self();

    memory = qpool_create(me, objsize);

    if ((q = qdqueue_new(me)) == NULL) {
	fprintf(stderr, "qdqueue_new() failed!\n");
	exit(-1);
    }

    /* prime the pump */
    /*qt_loop_balance(0, THREAD_COUNT * ELEMENT_COUNT, loop_queuer, q);
    qt_loop_balance(0, THREAD_COUNT * ELEMENT_COUNT, loop_dequeuer, q);
    if (!qdqueue_empty(me, q)) {
	fprintf(stderr, "qdqueue not empty after priming!\n");
	exit(-2);
    }*/

    qtimer_start(timer);
    qt_loop_balance(0, THREAD_COUNT * ELEMENT_COUNT, loop_queuer, q);
    qtimer_stop(timer);
    printf("loop balance enqueue: %f secs\n", qtimer_secs(timer));
    qtimer_start(timer);
    qt_loop_balance(0, THREAD_COUNT * ELEMENT_COUNT, loop_dequeuer, q);
    qtimer_stop(timer);
    printf("loop balance dequeue: %f secs\n", qtimer_secs(timer));
    if (!qdqueue_empty(me, q)) {
	fprintf(stderr, "qdqueue not empty after loop balance test!\n");
	exit(-2);
    }

    rets = calloc(THREAD_COUNT, sizeof(aligned_t));
    assert(rets != NULL);
    qtimer_start(timer);
    for (i = 0; i < THREAD_COUNT; i++) {
	assert(qthread_fork(dequeuer, q, &(rets[i])) == QTHREAD_SUCCESS);
    }
    for (i = 0; i < THREAD_COUNT; i++) {
	assert(qthread_fork(queuer, q, NULL) == QTHREAD_SUCCESS);
    }
    for (i = 0; i < THREAD_COUNT; i++) {
	assert(qthread_readFF(me, NULL, &(rets[i])) == QTHREAD_SUCCESS);
    }
    qtimer_stop(timer);
    if (!qdqueue_empty(me, q)) {
	fprintf(stderr, "qdqueue not empty after threaded test!\n");
	exit(-2);
    }
    printf("threaded dq test: %f secs\n", qtimer_secs(timer));
    free(rets);

    if (qdqueue_destroy(me, q) != QTHREAD_SUCCESS) {
	fprintf(stderr, "qdqueue_destroy() failed!\n");
	exit(-2);
    }

    if (interactive) {
	printf("success!\n");
    }

    qthread_finalize();
    return 0;
}

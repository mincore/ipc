/* =====================================================================
 * Copyright (C) 2016 chen shuangping. All Rights Reserved
 *    Filename: ipc.c
 * Description:
 *     Created: Wed 11 May 2016 04:46:14 PM CST
 *      Author: <csp> mincore@163.com
 * =====================================================================
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <pthread.h>

#include "ipc.h"

#define MBUF_GROW_STEP 64
#define MBUF_GROW_SIZE(sz) (((sz)%MBUF_GROW_STEP) ? ((sz)/MBUF_GROW_STEP+1)*MBUF_GROW_STEP : (sz))

struct msg {
    int id;
    int size;
};

struct mbuf {
    void *buf;
    int size;
};

struct waitingcond {
    int id;
    int msg_arrived;
    struct mbuf mbuf;
    pthread_cond_t cond;
};

struct ipc {
    int fd;
    pid_t pid;
    ipc_cb cb;
    pthread_mutex_t fd_lock;
    pthread_mutex_t cond_lock;
#define MAX_THREAD_COND 32
    struct waitingcond wconds[MAX_THREAD_COND];
};

static void mbuf_init(struct mbuf *mbuf, int size) {
    mbuf->buf = malloc(size);
    mbuf->size = size;
}

static void mbuf_expand(struct mbuf *mbuf, int size) {
    char *p;
    if (mbuf->size < size) {
        mbuf->size = MBUF_GROW_SIZE(size);
        p = realloc(mbuf->buf, mbuf->size);
        if (!p) {
            free(mbuf->buf);
            p = malloc(mbuf->size);
        }
        mbuf->buf = p;
    }
}

static struct waitingcond* ipc_get_free_wc(struct ipc *ipc, int id) {
    struct waitingcond *wc = NULL;
    int i;

    pthread_mutex_lock(&ipc->cond_lock);
    for (i=0; i<MAX_THREAD_COND; i++) {
        if (ipc->wconds[i].msg_arrived == 1) {
            wc = &ipc->wconds[i];
            wc->msg_arrived = 0;
            wc->id = id;
            break;
        }
    }
    pthread_mutex_unlock(&ipc->cond_lock);

    return wc;
}

static struct waitingcond* ipc_find_waiting_wc(struct ipc *ipc, int id) {
    struct waitingcond *wc = NULL;
    int i;

    pthread_mutex_lock(&ipc->cond_lock);
    for (i=0; i<MAX_THREAD_COND; i++) {
        if (ipc->wconds[i].id == id && ipc->wconds[i].msg_arrived == 0) {
            wc = &ipc->wconds[i];
            break;
        }
    }
    pthread_mutex_unlock(&ipc->cond_lock);

    return wc;
}


static void msg_read(struct ipc *ipc, struct msg *m, struct mbuf *mbuf) {
    assert(read(ipc->fd, m, sizeof(struct msg)) == sizeof(struct msg));
    if (m->size) {
        mbuf_expand(mbuf, m->size);
        assert(read(ipc->fd, mbuf->buf, m->size) == m->size);
    }
}

static int msg_write(struct ipc *ipc, int id, void *buf, int size) {
    struct msg m = {.id = id, .size = size};
    int ret = write(ipc->fd, &m, sizeof(struct msg));
    if (ret == -1)
        return ret;

    if (buf && size) {
        assert(write(ipc->fd, buf, size) == size);
        ret += size;
    }

    return ret;
}

static void* msg_loop(void *arg) {
    struct ipc *ipc = (struct ipc *)arg;
    struct msg m = {0};
    struct mbuf mbuf;

    mbuf_init(&mbuf, 256);

    while (m.id != ID_REPLY_DESTROY) {
        if (ipc->cb) {
            msg_read(ipc, &m, &mbuf);
            ipc->cb(ipc, m.id, mbuf.buf, m.size);
        } else {
            assert(read(ipc->fd, &m, sizeof(struct msg)) == sizeof(struct msg));
            struct waitingcond *wc = ipc_find_waiting_wc(ipc, m.id);
            assert(wc && m.size == wc->mbuf.size);
            if (m.size > 0)
                assert(read(ipc->fd, wc->mbuf.buf, m.size) == m.size);
            wc->msg_arrived = 1;
            pthread_cond_signal(&wc->cond);
        }
    }

    free(mbuf.buf);
    return NULL;
}

static int ipc_create_thread(void*(*func)(void *), void *arg) {
    pthread_t pthread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    return pthread_create(&pthread, &attr, func, arg);
}

struct ipc* ipc_create(ipc_cb cb) {
    struct ipc *ipc;
    int sv[2];
    int i;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1)
        return NULL;

    ipc = malloc(sizeof(struct ipc));
    pthread_mutex_init(&ipc->fd_lock, NULL);
    pthread_mutex_init(&ipc->cond_lock, NULL);

    for (i=0; i<MAX_THREAD_COND; i++) {
        ipc->wconds[i].id = 0;
        ipc->wconds[i].msg_arrived = 1;
        pthread_cond_init(&ipc->wconds[i].cond, NULL);
    }

    ipc->pid = fork();

    switch (ipc->pid) {
    case -1:
        free(ipc);
        return NULL;
    case 0:
        close(sv[0]);
        ipc->cb = cb; ipc->fd = sv[1];
        msg_loop(ipc);
        exit(0);
    default:
        close(sv[1]);
        ipc->cb = NULL; ipc->fd = sv[0];
        ipc_create_thread(msg_loop, ipc);
        break;
    }

    ipc_call(ipc, ID_REPLY_CREATE, NULL, 0, NULL, 0);

    return ipc;
}

void ipc_destroy(struct ipc *ipc) {
    int i;

    ipc_call(ipc, ID_REPLY_DESTROY, NULL, 0, NULL, 0);
    waitpid(ipc->pid, NULL, 0);
    close(ipc->fd);

    for (i=0; i<MAX_THREAD_COND; i++)
        pthread_cond_destroy(&ipc->wconds[i].cond);

    pthread_mutex_destroy(&ipc->fd_lock);
    pthread_mutex_destroy(&ipc->cond_lock);
    free(ipc);
}

int ipc_call(struct ipc *ipc, int id, void *in, int in_size, void *out, int out_size) {
    struct waitingcond *wc = ipc_get_free_wc(ipc, id);
    if (!wc)
        return -1;

    wc->mbuf.buf = out;
    wc->mbuf.size = out_size;

    pthread_mutex_lock(&ipc->fd_lock);
    if (msg_write(ipc, id, in, in_size) == -1) {
        pthread_mutex_unlock(&ipc->fd_lock);
        return -1;
    }
    pthread_mutex_unlock(&ipc->fd_lock);

    pthread_mutex_lock(&ipc->cond_lock);
    while (!wc->msg_arrived)
        pthread_cond_wait(&wc->cond, &ipc->cond_lock);
    assert(out_size == wc->mbuf.size);
    pthread_mutex_unlock(&ipc->cond_lock);

    return 0;
}

int ipc_reply(struct ipc *ipc, int id, void *buf, int size) {
    int ret;
    pthread_mutex_lock(&ipc->fd_lock);
    ret = msg_write(ipc, id, buf, size);
    pthread_mutex_unlock(&ipc->fd_lock);
    return ret;
}


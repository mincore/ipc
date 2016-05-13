/* =====================================================================
 * Copyright (C) 2016 chen shuangping. All Rights Reserved
 *    Filename: test.c
 * Description:
 *     Created: Thu 12 May 2016 04:38:41 PM CST
 *      Author: csp@kuangzhitech.com
 * =====================================================================
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

#include "ipc.h"

struct A {
    int x, y;
};

struct B {
    int x, y, z;
};

#define ID_N 0
#define ID_A 1
#define ID_B 2

static sem_t gsema;
static sem_t gsemb;
static struct A *ga = NULL;
static struct B *gb = NULL;
static int gida;
static int gidb;

void* run_a(void *arg) {
    struct ipc *ipc = arg;

    sem_wait(&gsema);
    printf("wait for ga. done\n");

    {
        struct B b;
        struct A *a = ga;
        b.x = a->x*10;
        b.y = a->y*10;
        b.z = a->x + a->y;
        free(ga); ga = NULL;
        sleep(3);
        ipc_reply(ipc, gida, &b, sizeof(b));
    }

    return NULL;
}

void* run_b(void *arg) {
    struct ipc *ipc = arg;

    sem_wait(&gsemb);
    printf("wait for gb. done\n");

    {
        struct B *b = gb;
        struct A a;
        a.x = b->x/10;
        a.y = b->y/10;
        free(gb); gb = NULL;
        sleep(1);
        ipc_reply(ipc, gidb, &a, sizeof(a));
    }

    return NULL;
}

static int create_thread(void*(*func)(void *), void *arg) {
    pthread_t pthread;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    return pthread_create(&pthread, &attr, func, arg);
}

void on_reply(struct ipc *ipc, int id, void *buf, int size) {
    switch (id) {

    case ID_REPLY_CREATE:
        sem_init(&gsema, 0, 0);
        sem_init(&gsemb, 0, 0);
        create_thread(run_a, ipc);
        create_thread(run_b, ipc);
        ipc_reply(ipc, id, NULL, 0);
        break;

    case ID_REPLY_DESTROY:
        ipc_reply(ipc, id, NULL, 0);
        break;

    case ID_A:
        assert(size == sizeof(struct A));
        ga = malloc(sizeof(struct A));
        memcpy(ga, buf, sizeof(struct A));
        gida = id;
        sem_post(&gsema);
        break;

    case ID_B:
        assert(size == sizeof(struct B));
        gb = malloc(sizeof(struct B));
        memcpy(gb, buf, sizeof(struct B));
        gidb = id;
        sem_post(&gsemb);
        break;

    case ID_N:
        ipc_reply(ipc, id, NULL, 0);
        break;
    }
}

static void* call_a(void *arg) {
    struct ipc *ipc = arg;
    struct B b;
    struct A a = {1, 2};
    ipc_call(ipc, ID_A, &a, sizeof(a), &b, sizeof(b));
    assert(b.x == 10 && b.y == 20 && b.z == 3);
    printf("call_a done. b: (%d %d %d)\n", b.x, b.y, b.z);
    return NULL;
}

static void* call_b(void *arg) {
    struct ipc *ipc = arg;
    struct B b = {10, 20,};
    struct A a;
    ipc_call(ipc, ID_B, &b, sizeof(b), &a, sizeof(a));
    assert(a.x == 1 && a.y == 2);
    printf("call_b done. a: (%d %d)\n", a.x, a.y);
    return NULL;
}

int main(int argc, char *argv[])
{
    struct ipc *ipc;

    ipc = ipc_create(on_reply);

    create_thread(call_a, ipc);
    create_thread(call_b, ipc);

    ipc_call(ipc, ID_N, NULL, 0, NULL, 0);
    printf("ID_N done.\n");

    sleep(5);

    ipc_destroy(ipc);

    printf("test done.\n");

    sleep(1000);

    return 0;
}


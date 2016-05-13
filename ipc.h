/* =====================================================================
 * Copyright (C) 2016 chen shuangping. All Rights Reserved
 *    Filename: ipc.h
 * Description:
 *     Created: Wed 11 May 2016 04:46:14 PM CST
 *      Author: <csp> mincore@163.com
 * =====================================================================
 */
#ifndef _IPC_H
#define _IPC_H

#define ID_REPLY_CREATE  0xFFFFFFFE
#define ID_REPLY_DESTROY 0xFFFFFFFF

struct ipc;

typedef void (*ipc_cb)(struct ipc *ipc, int id, void *buf, int size);

struct ipc* ipc_create(ipc_cb cb);
void ipc_destroy(struct ipc *ipc);

int ipc_call(struct ipc *ipc, int id, void *in, int in_size, void *out, int out_size);
int ipc_reply(struct ipc *ipc, int id, void *buf, int size);

#endif

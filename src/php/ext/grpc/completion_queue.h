/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef GRPC_PHP_GRPC_COMPLETION_QUEUE_H_
#define GRPC_PHP_GRPC_COMPLETION_QUEUE_H_

#include <php.h>

#include <grpc/grpc.h>
#include <stdbool.h>

/* The global completion queue for all operations */
extern grpc_completion_queue *completion_queue;

/* The completion queue for client-async operations */
extern grpc_completion_queue *next_queue;
extern int pending_batches;
extern bool draining_next_queue;

/* Initializes the completion queue */
void grpc_php_init_completion_queue(TSRMLS_D);

/* Shut down the completion queue */
void grpc_php_shutdown_completion_queue(TSRMLS_D);

/* Initializes the next queue */
void grpc_php_init_next_queue(TSRMLS_D);

/* Drains the next queue */
bool grpc_php_drain_next_queue(bool shutdown, gpr_timespec deadline TSRMLS_DC);

/* Shut down the next queue */
void grpc_php_shutdown_next_queue(TSRMLS_D);


#endif /* GRPC_PHP_GRPC_COMPLETION_QUEUE_H_ */

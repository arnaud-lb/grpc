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

#include "completion_queue.h"
#include "batch.h"

#include <php.h>
#include <stdbool.h>

grpc_completion_queue *completion_queue;

grpc_completion_queue *next_queue;
int pending_batches;
bool draining_next_queue;

void grpc_php_init_completion_queue(TSRMLS_D) {
  completion_queue = grpc_completion_queue_create_for_pluck(NULL);
}

void grpc_php_shutdown_completion_queue(TSRMLS_D) {
  grpc_completion_queue_shutdown(completion_queue);
  grpc_completion_queue_destroy(completion_queue);
}

void grpc_php_init_next_queue(TSRMLS_D) {
  next_queue = grpc_completion_queue_create_for_next(NULL);
  pending_batches = 0;
  draining_next_queue = false;
}

bool grpc_php_drain_next_queue(bool shutdown, gpr_timespec deadline TSRMLS_DC) {
  grpc_event event;
  zval params[2];
  zval retval;
  if (draining_next_queue) {
    return true;
  }
  if (pending_batches == 0) {
    return false;
  }
  draining_next_queue = true;
  do {
    event = grpc_completion_queue_next(next_queue, deadline, NULL);
    if (event.type == GRPC_OP_COMPLETE) {
      struct batch *batch = (struct batch*) event.tag;

      if (!shutdown) {
        if (event.success) {
          ZVAL_NULL(&params[0]);
          batch_consume(batch, &params[1]);
          batch->fci.param_count = 2;
        } else {
          ZVAL_STRING(&params[0], "The async function encountered an error");
          batch->fci.param_count = 1;
        }
        batch->fci.params = params;
        batch->fci.retval = &retval;

        zend_call_function(&batch->fci, &batch->fcc);

        for (int i = 0; i < batch->fci.param_count; i++) {
          zval_dtor(&params[i]);
        }
        zval_dtor(&retval);
      }

      batch_destroy(batch);
      pending_batches--;
      if (pending_batches == 0) {
        break;
      }
    }
  } while (event.type != GRPC_QUEUE_TIMEOUT);
  draining_next_queue = false;

  return pending_batches > 0 ? true : false;
}

void grpc_php_shutdown_next_queue(TSRMLS_D) {
  while (grpc_php_drain_next_queue(true, gpr_inf_future(GPR_CLOCK_MONOTONIC) TSRMLS_CC));
  grpc_completion_queue_shutdown(next_queue);
  grpc_completion_queue_destroy(next_queue);
}

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

#include "call.h"
#include "batch.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <php.h>
#include <php_ini.h>
#include <ext/standard/info.h>
#include <ext/spl/spl_exceptions.h>
#include "php_grpc.h"
#include "call_credentials.h"

#include <zend_exceptions.h>
#include <zend_hash.h>

#include <stdbool.h>

#include <grpc/support/alloc.h>
#include <grpc/grpc.h>

#include "completion_queue.h"
#include "timeval.h"
#include "channel.h"
#include "byte_buffer.h"

zend_class_entry *grpc_ce_call;
#if PHP_MAJOR_VERSION >= 7
static zend_object_handlers call_ce_handlers;
#endif

/* Frees and destroys an instance of wrapped_grpc_call */
PHP_GRPC_FREE_WRAPPED_FUNC_START(wrapped_grpc_call)
  if (p->owned && p->wrapped != NULL) {
    grpc_call_unref(p->wrapped);
  }
PHP_GRPC_FREE_WRAPPED_FUNC_END()

/* Initializes an instance of wrapped_grpc_call to be associated with an
 * object of a class specified by class_type */
php_grpc_zend_object create_wrapped_grpc_call(zend_class_entry *class_type
                                              TSRMLS_DC) {
  PHP_GRPC_ALLOC_CLASS_OBJECT(wrapped_grpc_call);
  zend_object_std_init(&intern->std, class_type TSRMLS_CC);
  object_properties_init(&intern->std, class_type);
  PHP_GRPC_FREE_CLASS_OBJECT(wrapped_grpc_call, call_ce_handlers);
}

/* Wraps a grpc_call struct in a PHP object. Owned indicates whether the
   struct should be destroyed at the end of the object's lifecycle */
zval *grpc_php_wrap_call(grpc_call *wrapped, bool owned TSRMLS_DC) {
  zval *call_object;
  PHP_GRPC_MAKE_STD_ZVAL(call_object);
  object_init_ex(call_object, grpc_ce_call);
  wrapped_grpc_call *call = Z_WRAPPED_GRPC_CALL_P(call_object);
  call->wrapped = wrapped;
  call->owned = owned;
  return call_object;
}

/**
 * Constructs a new instance of the Call class.
 * @param Channel $channel_obj The channel to associate the call with.
 *                             Must not be closed.
 * @param string $method The method to call
 * @param Timeval $deadline_obj The deadline for completing the call
 * @param string $host_override The host is set by user (optional)
 */
PHP_METHOD(Call, __construct) {
  zval *channel_obj;
  char *method;
  php_grpc_int method_len;
  zval *deadline_obj;
  char *host_override = NULL;
  php_grpc_int host_override_len = 0;
  wrapped_grpc_call *call = Z_WRAPPED_GRPC_CALL_P(getThis());
  zend_bool client_async = 0;
  grpc_completion_queue *queue;

  /* "OsO|s" == 1 Object, 1 string, 1 Object, 1 optional string */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "OsO|sb", &channel_obj,
                            grpc_ce_channel, &method, &method_len,
                            &deadline_obj, grpc_ce_timeval, &host_override,
                            &host_override_len, &client_async) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "Call expects a Channel, a String, a Timeval, "
                         "an optional String, and an optional Boolean",
                         1 TSRMLS_CC);
    return;
  }
  wrapped_grpc_channel *channel = Z_WRAPPED_GRPC_CHANNEL_P(channel_obj);
  gpr_mu_lock(&channel->wrapper->mu);
  if (channel->wrapper->wrapped == NULL) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "Call cannot be constructed from a closed Channel",
                         1 TSRMLS_CC);
    gpr_mu_unlock(&channel->wrapper->mu);
    return;
  }
  add_property_zval(getThis(), "channel", channel_obj);
  wrapped_grpc_timeval *deadline = Z_WRAPPED_GRPC_TIMEVAL_P(deadline_obj);
  grpc_slice method_slice = grpc_slice_from_copied_string(method);
  grpc_slice host_slice = host_override != NULL ?
      grpc_slice_from_copied_string(host_override) : grpc_empty_slice();
  queue = client_async ? next_queue : completion_queue;
  call->wrapped =
    grpc_channel_create_call(channel->wrapper->wrapped, NULL,
                             GRPC_PROPAGATE_DEFAULTS,
                             queue, method_slice,
                             host_override != NULL ? &host_slice : NULL,
                             deadline->wrapped, NULL);
  grpc_slice_unref(method_slice);
  grpc_slice_unref(host_slice);
  call->owned = true;
  call->client_async = client_async ? true : false;
  gpr_mu_unlock(&channel->wrapper->mu);
}

PHP_METHOD(Call, drainCompletionQueue) {
  zend_long timeout;
  gpr_timespec deadline;

  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "l", &timeout) ==
      FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "drainCompletionQueue expects an Int",
                         1 TSRMLS_CC);
    return;
  }

  deadline = gpr_time_add(gpr_now(GPR_CLOCK_MONOTONIC), gpr_time_from_micros(timeout, GPR_TIMESPAN));
  RETURN_BOOL(grpc_php_drain_next_queue(false, deadline TSRMLS_CC) ? 1 : 0);
}

/**
 * Start a batch of RPC actions.
 * @param array $array Array of actions to take
 * @return object Object with results of all actions
 */
PHP_METHOD(Call, startBatch) {
  php_grpc_ulong index;
  zval *value;
  zval *inner_value;
  zval *message_value;
  zval *message_flags;
  grpc_call_error error;
  wrapped_grpc_call *call = Z_WRAPPED_GRPC_CALL_P(getThis());
  
  zval *array;
  HashTable *array_hash;
  HashTable *status_hash;
  HashTable *message_hash;

  zend_fcall_info fci;
  zend_fcall_info_cache fcc;
  struct batch *batch = NULL;

  /* "a" == 1 array */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "a|f", &array, &fci, &fcc) ==
      FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "startBatch expects an array and an optional callable",
                         1 TSRMLS_CC);
    goto cleanup;
  }

  if (call->client_async && ZEND_NUM_ARGS() < 2) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "startBatch requires a callable argument when "
                         "constructed with client_async=true", 1 TSRMLS_CC);
    goto cleanup;
  }

  batch = batch_new(call->wrapped);

  if (ZEND_NUM_ARGS() > 1) {
    Z_ADDREF(fci.function_name);
    batch->fci = fci;
    batch->fcc = fcc;
  }

  array_hash = Z_ARRVAL_P(array);

  char *key = NULL;
  int key_type;
  PHP_GRPC_HASH_FOREACH_LONG_KEY_VAL_START(array_hash, key, key_type, index,
                                           value)
    if (key_type != HASH_KEY_IS_LONG || key != NULL) {
      zend_throw_exception(spl_ce_InvalidArgumentException,
                           "batch keys must be integers", 1 TSRMLS_CC);
      goto cleanup;
    }
    switch(index) {
    case GRPC_OP_SEND_INITIAL_METADATA:
      if (!create_metadata_array(value, &batch->metadata)) {
        zend_throw_exception(spl_ce_InvalidArgumentException,
                             "Bad metadata value given", 1 TSRMLS_CC);
        goto cleanup;
      }
      batch->ops[batch->op_num].data.send_initial_metadata.count = batch->metadata.count;
      batch->ops[batch->op_num].data.send_initial_metadata.metadata = batch->metadata.metadata;
      break;
    case GRPC_OP_SEND_MESSAGE:
      if (Z_TYPE_P(value) != IS_ARRAY) {
        zend_throw_exception(spl_ce_InvalidArgumentException,
                             "Expected an array for send message",
                             1 TSRMLS_CC);
        goto cleanup;
      }
      message_hash = Z_ARRVAL_P(value);
      if (php_grpc_zend_hash_find(message_hash, "flags", sizeof("flags"),
                         (void **)&message_flags) == SUCCESS) {
        if (Z_TYPE_P(message_flags) != IS_LONG) {
          zend_throw_exception(spl_ce_InvalidArgumentException,
                               "Expected an int for message flags",
                               1 TSRMLS_CC);
        }
        batch->ops[batch->op_num].flags = Z_LVAL_P(message_flags) & GRPC_WRITE_USED_MASK;
      }
      if (php_grpc_zend_hash_find(message_hash, "message", sizeof("message"),
                         (void **)&message_value) != SUCCESS ||
          Z_TYPE_P(message_value) != IS_STRING) {
        zend_throw_exception(spl_ce_InvalidArgumentException,
                             "Expected a string for send message",
                             1 TSRMLS_CC);
        goto cleanup;
      }
      batch->ops[batch->op_num].data.send_message.send_message =
          string_to_byte_buffer(Z_STRVAL_P(message_value),
                                Z_STRLEN_P(message_value));
      break;
    case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
      break;
    case GRPC_OP_SEND_STATUS_FROM_SERVER:
      status_hash = Z_ARRVAL_P(value);
      if (php_grpc_zend_hash_find(status_hash, "metadata", sizeof("metadata"),
                         (void **)&inner_value) == SUCCESS) {
        if (!create_metadata_array(inner_value, &batch->trailing_metadata)) {
          zend_throw_exception(spl_ce_InvalidArgumentException,
                               "Bad trailing metadata value given",
                               1 TSRMLS_CC);
          goto cleanup;
        }
        batch->ops[batch->op_num].data.send_status_from_server.trailing_metadata =
            batch->trailing_metadata.metadata;
        batch->ops[batch->op_num].data.send_status_from_server.trailing_metadata_count =
            batch->trailing_metadata.count;
      }
      if (php_grpc_zend_hash_find(status_hash, "code", sizeof("code"),
                         (void**)&inner_value) == SUCCESS) {
        if (Z_TYPE_P(inner_value) != IS_LONG) {
          zend_throw_exception(spl_ce_InvalidArgumentException,
                               "Status code must be an integer",
                               1 TSRMLS_CC);
          goto cleanup;
        }
        batch->ops[batch->op_num].data.send_status_from_server.status =
            Z_LVAL_P(inner_value);
      } else {
        zend_throw_exception(spl_ce_InvalidArgumentException,
                             "Integer status code is required",
                             1 TSRMLS_CC);
        goto cleanup;
      }
      if (php_grpc_zend_hash_find(status_hash, "details", sizeof("details"),
                         (void**)&inner_value) == SUCCESS) {
        if (Z_TYPE_P(inner_value) != IS_STRING) {
          zend_throw_exception(spl_ce_InvalidArgumentException,
                               "Status details must be a string",
                               1 TSRMLS_CC);
          goto cleanup;
        }
        batch->send_status_details = grpc_slice_from_copied_string(
          Z_STRVAL_P(inner_value));
        batch->ops[batch->op_num].data.send_status_from_server.status_details =
          &batch->send_status_details;
      } else {
        zend_throw_exception(spl_ce_InvalidArgumentException,
                             "String status details is required",
                             1 TSRMLS_CC);
        goto cleanup;
      }
      break;
    case GRPC_OP_RECV_INITIAL_METADATA:
      batch->ops[batch->op_num].data.recv_initial_metadata.recv_initial_metadata =
          &batch->recv_metadata;
      break;
    case GRPC_OP_RECV_MESSAGE:
      batch->ops[batch->op_num].data.recv_message.recv_message = &batch->message;
      break;
    case GRPC_OP_RECV_STATUS_ON_CLIENT:
      batch->ops[batch->op_num].data.recv_status_on_client.trailing_metadata =
          &batch->recv_trailing_metadata;
      batch->ops[batch->op_num].data.recv_status_on_client.status = &batch->status;
      batch->ops[batch->op_num].data.recv_status_on_client.status_details =
          &batch->recv_status_details;
      break;
    case GRPC_OP_RECV_CLOSE_ON_SERVER:
      batch->ops[batch->op_num].data.recv_close_on_server.cancelled =
          &batch->cancelled;
      break;
    default:
      zend_throw_exception(spl_ce_InvalidArgumentException,
                           "Unrecognized key in batch", 1 TSRMLS_CC);
      goto cleanup;
    }
    batch->ops[batch->op_num].op = (grpc_op_type)index;
    batch->ops[batch->op_num].flags = 0;
    batch->ops[batch->op_num].reserved = NULL;
    batch->op_num++;
  PHP_GRPC_HASH_FOREACH_END()

  error = grpc_call_start_batch(call->wrapped, batch->ops, batch->op_num, batch,
                                NULL);
  if (error != GRPC_CALL_OK) {
    zend_throw_exception(spl_ce_LogicException,
                         "start_batch was called incorrectly",
                         (long)error TSRMLS_CC);
    goto cleanup;
  }

  if (!call->client_async) {
    grpc_completion_queue_pluck(completion_queue, batch,
                                gpr_inf_future(GPR_CLOCK_REALTIME), NULL);
    batch_consume(batch, return_value);
  } else {
    pending_batches++;
    batch = NULL;
  }

cleanup:
  batch_destroy(batch);
}

/**
 * Get the endpoint this call/stream is connected to
 * @return string The URI of the endpoint
 */
PHP_METHOD(Call, getPeer) {
  wrapped_grpc_call *call = Z_WRAPPED_GRPC_CALL_P(getThis());
  PHP_GRPC_RETURN_STRING(grpc_call_get_peer(call->wrapped), 1);
}

/**
 * Cancel the call. This will cause the call to end with STATUS_CANCELLED
 * if it has not already ended with another status.
 * @return void
 */
PHP_METHOD(Call, cancel) {
  wrapped_grpc_call *call = Z_WRAPPED_GRPC_CALL_P(getThis());
  grpc_call_cancel(call->wrapped, NULL);
}

/**
 * Set the CallCredentials for this call.
 * @param CallCredentials $creds_obj The CallCredentials object
 * @return int The error code
 */
PHP_METHOD(Call, setCredentials) {
  zval *creds_obj;

  /* "O" == 1 Object */
  if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "O", &creds_obj,
                            grpc_ce_call_credentials) == FAILURE) {
    zend_throw_exception(spl_ce_InvalidArgumentException,
                         "setCredentials expects 1 CallCredentials",
                         1 TSRMLS_CC);
    return;
  }

  wrapped_grpc_call_credentials *creds =
    Z_WRAPPED_GRPC_CALL_CREDS_P(creds_obj);
  wrapped_grpc_call *call = Z_WRAPPED_GRPC_CALL_P(getThis());

  grpc_call_error error = GRPC_CALL_ERROR;
  error = grpc_call_set_credentials(call->wrapped, creds->wrapped);
  RETURN_LONG(error);
}

ZEND_BEGIN_ARG_INFO_EX(arginfo_construct, 0, 0, 3)
  ZEND_ARG_INFO(0, channel)
  ZEND_ARG_INFO(0, method)
  ZEND_ARG_INFO(0, deadline)
  ZEND_ARG_INFO(0, host_override)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_startBatch, 0, 0, 1)
  ZEND_ARG_INFO(0, ops)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_drainCompletionQueue, 0, 0, 1)
  ZEND_ARG_INFO(0, timeout_micros)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_getPeer, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_cancel, 0, 0, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_setCredentials, 0, 0, 1)
  ZEND_ARG_INFO(0, credentials)
ZEND_END_ARG_INFO()

static zend_function_entry call_methods[] = {
  PHP_ME(Call, drainCompletionQueue, arginfo_drainCompletionQueue, ZEND_ACC_STATIC | ZEND_ACC_PUBLIC)
  PHP_ME(Call, __construct, arginfo_construct, ZEND_ACC_PUBLIC | ZEND_ACC_CTOR)
  PHP_ME(Call, startBatch, arginfo_startBatch, ZEND_ACC_PUBLIC)
  PHP_ME(Call, getPeer, arginfo_getPeer, ZEND_ACC_PUBLIC)
  PHP_ME(Call, cancel, arginfo_cancel, ZEND_ACC_PUBLIC)
  PHP_ME(Call, setCredentials, arginfo_setCredentials, ZEND_ACC_PUBLIC)
  PHP_FE_END
};

void grpc_init_call(TSRMLS_D) {
  zend_class_entry ce;
  INIT_CLASS_ENTRY(ce, "Grpc\\Call", call_methods);
  ce.create_object = create_wrapped_grpc_call;
  grpc_ce_call = zend_register_internal_class(&ce TSRMLS_CC);
  PHP_GRPC_INIT_HANDLER(wrapped_grpc_call, call_ce_handlers);
}

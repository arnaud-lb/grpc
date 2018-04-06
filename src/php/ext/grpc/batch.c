/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "batch.h"
#include "php_grpc.h"
#include "metadata_array.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <php.h>
#include <zend_API.h>
#include <zend_exceptions.h>
#include <stdbool.h>
#include <grpc/support/alloc.h>
#include <grpc/grpc.h>

#include "byte_buffer.h"

struct batch* batch_new(grpc_call *wrapped) {
  struct batch* batch = ecalloc(1, sizeof(*batch));
  batch->wrapped = wrapped;
  grpc_metadata_array_init(&batch->metadata);
  grpc_metadata_array_init(&batch->trailing_metadata);
  grpc_metadata_array_init(&batch->recv_metadata);
  grpc_metadata_array_init(&batch->recv_trailing_metadata);
  batch->recv_status_details = grpc_empty_slice();
  batch->send_status_details = grpc_empty_slice();
  return batch;
}

void batch_destroy(struct batch* batch) {
  if (batch == NULL) {
    return;
  }
  grpc_php_metadata_array_destroy_including_entries(&batch->metadata);
  grpc_php_metadata_array_destroy_including_entries(&batch->trailing_metadata);
  grpc_metadata_array_destroy(&batch->recv_metadata);
  grpc_metadata_array_destroy(&batch->recv_trailing_metadata);
  grpc_slice_unref(batch->recv_status_details);
  grpc_slice_unref(batch->send_status_details);
  zval_dtor(&batch->fci.function_name);
  for (int i = 0; i < batch->op_num; i++) {
    if (batch->ops[i].op == GRPC_OP_SEND_MESSAGE) {
      grpc_byte_buffer_destroy(batch->ops[i].data.send_message.send_message);
    }
    if (batch->ops[i].op == GRPC_OP_RECV_MESSAGE) {
      grpc_byte_buffer_destroy(batch->message);
    }
  }
  efree(batch);
}

void batch_consume(struct batch* batch, zval *result) {
  char *message_str;
  size_t message_len;
  zval *recv_status;
#if PHP_MAJOR_VERSION >= 7
  zval *recv_md;
#endif

  object_init(result);

  for (int i = 0; i < batch->op_num; i++) {
    switch(batch->ops[i].op) {
    case GRPC_OP_SEND_INITIAL_METADATA:
      add_property_bool(result, "send_metadata", true);
      break;
    case GRPC_OP_SEND_MESSAGE:
      add_property_bool(result, "send_message", true);
      break;
    case GRPC_OP_SEND_CLOSE_FROM_CLIENT:
      add_property_bool(result, "send_close", true);
      break;
    case GRPC_OP_SEND_STATUS_FROM_SERVER:
      add_property_bool(result, "send_status", true);
      break;
    case GRPC_OP_RECV_INITIAL_METADATA:
#if PHP_MAJOR_VERSION < 7
      array = grpc_parse_metadata_array(&batch->recv_metadata TSRMLS_CC);
      add_property_zval(result, "metadata", array);
#else
      recv_md = grpc_parse_metadata_array(&batch->recv_metadata);
      add_property_zval(result, "metadata", recv_md);
      zval_ptr_dtor(recv_md);
      PHP_GRPC_FREE_STD_ZVAL(recv_md);
#endif
      PHP_GRPC_DELREF(array);
      break;
    case GRPC_OP_RECV_MESSAGE:
      byte_buffer_to_string(batch->message, &message_str, &message_len);
      if (message_str == NULL) {
        add_property_null(result, "message");
      } else {
        php_grpc_add_property_stringl(result, "message", message_str,
                                      message_len, false);
        PHP_GRPC_FREE_STD_ZVAL(message_str);
      }
      break;
    case GRPC_OP_RECV_STATUS_ON_CLIENT:
      PHP_GRPC_MAKE_STD_ZVAL(recv_status);
      object_init(recv_status);
#if PHP_MAJOR_VERSION < 7
      array = grpc_parse_metadata_array(&batch->recv_trailing_metadata TSRMLS_CC);
      add_property_zval(recv_status, "metadata", array);
#else
      recv_md = grpc_parse_metadata_array(&batch->recv_trailing_metadata);
      add_property_zval(recv_status, "metadata", recv_md);
      zval_ptr_dtor(recv_md);
      PHP_GRPC_FREE_STD_ZVAL(recv_md);
#endif
      PHP_GRPC_DELREF(array);
      add_property_long(recv_status, "code", batch->status);
      char *status_details_text = grpc_slice_to_c_string(batch->recv_status_details);
      php_grpc_add_property_string(recv_status, "details", status_details_text,
                                   true);
      gpr_free(status_details_text);
      add_property_zval(result, "status", recv_status);
#if PHP_MAJOR_VERSION >= 7
      zval_ptr_dtor(recv_status);
#endif
      PHP_GRPC_DELREF(recv_status);
      PHP_GRPC_FREE_STD_ZVAL(recv_status);
      break;
    case GRPC_OP_RECV_CLOSE_ON_SERVER:
      add_property_bool(result, "cancelled", batch->cancelled);
      break;
    default:
      break;
    }
  }
}

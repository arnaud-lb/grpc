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

#ifndef NET_GRPC_PHP_GRPC_BATCH_H_
#define NET_GRPC_PHP_GRPC_BATCH_H_

#include <grpc/grpc.h>
#include <zend_API.h>

struct batch {
    grpc_call *wrapped;
    zend_fcall_info fci;
    zend_fcall_info_cache fcc;

    grpc_op ops[8];
    size_t op_num;

    grpc_metadata_array metadata;
    grpc_metadata_array trailing_metadata;
    grpc_metadata_array recv_metadata;
    grpc_metadata_array recv_trailing_metadata;
    grpc_status_code status;
    grpc_slice recv_status_details;
    grpc_slice send_status_details;
    grpc_byte_buffer *message;
    int cancelled;
};

struct batch* batch_new(grpc_call *wrapped);
void batch_consume(struct batch* batch, zval *result);
void batch_destroy(struct batch* batch);

#endif /* NET_GRPC_PHP_GRPC_BATCH_H_ */

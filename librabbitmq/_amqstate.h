#ifndef __PYRMQ_AMQSTATE_H__
#define __PYRMQ_AMQSTATE_H__

#include <amqp.h>

typedef enum amqp_connection_state_enum_ {
  CONNECTION_STATE_IDLE = 0,
  CONNECTION_STATE_INITIAL,
  CONNECTION_STATE_HEADER,
  CONNECTION_STATE_BODY
} amqp_connection_state_enum;

typedef struct amqp_link_t_ {
  struct amqp_link_t_ *next;
  void *data;
} amqp_link_t;

struct amqp_connection_state_t_ {
  amqp_pool_t frame_pool;
  amqp_pool_t decoding_pool;

  amqp_connection_state_enum state;

  int channel_max;
  int frame_max;
  int heartbeat;
  amqp_bytes_t inbound_buffer;

  size_t inbound_offset;
  size_t target_size;

  amqp_bytes_t outbound_buffer;

  int sockfd;
  amqp_bytes_t sock_inbound_buffer;
  size_t sock_inbound_offset;
  size_t sock_inbound_limit;

  amqp_link_t *first_queued_frame;
  amqp_link_t *last_queued_frame;

  amqp_rpc_reply_t most_recent_api_result;
};

#endif /* __PYRMQ_AMQSTATE_H__ */

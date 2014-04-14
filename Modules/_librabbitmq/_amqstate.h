#ifndef __PYRMQ_AMQSTATE_H__
#define __PYRMQ_AMQSTATE_H__

#include <amqp.h>

/* 7 bytes up front, then payload, then 1 byte footer */
#define HEADER_SIZE 7
#define FOOTER_SIZE 1
#define POOL_TABLE_SIZE 16

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

typedef struct amqp_pool_table_entry_t_ {
  struct amqp_pool_table_entry_t_ *next;
  amqp_pool_t pool;
  amqp_channel_t channel;
} amqp_pool_table_entry_t;

struct amqp_connection_state_t_ {
  amqp_pool_table_entry_t *pool_table[POOL_TABLE_SIZE];

  amqp_connection_state_enum state;

  int channel_max;
  int frame_max;
  int heartbeat;

  /* buffer for holding frame headers.  Allows us to delay allocating
   * the raw frame buffer until the type, channel, and size are all known
   */
  char header_buffer[HEADER_SIZE + 1];
  amqp_bytes_t inbound_buffer;

  size_t inbound_offset;
  size_t target_size;

  amqp_bytes_t outbound_buffer;

  amqp_socket_t *socket;

  amqp_bytes_t sock_inbound_buffer;
  size_t sock_inbound_offset;
  size_t sock_inbound_limit;

  amqp_link_t *first_queued_frame;
  amqp_link_t *last_queued_frame;

  amqp_rpc_reply_t most_recent_api_result;

  uint64_t next_recv_heartbeat;
  uint64_t next_send_heartbeat;

  amqp_table_t server_properties;
  amqp_pool_t properties_pool;
};


amqp_pool_t *amqp_get_or_create_channel_pool(amqp_connection_state_t, amqp_channel_t);

#endif /* __PYRMQ_AMQSTATE_H__ */

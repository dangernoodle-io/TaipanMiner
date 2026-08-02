# tm_stratum -- protocol FSM (bb_fsm over bb_tcp_client, via a thin transport
# ops seam), the pure JSON-RPC builders/parsers (bb_serialize_json tok
# recorder), coinbase/header/work building, and the producer snapshot surface
# (bb_serialize descriptor). See include/stratum.h.
#
# REQUIRES:
#   - bb_fsm            -- bb_fsm_desc_t/bb_fsm_row_t appear in stratum_fsm.h
#   - bb_serialize       -- bb_serialize_desc_t appears in stratum_producer.h
#   - bb_serialize_json  -- bb_serialize_json_tok_recorder_t/tok_idx_t appear
#                            in stratum_machine.h (the response-handler
#                            signatures)
#   - bb_core            -- bb_err_t appears in stratum_producer.h
#   - tm_mining          -- mining_work_t (work.h) / mining_result_t (mining.h)
#                            appear in work_build.h.
#   - tm_pool_client     -- tm_pool_client_ops_t (stratum_pool_client.h, the
#                            V1 pool-client adapter's public getter) and
#                            tm_pool_work_ops_t (stratum_fsm_cfg_t.work) both
#                            live there now -- the cross-core work/result
#                            seam (formerly this component's own
#                            stratum_work_seam.h) was relocated to
#                            tm_pool_client (TA-571 firmware review finding
#                            #1: it is protocol-agnostic, built entirely from
#                            tm_mining's own types, so it belongs to the
#                            abstraction that owns the cross-core seam, not
#                            to this one protocol backend). The dependency
#                            with tm_pool_client is now ONE-DIRECTIONAL:
#                            tm_pool_client has no REQUIRES on tm_stratum at
#                            all (see its own CMakeLists.txt).
#
# PRIV_REQUIRES:
#   - bb_tcp_client -- referenced solely by stratum_transport_esp.c (an
#     ESP_PLATFORM-guarded .c file), never by any public header.
#   - bb_str -- bb_str_hex_to_bytes/bb_str_bytes_to_hex, used only from
#     work_build.c and stratum_machine.c, never a public header.
#   - bb_log -- bb_log_w, used only from stratum_transport_esp.c (an
#     ESP_PLATFORM-guarded .c file), never a public header.
idf_component_register(
    SRCS
        "src/stratum_backoff.c"
        "src/stratum_machine.c"
        "src/stratum_reqid.c"
        "src/stratum_fsm.c"
        "src/stratum_pool_client.c"
        "src/stratum_producer.c"
        "src/stratum_transport_esp.c"
        "src/work_build.c"
    INCLUDE_DIRS "include"
    REQUIRES bb_fsm bb_serialize bb_serialize_json bb_core tm_mining tm_pool_client
    PRIV_REQUIRES bb_tcp_client bb_str bb_log
)

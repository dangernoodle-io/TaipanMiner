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
#   - tm_mining          -- mining_work_t (work.h) appears in work_build.h;
#                            mining_result_t (mining.h) appears in
#                            stratum_work_seam.h. No cycle: tm_mining never
#                            references tm_stratum.
#
# PRIV_REQUIRES:
#   - bb_tcp_client -- referenced solely by stratum_transport_esp.c (an
#     ESP_PLATFORM-guarded .c file), never by any public header.
#   - bb_str -- bb_str_hex_to_bytes/bb_str_bytes_to_hex, used only from
#     work_build.c and stratum_machine.c, never a public header.
idf_component_register(
    SRCS
        "src/stratum_backoff.c"
        "src/stratum_machine.c"
        "src/stratum_reqid.c"
        "src/stratum_fsm.c"
        "src/stratum_producer.c"
        "src/stratum_transport_esp.c"
        "src/work_build.c"
    INCLUDE_DIRS "include"
    REQUIRES bb_fsm bb_serialize bb_serialize_json bb_core tm_mining
    PRIV_REQUIRES bb_tcp_client bb_str
)

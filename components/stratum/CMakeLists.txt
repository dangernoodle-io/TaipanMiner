# stratum -- protocol FSM (bb_fsm over bb_tcp_client, via a thin transport
# ops seam), the pure JSON-RPC builders/parsers, and the producer snapshot
# surface (bb_serialize descriptor). See include/stratum.h.
#
# REQUIRES bb_fsm (bb_fsm_desc_t/bb_fsm_row_t appear in stratum_fsm.h) and
# bb_serialize (bb_serialize_desc_t appears in stratum_snap.h) -- both
# public-header deps. bb_json appears in stratum_utils.h/stratum_machine.h
# (bb_json_t is the wire-parsing type these pure modules operate on) --
# also REQUIRES, not PRIV_REQUIRES, for the same reason.
#
# bb_tcp_client is PRIV_REQUIRES-only: it is referenced solely by
# stratum_transport_esp.c (an ESP_PLATFORM-guarded .c file), never by any
# public header -- consumers of stratum.h never need to see bb_tcp_client's
# types.
idf_component_register(
    SRCS
        "src/stratum_backoff.c"
        "src/stratum_watchdogs.c"
        "src/stratum_utils.c"
        "src/stratum_machine.c"
        "src/stratum_reqid.c"
        "src/stratum_fsm.c"
        "src/stratum_snap.c"
        "src/stratum_transport_esp.c"
    INCLUDE_DIRS "include"
    REQUIRES bb_fsm bb_serialize bb_json
    PRIV_REQUIRES bb_tcp_client
)

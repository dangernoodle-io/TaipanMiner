idf_component_register(
    SRCS "src/work_build.c"
    INCLUDE_DIRS "include"
    # Private: only referenced from work_build.c, never a public header.
    # tm_mining -- sha256d() (sha256.h). bb_core -- bb_store_le32/be32
    # (bb_byte_order.h). bb_str -- bb_str_hex_to_bytes() (bb_str.h).
    PRIV_REQUIRES tm_mining bb_core bb_str
)

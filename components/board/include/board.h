#pragma once

#if defined(BOARD_TDONGLE_S3)
#  include "boards/tdongle_s3.h"
#elif defined(BOARD_BITAXE_601)
#  include "boards/bitaxe_601.h"
#elif defined(BOARD_BITAXE_403)
#  include "boards/bitaxe_403.h"
#else
#  error "Unknown board — add -DBOARD_xxx to build_flags"
#endif

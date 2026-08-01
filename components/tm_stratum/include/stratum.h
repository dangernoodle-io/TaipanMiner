#pragma once

// Public component surface: the FSM (states/service loop) and the producer
// snapshot (gather/descriptor). See stratum_fsm.h and stratum_producer.h for
// the actual declarations -- this header exists purely as the single include
// a consumer (the composition root) reaches for.
#include "stratum_fsm.h"
#include "stratum_producer.h"

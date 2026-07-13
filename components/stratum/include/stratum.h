#pragma once

// Public component surface: the FSM (states/service loop) and the producer
// snapshot (gather/descriptor). See stratum_fsm.h and stratum_snap.h for the
// actual declarations -- this header exists purely as the single include a
// consumer (main.c) reaches for.
#include "stratum_fsm.h"
#include "stratum_snap.h"

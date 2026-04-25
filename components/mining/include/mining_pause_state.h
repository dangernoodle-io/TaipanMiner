#pragma once

#include <stdbool.h>

// Pure state model for mining pause/resume coordination. The caller (OTA) sets
// pause_requested and waits on an ack signal. The mining task observes
// pause_requested via on_check() (which sets pause_active and returns true so
// the task parks itself), then waits on a resume signal. on_resume() clears
// pause_requested; on_resumed() (called by the task after the resume signal
// fires) clears pause_active.
//
// The signaling primitives (semaphores in production, or test stubs) live
// outside this module.

typedef struct {
    bool pause_requested;
    bool pause_active;
} mining_pause_state_t;

// Initialize the pause state to both flags false.
void mining_pause_state_init(mining_pause_state_t *s);

// External caller: mark a pause request. Sets pause_requested=true.
void mining_pause_state_request(mining_pause_state_t *s);

// Mining task entering its check site. Returns true if a pause was requested
// (task should park itself); sets pause_active=true. Returns false otherwise
// with no state change.
bool mining_pause_state_on_check(mining_pause_state_t *s);

// External caller: resume. Returns true if the mining task is currently
// parked (pause_active) — caller should signal the resume primitive. In
// either case, clears pause_requested.
bool mining_pause_state_on_resume(mining_pause_state_t *s);

// Mining task: observed the resume signal, now exiting its park. Clears
// pause_active.
void mining_pause_state_on_resumed(mining_pause_state_t *s);

// External caller: ack-timeout path. Pause request never observed by the
// task; abort the pause and clear pause_requested.
void mining_pause_state_on_ack_timeout(mining_pause_state_t *s);

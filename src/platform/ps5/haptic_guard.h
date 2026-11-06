// SPDX-License-Identifier: GPL-3.0-only
// Runs a call into the haptic channel so that a fault in it ends the call, not the host, while its format is being found out
// (docs/re/haptics.md). Only one thread may use it.
#ifndef PROSPERO_HAPTIC_GUARD_H
#define PROSPERO_HAPTIC_GUARD_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define PS_HAPTIC_FAULT (-1000)
typedef struct {
    int signal;
    uint64_t address, instruction;
} ps_haptic_fault;
// fn(arg)'s result, or PS_HAPTIC_FAULT with fault filled in.
int ps_haptic_guarded(int (*fn)(void *), void *arg, ps_haptic_fault *fault);
// Writes size bytes of this process's memory at address to path, for reading the system library's code: its pages can't be read
// directly. Parts that can't be read stay zero. 0 or error.
int ps_haptic_dump_memory(const char *path, uintptr_t address, size_t size);
#ifdef __cplusplus
}
#endif
#endif

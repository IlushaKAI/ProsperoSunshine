// SPDX-License-Identifier: GPL-3.0-only
#include "haptic_guard.h"
#include <ps5/mdbg.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

static const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
static struct sigaction previous[sizeof signals / sizeof *signals];
static sigjmp_buf resume;
static volatile int guarding;
static pthread_t guarded;
static ps_haptic_fault last;

static void on_fault(int number, siginfo_t *info, void *context)
{
    if (guarding && pthread_equal(pthread_self(), guarded)) {
        guarding = 0;
        last.signal = number;
        last.address = (uint64_t)(uintptr_t)info->si_addr;
        last.instruction = (uint64_t)((ucontext_t *)context)->uc_mcontext.mc_rip;
        siglongjmp(resume, 1);
    }
    // Not ours: back to what was there, and the same instruction faults again.
    for (size_t i = 0; i < sizeof signals / sizeof *signals; i++)
        if (signals[i] == number) sigaction(number, &previous[i], NULL);
}

int ps_haptic_guarded(int (*fn)(void *), void *arg, ps_haptic_fault *fault)
{
    static int installed;
    if (!installed) {
        struct sigaction action;
        memset(&action, 0, sizeof action);
        action.sa_sigaction = on_fault;
        action.sa_flags = SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        for (size_t i = 0; i < sizeof signals / sizeof *signals; i++) sigaction(signals[i], &action, &previous[i]);
        installed = 1;
    }
    guarded = pthread_self();
    if (sigsetjmp(resume, 1)) {
        if (fault) *fault = last;
        return PS_HAPTIC_FAULT;
    }
    guarding = 1;
    int rc = fn(arg);
    guarding = 0;
    return rc;
}

int ps_haptic_dump_memory(const char *path, uintptr_t address, size_t size)
{
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    static uint8_t page[0x1000];
    for (size_t done = 0; done < size; done += sizeof page) {
        memset(page, 0, sizeof page);
        mdbg_copyout(getpid(), (intptr_t)(address + done), page, sizeof page);
        fwrite(page, 1, sizeof page, file);
    }
    fclose(file);
    return 0;
}

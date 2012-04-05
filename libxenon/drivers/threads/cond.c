/*
 * File:   cond.c
 * Author: cc
 *
 * Created on 4 mars 2012, 16:48
 * Based on http://hg.libsdl.org/SDL/file/fba40d9f4a73/src/thread/generic/SDL_syscond.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mutex.h"
#include "cond.h"



#define MAX_SEMAPHORE 32*1024
#define LOCK_INFINITE -1

COND * cond_create() {
    COND * c = (COND*) malloc(sizeof (COND));
    memset(c, 0, sizeof (COND));
    c->lock = mutex_create(1);
    c->wait_sem = mutex_create(MAX_SEMAPHORE);
    c->wait_done = mutex_create(MAX_SEMAPHORE);
    c->waiting = c->signals = 0;
    if (!c->lock || !c->wait_sem || !c->wait_done) {
        cond_delete(c);
        c = NULL;
    }
    return c;
};

void cond_wait(COND * c, MUTEX *m) {
    if(c&&m){
        // we are waiting
        mutex_acquire(c->lock, LOCK_INFINITE);
        c->waiting++;
        mutex_release(c->lock);

        // release mutex aquired by the caller
        mutex_release(m);

        // wait for semaphore
        mutex_acquire(c->wait_sem, LOCK_INFINITE);

        // finished to wait, now we said that signal is ready
        mutex_acquire(c->lock, LOCK_INFINITE);
        if (c->signals > 0) {

            // we eat 1 signal
            mutex_release(c->wait_done);

            c->signals--;
        }
        c->waiting--;
        mutex_release(c->lock);

        // relock the mutex of the caller
        mutex_acquire(m, LOCK_INFINITE);
    }
};

void cond_signal(COND * c) {
    if(c){
        mutex_acquire(c->lock, LOCK_INFINITE);

        if (c->waiting > c->signals) {
            c->signals++;
            mutex_release(c->wait_sem);
            mutex_release(c->lock);
            mutex_acquire(c->wait_done, LOCK_INFINITE);
        } else {
            mutex_release(c->lock);
        }
    }
};

void cond_broadcast(COND * c) {
    if(c){
        mutex_acquire(c->lock, LOCK_INFINITE);
        if (c->waiting > c->signals) {
            int i, num_waiting;

            num_waiting = (c->waiting - c->signals);
            c->signals = c->waiting;
            for (i = 0; i < num_waiting; ++i) {
                mutex_release(c->wait_sem);
            }
            /* Now all released threads are blocked here, waiting for us.
               Collect them all (and win fabulous prizes!) :-)
             */
            mutex_release(c->lock);
            for (i = 0; i < num_waiting; ++i) {
                mutex_acquire(c->wait_done, LOCK_INFINITE);
            }
        } else {
            mutex_release(c->lock);
        }
    }
};
void cond_delete(COND * c){
    if(c){
        mutex_destroy(c->wait_sem);
        mutex_destroy(c->wait_done);
        mutex_destroy(c->lock);

        free(c);
    }
};


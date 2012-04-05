/*
 * File:   cond.h
 * Author: cc
 *
 * Created on 4 mars 2012, 16:48
 */

#ifndef COND_H
#define	COND_H

    /**
     * Semaphores and mutex are the same
     */
    typedef struct {
        MUTEX *lock;
        int waiting;
        int signals;
        MUTEX *wait_sem;
        MUTEX *wait_done;
    } COND;

    COND * cond_create();
    void cond_wait(COND *, MUTEX *);
    void cond_signal(COND *);
    void cond_broadcast(COND *);
    void cond_delete(COND *);

#endif	/* COND_H */


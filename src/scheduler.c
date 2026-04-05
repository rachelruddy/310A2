#include <stdio.h>
#include <stdlib.h>
#include "scheduler.h"
#include "shellmemory.h"
#include "interpreter.h"
#include "shell.h"
#include <string.h>
#include <pthread.h>

//next pid counter
static int next_pid = 1;

//head and tail of queue
static PCB *ready_head = NULL;
static PCB *ready_tail = NULL;

//instructions to execute as timer for RR
static const int max_instr = 2;

// ---------------- MT scheduler skeleton ----------------
static const int mt_worker_count = 1;
static pthread_t mt_workers[1];
static pthread_mutex_t mt_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t mt_cv = PTHREAD_COND_INITIALIZER;
static int mt_enabled = 0;
static int mt_shutdown = 0;
static int mt_running = 0;
static int mt_active_workers = 0;
static int mt_quantum = 2;
static int mt_quit_requested = 0;
static int scheduler_running = 0;
static PCB *mt_current_pcb = NULL;

static const char *fetch_current_line(PCB *pcb) {
    return program_get_line_VA(pcb);
}

int scheduler_is_running(void) {
    return scheduler_running;
}

void invalidate_frame(int frame_start) {
    pthread_mutex_lock(&mt_lock);
    PCB *cur = ready_head;
    while (cur) {
        for (int i = 0; i < cur->num_pages; i++) {
            if (cur->page_table[i] == frame_start) {
                cur->page_table[i] = -1;
            }
        }
        cur = cur->next;
    }

    if (mt_current_pcb) {
        for (int i = 0; i < mt_current_pcb->num_pages; i++) {
            if (mt_current_pcb->page_table[i] == frame_start) {
                mt_current_pcb->page_table[i] = -1;
            }
        }
    }
    pthread_mutex_unlock(&mt_lock);
}

static int rr_quantum_for_policy(const char *policy) {
    if (strcmp(policy, "RR30") == 0) {
        return 30;
    }
    return max_instr;
}

static void enqueue_nolock(PCB *pcb) {
    pcb->next = NULL;
    if (!ready_tail) {
        ready_head = pcb;
        ready_tail = pcb;
        return;
    }
    ready_tail->next = pcb;
    ready_tail = pcb;
}

static void enqueue_front_nolock(PCB *pcb) {
    if (!ready_head) {
        pcb->next = NULL;
        ready_head = pcb;
        ready_tail = pcb;
        return;
    }

    pcb->next = ready_head;
    ready_head = pcb;
}

static PCB *dequeue_nolock(void) {
    if (!ready_head) {
        return NULL;
    }
    PCB *pcb = ready_head;
    ready_head = ready_head->next;
    if (!ready_head) {
        ready_tail = NULL;
    }
    pcb->next = NULL;
    return pcb;
}

static void *mt_worker_main(void *arg) {
    (void)arg;

    pthread_mutex_lock(&mt_lock);
    while (!mt_shutdown) {
        while (!mt_shutdown && (!mt_running || !ready_head)) {
            pthread_cond_wait(&mt_cv, &mt_lock);
        }

        if (mt_shutdown) {
            break;
        }

        PCB *pcb = dequeue_nolock();
        if (!pcb) {
            continue;
        }
        mt_current_pcb = pcb;
        mt_active_workers++;
        int quantum = mt_quantum;
        pthread_mutex_unlock(&mt_lock);

        int done = 0;
        for (int i = 0; i < quantum; i++) {
            if (pcb->pc < pcb->code_len) {
                const char *line = fetch_current_line(pcb);
                if (line) {
                    (void)parseInput((char *)line);
                    pcb->pc++;
                } else if (pcb->page_faulted) {
                    break;
                } else {
                    done = 1;
                    break;
                }
            } else {
                done = 1;
                break;
            }
        }

        if (pcb->pc >= pcb->code_len) {
            done = 1;
        }

        pthread_mutex_lock(&mt_lock);
        mt_active_workers--;
        if (mt_current_pcb == pcb) {
            mt_current_pcb = NULL;
        }

        if (done || pcb->pc >= pcb->code_len) {
            // get program associated with this PCB
            Program *program = pcb->program;
            if (program) {
                program->count--;
            }
            free(pcb);
        } else {
            enqueue_nolock(pcb);
            pthread_cond_broadcast(&mt_cv);
        }

        if (mt_quit_requested && !ready_head && mt_active_workers == 0) {
            mt_shutdown = 1;
            pthread_cond_broadcast(&mt_cv);
        }

        if (mt_running && !ready_head && mt_active_workers == 0) {
            pthread_cond_broadcast(&mt_cv);
        }
    }
    pthread_mutex_unlock(&mt_lock);

    return NULL;
}

int scheduler_policy_supports_mt(const char *policy) {
    return (strcmp(policy, "RR") == 0 || strcmp(policy, "RR30") == 0);
}

int scheduler_mt_enable(void) {
    pthread_mutex_lock(&mt_lock);
    if (mt_enabled) {
        pthread_mutex_unlock(&mt_lock);
        return 0;
    }

    mt_shutdown = 0;
    mt_quit_requested = 0;
    pthread_mutex_unlock(&mt_lock);

    for (int i = 0; i < mt_worker_count; i++) {
        if (pthread_create(&mt_workers[i], NULL, mt_worker_main, NULL) != 0) {
            pthread_mutex_lock(&mt_lock);
            mt_shutdown = 1;
            pthread_cond_broadcast(&mt_cv);
            pthread_mutex_unlock(&mt_lock);

            for (int j = 0; j < i; j++) {
                pthread_join(mt_workers[j], NULL);
            }
            return 1;
        }
    }

    pthread_mutex_lock(&mt_lock);
    mt_enabled = 1;
    pthread_mutex_unlock(&mt_lock);
    return 0;
}

void scheduler_mt_shutdown(void) {
    pthread_t self = pthread_self();

    pthread_mutex_lock(&mt_lock);
    if (!mt_enabled) {
        pthread_mutex_unlock(&mt_lock);
        return;
    }

    mt_quit_requested = 1;
    while (ready_head || mt_active_workers > 0) {
        pthread_cond_wait(&mt_cv, &mt_lock);
    }

    mt_shutdown = 1;
    pthread_cond_broadcast(&mt_cv);
    pthread_mutex_unlock(&mt_lock);

    for (int i = 0; i < mt_worker_count; i++) {
        if (pthread_equal(mt_workers[i], self)) {
            continue;
        }
        pthread_join(mt_workers[i], NULL);
    }

    pthread_mutex_lock(&mt_lock);
    mt_enabled = 0;
    mt_running = 0;
    mt_shutdown = 0;
    mt_active_workers = 0;
    mt_quit_requested = 0;
    pthread_mutex_unlock(&mt_lock);
}

int scheduler_mt_is_enabled(void) {
    pthread_mutex_lock(&mt_lock);
    int enabled = mt_enabled;
    pthread_mutex_unlock(&mt_lock);
    return enabled;
}

void scheduler_mt_request_quit(void) {
    pthread_mutex_lock(&mt_lock);
    if (mt_enabled) {
        mt_quit_requested = 1;
        if (!ready_head && mt_active_workers == 0) {
            mt_shutdown = 1;
        }
        pthread_cond_broadcast(&mt_cv);
    }
    pthread_mutex_unlock(&mt_lock);
}

int scheduler_mt_is_worker_thread(void) {
    pthread_t self = pthread_self();
    int is_worker = 0;

    pthread_mutex_lock(&mt_lock);
    if (mt_enabled) {
        for (int i = 0; i < mt_worker_count; i++) {
            if (pthread_equal(mt_workers[i], self)) {
                is_worker = 1;
                break;
            }
        }
    }
    pthread_mutex_unlock(&mt_lock);

    return is_worker;
}
// -------------- end MT scheduler skeleton --------------


//enqueue a PCB into ready queue ordered by score (ascending)
static void enqueue_by_score(PCB *pcb){
    pcb->next = NULL;

    if (!ready_head) {
        ready_head = pcb;
        ready_tail = pcb;
        return;
    }

    if (pcb->score <= ready_head->score) {
        pcb->next = ready_head;
        ready_head = pcb;
        return;
    }

    PCB *cur = ready_head;
    while (cur->next && cur->next->score < pcb->score) {
        cur = cur->next;
    }

    pcb->next = cur->next;
    cur->next = pcb;
    if (!pcb->next) {
        ready_tail = pcb;
    }
}

//decrease score of all waiting processes by 1 (minimum 0)
static void age_ready_queue(void){
    PCB *cur = ready_head;
    while (cur) {
        if (cur->score > 0) {
            cur->score--;
        }
        cur = cur->next;
    }
}


//enqueue a PCB to tail of queue
void enqueue(PCB *pcb){
    pthread_mutex_lock(&mt_lock);
    enqueue_nolock(pcb);
    pthread_cond_broadcast(&mt_cv);
    pthread_mutex_unlock(&mt_lock);
}

void enqueue_front(PCB *pcb) {
    pthread_mutex_lock(&mt_lock);
    enqueue_front_nolock(pcb);
    pthread_cond_broadcast(&mt_cv);
    pthread_mutex_unlock(&mt_lock);
}

//dequeue a PCB from head of queue
PCB* dequeue(void){
    pthread_mutex_lock(&mt_lock);
    PCB *pcb = dequeue_nolock();
    pthread_mutex_unlock(&mt_lock);
    return pcb;
}

//get next PID
int get_next_pid(void){
    return next_pid++;
}

//run the scheduler
void run_scheduler(const char *policy){
    if (scheduler_running) {
        return;
    }
    scheduler_running = 1;

    PCB *pcb = NULL;

    if (mt_enabled && scheduler_policy_supports_mt(policy)) {
        pthread_mutex_lock(&mt_lock);
        mt_quantum = rr_quantum_for_policy(policy);
        mt_running = 1;
        pthread_cond_broadcast(&mt_cv);
        pthread_mutex_unlock(&mt_lock);
        scheduler_running = 0;
        return;
    }

    //FCFS and SJF implementation
    if(strcmp(policy, "FCFS") == 0 || strcmp(policy, "SJF") == 0){
        while ((pcb = dequeue()) != NULL) {
            int fetch_failed = 0;
            while (pcb->pc < pcb->code_len) {
                const char *line = fetch_current_line(pcb);
                if (line) {
                    (void)parseInput((char *)line);
                    pcb->pc++;
                } else if (pcb->page_faulted) {
                    continue;
                } else {
                    fetch_failed = 1;
                    break;
                }
            }
            //Assignment 3: instead of just freeing memory where pcb's program lived, first check that no other process is using it.
            Program *program = pcb->program;
            if (program) {
                program->count--;
            }
            free(pcb);
            if (fetch_failed) {
                continue;
            }
        }
    }
    else if (strcmp(policy, "AGING") == 0){
        while ((pcb = dequeue()) != NULL) {
            const char *line = fetch_current_line(pcb);
            if (line) {
                (void)parseInput((char *)line);
                pcb->pc++;
            } else if (pcb->page_faulted) {
                age_ready_queue();
                enqueue_by_score(pcb);
                continue;
            } else {
                Program *program = pcb->program;
                if (program) {
                    program->count--;
                }
                free(pcb);
                continue;
            }

            age_ready_queue();

            if (pcb->pc >= pcb->code_len) {
                //Assignment 3: instead of just freeing memory where pcb's program lived, first check that no other process is using it.
                Program *program = pcb->program;
                if (program) {
                    program->count--;
                }
                free(pcb);
            } else {
                enqueue_by_score(pcb);
            }
        }
    }
    //RR and RR30 implementation
    else{
        int quantum = rr_quantum_for_policy(policy);
        while ((pcb = dequeue()) != NULL) {
            //execute one policy quantum at a time
            int done = 0;
            for (int i = 0; i < quantum; i++){
                if(pcb->pc < pcb->code_len){
                    const char *line = fetch_current_line(pcb);
                    if (line) {
                        (void)parseInput((char *)line);
                        pcb->pc++;
                    } else if (pcb->page_faulted) {
                        break;
                    } else {
                        Program *program = pcb->program;
                        if (program) {
                            program->count--;
                        }
                        free(pcb);
                        done = 1;
                        break;
                    }
                }
                else{
                    //Assignment 3: instead of just freeing memory where pcb's program lived, first check that no other process is using it.
                    Program *program = pcb->program;
                    if (program) {
                        program->count--;
                    }
                    free(pcb);
                    done = 1;
                    break;
                }
            }

            if (!done && pcb->pc >= pcb->code_len) {
                Program *program = pcb->program;
                if (program) {
                    program->count--;
                }
                free(pcb);
                done = 1;
            }

            //enqueue the pcb back to tail of queue if prgrm not done
            if(done == 0){
                enqueue(pcb);
            }
        }
    }

    //reset PIDs
    next_pid = 1;
    scheduler_running = 0;

}
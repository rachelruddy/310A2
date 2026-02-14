#ifndef SCHEDULER_H
#define SCHEDULER_H

// Define PCB struct
typedef struct PCB {
    int pid;
    int code_start;
    int code_len;
    int pc;
    struct PCB *next;
} PCB;

//enqueue/dequeue pcb
void enqueue(PCB *pcb);
PCB* dequeue(void);

//scheduler run
void run_scheduler(const char *policy);
int get_next_pid(void);

#endif
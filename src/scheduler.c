#include <stdio.h>
#include <stdlib.h>
#include "scheduler.h"
#include "shellmemory.h"
#include "interpreter.h"
#include "shell.h"
#include <string.h>

//next pid counter
static int next_pid = 1;

//head and tail of queue
static PCB *ready_head = NULL;
static PCB *ready_tail = NULL;


//enqueue a PCB to tail of queue
void enqueue(PCB *pcb){
    //set next pcb in queue after tail to null
    pcb->next = NULL;
    if (!ready_tail) {
        ready_head = pcb;
        ready_tail = pcb;
        return;
    }
    ready_tail->next = pcb;
    ready_tail = pcb;
}

//dequeue a PCB from head of queue
PCB* dequeue(void){
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

//get next PID
int get_next_pid(void){
    return next_pid++;
}

//run the scheduler
void run_scheduler(const char *policy){
    PCB *pcb = NULL;

    //FCFS and SJF implementation
    if(strcmp(policy, "FCFS") == 0 || strcmp(policy, "SJF") == 0){
        while ((pcb = dequeue()) != NULL) {
            while (pcb->pc < pcb->code_len) {
                const char *line = program_get_line(pcb->code_start + pcb->pc);
                if (line) {
                    (void)parseInput((char *)line);
                }
                pcb->pc++;
            }
            program_free(pcb->code_start, pcb->code_len);
            free(pcb);
        }
    }

    //RR and AGING implementations
    else{
        //TODO
    }

    //reset PIDs
    next_pid = 1;
}
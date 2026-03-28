#ifndef SCHEDULER_H
#define SCHEDULER_H

// Define PCB struct
typedef struct PCB {
  int pid;
  int code_start;
  int code_len;
  int pc;
  int score;
  struct PCB *next;
} PCB;

//enqueue/dequeue pcb
void enqueue(PCB *pcb);
void enqueue_front(PCB *pcb);
PCB* dequeue(void);

//scheduler run
void run_scheduler(const char *policy);
int get_next_pid(void);

// Stupid MT stuff
int scheduler_policy_supports_mt(const char *policy);
int scheduler_mt_enable(void);
void scheduler_mt_shutdown(void);
int scheduler_mt_is_enabled(void);
void scheduler_mt_request_quit(void);
int scheduler_mt_is_worker_thread(void);

#endif

#ifndef SHELLMEMORY_H
#define SHELLMEMORY_H

#include <stdio.h>
#include <stdlib.h>

#ifndef VAR_STORE_SIZE
#define VAR_STORE_SIZE 1000
#endif

#ifndef FRAME_STORE_SIZE
#define FRAME_STORE_SIZE 999
#endif

#define MEM_SIZE VAR_STORE_SIZE
#define PROGRAM_MEM_SIZE FRAME_STORE_SIZE
#define MAX_PROGRAMS 10
#define FRAME_SIZE 3
#define NUM_FRAMES (PROGRAM_MEM_SIZE/FRAME_SIZE)

#ifndef MAX_PAGES
#define MAX_PAGES 333
#endif

// Define a new struct to keep track of which programs exist in memory.
// Allows multiple processes to share memory for same script.
typedef struct Program {
  char* name;
  int code_start;
  int code_len;
  // count keeps track of how many processes are currently using this program
  int count;    
  int frames[MAX_PAGES];
  int num_pages;
  char **backing_store;
  int backing_len;
} Program;

void mem_init();
char *mem_get_value(char *var);
void mem_set_value(char *var, char *value);

int program_store(FILE *p, int *code_start, int *code_len, Program *program);
const char *program_get_line(int index);
void program_free(Program *program);

// Define a Program struct array to keep track of all the programs currently running in memory.
// prog_count keeps track of how many programs are currently in memory
extern Program programs[MAX_PROGRAMS];
extern int prog_count; 

//Define table to keep track of which frames are available in memory
extern int frames[NUM_FRAMES]; // 1 = occupied, 0 = free

//get prgrm line from memory based on VA:
// shellmemory.h
struct PCB; // forward declaration
const char* get_va(struct PCB *pcb);

#endif
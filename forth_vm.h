#ifndef FORTH_VM_H
#define FORTH_VM_H

#include <stdint.h>

#define FORTH_STACK_CELLS      64

/*
 * THE MONOLITHIC VIRTUAL MACHINE CONTEXT
 * This contains everything required to execute an independent Forth task.
 * For multiple tasks under FreeRTOS, each thread instantiates its own copy.
 */
typedef struct {
    /* Stacks (Fast RAM Operational Buffers) */
    uint32_t data_stack[FORTH_STACK_CELLS];
    uint32_t return_stack[FORTH_STACK_CELLS];

    /* VM Registers */
    uint32_t sp;               /* Data Stack Index (0 to FORTH_STACK_CELLS) */
    uint32_t rp;               /* Return Stack Index (0 to FORTH_STACK_CELLS) */
    uint32_t ip;               /* Instruction Pointer (Virtual PC for Threaded Code) */
    uint32_t state;            /* 0 = Interpretation Mode (REPL), 1 = Compilation Mode */
    uint32_t latest_word;      /* Physical address of the latest compiled user word in SPI-RAM */
    uint32_t dict_free_ptr;    /* Compilation Frontier (Next free address in SPI-RAM dictionary space) */

    /* Control & Exception Flags */
    volatile uint32_t abort_flag;
    uint32_t quiet_mode;       /* Silence REPL output ("ok") during file processing */
} ForthMachineState_t;

/* Global reference to the currently active Forth instance */
extern ForthMachineState_t *current_forth_vm;

/* Lifecycle and Runtime Control Hooks */
void vm_init(void);
void vm_context_init(ForthMachineState_t *state, uint32_t dict_start_addr);
void forth_abort(const char *error_msg);
void forth_abort_with_context(const char *error_msg);

/* Thread-Safe Context-Aware Stack Operations */
void     forth_push(uint32_t val);
uint32_t forth_pop(void);
void     forth_r_push(uint32_t val);
uint32_t forth_r_pop(void);

/* Stack Manipulation Primitives */
void forth_drop(void);
void forth_dup(void);
void forth_swap(void);
void forth_over(void);
void vm_dump_stack(void);

#endif /* FORTH_VM_H */

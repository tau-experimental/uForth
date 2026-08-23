#include "forth_vm.h"
#include <stdio.h>

/* Global pointer tracking the execution context of the executing thread */
ForthMachineState_t *current_forth_vm = NULL;

/* Глобальная классическая функция мягкого сброса ВМ */
void forth_abort(const char *error_msg) {
    /* Просто вызываем контекстный сброс для текущей активной машины */
    forth_abort_with_context(error_msg);
}

void vm_init(void) {
    if (current_forth_vm) {
        current_forth_vm->sp = 0;
        current_forth_vm->rp = 0;
        current_forth_vm->ip = 0;
        current_forth_vm->state = 0;
        current_forth_vm->abort_flag = 0;
        current_forth_vm->quiet_mode = 0;
    }
}

void vm_context_init(ForthMachineState_t *state, uint32_t dict_start_addr) {
    if (!state) return;

    state->sp = 0;
    state->rp = 0;
    state->ip = 0;
    state->state = 0;
    state->latest_word = 0;
    state->dict_free_ptr = dict_start_addr;
    state->abort_flag = 0;
    state->quiet_mode = 0;

    /* Bind this initialized state as the active runtime engine instance */
    current_forth_vm = state;
}

void forth_abort_with_context(const char *error_msg) {
    printf("\n[FORTH CONTEXT ERROR] %s\n", error_msg);
    if (current_forth_vm) {
        current_forth_vm->sp = 0;
        current_forth_vm->rp = 0;
        current_forth_vm->abort_flag = 1;
        current_forth_vm->state = 0; /* Fall back safely to REPL mode */
    }
}

void forth_push(uint32_t val) {
    if (!current_forth_vm || current_forth_vm->abort_flag) return;

    if (current_forth_vm->sp >= FORTH_STACK_CELLS) {
        forth_abort_with_context("Data Stack Overflow!");
        return;
    }

    current_forth_vm->data_stack[current_forth_vm->sp] = val;
    current_forth_vm->sp++;
}

uint32_t forth_pop(void) {
    if (!current_forth_vm || current_forth_vm->abort_flag) return 0;

    if (current_forth_vm->sp == 0) {
        forth_abort_with_context("Data Stack Underflow (Stack is empty)!");
        return 0;
    }

    current_forth_vm->sp--;
    return current_forth_vm->data_stack[current_forth_vm->sp];
}

void forth_r_push(uint32_t val) {
    if (!current_forth_vm || current_forth_vm->abort_flag) return;

    if (current_forth_vm->rp >= FORTH_STACK_CELLS) {
        forth_abort_with_context("Return Stack Overflow!");
        return;
    }

    current_forth_vm->return_stack[current_forth_vm->rp] = val;
    current_forth_vm->rp++;
}

uint32_t forth_r_pop(void) {
    if (!current_forth_vm || current_forth_vm->abort_flag) return 0;

    if (current_forth_vm->rp == 0) {
        forth_abort_with_context("Return Stack Underflow!");
        return 0;
    }

    current_forth_vm->rp--;
    return current_forth_vm->return_stack[current_forth_vm->rp];
}

void forth_drop(void) { (void)forth_pop(); }

void forth_dup(void) {
    if (!current_forth_vm || current_forth_vm->abort_flag) return;
    if (current_forth_vm->sp == 0) {
        forth_abort_with_context("DUP Underflow!");
        return;
    }
    forth_push(current_forth_vm->data_stack[current_forth_vm->sp - 1]);
}

void forth_swap(void) {
    if (!current_forth_vm || current_forth_vm->abort_flag) return;
    if (current_forth_vm->sp < 2) {
        forth_abort_with_context("SWAP Underflow!");
        return;
    }
    uint32_t tmp = current_forth_vm->data_stack[current_forth_vm->sp - 1];
    current_forth_vm->data_stack[current_forth_vm->sp - 1] = current_forth_vm->data_stack[current_forth_vm->sp - 2];
    current_forth_vm->data_stack[current_forth_vm->sp - 2] = tmp;
}

void forth_over(void) {
    if (!current_forth_vm || current_forth_vm->abort_flag) return;
    if (current_forth_vm->sp < 2) {
        forth_abort_with_context("OVER Underflow!");
        return;
    }
    forth_push(current_forth_vm->data_stack[current_forth_vm->sp - 2]);
}

void vm_dump_stack(void) {
    if (!current_forth_vm) return;
    uint32_t i;
    printf("<%u> ", current_forth_vm->sp);
    for (i = 0; i < current_forth_vm->sp; i++) {
        printf("%d ", (int32_t)current_forth_vm->data_stack[i]);
    }
    printf("\n");
}

# uForth
my humble attempt to build embedded Forth machine without real knowledge of how to do so

## Integrated Context-Aware Forth Kernel for Embedded Systems
An industrial-grade, object-oriented, thread-safe, and fault-tolerant Forth virtual machine environment written in ANSI C, explicitly engineered for resource-constrained microcontrollers (e.g., STM32, ESP32, Milandr) deploying external SPI/QSPI RAM buffers and FreeRTOS.
## 📊 1. Memory Tier & Bus Architecture
The environment completely isolates state variables and memory regions to guarantee reentrancy and prevent boundary overlaps.

+-----------------------------------------------------------------------+

|                       FAST_SRAM_BASE (0x20000000)                     |
+-------------------+-------------------+-------------------------------+

| Data Stack (256B) | Return Stack(256B)| System Control Registers      |
| [0x0000 - 0x0100] | [0x0100 - 0x0200] | (SP, RP, IP, STATE, LATEST)   |
+-------------------+-------------------+-------------------------------+

| ATOMIC CELL POOL                      | STRUCT CHUNK POOL             |
| 256 Fast Cells (SRAM Data Base)       | Lisp-like allocation lists    |
| Offset: 0x0800                        | Offset: 0x0C00                |
+---------------------------------------+-------------------------------+

+-----------------------------------------------------------------------+

|                       EXT_SPI_RAM_BASE (0x60000000)                    |
+---------------------------------------+-------------------------------+

| COMPACTING DYNAMIC HEAP POOL          | SEGREGATED THREAD DICTIONARIES|
| Linked blocks with automatic In-Place | Segmented user macro ranges   |
| Coalescing link compaction            | Start Boundary: 0x60080000    |
+---------------------------------------+-------------------------------+

------------------------------
## 🗒 2. Core Virtual Machine Type Definitions (forth_vm.h)

#ifndef FORTH_VM_H#define FORTH_VM_H
#include <stdint.h>
#define FORTH_STACK_CELLS      64#define FORTH_MAX_WORD_LEN     33  /* 1 byte prefix + 32 bits binary data */#define TOKEN_BUFFER_SIZE      36  /* Padded safe string limit */#define STRING_BUFFER_SIZE     256 /* Maximum line length for I/O and streaming */#define FORTH_MAX_STRING_LEN   255
/* Monolithic isolated Virtual Machine Thread Context Structure */typedef struct {
    uint32_t data_stack[FORTH_STACK_CELLS];
    uint32_t return_stack[FORTH_STACK_CELLS];
    uint32_t sp;               /* Data Stack array index tracking marker */
    uint32_t rp;               /* Return Stack array index tracking marker */
    uint32_t ip;               /* Instruction Pointer (Virtual Program Counter) */
    uint32_t state;            /* 0 = Interpretation Mode (REPL), 1 = Compilation Mode */
    uint32_t latest_word;      /* Physical address of the latest compiled user dictionary macro */
    uint32_t dict_free_ptr;    /* Compilation Frontier pointer inside external SPI-RAM */
    volatile uint32_t abort_flag;
    uint32_t quiet_mode;       /* Suppresses REPL terminal output "ok" loops during file loads */
} ForthMachineState_t;
extern ForthMachineState_t *current_forth_vm;
void vm_context_init(ForthMachineState_t *state, uint32_t dict_start_addr);void forth_abort_with_context(const char *error_msg);void forth_interpret_line(const char *line);
void     forth_push(uint32_t val);uint32_t forth_pop(void);void     forth_r_push(uint32_t val);uint32_t forth_r_pop(void);
#endif /* FORTH_VM_H */

------------------------------
## 🌟 3. Critical Context-Aware Driver Implementations (forth_dict.c)## A. All-Inclusive Binary & Hex Lexical Analyzer

static int parse_forth_number(const char *token, uint32_t *out_val) {
    char *endptr;
    int base = 10;
    const char *num_ptr = token;
    int is_negative = 0;

    if (*num_ptr == '-') {
        is_negative = 1;
        num_ptr++;
    }

    if (*num_ptr == '$' && *(num_ptr + 1) != '\0') {
        base = 16;
        num_ptr++;
    }
    else if (*num_ptr == '%' && *(num_ptr + 1) != '\0') {
        base = 2;
        num_ptr++;
    }
    else if (*num_ptr == '0' && (*(num_ptr + 1) == 'x' || *(num_ptr + 1) == 'X') && *(num_ptr + 2) != '\0') {
        base = 16;
        num_ptr += 2;
    }

    long long parsed_val = strtoll(num_ptr, &endptr, base);

    if (*endptr == '\0' && endptr != num_ptr) {
        if (is_negative) parsed_val = -parsed_val;
        *out_val = (uint32_t)parsed_val;
        return 1; /* Match found: token processed as a raw integer literal */
    }
    return 0; /* Token mismatch: trigger dictionary tree search lookups */
}

## B. Pure Signal Preservation Dynamic Arithmetic Right Shift (arshift)

void forth_primitive_arshift(void) {
    if (!current_forth_vm) return;
    uint32_t shift_count = forth_pop();
    uint32_t val = forth_pop();
    if (current_forth_vm->abort_flag) return;

    if (shift_count >= 32) {
        forth_push((val & 0x80000000) ? 0xFFFFFFFF : 0);
        return;
    }

    int32_t signed_val = (int32_t)val;

    /* Isolate independent width bounds checking to execute proper Sign Extension */
    if ((val & 0xFFFFFF00) == 0) {
        if (val & 0x80) signed_val |= 0xFFFFFF00; /* 8-bit negative byte bounds matched */
    }
    else if ((val & 0xFFFF0000) == 0) {
        if (val & 0x8000) signed_val |= 0xFFFF0000; /* 16-bit negative word bounds matched */
    }

    /* Executes host-level machine ASR with perfect trailing bit sign duplication */
    forth_push((uint32_t)(signed_val >> shift_count));
}

## C. Secure Virtual File System Table Sub-Layer (64-bit to 32-bit Bridge)

#define MAX_OPEN_FILES  4static FILE *sys_file_table[MAX_OPEN_FILES] = { NULL };
void forth_primitive_f_create(void) {
    if (!current_forth_vm) return;
    uint32_t name_len = forth_pop();
    uint32_t name_addr = forth_pop();
    if (current_forth_vm->abort_flag) return;

    char filename[STRING_BUFFER_SIZE];
    if (name_len > FORTH_MAX_STRING_LEN) name_len = FORTH_MAX_STRING_LEN;
    hw_spi_ram_read_buf(name_addr, (uint8_t *)filename, name_len);
    filename[name_len] = '\0';

    uint32_t slot_idx = 0;
    for (uint32_t i = 0; i < MAX_OPEN_FILES; i++) {
        if (sys_file_table[i] == NULL) {
            slot_idx = i + 1; /* Forth IDs are index mapped 1 to 4 */
            break;
        }
    }
    if (slot_idx == 0) { forth_push(0); return; }

    FILE *f = fopen(filename, "w+");
    if (!f) { forth_push(0); return; }

    sys_file_table[slot_idx - 1] = f;
    forth_push(slot_idx); /* Push safe scalar ID index back to stack */
}

## D. Single-Pointer String Stream Evaluation Engine

void forth_interpret_line(const char *line) {
    char token[TOKEN_BUFFER_SIZE];
    uint32_t token_ptr = 0;
    int compiling_new_word = 0;
    const char *p = line;
    
    while (*p != '\0') {
        if (current_forth_vm->abort_flag) break;
        char ch = *p;
        
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            if (token_ptr > 0) {
                token[token_ptr] = '\0';
                if (compiling_new_word) {
                    dict_add_word(token); compiling_new_word = 0;
                } else if (strcmp(token, ":") == 0) {
                    forth_cmd_colon(); compiling_new_word = 1;
                } else if (strcasecmp(token, ".\"") == 0) {
                    forth_cmd_dot_quote(&p); token_ptr = 0; continue;
                } else if (strcasecmp(token, "s\"") == 0) {
                    forth_cmd_s_quote(&p); token_ptr = 0; continue;
                } else {
                    process_token(token);
                }
                token_ptr = 0;
            }
        } else {
            if (token_ptr < FORTH_MAX_WORD_LEN) token[token_ptr++] = ch;
        }
        p++;
    }
    /* Residual token cleanup line segment skipped here for documentation brevity */
}

------------------------------
## 📌 4. Flash Constant Pre-Compilation Sequence
To save static execution memory on the microcontroller, format definitions are stored as text streams directly in the microcontroller's programmatic read-only Flash .rodata array block and evaluated inside dict_init() prior to starting the REPL console interface thread:

static const char *const flash_init_constants[] = {
    "$22 constant B.HEX",   /* Byte output formatting mask to Hex representation */
    "$23 constant B.BIN",   /* Byte output formatting mask to Binary representation */
    "$42 constant W.HEX",   /* Word output formatting mask to Hex representation */
    "$43 constant W.BIN",   /* Word output formatting mask to Binary representation */
    "$13 constant N.BIN",   /* Nibble output formatting mask to Binary representation */
    "$10 constant N.DEC"    /* Nibble output formatting mask to Signed Decimal representation */
};

------------------------------
## 📌 5. The Complete Verified Validation Script (automation_cycle.fs)
This is the precise, completed text script file successfully loaded across the unified pipeline to prove cross-tier dynamic linkage:

\ --- Industrial Lifecycle & I/O Verification Script ---

0 value CELL-ADDR
0 value HEAP-ADDR
0 value TEST-FILE

: RUN-LIFECYCLE
    cr ." [STAGE 1]: Allocating fast hardware cell... " cr
    fast-cell -> CELL-ADDR
    
    \ Configure an 8-bit GPIO register mask via binary notation
    %11001010 CELL-ADDR !
    ."   Cell secured at: " CELL-ADDR . cr
    ."   Cell holds data: " CELL-ADDR @ . cr

    cr ." [STAGE 2]: Creating telemetry text file... " cr
    s" telemetry_snapshot.bin" f-create -> TEST-FILE
    ."   File assigned VFS Slot: " TEST-FILE . cr

    \ Flush cell data directly out of RAM straight onto disk sectors
    CELL-ADDR 4 TEST-FILE f-write
    TEST-FILE f-close
    ."   Data streamed and file closed cleanly. " cr

    cr ." [STAGE 3]: Cleaning up fast cell infrastructure... " cr
    CELL-ADDR fast-cell-free
    ."   Cell cleanly returned to the atomic pool." cr

    cr ." [STAGE 4]: Performing isolated data stack arithmetic calculation... " cr
    100 200 + 5 * 1500 = if
        ."   Calculation evaluation: SUCCESS (1500 verified)" cr
    else
        ."   Calculation evaluation: FAILED" cr
        abort
    then

    cr ." [STAGE 5]: Re-opening file for reading and allocating heap... " cr
    s" telemetry_snapshot.bin" f-open -> TEST-FILE
    
    \ Pull a 4-byte payload cell block out of our compacting memory heap
    4 allocate drop -> HEAP-ADDR
    ."   Heap block secured at: " HEAP-ADDR . cr

    \ Package-burst extract file data back across the simulated bus into the heap
    HEAP-ADDR 4 TEST-FILE f-read drop
    TEST-FILE f-close
    ."   Data read back and file closed cleanly. " cr

    cr ." [STAGE 6]: FINAL INTEGRITY VERIFICATION " cr
    ."   Extracted data currently living in Heap block: " HEAP-ADDR @ . cr
    
    \ Verify data byte integrity matching our compiled binary input
    HEAP-ADDR @ %11001010 = if
        ."   [SUCCESS]: Hardware lifecycle execution completed flawlessly!" cr
    else
        ."   [CRITICAL FAULT]: Data corruption or bit-flip detected in transit!" cr
    then
    cr
    
    \ Free heap allocation memory space and execute automatic link coalescence compaction
    HEAP-ADDR free . ;

------------------------------
## 📌 6. Successful Test Execution Telemetry Log Output

> s" automation_cycle.fs" included

--------------------------------------------------
[SUCCESS] Automation Engine successfully synchronized.
  Root script:               'automation_cycle.fs'
  SPI-RAM Dictionary size:   1636 bytes
--------------------------------------------------

 ok
Stack: <0> 
> RUN-LIFECYCLE

[STAGE 1]: Allocating fast hardware cell... 
  Cell secured at: 536872992 
  Cell holds data: 202 

[STAGE 2]: Creating telemetry text file... 
  File assigned VFS Slot: 1 
  Data streamed and file closed cleanly. 

[STAGE 3]: Cleaning up fast cell infrastructure... 
  Cell cleanly returned to the atomic pool.

[STAGE 4]: Performing isolated data stack arithmetic calculation... 
  Calculation evaluation: SUCCESS (1500 verified)

[STAGE 5]: Re-opening file for reading and allocating heap... 
[DEBUG HEAP] allocate invoked! Requested size: 4 bytes. Current SP before push: 0
[DEBUG HEAP] Memory successfully allocated at 0x60000008
  Heap block secured at: 1610612744 
  Data read back and file closed cleanly. 

[STAGE 6]: FINAL INTEGRITY VERIFICATION 
  Extracted data currently living in Heap block: 202 
  [SUCCESS]: Hardware lifecycle execution completed flawlessly!

[DEBUG HEAP] free invoked for address 0x60000008
[DEBUG HEAP] Block at 0x60000008 (Size: 12) marked as FREE.
[DEBUG HEAP] COALESCE SUCCESS: Merged block 0x60000000 with 0x6000000C. New Size: 1048584
0  ok
Stack: <0> 


#include <stdio.h>
#include <string.h>
#include "hardware.h"
#include "forth_vm.h"
#include "forth_cell_pool.h"
#include "forth_chunk_pool.h"
#include "forth_heap.h"
#include "forth_dict.h"
#include "forth_debug.h"

/* Выделяем ровно ОДИН глобальный экземпляр Forth-машины */
static ForthMachineState_t system_forth_instance;

int main(int argc, char *argv[]) {
    printf("==================================================\n");
    printf("--- INTELLECTUAL EMBEDDED FORTH TERMINAL (REPL) ---\n");
    printf("==================================================\n");

    /* 1. Инициализация аппаратных абстракций шины и пулов памяти хоста */
    if (!hw_init()) {
        fprintf(stderr, "[HARDWARE FAULT] Cannot initialize simulated bus layout.\n");
        return -1;
    }
    cell_pool_init();
    chunk_pool_init();
    heap_init();

    forth_dict_validate_sorting();

    /* 2. Инициализация нашей чистой, изолированной контекстной переменной */
    /* Выделяем под словарь словаря штатные 512 КБ во внешней SPI-RAM (отметка 0x80000) */
    vm_context_init(&system_forth_instance, EXT_SPI_RAM_BASE + 0x80000);
    dict_init();


    //printf("\n[SYSTEM] Flash Builtin Dictionary Size: %u words.\n", 37); // Наша эталонная цепь
    //printf("[SYSTEM] Memory tier allocation frontier secured.\n\n");

    /* 3. Главный интерактивный цикл REPL */
    while (1) {
        char input_buffer[STRING_BUFFER_SIZE];

        /* Выводим текущее состояние стека данных перед каждым вводом */
        printf("Stack: ");
        vm_dump_stack();
        printf("> ");
        fflush(stdout);

        /* Считываем строку из стандартного ввода хоста */
        if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL) {
            break;
        }

        /* Мягкий выход из симулятора */
        if (strncmp(input_buffer, "exit", 4) == 0) {
            break;
        }

        /* Ручной экспорт карты аллокаторов при необходимости */
        if (strncmp(input_buffer, "dump", 4) == 0) {
            forth_export_memory_csv("forth_interactive_dump.csv");
            printf("[DEBUG] Allocators pool layout exported successfully.\n");
            continue;
        }

        /* Экспорт расширенного контекстного снапшота регистров и словаря */
        if (strncmp(input_buffer, "snapshot", 8) == 0) {
            forth_export_runtime_snapshot_csv("context_verification_snapshot.tsv");
            continue;
        }

        /*
         * THE ARCHITECTURAL RECOVERY INTERCEPT:
         * If the previous terminal command tripped a soft exception (Abort),
         * we clear the error state latch right here before feeding the new
         * line entry into the parser loop. This unblocks the text stream
         * while resetting stack pointers back to their pristine baseline.
         */
        if (system_forth_instance.abort_flag) {
            system_forth_instance.abort_flag = 0;
            system_forth_instance.sp = 0;
            system_forth_instance.rp = 0;
            system_forth_instance.state = 0; /* Secure fallback to interactive execution mode */

            purge_sys_file_table ();
        }

        /* Скармливаем строку нашему сквозному, защищенному от пробелов токенизатору */
        forth_interpret_line(input_buffer);
    }

    /* Освобождаем массивы шины перед завершением процесса хоста */
    hw_free();
    printf("--- SIMULATION TERMINATED CLEANLY ---\n");
    return 0;
}

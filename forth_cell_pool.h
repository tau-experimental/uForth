#ifndef FORTH_CELL_POOL_H
#define FORTH_CELL_POOL_H

#include <stdint.h>

/* Параметры пула: 256 ячеек по 4 байта = 1024 байта */
#define CELL_POOL_COUNT        256
#define CELL_SIZE              4
#define CELL_POOL_SIZE         (CELL_POOL_COUNT * CELL_SIZE)
#define CELL_BITMAP_SIZE       (CELL_POOL_COUNT / 8) /* 32 байта */

/* Карта распределения памяти в fast_sram (продолжение за SYS_VARS_BASE) */
/* На нужды SYS_VARS_BASE мы закладывали немного, пусть этот пул начнется с отступа 0x0400 */
//#define CELL_BITMAP_BASE       (FAST_SRAM_BASE + 0x0400)
#define CELL_BITMAP_BASE       (FAST_SRAM_BASE + 0x0800) /* Атомарный пул ячеек */
#define CELL_DATA_BASE         (CELL_BITMAP_BASE + CELL_BITMAP_SIZE)

/* Инициализация пула атомарных переменных */
void cell_pool_init(void);

/* Выделение одной 32-битной ячейки. Возвращает физический адрес в fast_sram или 0, если пул полон */
uint32_t forth_alloc_fast_cell(void);

/* Освобождение ячейки по её физическому адресу */
void forth_free_fast_cell(uint32_t addr);

/* Си-примитивы для Forth-машины (взаимодействие через стек) */
void forth_cmd_fast_cell(void);      /* ( -- sram_address ) */
void forth_cmd_fast_cell_free(void); /* ( sram_address -- ) */

/* Вывод статистики пула для отладочной команды MEM-STAT */
void cell_pool_dump_status(void);

uint32_t debug_get_cell_allocated_count(void);

#endif /* FORTH_CELL_POOL_H */

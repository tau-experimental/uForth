#ifndef FORTH_CHUNK_POOL_H
#define FORTH_CHUNK_POOL_H

#include <stdint.h>

/* Определение конфигурации пула на этапе компиляции */
#define CHUNK_COUNT            20    /* Количество чанков в пуле */
#define CHUNK_SIZE             32    /* Размер одного чанка в байтах */
#define CHUNK_POOL_SIZE        (CHUNK_COUNT * CHUNK_SIZE)

/* Карта распределения памяти в fast_sram (следующий блок за атомарным пулом) */
//#define CHUNK_POOL_BASE        (FAST_SRAM_BASE + 0x0A00)
#define CHUNK_POOL_BASE        (FAST_SRAM_BASE + 0x0C00) /* Структурный пул чанков */

/* Инициализация пула чанков: формирование цепочки свободных блоков */
void chunk_pool_init(void);

/* Выделение одного чанка. Возвращает физический адрес в fast_sram или 0, если пул пуст */
uint32_t forth_alloc_chunk(void);

/* Возврат чанка в пул по его физическому адресу */
void forth_free_chunk(uint32_t addr);

/* Си-примитивы для Forth-машины (взаимодействие через стек) */
void forth_cmd_alloc_chunk(void);    /* ( -- sram_address ) */
void forth_cmd_free_chunk(void);     /* ( sram_address -- ) */

/* Вывод статистики пула для отладочной команды MEM-STAT */
void chunk_pool_dump_status(void);

uint32_t debug_get_chunk_free_head(void);
uint32_t debug_get_chunk_allocated_count(void);

#endif /* FORTH_CHUNK_POOL_H */

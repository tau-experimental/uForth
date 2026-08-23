#ifndef FORTH_HEAP_H
#define FORTH_HEAP_H

#include <stdint.h>

/* Карта распределения внешней памяти */
#define HEAP_BASE_ADDR         EXT_SPI_RAM_BASE
#define HEAP_MANAGED_SIZE      EXT_SPI_RAM_SIZE

/* Инициализация динамической кучи во внешней SPI-RAM */
void heap_init(void);

/* Выделение буфера произвольного размера. Возвращает адрес или 0, если памяти нет */
uint32_t forth_heap_allocate(uint32_t size);

/* Освобождение буфера по его физическому адресу внешней памяти */
uint32_t forth_heap_free(uint32_t payload_addr);

/* Стандартные Forth-примитивы взаимодействия через стек */
void forth_cmd_allocate(void); /* ( bytes -- spi_address error_code ) */
void forth_cmd_free(void);     /* ( spi_address -- error_code ) */

/* Статистика для команды MEM-STAT */
void heap_dump_status(void);

#endif /* FORTH_HEAP_H */

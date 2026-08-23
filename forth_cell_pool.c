#include "forth_cell_pool.h"
#include "hardware.h"
#include "forth_vm.h"
#include <stdio.h>

/*
 * ИНКАПСУЛЯЦИЯ: Состояние атомарного пула теперь полностью изолировано.
 * Битмап и счетчик выделены в Си-память драйвера.
 */
static uint8_t cell_bitmap[CELL_BITMAP_SIZE];
static uint32_t cell_allocated_count = 0;

void cell_pool_init(void) {
    uint32_t i;

    /* Сбрасываем битовую карту в 0 (все ячейки свободны) */
    for (i = 0; i < CELL_BITMAP_SIZE; i++) {
        cell_bitmap[i] = 0;
    }
    cell_allocated_count = 0;
}

void forth_cmd_fast_cell(void) {
    uint32_t byte_idx, bit_idx;

    /* Сканируем битовую карту в поисках первого свободного бита (0) */
    for (byte_idx = 0; byte_idx < CELL_BITMAP_SIZE; byte_idx++) {
        uint8_t bitmap_byte = cell_bitmap[byte_idx];

        if (bitmap_byte != 0xFF) { /* Если в байте есть хотя бы один свободный бит */
            for (bit_idx = 0; bit_idx < 8; bit_idx++) {
                if ((bitmap_byte & (1 << bit_idx)) == 0) {

                    /* Занимаем ячейку: взводим бит в 1 */
                    cell_bitmap[byte_idx] |= (1 << bit_idx);
                    cell_allocated_count++;

                    /* Вычисляем физический адрес ячейки в FAST_SRAM */
                    uint32_t cell_idx = (byte_idx * 8) + bit_idx;
                    uint32_t cell_addr = CELL_DATA_BASE + (cell_idx * CELL_SIZE);

                    /* По правилам безопасности Forth-машины зануляем память при выделении */
                    hw_write32(cell_addr, 0);

                    /* Выталкиваем физический адрес на стек Forth */
                    forth_push(cell_addr);
                    return;
                }
            }
        }
    }

    printf("\n[ALLOC FAULT] Out of Fast Cell Pool memory!\n");
    forth_push(0); /* Возвращаем защитный ноль */
}

void forth_cmd_fast_cell_free(void) {
    uint32_t addr = forth_pop();

    /* Валидация границ пула */
    if (addr < CELL_DATA_BASE || addr >= (CELL_DATA_BASE + CELL_POOL_SIZE)) {
        printf("\n[FREE FAULT] Address 0x%08X is out of Fast Cell Pool bounds!\n", addr);
        return;
    }

    uint32_t offset = addr - CELL_DATA_BASE;
    uint32_t cell_idx = offset / CELL_SIZE;
    uint32_t byte_idx = cell_idx / 8;
    uint32_t bit_idx = cell_idx % 8;

    /* Мягкая обработка Double Free */
    if ((cell_bitmap[byte_idx] & (1 << bit_idx)) == 0) {
        printf("\n[FORTH WARNING] Double free ignored at address 0x%08X (Cell already FREE)!\n", addr);
        return;
    }

    /* Освобождаем ячейку: сбрасываем бит в 0 */
    cell_bitmap[byte_idx] &= ~(1 << bit_idx);
    if (cell_allocated_count > 0) cell_allocated_count--;

    /* Очищаем пользовательские данные при удалении ячейки */
    hw_write32(addr, 0);
}

void cell_pool_dump_status(void) {
    uint32_t total_cells = CELL_BITMAP_SIZE * 8;
    printf("--- FAST CELLS POOL ---\n");
    printf("  Total cells: %u, Allocated: %u, Free: %u\n",
           total_cells, cell_allocated_count, total_cells - cell_allocated_count);
}

/* Экспортные геттеры для нашей отладочной инспекции */
uint8_t debug_get_cell_bitmap_byte(uint32_t byte_idx) {
    if (byte_idx < CELL_BITMAP_SIZE) return cell_bitmap[byte_idx];
    return 0;
}
uint32_t debug_get_cell_allocated_count(void) { return cell_allocated_count; }

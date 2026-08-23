#include "forth_chunk_pool.h"
#include "hardware.h"
#include "forth_vm.h"
#include <stdio.h>
#include <stdlib.h>

static uint32_t chunk_free_list_head = 0;
static uint32_t chunk_allocated_count = 0;

void chunk_pool_init(void) {
    uint32_t i;

    /* Инициализируем Си-переменные вместо записи по макро-адресам */
    chunk_free_list_head = CHUNK_POOL_BASE;
    chunk_allocated_count = 0;

    /* Связываем чанки по SPI-шине памяти в цепочку */
    for (i = 0; i < CHUNK_COUNT - 1; i++) {
        uint32_t current_chunk_addr = CHUNK_POOL_BASE + (i * CHUNK_SIZE);
        uint32_t next_chunk_addr = current_chunk_addr + CHUNK_SIZE;
        hw_write32(current_chunk_addr, next_chunk_addr);
    }

    uint32_t last_chunk_addr = CHUNK_POOL_BASE + ((CHUNK_COUNT - 1) * CHUNK_SIZE);
    hw_write32(last_chunk_addr, 0);
}

/*
 * Обновляем функции выделения и очистки — они теперь работают
 * со стабильными Си-переменными напрямую
 */
void forth_cmd_alloc_chunk(void) {
    if (chunk_free_list_head == 0) {
        printf("\n[ALLOC FAULT] Out of structural chunk memory!\n");
        return;
    }

    uint32_t allocated_addr = chunk_free_list_head;

    /* Читаем из первого слова выделяемого чанка адрес следующего свободного */
    chunk_free_list_head = hw_read32(allocated_addr);
    chunk_allocated_count++;

    /* По правилам безопасности зануляем содержимое выделенного чанка */
    for (uint32_t i = 0; i < CHUNK_SIZE; i += 4) {
        hw_write32(allocated_addr + i, 0);
    }

    forth_push(allocated_addr);
}

void forth_cmd_free_chunk(void) {
    uint32_t chunk_addr = forth_pop();

    /* Валидация границ */
    if (chunk_addr < CHUNK_POOL_BASE || chunk_addr >= (CHUNK_POOL_BASE + (CHUNK_COUNT * CHUNK_SIZE))) {
        printf("[FREE FAULT] Attempt to free invalid chunk address 0x%08X\n", chunk_addr);
        return;
    }

    /* Возвращаем чанк в голову списка свободных */
    hw_write32(chunk_addr, chunk_free_list_head);
    chunk_free_list_head = chunk_addr;

    if (chunk_allocated_count > 0) chunk_allocated_count--;
}

void chunk_pool_dump_status(void) {
    printf("--- STRUCT CHUNKS POOL ---\n");
    printf("  Total chunks: %d, Allocated: %u, Free: %u\n",
           CHUNK_COUNT, chunk_allocated_count, CHUNK_COUNT - chunk_allocated_count);
}

/* Экспортные геттеры для нашей отладочной функции snapshot_csv */
uint32_t debug_get_chunk_free_head(void) { return chunk_free_list_head; }
uint32_t debug_get_chunk_allocated_count(void) { return chunk_allocated_count; }

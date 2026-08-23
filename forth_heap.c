#include "forth_heap.h"
#include "hardware.h"
#include "forth_vm.h"
#include <stdio.h>
#include <stdlib.h>

/* Структура заголовка блока (для работы через функции чтения/записи SPI) */
#define BLOCK_HEADER_SIZE      8
#define OFFSET_SIZE_FLAGS      0
#define OFFSET_NEXT_BLOCK      4

#define BUSY_BIT_MASK          0x00000001
#define SIZE_MASK              0xFFFFFFFE

void heap_init(void) {
    /* При старте создаем один огромный свободный блок во внешней памяти */
    uint32_t initial_block = HEAP_BASE_ADDR;

    /* Размер блока равен всей куче, младший бит = 0 (свободен) */
    hw_write32(initial_block + OFFSET_SIZE_FLAGS, HEAP_MANAGED_SIZE & SIZE_MASK);
    /* Следующего блока пока нет (0) */
    hw_write32(initial_block + OFFSET_NEXT_BLOCK, 0);
}

uint32_t forth_heap_allocate(uint32_t size) {
    /* Выравниваем размер полезной нагрузки по границе 4 байт */
    size = (size + 3) & ~3;
    uint32_t required_total_size = size + BLOCK_HEADER_SIZE;

    uint32_t curr_block = HEAP_BASE_ADDR;

    while (curr_block != 0) {
        uint32_t size_flags = hw_read32(curr_block + OFFSET_SIZE_FLAGS);
        uint32_t block_size = size_flags & SIZE_MASK;
        int is_busy = size_flags & BUSY_BIT_MASK;
        uint32_t next_block = hw_read32(curr_block + OFFSET_NEXT_BLOCK);

        if (!is_busy && block_size >= required_total_size) {
            /* Нашли подходящий свободный блок! Проверяем, можно ли его расколоть */
            if (block_size >= required_total_size + BLOCK_HEADER_SIZE + 4) {
                /* Высчитываем адрес нового (оставшегося) свободного блока */
                uint32_t new_free_block = curr_block + required_total_size;
                uint32_t new_free_size = block_size - required_total_size;

                /* Записываем параметры нового свободного блока */
                hw_write32(new_free_block + OFFSET_SIZE_FLAGS, new_free_size & SIZE_MASK);
                hw_write32(new_free_block + OFFSET_NEXT_BLOCK, next_block);

                /* Корректируем текущий блок: он становится занятым и уменьшается в размере */
                hw_write32(curr_block + OFFSET_SIZE_FLAGS, (required_total_size & SIZE_MASK) | BUSY_BIT_MASK);
                hw_write32(curr_block + OFFSET_NEXT_BLOCK, new_free_block);
            } else {
                /* Блок почти под завязку, отдаем его целиком без раскалывания */
                hw_write32(curr_block + OFFSET_SIZE_FLAGS, block_size | BUSY_BIT_MASK);
            }

            /* Возвращаем физический адрес полезной нагрузки (сразу за заголовком) */
            return curr_block + BLOCK_HEADER_SIZE;
        }

        curr_block = next_block;
    }

    return 0; /* Мало внешней памяти (Out of SPI-RAM Memory) */
}

uint32_t forth_heap_free(uint32_t payload_addr) {
    /* 1. Жесткая валидация границ кучи */
    if (payload_addr < HEAP_BASE_ADDR || payload_addr >= (HEAP_BASE_ADDR + HEAP_MANAGED_SIZE)) {
        printf("[DEBUG HEAP] free ERROR: Address 0x%08X is outside heap bounds!\n", payload_addr);
        return 1;
    }

    /* 2. Смещаемся назад на 8 байт, чтобы считать заголовок целевого блока */
    uint32_t target_header = payload_addr - 8;
    uint32_t target_size_flags = hw_read32(target_header);
    uint32_t target_size = target_size_flags & 0xFFFFFFFE;
    int is_allocated = target_size_flags & 0x00000001;

    if (!is_allocated) {
        printf("[DEBUG HEAP] free ERROR: Double free attempt at address 0x%08X!\n", payload_addr);
        return 1;
    }

    /* 3. Переводим блок в статус СВОБОДЕН (сбрасываем младший бит в 0) */
    hw_write32(target_header, target_size);
    printf("[DEBUG HEAP] Block at 0x%08X (Size: %u) marked as FREE.\n", payload_addr, target_size);

    /* ========================================================================= */
    /* ПОЛНОЦЕННЫЙ АЛГОРИТМ СКЛЕИВАНИЯ (COALESCE КУЧИ SPI-RAM)                   */
    /* ========================================================================= */
    uint32_t curr_block = HEAP_BASE_ADDR;

    while (curr_block != 0) {
        uint32_t curr_size_flags = hw_read32(curr_block);
        uint32_t curr_size = curr_size_flags & 0xFFFFFFFE;
        int curr_busy = curr_size_flags & 0x00000001;
        uint32_t next_block_ptr = hw_read32(curr_block + 4);

        /* Если текущий блок свободен и у него есть физический сосед справа */
        if (!curr_busy && next_block_ptr != 0) {
            uint32_t next_size_flags = hw_read32(next_block_ptr);
            uint32_t next_size = next_size_flags & 0xFFFFFFFE;
            int next_busy = next_size_flags & 0x00000001;

            /* ЕСЛИ СОСЕД ТОЖЕ СВОБОДЕН — СКЛЕИВАЕМ ИХ НАМЕРТВО! */
            if (!next_busy) {
                /* Новый размер = размер текущего + размер соседа + 8 байт его заголовка */
                uint32_t merged_size = curr_size + next_size + 8;
                uint32_t future_next_ptr = hw_read32(next_block_ptr + 4);

                /* Обновляем заголовок текущего блока */
                hw_write32(curr_block, merged_size); /* Младший бит и так 0 (FREE) */
                hw_write32(curr_block + 4, future_next_ptr);

                printf("[DEBUG HEAP] COALESCE SUCCESS: Merged block 0x%08X with 0x%08X. New Size: %u\n",
                       curr_block, next_block_ptr, merged_size);

                /*
                 * Критически важно: не двигаем curr_block вперед на этой итерации,
                 * так как новообразованный блок может граничить со СЛЕДУЮЩИМ свободным блоком!
                 */
                continue;
            }
        }
        /* Двигаемся к следующему блоку по цепочке */
        curr_block = next_block_ptr;
    }

    return 0; /* Успешное завершение транзакции кучи */
}

void heap_dump_status(void) {
    uint32_t curr_block = HEAP_BASE_ADDR;
    uint32_t total_free = 0;
    uint32_t total_allocated = 0;
    uint32_t blocks_count = 0;

    while (curr_block != 0) {
        uint32_t size_flags = hw_read32(curr_block + OFFSET_SIZE_FLAGS);
        uint32_t block_size = size_flags & SIZE_MASK;
        int is_busy = size_flags & BUSY_BIT_MASK;

        if (is_busy) {
            total_allocated += block_size;
        } else {
            total_free += block_size;
        }
        blocks_count++;
        curr_block = hw_read32(curr_block + OFFSET_NEXT_BLOCK);
    }

    printf("--- EXTERNAL SPI-RAM HEAP ---\n");
    printf("  Total managed space: %d KB\n", HEAP_MANAGED_SIZE / 1024);
    printf("  Allocated payload+headers: %d bytes, Free space: %d bytes\n", total_allocated, total_free);
    printf("  Total structural heap blocks: %u\n", blocks_count);
}

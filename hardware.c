#include "hardware.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t fast_sram[FAST_SRAM_SIZE];
static uint8_t *ext_spi_ram = NULL;

int hw_init(void) {
    /* Обнуляем быструю память */
    memset(fast_sram, 0, FAST_SRAM_SIZE);

    /* Эмулируем внешнюю память динамически на ПК */
    ext_spi_ram = (uint8_t *)malloc(EXT_SPI_RAM_SIZE);
    if (!ext_spi_ram) {
        fprintf(stderr, "Critical Error: Could not allocate simulated SPI-RAM\n");
        return 0;
    }
    memset(ext_spi_ram, 0, EXT_SPI_RAM_SIZE);
    return 1;
}

void hw_free(void) {
    if (ext_spi_ram) {
        free(ext_spi_ram);
        ext_spi_ram = NULL;
    }
}

void hw_check_alignment(uint32_t addr, uint32_t alignment) {
    if ((addr % alignment) != 0) {
        fprintf(stderr, "HARDWARE FAULT: Unaligned access at address 0x%08X (alignment %d required)\n", addr, alignment);
        /* На реальном МК здесь будет HardFault_Handler. Для отладки на ПК роняем программу */
        exit(1);
    }
}

uint8_t hw_read8(uint32_t addr) {
    if (addr >= FAST_SRAM_BASE && addr < (FAST_SRAM_BASE + FAST_SRAM_SIZE)) {
        return fast_sram[addr - FAST_SRAM_BASE];
    }
    if (addr >= EXT_SPI_RAM_BASE && addr < (EXT_SPI_RAM_BASE + EXT_SPI_RAM_SIZE)) {
        return ext_spi_ram[addr - EXT_SPI_RAM_BASE];
    }
    fprintf(stderr, "HARDWARE FAULT: Read Access Violation at address 0x%08X\n", addr);
    exit(1);
}

void hw_write8(uint32_t addr, uint8_t val) {
    if (addr >= FAST_SRAM_BASE && addr < (FAST_SRAM_BASE + FAST_SRAM_SIZE)) {
        fast_sram[addr - FAST_SRAM_BASE] = val;
        return;
    }
    if (addr >= EXT_SPI_RAM_BASE && addr < (EXT_SPI_RAM_BASE + EXT_SPI_RAM_SIZE)) {
        ext_spi_ram[addr - EXT_SPI_RAM_BASE] = val;
        return;
    }
    fprintf(stderr, "HARDWARE FAULT: Write Access Violation at address 0x%08X\n", addr);
    exit(1);
}

/* Эмуляция чтения 32-битного слова с проверкой выравнивания памяти */
uint32_t hw_read32(uint32_t addr) {
    hw_check_alignment(addr, 4);

    if (addr >= FAST_SRAM_BASE && addr < (FAST_SRAM_BASE + FAST_SRAM_SIZE)) {
        /* На ПК делаем безопасный кастинг через memcpy для предотвращения UB самого хоста */
        uint32_t val;
        memcpy(&val, &fast_sram[addr - FAST_SRAM_BASE], 4);
        return val;
    }
    if (addr >= EXT_SPI_RAM_BASE && addr < (EXT_SPI_RAM_BASE + EXT_SPI_RAM_SIZE)) {
        uint32_t val;
        memcpy(&val, &ext_spi_ram[addr - EXT_SPI_RAM_BASE], 4);
        return val;
    }
    fprintf(stderr, "HARDWARE FAULT: Read32 Access Violation at address 0x%08X\n", addr);
    exit(1);
}

void hw_write32(uint32_t addr, uint32_t val) {
    hw_check_alignment(addr, 4);

    if (addr >= FAST_SRAM_BASE && addr < (FAST_SRAM_BASE + FAST_SRAM_SIZE)) {
        memcpy(&fast_sram[addr - FAST_SRAM_BASE], &val, 4);
        return;
    }
    if (addr >= EXT_SPI_RAM_BASE && addr < (EXT_SPI_RAM_BASE + EXT_SPI_RAM_SIZE)) {
        memcpy(&ext_spi_ram[addr - EXT_SPI_RAM_BASE], &val, 4);
        return;
    }
    fprintf(stderr, "HARDWARE FAULT: Write32 Access Violation at address 0x%08X\n", addr);
    forth_export_runtime_snapshot_csv("CORE_DUMP.tsv");
    exit(1);
}

/* 16-битные операции (пригодятся для GUI, шрифтов, коротких координат) */
uint16_t hw_read16(uint32_t addr) {
    hw_check_alignment(addr, 2);
    if (addr >= FAST_SRAM_BASE && addr < (FAST_SRAM_BASE + FAST_SRAM_SIZE)) {
        uint16_t val;
        memcpy(&val, &fast_sram[addr - FAST_SRAM_BASE], 2);
        return val;
    }
    if (addr >= EXT_SPI_RAM_BASE && addr < (EXT_SPI_RAM_BASE + EXT_SPI_RAM_SIZE)) {
        uint16_t val;
        memcpy(&val, &ext_spi_ram[addr - EXT_SPI_RAM_BASE], 2);
        return val;
    }
    fprintf(stderr, "HARDWARE FAULT: Read16 Access Violation at address 0x%08X\n", addr);
    exit(1);
}

void hw_write16(uint32_t addr, uint16_t val) {
    hw_check_alignment(addr, 2);
    if (addr >= FAST_SRAM_BASE && addr < (FAST_SRAM_BASE + FAST_SRAM_SIZE)) {
        memcpy(&fast_sram[addr - FAST_SRAM_BASE], &val, 2);
        return;
    }
    if (addr >= EXT_SPI_RAM_BASE && addr < (EXT_SPI_RAM_BASE + EXT_SPI_RAM_SIZE)) {
        memcpy(&ext_spi_ram[addr - EXT_SPI_RAM_BASE], &val, 2);
        return;
    }
    fprintf(stderr, "HARDWARE FAULT: Write16 Access Violation at address 0x%08X\n", addr);
    exit(1);
}

/* Потоковые операции с автоматическим заворачиванием адреса (Ring Boundary) */
void hw_spi_ram_read_buf(uint32_t addr, uint8_t *dest, uint32_t len) {
    uint32_t i;
    for (i = 0; i < len; i++) {
        uint32_t current_addr = addr + i;
        /* Если вышли за границы 1 МБ SPI-RAM, заворачиваем в начало пула SPI-RAM */
        if (current_addr >= (EXT_SPI_RAM_BASE + EXT_SPI_RAM_SIZE)) {
            current_addr = EXT_SPI_RAM_BASE + (current_addr % EXT_SPI_RAM_SIZE);
        }
        dest[i] = hw_read8(current_addr);
    }
}

void hw_spi_ram_write_buf(uint32_t addr, const uint8_t *src, uint32_t len) {
    uint32_t i;
    for (i = 0; i < len; i++) {
        uint32_t current_addr = addr + i;
        if (current_addr >= (EXT_SPI_RAM_BASE + EXT_SPI_RAM_SIZE)) {
            current_addr = EXT_SPI_RAM_BASE + (current_addr % EXT_SPI_RAM_SIZE);
        }
        hw_write8(current_addr, src[i]);
    }
}

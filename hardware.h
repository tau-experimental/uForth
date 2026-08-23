#ifndef HARDWARE_H
#define HARDWARE_H

#include <stdint.h>
#include <stddef.h>

/* Жесткие аппаратные ограничения целевой платформы */
#define FAST_SRAM_SIZE      (32 * 1024)       /* 32 КБ быстрой памяти */
#define EXT_SPI_RAM_SIZE    (1024 * 1024)     /* 1 МБ внешней SPI-RAM */

/* Карты адресов для диспетчера памяти */
#define FAST_SRAM_BASE      0x20000000
#define EXT_SPI_RAM_BASE    0x60000000

/* Глобальный пул быстрой памяти (физически на МК это статический массив в SRAM) */
extern uint8_t fast_sram[FAST_SRAM_SIZE];

/* Инициализация и деинициализация аппаратной прослойки эмулятора */
int  hw_init(void);
void hw_free(void);

/* Интерфейс доступа ко всем типам памяти (Диспетчер шины) */
uint8_t  hw_read8(uint32_t addr);
uint16_t hw_read16(uint32_t addr);
uint32_t hw_read32(uint32_t addr);

void hw_write8(uint32_t addr, uint8_t val);
void hw_write16(uint32_t addr, uint16_t val);
void hw_write32(uint32_t addr, uint32_t val);

/* Потоковые операции для SPI-RAM (имитация Sequential Read/Write) */
void hw_spi_ram_read_buf(uint32_t addr, uint8_t *dest, uint32_t len);
void hw_spi_ram_write_buf(uint32_t addr, const uint8_t *src, uint32_t len);

/* Вспомогательная функция проверки выравнивания для 16 и 32-битных слов */
void hw_check_alignment(uint32_t addr, uint32_t alignment);

#endif /* HARDWARE_H */

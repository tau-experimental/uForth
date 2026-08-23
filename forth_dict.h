#ifndef FORTH_DICT_H
#define FORTH_DICT_H

#include <stdint.h>

/* Максимальная длина имени Forth-слова (без учета нулевого терминатора) */
#define FORTH_MAX_WORD_LEN    33
#define TOKEN_BUFFER_SIZE     36 // с запасом и для выравнивания, было: (FORTH_MAX_WORD_LEN + 1)

#define FORTH_MAX_STRING_LEN    255
#define STRING_BUFFER_SIZE     (FORTH_MAX_STRING_LEN + 1)

/* Флаги компилятора Forth */
#define FLAG_IMMEDIATE  0x80
#define LEN_MASK        0x1F
/* Переменные состояния компилятора (живут в SYS_VARS_BASE) */
#define ADDR_STATE           (SYS_VARS_BASE + 24) /* 0 - исполнение, 1 - компиляция */

/* Си-коллбэк для встроенных примитивов */
typedef void (*forth_xt_t)(void);

/* Определение структуры встроенного (Flash) слова для Си-компилятора */
typedef struct forth_word_builtin {
    const struct forth_word_builtin *link;
    uint8_t flags_len;
    const char *name;
    forth_xt_t xt;
} forth_word_builtin_t;

/* Глобальный флаг тишины (0 - REPL с выводом ok/compiled, 1 - тихий парсинг файлов) */
extern volatile uint32_t vm_quiet_mode;

/* Инициализация подсистемы словаря */
void dict_init(void);

/* Поиск слова по имени (сначала ищет в SPI-RAM, затем во встроенном словаре Flash) */
uint32_t dict_find(const char *name, forth_xt_t *out_builtin_xt);

/* Регистрация нового слова во внешней SPI-RAM */
//void dict_add_word(const char *name, uint32_t xt_address, uint8_t flags);
void dict_add_word(const char *name);

/* Главная функция REPL: обработка текстовой строки */
void forth_interpret_line(const char *line);

/* Функция запуска шитого кода пользовательского слова */
void forth_execute_xt(uint32_t xt);

#endif /* FORTH_DICT_H */

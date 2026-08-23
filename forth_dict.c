#include "forth_dict.h"
#include "hardware.h"
#include "forth_vm.h"
#include "forth_cell_pool.h"
#include "forth_chunk_pool.h"
#include "forth_heap.h"
#include "forth_debug.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define DICT_SPI_RAM_START   (EXT_SPI_RAM_BASE + 0x80000)

static int line_comment_flag = 0; /* Внутренний флаг пропуска комментариев до конца строки */

static const char *const flash_init_constants[] = {
    "$22 constant B.HEX",   /* Байт в Hex */
    "$23 constant B.BIN",   /* Байт в двоичном виде */
    "$42 constant W.HEX",   /* 16-битное Слово в Hex */
    "$43 constant W.BIN",   /* 16-битное Слово в двоичном виде */
    "$13 constant N.BIN",   /* 4-битная Тетрада в двоичном виде */
    "$10 constant N.DEC"    /* 4-битная Тетрада со знаком */
};

#define INIT_CONSTANTS_COUNT  (sizeof(flash_init_constants) / sizeof(flash_init_constants[0]))

/* Перечисление уникальных ID для встроенных Си-примитивов (Execution Tokens) */
enum {
    XT_NONE = 0,
	XT_ABORT,                 /* abort */
    XT_EXIT,
    XT_CR,
    XT_DOT,
	XT_DOT_UNSIGNED, /* u. */
	XT_DOT_HEX,		/* h. */
	XT_DOT_BINARY,	/* b. */
	XT_DOT_FORMATTED,
    XT_MUL,
    XT_SUB,
    XT_ADD,
    XT_AND,        /* and */
    XT_OR,         /* or */
    XT_XOR,        /* xor */
    XT_INVERT,     /* invert */
    XT_LSHIFT,     /* lshift (логический сдвиг влево) */
    XT_RSHIFT,     /* rshift (логический сдвиг вправо) */
    XT_ARSHIFT,     /* arshift (арифметический сдвиг вправо) */
    XT_DROP,
    XT_OVER,
    XT_SWAP,
    XT_DUP,
    XT_0EQUAL, /* НОВЫЙ ТОКЕН ШАГА 8 */
    /* ОПЕРАТОРЫ ОТНОШЕНИЙ ШАГА 8 */
    XT_EQUAL,       /* =  */
    XT_NOT_EQUAL,   /* <> */
    XT_LESS_THAN,   /* <  */
    XT_GREATER_THAN,/* >  */
    XT_FAST_CELL_FREE,
    XT_FAST_CELL,
    XT_FREE_CHUNK,
    XT_ALLOC_CHUNK,
    XT_FREE,
    XT_ALLOCATE,

	XT_TYPE,				/* type */
	XT_COUNT,                 /* count */

    XT_SEMICOLON,
    XT_COLON,
    XT_DOT_QUOTE,     /* ." */
    XT_PRIMITIVE_P_DOT_QUOTE, /* Скрытый рантайм-примитив (.") */
    XT_S_QUOTE,               /* s" */
    XT_PRIMITIVE_P_S_QUOTE,   /* Скрытый рантайм-примитив (s") */
    XT_INCLUDED,               /* Теперь это честный Си-примитив ядра! */
    /* НОВЫЙ ТОКЕН ДЛЯ ВЫГРУЗКИ КАРТЫ ПАМЯТИ */
    XT_MEM_DUMP,
    /* ТОКЕНЫ РАБОТЫ С ПАМЯТЬЮ */
	XT_STORE,       /* !  */
	XT_FETCH,       /* @  */
	XT_C_STORE,     /* c! */
	XT_C_FETCH,     /* c@ */

    XT_LIT,
    XT_BRANCH,
    XT_0BRANCH,
    XT_CMD_IF,
    XT_CMD_ELSE,
    XT_CMD_THEN,

    XT_CMD_BEGIN,
    XT_CMD_UNTIL,
    XT_CMD_AGAIN,
	XT_BACKSLASH, /* НОВЫЙ ТОКЕН ШАГА 9 ДЛЯ КОММЕНТАРИЕВ */
    XT_CMD_CHAR,      /* CHAR */
    XT_CMD_BRACKET_CHAR, /* [CHAR] */
	XT_EMIT,
    XT_TO_R,   /* >r */
    XT_FROM_R,  /* r> */
	XT_CMD_VARIABLE,          /* variable keyword compiler token */
	XT_PRIMITIVE_P_VARIABLE,   /* The hidden execution-time worker token */
    XT_CMD_CONSTANT,            /* constant */
    XT_PRIMITIVE_P_CONSTANT,     /* Скрытый рантайм-обработчик константы */
	XT_CMD_VALUE,            /* value */
	XT_PRIMITIVE_P_TO,
    XT_CMD_TO,               /* to */
    XT_PRIMITIVE_P_VALUE,     /* Скрытый рантайм-обработчик значения */
	XT_F_OPEN,              /* s" filename" f-open  ( c-addr len -- file-id ) */
	XT_F_CREATE,              /* s" filename" f-create  ( c-addr len -- file-id ) */
	XT_F_WRITE,               /* buf-addr len file-id f-write ( -- )              */
	XT_F_READ,                /* buf-addr max file-id f-read  ( -- actual-len )   */
	XT_F_CLOSE                /* file-id f-close              ( -- ) */
};

void forth_primitive_abort(void) {
    /* Вызываем наш готовый контекстный сброс, который мы отладили ранее */
    forth_abort_with_context("Script requested execution ABORT");
}

/* Си-функции примитивов */
void forth_add(void) { int32_t b = forth_pop(); int32_t a = forth_pop(); forth_push(a + b); }
void forth_sub(void) { int32_t b = forth_pop(); int32_t a = forth_pop(); forth_push(a - b); }
void forth_mul(void) { int32_t b = forth_pop(); int32_t a = forth_pop(); forth_push(a * b); }

void forth_primitive_and(void) {
    if (!current_forth_vm) return;
    uint32_t b = forth_pop();
    uint32_t a = forth_pop();
    if (!current_forth_vm->abort_flag) {
        forth_push(a & b);
    }
}

void forth_primitive_or(void) {
    if (!current_forth_vm) return;
    uint32_t b = forth_pop();
    uint32_t a = forth_pop();
    if (!current_forth_vm->abort_flag) {
        forth_push(a | b);
    }
}

void forth_primitive_xor(void) {
    if (!current_forth_vm) return;
    uint32_t b = forth_pop();
    uint32_t a = forth_pop();
    if (!current_forth_vm->abort_flag) {
        forth_push(a ^ b);
    }
}

void forth_primitive_invert(void) {
    if (!current_forth_vm) return;
    uint32_t a = forth_pop();
    if (!current_forth_vm->abort_flag) {
        forth_push(~a); /* Побитовое НЕ */
    }
}

void forth_primitive_lshift(void) {
    if (!current_forth_vm) return;
    uint32_t shift_count = forth_pop();
    uint32_t val = forth_pop();
    if (!current_forth_vm->abort_flag) {
        /* Безопасное ограничение сдвига для 32-битного процессора */
        if (shift_count >= 32) forth_push(0);
        else forth_push(val << shift_count);
    }
}

void forth_primitive_rshift(void) {
    if (!current_forth_vm) return;
    uint32_t shift_count = forth_pop();
    uint32_t val = forth_pop();
    if (!current_forth_vm->abort_flag) {
        if (shift_count >= 32) forth_push(0);
        else forth_push(val >> shift_count); /* Логический беззнаковый сдвиг */
    }
}

void forth_primitive_arshift(void) {
    if (!current_forth_vm) return;
    uint32_t shift_count = forth_pop();
    uint32_t val = forth_pop();
    if (current_forth_vm->abort_flag) return;

    if (shift_count >= 32) {
        forth_push((val & 0x80000000) ? 0xFFFFFFFF : 0);
        return;
    }

    int32_t signed_val = (int32_t)val;

    /*
     * ИСПРАВЛЕНИЕ: Полностью изолируем проверки границ.
     * Сначала проверяем самую узкую маску - Байт.
     * Если число укладывается в 8 бит и взведен 7-й бит (0x80), расширяем знак!
     */
    if ((val & 0xFFFFFF00) == 0) {
        if (val & 0x80) {
            signed_val |= 0xFFFFFF00;
        }
    }
    /* Если число не уложилось в байт, но укладывается в 16-битное слово */
    else if ((val & 0xFFFF0000) == 0) {
        if (val & 0x8000) {
            signed_val |= 0xFFFF0000;
        }
    }

    /* Теперь сдвиг вправо на Си-уровне выполнится с идеальным размножением знака */
    forth_push((uint32_t)(signed_val >> shift_count));
}

void forth_dot(void) { int32_t a = forth_pop(); printf("%d ", a); }
void forth_primitive_dot_unsigned(void) {
    if (!current_forth_vm) return;
    uint32_t val = forth_pop();
    if (!current_forth_vm->abort_flag) {
        /* Печатаем как строго беззнаковое 32-битное число */
        printf("%u ", val);
    }
}

void forth_primitive_dot_hex(void) {
    if (!current_forth_vm) return;
    uint32_t val = forth_pop();
    if (!current_forth_vm->abort_flag) {
        /* Печатаем в красивом Hex-формате с заглавными буквами */
        printf("0x%08X ", val);
    }
}

void forth_primitive_dot_binary(void) { /* вот уж действительно примитив! */
    if (!current_forth_vm) return;
    uint32_t val = forth_pop();
    if (!current_forth_vm->abort_flag) {
    	int i;
    	putchar ('%');
    	for (i=31; i>=0; i--) {
    		if (val & (0x01<<i)) {
    			putchar('1');
    		} else {
    			putchar('0');
    		}
    	}
    	putchar(' ');
    }
}

/* Вспомогательная функция для ручного вывода двоичных чисел заданной ширины */
static void print_binary_width(uint32_t val, uint32_t width_bits) {
    /* Печатаем биты слева направо, начиная со старшего для указанной ширины */
    for (int32_t i = (int32_t)width_bits - 1; i >= 0; i--) {
        putchar((val & (1U << i)) ? '1' : '0');
    }
}

void forth_primitive_dot_formatted(void) {
    if (!current_forth_vm) return;

    /* Снимаем аргументы со стека Форта */
    uint32_t descriptor = forth_pop();
    uint32_t raw_val = forth_pop();

    if (current_forth_vm->abort_flag) return;

    /* Расшифровываем битовую маску дескриптора */
    uint8_t width_mode = (descriptor >> 4) & 0x0F;
    uint8_t radix_mode = descriptor & 0x0F;

    uint32_t width_bits = 32;
    uint32_t mask = 0xFFFFFFFF;

    /* 1. Определяем разрядность и накладываем аппаратную маску */
    switch (width_mode) {
        case 0x1: width_bits = 4;  mask = 0x0000000F; break; /* Тетрада */
        case 0x2: width_bits = 8;  mask = 0x000000FF; break; /* Байт */
        case 0x4: width_bits = 16; mask = 0x0000FFFF; break; /* 16-битное слово */
        default:  width_bits = 32; mask = 0xFFFFFFFF; break; /* 32-битное слово */
    }

    uint32_t prepared_val = raw_val & mask;

    /* 2. Определяем базис счисления и способ представления знака */
    switch (radix_mode) {
        case 0x0: { /* Десятичное со знаком */
            /* Восстанавливаем знак (Sign Extension) в зависимости от выбранной ширины */
            int32_t signed_val = (int32_t)prepared_val;
            if (width_mode == 0x1 && (prepared_val & 0x8))  signed_val |= 0xFFFFFFF0;
            if (width_mode == 0x2 && (prepared_val & 0x80)) signed_val |= 0xFFFFFF00;
            if (width_mode == 0x4 && (prepared_val & 0x8000)) signed_val |= 0xFFFF0000;
            printf("%d ", signed_val);
            break;
        }
        case 0x1: { /* Десятичное БЕЗ знака */
            printf("%u ", prepared_val);
            break;
        }
        case 0x2: { /* Шестнадцатеричное */
            /* Настраиваем ширину вывода Hex под выбранный тип данных */
            if (width_mode == 0x1)      printf("0x%X ", prepared_val);
            else if (width_mode == 0x2) printf("0x%02X ", prepared_val);
            else if (width_mode == 0x4) printf("0x%04X ", prepared_val);
            else                        printf("0x%08X ", prepared_val);
            break;
        }
        case 0x3: { /* Двоичное */
            putchar('%');
            print_binary_width(prepared_val, width_bits);
            putchar(' ');
            break;
        }
        default: {
            /* Задел на будущее (вещественные числа и т.д.) */
            printf("%u ", prepared_val);
            break;
        }
    }
}

void forth_cr(void)  { printf("\n"); }

void forth_0equal(void) {
    int32_t a = (int32_t)forth_pop();
    forth_push(a == 0 ? -1 : 0);
}

void forth_equal(void) {
    int32_t b = (int32_t)forth_pop();
    int32_t a = (int32_t)forth_pop();
    forth_push ((a == b) ? -1 : 0);
}

void forth_not_equal(void) {
    int32_t b = (int32_t)forth_pop();
    int32_t a = (int32_t)forth_pop();
    forth_push ((a != b) ? -1 : 0);
}

void forth_less_than(void) {
    int32_t b = (int32_t)forth_pop();
    int32_t a = (int32_t)forth_pop();
    forth_push ((a < b) ? -1 : 0);
}

void forth_greater_than(void) {
    int32_t b = (int32_t)forth_pop();
    int32_t a = (int32_t)forth_pop();
    forth_push ((a > b) ? -1 : 0);
}

void forth_cmd_semicolon(void);
void forth_cmd_colon(void);
void forth_cmd_if(void);
void forth_cmd_else(void);
void forth_cmd_then(void);

void forth_cmd_included(void);
void forth_primitive_included(void);

void forth_cmd_mem_dump(void) {
    /* Вызываем наш готовый CSV-экспорт */
    forth_export_memory_csv("forth_runtime_dump.csv");
}

/* Вспомогательная функция для вычленения следующего токена из строки парсера */
static char get_next_char_from_line(const char **line_ptr) {
    const char *p = *line_ptr;
    /* Пропускаем пробелы перед символом-аргументом */
    while (*p == ' ' || *p == '\t') p++;

    char ch = *p;
    /* Сдвигаем указатель парсера за пределы этого символа/слова */
    while (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != '\0') p++;
    *line_ptr = p;

    return ch;
}

void forth_cmd_char(const char **line_ptr) {
    char ch = get_next_char_from_line(line_ptr);
    if (ch != '\0') {
        forth_push((uint32_t)ch);
    } else {
        forth_abort("CHAR missing argument");
    }
}

void forth_cmd_bracket_char(const char **line_ptr) {
    char ch = get_next_char_from_line(line_ptr);
    if (ch != '\0') {
        /*
         * Поскольку мы находимся внутри определения, [CHAR] работает как макрос:
         * он компилирует токен XT_LIT и сам ASCII-код символа в SPI-RAM!
         */
        uint32_t free_ptr = current_forth_vm->dict_free_ptr;
        hw_write32(free_ptr, XT_LIT);
        hw_write32(free_ptr + 4, (uint32_t)ch);
        hw_write32(current_forth_vm->dict_free_ptr, free_ptr + 8);
    } else {
        forth_abort("[CHAR] missing argument");
    }
}

void forth_primitive_p_s_quote(void) {
    /* 1. Читаем длину строки из ячейки шитого кода */
    uint32_t str_len = hw_read32(current_forth_vm->ip);
    current_forth_vm->ip += 4;

    /* 2. Кладем на стек Forth адрес начала текста во внешней памяти и длину */
    forth_push(current_forth_vm->ip);
    forth_push(str_len);

    /* 3. Сдвигаем IP вперед за пределы текста с учетом 4-байтового выравнивания */
    uint32_t aligned_len = (str_len + 3) & ~3;
    current_forth_vm->ip += aligned_len;
}

void forth_cmd_s_quote(const char **line_ptr) {
    uint32_t state = current_forth_vm->state;;
    const char *p = *line_ptr;

    if (*p == ' ') p++;

    /* Декларативный буфер без хардкода */
    char str_buf[STRING_BUFFER_SIZE];
    uint32_t len = 0;

    /* Цикл строго контролируется макросом */
    while (*p != '\0' && len < FORTH_MAX_STRING_LEN) {
        /*
         * ПРОВЕРКА ЭКРАНИРОВАНИЯ:
         * Если встретили '\' и за ним идет кавычка '"' или еще один '\'
         */
        if (*p == '\\' && (*(p + 1) == '"' || *(p + 1) == '\\')) {
            p++; /* Пропускаем сам экранирующий слэш */
            str_buf[len++] = *p++; /* Записываем символ за ним (кавычку) как обычный текст */
            continue;
        }

        /* Если встретили обычную (неэкранированную) закрывающую кавычку — строка окончена */
        if (*p == '"') {
            break;
        }

        /* Обычный символ строки */
        str_buf[len++] = *p++;
    }
    if (*p == '"') p++; /* Пропускаем закрывающую кавычку */
    str_buf[len] = '\0';

    *line_ptr = p;

    if (state == 1) {
        uint32_t free_ptr = current_forth_vm->dict_free_ptr;
        hw_write32(free_ptr, XT_PRIMITIVE_P_S_QUOTE);
        hw_write32(free_ptr + 4, len);

        /* Потоковый пакетный сброс в SPI-RAM */
        hw_spi_ram_write_buf(free_ptr + 8, (const uint8_t *)str_buf, len);

        uint32_t aligned_len = (len + 3) & ~3;
        current_forth_vm->dict_free_ptr = free_ptr + 8 + aligned_len;
    } else {
        /* Для REPL используем безопасный хвост SPI-RAM */
        uint32_t temp_str_addr = EXT_SPI_RAM_BASE + EXT_SPI_RAM_SIZE - STRING_BUFFER_SIZE;
        hw_spi_ram_write_buf(temp_str_addr, (const uint8_t *)str_buf, len);
        forth_push(temp_str_addr);
        forth_push(len);
    }
}

void forth_primitive_count(void) {
    if (!current_forth_vm) return;

    /* Снимаем базовый адрес строки со стека */
    uint32_t base_addr = forth_pop();
    if (current_forth_vm->abort_flag) return;

    /* 1. Читаем один байт длины по этому адресу из нашей Memory-Mapped памяти */
    uint8_t str_len = hw_read8(base_addr);

    /* 2. Выталкиваем на стек адрес самого текста (+1 байт вперед) и считанную длину */
    forth_push(base_addr + 1);
    forth_push((uint32_t)str_len);
}

void forth_primitive_type(void) {
    if (!current_forth_vm) return;
    uint32_t len = forth_pop();
    uint32_t addr = forth_pop();
    if (current_forth_vm->abort_flag) return;

    char temp_buf[STRING_BUFFER_SIZE];
    while (len > 0) {
        uint32_t chunk = (len > FORTH_MAX_STRING_LEN) ? FORTH_MAX_STRING_LEN : len;
        hw_spi_ram_read_buf(addr, (uint8_t *)temp_buf, chunk);

        /* Выводим в стандартный поток */
        for (uint32_t i = 0; i < chunk; i++) {
            putchar(temp_buf[i]);
        }
        len -= chunk;
        addr += chunk;
    }
}

/* Рантайм-обработчик: срабатывает, когда пользователь вызывает константу по имени */
void forth_primitive_p_constant(void) {
    /* Виртуальный IP указывает прямо на 4-байтовую ячейку со значением внутри тела слова */
    uint32_t data_addr = current_forth_vm->ip;
    uint32_t val = hw_read32(data_addr);

    /* Выталкиваем неизменяемое значение на стек данных */
    forth_push(val);

    /* Корректно выходим из подпрограммы через стек возвратов */
    uint32_t saved_ip = forth_r_pop();
    current_forth_vm->ip = saved_ip;
}

/* Компилятор: создаёт слово-константу, забирая число со стека */
void forth_cmd_constant(const char **line_ptr) {
    const char *p = *line_ptr;
    while (*p == ' ' || *p == '\t') p++;

    char name_buf[TOKEN_BUFFER_SIZE];
    uint32_t len = 0;
    while (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != '\0' && len < FORTH_MAX_WORD_LEN) {
        name_buf[len++] = *p++;
    }
    name_buf[len] = '\0';
    *line_ptr = p;

    if (len == 0) {
        forth_abort_with_context("CONSTANT missing name argument");
        return;
    }

    /* Снимаем число со стека данных, которое было передано ПЕРЕД словом constant */
    uint32_t const_val = forth_pop();
    if (current_forth_vm->abort_flag) return;

    /* Создаем стандартный заголовок слова в SPI-RAM */
    dict_add_word(name_buf);

    uint32_t free_ptr = current_forth_vm->dict_free_ptr;

    /* Записываем скрытый токен рантайма константы */
    hw_write32(free_ptr, XT_PRIMITIVE_P_CONSTANT);
    /* Записываем само значение */
    hw_write32(free_ptr + 4, const_val);

    current_forth_vm->dict_free_ptr = free_ptr + 8;
}

/* Рантайм-обработчик: срабатывает, когда пользователь вызывает созданное слово VALUE */
void forth_primitive_p_value(void) {
    /* Виртуальный IP указывает прямо на 4-байтовую ячейку данных внутри тела слова */
    uint32_t data_addr = current_forth_vm->ip;
    uint32_t val = hw_read32(data_addr);

    /* Выталкиваем само значение на стек данных */
    forth_push(val);

    /* Корректно выходим из подпрограммы через стек возвратов */
    uint32_t saved_ip = forth_r_pop();
    current_forth_vm->ip = saved_ip;
}

/* Компилятор: срабатывает, когда в REPL пишут '0 value MY-VAL' */
void forth_cmd_value(const char **line_ptr) {
    const char *p = *line_ptr;
    while (*p == ' ' || *p == '\t') p++;

    char name_buf[TOKEN_BUFFER_SIZE];
    uint32_t len = 0;
    while (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != '\0' && len < FORTH_MAX_WORD_LEN) {
        name_buf[len++] = *p++;
    }
    name_buf[len] = '\0';
    *line_ptr = p;

    if (len == 0) {
        forth_abort_with_context("VALUE missing name argument");
        return;
    }

    /* Снимаем со стека начальное значение, которое было передано перед словом value */
    uint32_t init_val = forth_pop();
    if (current_forth_vm->abort_flag) return;

    /* Создаем стандартный заголовок слова в SPI-RAM */
    dict_add_word(name_buf);

    uint32_t free_ptr = current_forth_vm->dict_free_ptr;
    /* Записываем скрытый токен рантайма */
    hw_write32(free_ptr, XT_PRIMITIVE_P_VALUE);
    /* Записываем начальное значение */
    hw_write32(free_ptr + 4, init_val);

    current_forth_vm->dict_free_ptr = free_ptr + 8;
}

/* Новое рантайм-слово: выполнится тогда, когда запустится RUN-LIFECYCLE */
void forth_primitive_p_to(void) {
    /* Виртуальный IP указывает на XT-адрес целевого value-слова в шитом коде */
    uint32_t ip = current_forth_vm->ip;
    uint32_t target_value_xt = hw_read32(ip);

    /* Снимаем число со стека данных (которое туда положит выполненный fast-cell) */
    uint32_t new_val = forth_pop();

    if (!current_forth_vm->abort_flag) {
        /* Патчим ячейку данных внутри тела этого value-слова (смещение +4 от XT) */
        hw_write32(target_value_xt + 4, new_val);
    }

    /* Сдвигаем IP вперед за пределы сохраненного XT-адреса */
    current_forth_vm->ip = ip + 4;
}

/* компилятор префикса TO / -> */
void forth_cmd_to(const char **line_ptr) {
    uint32_t state = current_forth_vm->state;
    const char *p = *line_ptr;

    while (*p == ' ' || *p == '\t') p++;

    char name_buf[TOKEN_BUFFER_SIZE];
    uint32_t len = 0;
    while (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != '\0' && len < FORTH_MAX_WORD_LEN) {
        name_buf[len++] = *p++;
    }
    name_buf[len] = '\0';
    *line_ptr = p;

    forth_xt_t builtin_xt = NULL;
    uint32_t xt = dict_find(name_buf, &builtin_xt);

    if (xt == 0 || builtin_xt != NULL) {
        forth_abort_with_context("TO/-> target must be a valid user-defined VALUE");
        return;
    }

    if (state == 1) {
        /*
         * РЕЖИМ КОМПИЛЯЦИИ (Внутри файла):
         * Никаких forth_pop()! Мы просто пишем в шитый код инструкцию рантайм-записи
         * XT_PRIMITIVE_P_TO, а следом за ней - XT-адрес нашего value-слова.
         */
        uint32_t free_ptr = current_forth_vm->dict_free_ptr;

        hw_write32(free_ptr, XT_PRIMITIVE_P_TO);
        hw_write32(free_ptr + 4, xt); /* Сохраняем адрес мишени прямо в коде */

        current_forth_vm->dict_free_ptr = free_ptr + 8;
    } else {
        /* РЕЖИМ REPL (Прямой ввод из консоли) - старая рабочая логика */
        uint32_t first_token = hw_read32(xt);
        if (first_token == XT_PRIMITIVE_P_VALUE) {
            uint32_t new_val = forth_pop();
            if (current_forth_vm->abort_flag) return;
            hw_write32(xt + 4, new_val);
        } else {
            forth_abort_with_context("TO/-> target is not a VALUE");
        }
    }
}

/* The Hidden Worker: What happens when an instantiated variable is actually executed */
void forth_primitive_p_variable(void) {
    /*
     * When a word executes, our Instruction Pointer (IP) points right inside its body.
     * For a variable, the 4-byte storage cell sits immediately after the XT token cell!
     * So the data address is simply the current value of our IP.
     */
    uint32_t data_cell_address = current_forth_vm->ip;

    /* Push the physical data cell address straight onto the Forth data stack */
    forth_push(data_cell_address);

    /* Exit the word cleanly by pulling the original return vector off the return stack */
    uint32_t saved_return_ip = forth_r_pop();
    current_forth_vm->ip = saved_return_ip;
}

/* The Compiler: What happens when you manually type 'variable <name>' in the console */
void forth_cmd_variable(const char **line_ptr) {
    const char *p = *line_ptr;

    /* Skip leading whitespace characters to isolate the upcoming name string */
    while (*p == ' ' || *p == '\t') p++;

    char name_buf[TOKEN_BUFFER_SIZE];
    uint32_t len = 0;

    while (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && *p != '\0' && len < FORTH_MAX_WORD_LEN) {
        name_buf[len++] = *p++;
    }
    name_buf[len] = '\0';
    *line_ptr = p;

    if (len == 0) {
        forth_abort_with_context("VARIABLE missing name argument");
        return;
    }

    /* 1. Compile a standard word header into our SPI-RAM dictionary tree */
    dict_add_word(name_buf);

    /* 2. Append our hidden execution-time worker token as the word's primary body instruction */
    uint32_t free_ptr = current_forth_vm->dict_free_ptr;
    hw_write32(free_ptr, XT_PRIMITIVE_P_VARIABLE);

    /* 3. Allocate 4 bytes of empty storage cell space initialized to 0 */
    hw_write32(free_ptr + 4, 0);

    /* 4. Advance the compilation frontier past both the execution token and the data cell (8 bytes) */
    current_forth_vm->dict_free_ptr = free_ptr + 8;
}

void forth_primitive_p_dot_quote(void) {
    //uint32_t ip = current_forth_vm->ip;
    uint32_t str_len = hw_read32(current_forth_vm->ip);
    current_forth_vm->ip += 4;

    uint32_t i;
    for (i = 0; i < str_len; i++) {
        putchar((char)hw_read8(current_forth_vm->ip + i));
    }

    uint32_t aligned_len = (str_len + 3) & ~3;
    current_forth_vm->ip += aligned_len;
}

void forth_primitive_store(void) {
    uint32_t addr = forth_pop();
    uint32_t val  = forth_pop();
    hw_write32(addr, val);
}

void forth_primitive_fetch(void) {
    uint32_t addr = forth_pop();
    uint32_t val  = hw_read32(addr);
    forth_push(val);
}

void forth_primitive_c_store(void) {
    uint32_t addr = forth_pop();
    uint8_t  val  = (uint8_t)forth_pop();
    hw_write8(addr, val);
}

void forth_primitive_c_fetch(void) {
    uint32_t addr = forth_pop();
    uint8_t  val  = hw_read8(addr);
    forth_push((uint32_t)val);
}

void forth_primitive_to_r(void) {
    /* Сняли со стека данных, положили на стек возвратов */
    uint32_t val = forth_pop();
    forth_r_push(val);
}

void forth_primitive_from_r(void) {
    /* Сняли со стека возвратов, вернули на стек данных */
    uint32_t val = forth_r_pop();
    forth_push(val);
}

void forth_primitive_exit(void) {
    //uint32_t saved_ip = forth_r_pop();
    //hw_write32(SYS_VARS_BASE + 28, saved_ip);
	current_forth_vm->ip = forth_r_pop();
}

/* Реализация примитивов управления логикой выполнения */
void forth_primitive_lit(void) {
    //uint32_t ip = current_forth_vm->ip;
    //uint32_t literal_val = hw_read32(ip);
    //forth_push(literal_val);
    //hw_write32(SYS_VARS_BASE + 28, ip + 4); /* Пропускаем ячейку с числом */

    uint32_t literal_val = hw_read32(current_forth_vm->ip);
    forth_push(literal_val);
    current_forth_vm->ip += 4;
}

void forth_primitive_branch(void) {
    //uint32_t ip = current_forth_vm->ip;
    //int32_t offset = (int32_t)hw_read32(ip);

	//int32_t offset = (int32_t)hw_read32(current_forth_vm->ip);
	//current_forth_vm->ip += offset;
	current_forth_vm->ip += (int32_t)hw_read32(current_forth_vm->ip);
}

void forth_primitive_0branch(void) {
#if 0
	uint32_t flag;
	//uint32_t flag = forth_pop();
    //uint32_t ip = current_forth_vm->ip;
    if (flag == 0) {
        //int32_t offset = (int32_t)hw_read32(ip);
        //hw_write32(SYS_VARS_BASE + 28, ip + offset);
    	current_forth_vm->ip += (int32_t)hw_read32(current_forth_vm->ip);
    } else {
        //hw_write32(SYS_VARS_BASE + 28, ip + 4);
    	current_forth_vm->ip += 4;
    }
#endif

    int32_t offset = (forth_pop() == 0) ? (int32_t)hw_read32(current_forth_vm->ip) : 4;
    current_forth_vm->ip += offset;
}

void forth_primitive_emit(void) {
    char ch = (char)forth_pop();
    if (!current_forth_vm->abort_flag) {
        putchar(ch);
    }
}

/* Си-обработчик для слова обратного слэша: взводит флаг тишины до конца текущей строки */
void forth_cmd_backslash(void) {
    line_comment_flag = 1;
}

#define MAX_OPEN_FILES  4
/* Изолированный массив 64-битных Си-указателей хоста */
static FILE *sys_file_table[MAX_OPEN_FILES] = { NULL };

void forth_primitive_f_open(void) {
    if (!current_forth_vm) return;

    uint32_t name_len = forth_pop();
    uint32_t name_addr = forth_pop();
    if (current_forth_vm->abort_flag) return;

    char filename[STRING_BUFFER_SIZE];
    if (name_len > FORTH_MAX_STRING_LEN) name_len = FORTH_MAX_STRING_LEN;
    hw_spi_ram_read_buf(name_addr, (uint8_t *)filename, name_len);
    filename[name_len] = '\0';

    uint32_t slot_idx = 0;
    for (uint32_t i = 0; i < MAX_OPEN_FILES; i++) {
        if (sys_file_table[i] == NULL) {
            slot_idx = i + 1;
            break;
        }
    }

    if (slot_idx == 0) {
        printf("[FATFS ERROR] File table full!\n");
        forth_push(0);
        return;
    }

    /* Открываем существующий файл ТОЛЬКО НА ЧТЕНИЕ */
    FILE *f = fopen(filename, "r");
    if (!f) {
        printf("[FATFS ERROR] Cannot open file '%s' for reading\n", filename);
        forth_push(0);
        return;
    }

    sys_file_table[slot_idx - 1] = f;
    forth_push(slot_idx);
}

void forth_primitive_f_create(void) {
   if (!current_forth_vm) return;

	uint32_t name_len = forth_pop();
	uint32_t name_addr = forth_pop();
	if (current_forth_vm->abort_flag) return;

	char filename[STRING_BUFFER_SIZE];
	if (name_len > FORTH_MAX_STRING_LEN) name_len = FORTH_MAX_STRING_LEN;
	hw_spi_ram_read_buf(name_addr, (uint8_t *)filename, name_len);
	filename[name_len] = '\0';

	/* Ищем свободный слот в таблице файлов */
	uint32_t slot_idx = 0;
	for (uint32_t i = 0; i < MAX_OPEN_FILES; i++) {
		if (sys_file_table[i] == NULL) {
			slot_idx = i + 1; /* Индексы Forth будут от 1 до MAX */
			break;
		}
	}

	if (slot_idx == 0) {
		printf("[FATFS ERROR] File table full! Close other files first.\n");
		forth_push(0);
		return;
	}

	FILE *f = fopen(filename, "w+");
	if (!f) {
		printf("[FATFS ERROR] Cannot create file '%s'\n", filename);
		forth_push(0);
		return;
	}

	/* Сохраняем реальный 64-битный указатель в Си-таблицу */
	sys_file_table[slot_idx - 1] = f;

	/* Выталкиваем БЕЗОПАСНЫЙ 32-битный индекс слота на стек Forth */
	forth_push(slot_idx);
}

void forth_primitive_f_write(void) {
    if (!current_forth_vm) return;

    uint32_t slot_id = forth_pop();
    uint32_t buf_len = forth_pop();
    uint32_t buf_addr = forth_pop();
    if (current_forth_vm->abort_flag) return;

    /* Валидация числового индекса */
    if (slot_id == 0 || slot_id > MAX_OPEN_FILES || sys_file_table[slot_id - 1] == NULL) {
        forth_abort_with_context("F-WRITE ERROR: Invalid or closed file handle index");
        return;
    }

    FILE *f = sys_file_table[slot_id - 1];
    char temp_write_buffer[STRING_BUFFER_SIZE];

    if (buf_len > FORTH_MAX_STRING_LEN) buf_len = FORTH_MAX_STRING_LEN;
    hw_spi_ram_read_buf(buf_addr, (uint8_t *)temp_write_buffer, buf_len);

    size_t written = fwrite(temp_write_buffer, 1, buf_len, f);
    if (written < buf_len) {
        forth_abort_with_context("F-WRITE ERROR: Disk IO stream full");
    }
}

void forth_primitive_f_read(void) {
    if (!current_forth_vm) return;

    uint32_t slot_id = forth_pop();
    uint32_t max_bytes = forth_pop();
    uint32_t dest_buf_addr = forth_pop();
    if (current_forth_vm->abort_flag) return;

    if (slot_id == 0 || slot_id > MAX_OPEN_FILES || sys_file_table[slot_id - 1] == NULL) {
        forth_push(0);
        return;
    }

    FILE *f = sys_file_table[slot_id - 1];
    char temp_read_buffer[STRING_BUFFER_SIZE];
    if (max_bytes > FORTH_MAX_STRING_LEN) max_bytes = FORTH_MAX_STRING_LEN;

    size_t bytes_fetched = fread(temp_read_buffer, 1, max_bytes, f);
    if (bytes_fetched > 0) {
        hw_spi_ram_write_buf(dest_buf_addr, (const uint8_t *)temp_read_buffer, bytes_fetched);
    }

    forth_push((uint32_t)bytes_fetched);
}

void forth_primitive_f_close(void) {
    if (!current_forth_vm) return;

    uint32_t slot_id = forth_pop();
    if (current_forth_vm->abort_flag) return;

    if (slot_id > 0 && slot_id <= MAX_OPEN_FILES && sys_file_table[slot_id - 1] != NULL) {
        fclose(sys_file_table[slot_id - 1]);
        sys_file_table[slot_id - 1] = NULL; /* Освобождаем слот в таблице */
    }
}

/* Вспомогательная Си-структура для снятия метрик памяти */
typedef struct {
    uint32_t fast_cells;
    uint32_t chunks;
    uint32_t spi_ram_used;
} mem_metrics_t;

static mem_metrics_t get_current_mem_metrics(void) {
    mem_metrics_t m;
    uint32_t byte_idx, bit_idx;

    /* 1. Считаем быстрые ячейки */
    m.fast_cells = 0;
    for (byte_idx = 0; byte_idx < CELL_BITMAP_SIZE; byte_idx++) {
        uint8_t b = hw_read8(CELL_BITMAP_BASE + byte_idx);
        for (bit_idx = 0; bit_idx < 8; bit_idx++) {
            if (b & (1 << bit_idx)) m.fast_cells++;
        }
    }

    /* 2. Считаем чанки */
    m.chunks = debug_get_chunk_allocated_count();

    /* 3. Считаем объем пользовательского словаря во внешней SPI-RAM */
    uint32_t dict_free_ptr = current_forth_vm->dict_free_ptr;
    m.spi_ram_used = dict_free_ptr - DICT_SPI_RAM_START;

    return m;
}

void forth_cmd_begin(void) {
    uint32_t free_ptr = current_forth_vm->dict_free_ptr;
    /* Просто сохраняем текущий адрес компиляции на стек возвратов RP как точку возврата */
    forth_r_push(free_ptr);
}

void forth_cmd_until(void) {
    uint32_t free_ptr = current_forth_vm->dict_free_ptr;
    /* Достаем адрес точки начала цикла BEGIN со стека RP */
    uint32_t begin_addr = forth_r_pop();

    /* 1. Компилируем условный переход назад */
    hw_write32(free_ptr, XT_0BRANCH);

    /* 2. Высчитываем отрицательное относительное смещение назад */
    /* Смещение считается от ячейки самого смещения (free_ptr + 4) до begin_addr */
    int32_t offset = (int32_t)begin_addr - (int32_t)(free_ptr + 4);

    hw_write32(free_ptr + 4, (uint32_t)offset);

    hw_write32(current_forth_vm->dict_free_ptr, free_ptr + 8);
}

void forth_cmd_again(void) {
    uint32_t free_ptr = current_forth_vm->dict_free_ptr;
    uint32_t begin_addr = forth_r_pop();

    /* 1. Компилируем безусловный переход назад */
    hw_write32(free_ptr, XT_BRANCH);

    /* 2. Высчитываем смещение назад */
    int32_t offset = (int32_t)begin_addr - (int32_t)(free_ptr + 4);

    hw_write32(free_ptr + 4, (uint32_t)offset);

    hw_write32(current_forth_vm->dict_free_ptr, free_ptr + 8);
}

void forth_cmd_dot_quote(const char **line_ptr) {
    uint32_t state = current_forth_vm->state;;
    const char *p = *line_ptr;

    if (*p == ' ') p++;

    char str_buf[STRING_BUFFER_SIZE];
    uint32_t len = 0;

    while (*p != '\0' && len < FORTH_MAX_STRING_LEN) {
        /*
         * ПРОВЕРКА ЭКРАНИРОВАНИЯ:
         * Если встретили '\' и за ним идет кавычка '"' или еще один '\'
         */
        if (*p == '\\' && (*(p + 1) == '"' || *(p + 1) == '\\')) {
            p++; /* Пропускаем сам экранирующий слэш */
            str_buf[len++] = *p++; /* Записываем символ за ним (кавычку) как обычный текст */
            continue;
        }

        /* Если встретили обычную (неэкранированную) закрывающую кавычку — строка окончена */
        if (*p == '"') {
            break;
        }

        /* Обычный символ строки */
        str_buf[len++] = *p++;
    }
    if (*p == '"') p++; /* Пропускаем закрывающую кавычку */
    str_buf[len] = '\0';

    *line_ptr = p;

    if (state == 1) {
        uint32_t free_ptr = current_forth_vm->dict_free_ptr;
        hw_write32(free_ptr, XT_PRIMITIVE_P_DOT_QUOTE);
        hw_write32(free_ptr + 4, len);

        hw_spi_ram_write_buf(free_ptr + 8, (const uint8_t *)str_buf, len);

        uint32_t aligned_len = (len + 3) & ~3;
        current_forth_vm->dict_free_ptr = free_ptr + 8 + aligned_len;
    } else {
        printf("%s", str_buf);
    }
}

void forth_cmd_allocate(void) {
	if (!current_forth_vm) return;
    uint32_t bytes = forth_pop();
    printf("[DEBUG HEAP] allocate invoked! Requested size: %u bytes. Current SP before push: %u\n",
    		bytes, current_forth_vm->sp);

    uint32_t addr = forth_heap_allocate(bytes);

    if (addr == 0) {
    	printf("[DEBUG HEAP] allocation FAILED! Out of memory in SPI-RAM heap.\n");
        forth_push(0);  /* Сбой выделения памяти */
        forth_push(-1); /* Код ошибки в Forth (True/-1 означает ошибку в ALLOCATE) */
    } else {
    	printf("[DEBUG HEAP] Memory successfully allocated at 0x%08X\n", addr);
        forth_push(addr);
        forth_push(0);  /* 0 означает отсутствие ошибок */
    }
}

void forth_cmd_free(void) {
	if (!current_forth_vm) return;
    uint32_t addr = forth_pop();
    if (current_forth_vm->abort_flag) return;
    printf("[DEBUG HEAP] free invoked for address 0x%08X\n", addr);
#if 0
    uint32_t result = forth_heap_free(addr);
    if (result == 0) {
        printf("[DEBUG HEAP] Memory cleanly released and merged.\n");
        forth_push(0); /* Код ошибки: УСПЕХ */
    } else {
        printf("[DEBUG HEAP] free FAILED! Invalid address or double free.\n");
        forth_push(1); /* Код ошибки: СБОЙ */
    }
#else
    uint32_t result = forth_heap_free(addr);
    forth_push(result);
#endif
}

/* Диспетчер вызова нативных Си-функций по их стабильному ID */
static void execute_native_id(uint32_t token_id) {
    switch (token_id) {
        case XT_EXIT:           forth_primitive_exit(); break;
        case XT_ABORT:          forth_primitive_abort(); break;
        case XT_CR:             forth_cr(); break;
        case XT_DOT:            forth_dot(); break;
        case XT_DOT_UNSIGNED:   forth_primitive_dot_unsigned(); break;
        case XT_DOT_HEX:        forth_primitive_dot_hex(); break;
        case XT_DOT_BINARY:     forth_primitive_dot_binary(); break;
        case XT_DOT_FORMATTED: 	forth_primitive_dot_formatted(); break;
        case XT_MUL:            forth_mul(); break;
        case XT_SUB:            forth_sub(); break;
        case XT_ADD:            forth_add(); break;
        case XT_AND:            forth_primitive_and(); break;
        case XT_OR:             forth_primitive_or(); break;
        case XT_XOR:            forth_primitive_xor(); break;
        case XT_INVERT:         forth_primitive_invert(); break;
        case XT_LSHIFT:         forth_primitive_lshift(); break;
        case XT_RSHIFT:         forth_primitive_rshift(); break;
        case XT_ARSHIFT:        forth_primitive_arshift(); break;
        case XT_DROP:           forth_drop(); break;
        case XT_OVER:           forth_over(); break;
        case XT_SWAP:           forth_swap(); break;
        case XT_DUP:            forth_dup(); break;
        case XT_0EQUAL:         forth_0equal(); break; /* ИСПРАВЛЕНИЕ */
        /* РЕГИСТРАЦИЯ ОПЕРАТОРОВ ОТНОШЕНИЙ */
        case XT_EQUAL:          forth_equal(); break;
        case XT_NOT_EQUAL:      forth_not_equal(); break;
        case XT_LESS_THAN:      forth_less_than(); break;
        case XT_GREATER_THAN:   forth_greater_than(); break;
        case XT_FAST_CELL_FREE: forth_cmd_fast_cell_free(); break;
        case XT_FAST_CELL:      forth_cmd_fast_cell(); break;
        case XT_FREE_CHUNK:     forth_cmd_free_chunk(); break;
        case XT_ALLOC_CHUNK:    forth_cmd_alloc_chunk(); break;
        case XT_TYPE: 			forth_primitive_type(); break;
        case XT_COUNT:          forth_primitive_count(); break;
        case XT_FREE:           forth_cmd_free(); break;
        case XT_ALLOCATE:       forth_cmd_allocate(); break;
        /* ИСПРАВЛЕНИЕ: Добавляем системные команды компилятора в диспетчер нативных ID */
        case XT_SEMICOLON:      forth_cmd_semicolon(); break;
        case XT_COLON:          forth_cmd_colon(); break;
        case XT_MEM_DUMP:       forth_cmd_mem_dump(); break;
        case XT_LIT:            forth_primitive_lit(); break;
        case XT_BRANCH:         forth_primitive_branch(); break;
        case XT_0BRANCH:        forth_primitive_0branch(); break;
        case XT_CMD_IF:         forth_cmd_if(); break;
        case XT_CMD_ELSE:       forth_cmd_else(); break;
        case XT_CMD_THEN:       forth_cmd_then(); break;
        case XT_CMD_BEGIN:      forth_cmd_begin(); break;
        case XT_CMD_UNTIL:      forth_cmd_until(); break;
        case XT_CMD_AGAIN:      forth_cmd_again(); break;
        case XT_STORE:          forth_primitive_store(); break;
        case XT_FETCH:          forth_primitive_fetch(); break;
        case XT_C_STORE:        forth_primitive_c_store(); break;
        case XT_C_FETCH:        forth_primitive_c_fetch(); break;
        case XT_BACKSLASH:      forth_cmd_backslash(); break;
        case XT_PRIMITIVE_P_DOT_QUOTE: forth_primitive_p_dot_quote(); break;
        case XT_PRIMITIVE_P_S_QUOTE: forth_primitive_p_s_quote(); break;
        case XT_INCLUDED:            forth_primitive_included(); break;
        case XT_EMIT:           forth_primitive_emit(); break;
        case XT_TO_R:           forth_primitive_to_r(); break;
        case XT_FROM_R:         forth_primitive_from_r(); break;
        case XT_PRIMITIVE_P_VALUE: forth_primitive_p_value(); break;
		case XT_CMD_VALUE:         /* Прямой перехват в парсере */ break;
		case XT_CMD_TO:            /* Прямой перехват в парсере */ break;
		case XT_PRIMITIVE_P_TO: forth_primitive_p_to(); break;
        case XT_PRIMITIVE_P_VARIABLE: forth_primitive_p_variable(); break;
		case XT_CMD_VARIABLE: {
			/* Handled direct-path via our interpret intercept parser line loop below */
		}; break;
		case XT_PRIMITIVE_P_CONSTANT: forth_primitive_p_constant(); break;
		case XT_CMD_CONSTANT:          /* Прямой перехват в парсере */ break;
        /* РЕГИСТРАЦИЯ СИМВОЛЬНЫХ ПАРСЕРОВ В ОБЩЕМ ДИСПЕТЧЕРЕ ТОКЕНОВ */
        case XT_CMD_CHAR: {
            /*
             * Если слово вызвано через XT-токен, у нас в этот миг нет прямой строки.
             * В реальном встраиваемом Forth в этот момент берется системный указатель
             * на текущий буфер ввода терминала TIB (Terminal Input Buffer).
             * Пока заложим вызов с заглушкой текущего контекста или предупреждением:
             */
            printf("[FORTH WARNING] CHAR invoked via XT requires active TIB context.\n");
        } break;
        case XT_CMD_BRACKET_CHAR: {
            printf("[FORTH WARNING] [CHAR] is IMMEDIATE and should not be invoked via XT.\n");
        }; break;

        case XT_F_CREATE:       forth_primitive_f_create(); break;
        case XT_F_OPEN:       forth_primitive_f_open(); break;
        case XT_F_WRITE:        forth_primitive_f_write(); break;
        case XT_F_READ:         forth_primitive_f_read(); break;
        case XT_F_CLOSE:        forth_primitive_f_close(); break;

        default:
            printf("[FORTH FAULT] Attempt to execute unknown Native ID: %u\n", token_id);
            exit(1);
    }
}

/* Диспетчер выполнения шитого кода */
void forth_execute_xt(uint32_t xt) {
    if (!current_forth_vm) return;

    uint32_t current_ip = xt;
    uint32_t old_ip = current_forth_vm->ip;
    forth_r_push(old_ip);

    current_forth_vm->ip = current_ip;

    while (current_forth_vm->ip != 0) {
        if (current_forth_vm->abort_flag) return;

        uint32_t ip = current_forth_vm->ip;
        uint32_t token_xt = hw_read32(ip);
        current_forth_vm->ip = ip + 4;

        if (token_xt < EXT_SPI_RAM_BASE) {
            execute_native_id(token_xt);
        } else {
            forth_execute_xt(token_xt);
        }
    }
}

/* Создание заголовка нового слова во внешней памяти SPI-RAM */
void dict_add_word(const char *name) {
    if (!current_forth_vm) return;

    /*
     * ИСПРАВЛЕНИЕ: Читаем указатель фронтира компиляции и адрес
     * последнего слова напрямую из Си-полей активного контекста!
     */
    uint32_t free_ptr = current_forth_vm->dict_free_ptr;
    uint32_t latest = current_forth_vm->latest_word;
    uint32_t name_len = strlen(name);

    if (name_len > FORTH_MAX_WORD_LEN) name_len = FORTH_MAX_WORD_LEN;

    /* 1. Записываем поле Link (указатель на предыдущее слово) во внешнюю память */
    hw_write32(free_ptr, latest);

    /* 2. Записываем поле Flags/Length (1 байт) */
    hw_write8(free_ptr + 4, (uint8_t)name_len);

    /* 3. Потоком гоним имя ASCII во внешнюю память */
    hw_spi_ram_write_buf(free_ptr + 5, (const uint8_t *)name, name_len);

    /* 4. Высчитываем адрес начала тела слова с выравниванием по границе 4 байт */
    uint32_t body_ptr = free_ptr + 5 + name_len;
    body_ptr = (body_ptr + 3) & ~3;

    /* 5. ИСПРАВЛЕНИЕ: Обновляем регистры ВМ прямо в Си-структуре! */
    current_forth_vm->latest_word = free_ptr;
    current_forth_vm->dict_free_ptr = body_ptr;
}

/* Реализация слов компиляции */
void forth_cmd_colon(void) {
    if (!current_forth_vm) return;
    /* ИСПРАВЛЕНИЕ: Переключаем ВМ в режим компиляции напрямую через поле структуры */
    current_forth_vm->state = 1;
}

void forth_cmd_semicolon(void) {
	if (!current_forth_vm) return;
    uint32_t free_ptr = current_forth_vm->dict_free_ptr;

    /* Компилируем стабильный ID примитива EXIT вместо Си-указателя */
    hw_write32(free_ptr, XT_EXIT);
    free_ptr += 4;

    current_forth_vm->dict_free_ptr = free_ptr;
    current_forth_vm->state = 0;
}

void forth_cmd_if(void) {
	if (!current_forth_vm) return;
    uint32_t free_ptr = current_forth_vm->dict_free_ptr;
    hw_write32(free_ptr, XT_0BRANCH);
    hw_write32(free_ptr + 4, 0); /* Резерв под смещение */
    forth_r_push(free_ptr + 4);  /* Запоминаем адрес для патча на стеке RP */
    current_forth_vm->dict_free_ptr = free_ptr + 8;
}

void forth_cmd_then(void) {
	if (!current_forth_vm) return;
    uint32_t free_ptr = current_forth_vm->dict_free_ptr;
    uint32_t patch_addr = forth_r_pop();
    int32_t offset = (int32_t)(free_ptr - patch_addr);
    hw_write32(patch_addr, (uint32_t)offset); /* Патчим относительное смещение */
}

void forth_cmd_else(void) {
	if (!current_forth_vm) return;
    uint32_t free_ptr = current_forth_vm->dict_free_ptr;
    hw_write32(free_ptr, XT_BRANCH);
    hw_write32(free_ptr + 4, 0); /* Резерв под смещение выхода из ELSE */
    uint32_t new_patch_addr = free_ptr + 4;
    free_ptr += 8;

    /* Закрываем предыдущий условный переход от IF на начало блока ELSE */
    uint32_t if_patch_addr = forth_r_pop();
    int32_t if_offset = (int32_t)(free_ptr - if_patch_addr);
    hw_write32(if_patch_addr, (uint32_t)if_offset);

    forth_r_push(new_patch_addr); /* Передаем новый патч для слова THEN */
    current_forth_vm->dict_free_ptr = free_ptr;
}


/* Структура встроенного словаря, привязанная к стабильным ID (XT-токенам) */
extern const forth_word_builtin_t w_semicolon;

static const forth_word_builtin_t w_cr     = { NULL,     2, "cr",     (forth_xt_t)XT_CR };

static const forth_word_builtin_t w_dot_hex  = { &w_cr,       2, "h.",   (forth_xt_t)XT_DOT_HEX };
static const forth_word_builtin_t w_dot_uns  = { &w_dot_hex,  2, "u.",   (forth_xt_t)XT_DOT_UNSIGNED };
static const forth_word_builtin_t w_dot_bin  = { &w_dot_uns,  2, "b.",   (forth_xt_t)XT_DOT_BINARY };
static const forth_word_builtin_t w_dot_fmt  = { &w_dot_bin,  2, "Q.", (forth_xt_t)XT_DOT_FORMATTED };

static const forth_word_builtin_t w_dot    = { &w_dot_fmt,    1, ".",      (forth_xt_t)XT_DOT };
static const forth_word_builtin_t w_mul    = { &w_dot,   1, "*",      (forth_xt_t)XT_MUL };
static const forth_word_builtin_t w_sub    = { &w_mul,   1, "-",      (forth_xt_t)XT_SUB };
static const forth_word_builtin_t w_add    = { &w_sub,   1, "+",      (forth_xt_t)XT_ADD };

static const forth_word_builtin_t w_arshift = { &w_add, 7, "arshift", (forth_xt_t)XT_ARSHIFT };
static const forth_word_builtin_t w_rshift  = { &w_arshift, 6, "rshift",  (forth_xt_t)XT_RSHIFT };
static const forth_word_builtin_t w_lshift  = { &w_rshift,  6, "lshift",  (forth_xt_t)XT_LSHIFT };
static const forth_word_builtin_t w_invert  = { &w_lshift,  6, "invert",  (forth_xt_t)XT_INVERT };
static const forth_word_builtin_t w_xor     = { &w_invert,  3, "xor",     (forth_xt_t)XT_XOR };
static const forth_word_builtin_t w_or      = { &w_xor,     2, "or",      (forth_xt_t)XT_OR };
static const forth_word_builtin_t w_and     = { &w_or,      3, "and",  (forth_xt_t)XT_AND };

static const forth_word_builtin_t w_drop   = { &w_and,   4, "drop",   (forth_xt_t)XT_DROP };
static const forth_word_builtin_t w_over   = { &w_drop,  4, "over",   (forth_xt_t)XT_OVER };
static const forth_word_builtin_t w_swap   = { &w_over,  4, "swap",   (forth_xt_t)XT_SWAP };

static const forth_word_builtin_t w_c_fetch  = { &w_swap,      2, "c@", (forth_xt_t)XT_C_FETCH };
static const forth_word_builtin_t w_c_store  = { &w_c_fetch,   2, "c!", (forth_xt_t)XT_C_STORE };
static const forth_word_builtin_t w_fetch    = { &w_c_store,   1, "@",  (forth_xt_t)XT_FETCH };
static const forth_word_builtin_t w_store    = { &w_fetch,     1, "!",  (forth_xt_t)XT_STORE };

static const forth_word_builtin_t w_greater   = { &w_store,     1, ">",  (forth_xt_t)XT_GREATER_THAN };
static const forth_word_builtin_t w_less      = { &w_greater,  1, "<",  (forth_xt_t)XT_LESS_THAN };
static const forth_word_builtin_t w_not_equal = { &w_less,     2, "<>", (forth_xt_t)XT_NOT_EQUAL };
static const forth_word_builtin_t w_equal     = { &w_not_equal,1, "=",  (forth_xt_t)XT_EQUAL };
static const forth_word_builtin_t w_0equal = { &w_equal,   2, "0=",     (forth_xt_t)XT_0EQUAL };

static const forth_word_builtin_t w_dup    = { &w_0equal, 3, "dup",    (forth_xt_t)XT_DUP };
static const forth_word_builtin_t w_f_free = { &w_dup,   14, "fast-cell-free", (forth_xt_t)XT_FAST_CELL_FREE };
static const forth_word_builtin_t w_f_cell = { &w_f_free, 9,  "fast-cell",      (forth_xt_t)XT_FAST_CELL };
static const forth_word_builtin_t w_c_free = { &w_f_cell, 10, "free-chunk",     (forth_xt_t)XT_FREE_CHUNK };
static const forth_word_builtin_t w_c_alloc= { &w_c_free, 11, "alloc-chunk",    (forth_xt_t)XT_ALLOC_CHUNK };
static const forth_word_builtin_t w_h_free = { &w_c_alloc,4,  "free",           (forth_xt_t)XT_FREE };
static const forth_word_builtin_t w_h_alloc= { &w_h_free, 8,  "allocate",       (forth_xt_t)XT_ALLOCATE };
static const forth_word_builtin_t w_mem_dump = { &w_h_alloc, 8, "mem-dump",  (forth_xt_t)XT_MEM_DUMP };

static const forth_word_builtin_t w_type = { &w_mem_dump, 4, "type",  (forth_xt_t)XT_TYPE };
static const forth_word_builtin_t w_count    = { &w_type,       5, "count",    (forth_xt_t)XT_COUNT };

static const forth_word_builtin_t w_again = { &w_count, FLAG_IMMEDIATE | 5, "again", (forth_xt_t)XT_CMD_AGAIN };
static const forth_word_builtin_t w_until = { &w_again,   FLAG_IMMEDIATE | 5, "until", (forth_xt_t)XT_CMD_UNTIL };
static const forth_word_builtin_t w_begin = { &w_until,   FLAG_IMMEDIATE | 5, "begin", (forth_xt_t)XT_CMD_BEGIN };

static const forth_word_builtin_t w_f_close  = { &w_begin,     7, "f-close",  (forth_xt_t)XT_F_CLOSE };
static const forth_word_builtin_t w_f_read   = { &w_f_close,   6, "f-read",   (forth_xt_t)XT_F_READ };
static const forth_word_builtin_t w_f_write  = { &w_f_read,    7, "f-write",  (forth_xt_t)XT_F_WRITE };
static const forth_word_builtin_t w_f_create = { &w_f_write,   8, "f-create", (forth_xt_t)XT_F_CREATE };
static const forth_word_builtin_t w_f_open = { &w_f_create,   6, "f-open", (forth_xt_t)XT_F_OPEN };

static const forth_word_builtin_t w_abort    = { &w_f_open,   5, "abort",    (forth_xt_t)XT_ABORT };
static const forth_word_builtin_t w_included  = { &w_abort,  8, "included", (forth_xt_t)XT_INCLUDED };
static const forth_word_builtin_t w_bracket_char = { &w_included, FLAG_IMMEDIATE | 6, "[CHAR]", (forth_xt_t)XT_CMD_BRACKET_CHAR };
static const forth_word_builtin_t w_char         = { &w_bracket_char, 4, "CHAR", (forth_xt_t)XT_CMD_CHAR };

static const forth_word_builtin_t w_variable = { &w_char, 8, "variable", (forth_xt_t)XT_CMD_VARIABLE };

static const forth_word_builtin_t w_emit = { &w_variable, 4, "emit", (forth_xt_t)XT_EMIT };
static const forth_word_builtin_t w_from_r = { &w_emit,   2, "r>", (forth_xt_t)XT_FROM_R };
static const forth_word_builtin_t w_to_r   = { &w_from_r, 2, ">r", (forth_xt_t)XT_TO_R };

static const forth_word_builtin_t w_to_arrow       = { &w_to_r,  FLAG_IMMEDIATE | 2, "->",    (forth_xt_t)XT_CMD_TO };
static const forth_word_builtin_t w_to_to       = { &w_to_arrow,  FLAG_IMMEDIATE | 2, "to",    (forth_xt_t)XT_CMD_TO };
static const forth_word_builtin_t w_value    = { &w_to_to,        5, "value", (forth_xt_t)XT_CMD_VALUE };
static const forth_word_builtin_t w_constant = { &w_value, 8, "constant", (forth_xt_t)XT_CMD_CONSTANT };
static const forth_word_builtin_t w_backslash    = { &w_constant,  FLAG_IMMEDIATE | 1, "\\", (forth_xt_t)XT_BACKSLASH };

static const forth_word_builtin_t w_s_quote   = { &w_backslash, FLAG_IMMEDIATE | 2, "s\"", (forth_xt_t)XT_S_QUOTE };
static const forth_word_builtin_t w_dotquote = { &w_s_quote, FLAG_IMMEDIATE | 1, ".\"", (forth_xt_t)XT_DOT_QUOTE };
static const forth_word_builtin_t w_then  = { &w_dotquote,    FLAG_IMMEDIATE | 4, "then", (forth_xt_t)XT_CMD_THEN };
static const forth_word_builtin_t w_else  = { &w_then,    FLAG_IMMEDIATE | 4, "else", (forth_xt_t)XT_CMD_ELSE };
static const forth_word_builtin_t w_if    = { &w_else,    FLAG_IMMEDIATE | 2, "if",   (forth_xt_t)XT_CMD_IF };
const forth_word_builtin_t w_semicolon = { &w_if, FLAG_IMMEDIATE | 1, ";", (forth_xt_t)XT_SEMICOLON };
 const forth_word_builtin_t w_colon     = { &w_semicolon, 1, ":", (forth_xt_t)XT_COLON };

static const forth_word_builtin_t *builtin_root = &w_colon;

void dict_init(void) {
    current_forth_vm->latest_word = 0;
    current_forth_vm->state =  0;
    /* Сброс виртуального IP при старте системы */
    current_forth_vm->ip = 0;
    current_forth_vm->quiet_mode = 1;

	printf("[SYSTEM] Compiling core constants from Flash memory to SPI-RAM...\n");

	/* Прямолинейный, надежный и экономный прогон строк из Flash */
	for (uint32_t i = 0; i < INIT_CONSTANTS_COUNT; i++) {
		forth_interpret_line(flash_init_constants[i]);

		/* Жесткий контроль: если где-то опечатка, МК сообщит об этом при старте */
		if (current_forth_vm->abort_flag) {
			printf("[FATAL STARTUP ERROR] Fault compiling constant expression: '%s'\n",
				   flash_init_constants[i]);
			return;
		}
	}

	/* Возвращаем консоль в стандартный интерактивный режим */
	current_forth_vm->quiet_mode = 0;
	printf("[SYSTEM] %u system format constants successfully secured.\n", INIT_CONSTANTS_COUNT);
}

uint32_t dict_find(const char *name, forth_xt_t *out_builtin_xt) {
    uint32_t name_len = strlen(name);
    uint32_t curr_word = current_forth_vm->latest_word;

    while (curr_word != 0) {
        uint8_t flags_len = hw_read8(curr_word + 4);
        uint32_t word_len = flags_len & LEN_MASK;

        if (word_len == name_len) {
            char dict_name[TOKEN_BUFFER_SIZE];
            uint32_t i;
            for (i = 0; i < word_len && i < FORTH_MAX_WORD_LEN; i++) {
                dict_name[i] = (char)hw_read8(curr_word + 5 + i);
            }
            dict_name[word_len] = '\0';

            if (strcasecmp(dict_name, name) == 0) {
                *out_builtin_xt = NULL;
                uint32_t xt_addr = curr_word + 5 + word_len;
                return (xt_addr + 3) & ~3;
            }
        }
        curr_word = hw_read32(curr_word);
    }

    const forth_word_builtin_t *curr_builtin = builtin_root;
    while (curr_builtin != NULL) {
        if (strcasecmp(curr_builtin->name, name) == 0) {
            *out_builtin_xt = curr_builtin->xt;
            /* Возвращаем числовой ID токена в качестве XT */
            return (uint32_t)(uintptr_t)(curr_builtin->xt);
        }
        curr_builtin = curr_builtin->link;
    }
    return 0;
}

/*
 * ALL-INCLUSIVE EMBEDDED NUMBER PARSER
 * Natively supports:
 *  - Standard Forth HEX prefix:      $DEADBEEF, $10
 *  - Standard Forth BINARY prefix:   %10101100, %1111
 *  - Standard C HEX prefix:          0xDEADBEEF
 *  - Standard signed decimals:       123, -45
 */
static int parse_forth_number(const char *token, uint32_t *out_val) {
    char *endptr;
    int base = 10;
    const char *num_ptr = token;
    int is_negative = 0;

    /* Handle unary negative sign if present (e.g., -$10 or -%101) */
    if (*num_ptr == '-') {
        is_negative = 1;
        num_ptr++;
    }

    /* 1. Detect standard Forth HEX prefix '$' */
    if (*num_ptr == '$' && *(num_ptr + 1) != '\0') {
        base = 16;
        num_ptr++;
    }
    /* 2. Detect standard Forth BINARY prefix '%' */
    else if (*num_ptr == '%' && *(num_ptr + 1) != '\0') {
        base = 2;
        num_ptr++;
    }
    /* 3. Detect standard C HEX prefix '0x' or '0X' */
    else if (*num_ptr == '0' && (*(num_ptr + 1) == 'x' || *(num_ptr + 1) == 'X') && *(num_ptr + 2) != '\0') {
        base = 16;
        num_ptr += 2;
    }

    /* Safe C-runtime execution under the mapped base constraints */
    long long parsed_val = strtoll(num_ptr, &endptr, base);

    /* If the engine successfully scanned the entire token segment, it's a valid number */
    if (*endptr == '\0' && endptr != num_ptr) {
        if (is_negative) {
            parsed_val = -parsed_val;
        }
        *out_val = (uint32_t)parsed_val;
        return 1; /* SUCCESS */
    }

    return 0; /* FAULT: Lexical mismatch, pass down to dictionary search */
}

/* Очистка токена от мусора и выполнение */
static void process_token(const char *token_name) {
	if (current_forth_vm->abort_flag) return;
	/* СНАЧАЛА проверяем, не является ли токен числом (в любом базисе) */
	uint32_t numeric_val = 0;
	if (parse_forth_number(token_name, &numeric_val)) {

		/* Если мы в режиме компиляции (state == 1) — компилируем литерал */
		if (current_forth_vm->state == 1) {
			uint32_t free_ptr = current_forth_vm->dict_free_ptr;
			hw_write32(free_ptr, XT_LIT);
			hw_write32(free_ptr + 4, numeric_val);
			current_forth_vm->dict_free_ptr = free_ptr + 8;
		} else {
			/* Если в режиме REPL — просто выталкиваем число на стек */
			forth_push(numeric_val);
		}
		return;
	}

	/* ЕСЛИ ЭТО НЕ ЧИСЛО — ищем в текстовом словаре (Flash и SPI-RAM) */

    uint32_t state = current_forth_vm->state;
    forth_xt_t builtin_xt = NULL;
    uint32_t xt = dict_find(token_name, &builtin_xt);

    if (xt != 0) {
        uint8_t is_immediate = 0;
        if (builtin_xt != NULL) {
            const forth_word_builtin_t *b = builtin_root;
            while(b) {
                if(b->xt == builtin_xt) {
                    is_immediate = b->flags_len & FLAG_IMMEDIATE;
                    break;
                }
                b = b->link;
            }
        }

        if (xt == (uint32_t)XT_BACKSLASH) {
            line_comment_flag = 1;
            return;
        }

        if (state == 1 && !is_immediate) {
            uint32_t free_ptr = current_forth_vm->dict_free_ptr;
            hw_write32(free_ptr, xt);
            current_forth_vm->dict_free_ptr = free_ptr + 4;
        } else {
            if (builtin_xt != NULL) {
                execute_native_id(xt);
            } else {
                forth_execute_xt(xt);
            }
        }
    } else {
        char *endptr;
        long val = strtol(token_name, &endptr, 10);
        if (*endptr == '\0') {
            if (state == 1) {
                uint32_t free_ptr = current_forth_vm->dict_free_ptr;
                hw_write32(free_ptr, XT_LIT);
                hw_write32(free_ptr + 4, (uint32_t)val);
                current_forth_vm->dict_free_ptr = free_ptr + 8;
            } else {
                forth_push((uint32_t)val);
            }
        } else {
            printf(" ? Unknown word: %s\n", token_name);
            current_forth_vm->abort_flag = 1;
        }
    }
}

void forth_interpret_line(const char *line) {
    char token[TOKEN_BUFFER_SIZE];
    uint32_t token_ptr = 0;
    int compiling_new_word = 0;

    /* Сквозной указатель разбора строки */
    const char *p = line;
    line_comment_flag = 0;

    while (*p != '\0') {
        if (current_forth_vm->abort_flag || line_comment_flag) break;

        char ch = *p;

        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            if (token_ptr > 0) {
                token[token_ptr] = '\0';

                if (compiling_new_word) {
                    dict_add_word(token);
                    compiling_new_word = 0;
                } else if (strcasecmp(token, "value") == 0) {
                    const char *current_pos = p;
                    forth_cmd_value(&current_pos);
                    p = current_pos;
                    token_ptr = 0;
                    continue;
                } else if ((strcasecmp(token, "to") == 0) || (strcmp(token, "->") == 0)) {
                    const char *current_pos = p;
                    forth_cmd_to(&current_pos);
                    p = current_pos;
                    token_ptr = 0;
                    continue;
                } else if (strcasecmp(token, "constant") == 0) {
                    const char *current_pos = p;
                    forth_cmd_constant(&current_pos);
                    p = current_pos;
                    token_ptr = 0;
                    continue;
                } else if (strcasecmp(token, "variable") == 0) {
                	/* Intercept the 'variable' statement inside your token router block */
					const char *current_pos = p; // p is our current character tracker
					forth_cmd_variable(&current_pos);
					p = current_pos;
					token_ptr = 0;
					continue;
				} else if (strcmp(token, ":") == 0) {
                    forth_cmd_colon();
                    compiling_new_word = 1;
                } else if (strcasecmp(token, ".\"") == 0) {
                    /*
                     * ИДЕАЛЬНАЯ СИНХРОНИЗАЦИЯ: Передаем адрес текущего пробела.
                     * Функция forth_cmd_dot_quote сама продвинет указатель p
                     * строго за закрывающую кавычку строки!
                     */
                    forth_cmd_dot_quote(&p);
                    token_ptr = 0;
                    continue; /* Переходим к следующему символу, минуя p++ */
                } else if (strcasecmp(token, "s\"") == 0) {
                    forth_cmd_s_quote(&p);
                    token_ptr = 0;
                    continue;
                } else {
                    process_token(token);
                }
                token_ptr = 0;
            }
        } else {
            if (token_ptr < FORTH_MAX_WORD_LEN) {
                token[token_ptr++] = ch;
            }
        }
        p++; /* Нативный Си-инкремент указателя */
    }

    /* Обработка остаточного токена в конце строки */
    if (token_ptr > 0 && !current_forth_vm->abort_flag && !line_comment_flag) {
        token[token_ptr] = '\0';
        if (compiling_new_word) {
            dict_add_word(token);
        } else if (strcmp(token, ":") == 0) {
            forth_cmd_colon();
        } else {
            process_token(token);
        }
    }

    if (!current_forth_vm->abort_flag && !current_forth_vm->quiet_mode && current_forth_vm->state == 0) {
        printf(" ok\n");
    }
}

/* ========================================================================= */
/* РЕАЛИЗАЦИЯ СТЕКА ФАЙЛОВ ДЛЯ ВЛОЖЕННОГО ВЫЗОВА СКРИПТОВ (INCLUDED)          */
/* ========================================================================= */
#define MAX_FILE_NESTING   4  /* Максимальная глубина вложенности файлов (init.fs -> c.fs -> ...) */
static FILE *file_stack[MAX_FILE_NESTING];
static uint32_t file_stack_ptr = 0;

void forth_primitive_included(void) {
    /* Снимаем со стека Forth длину и адрес строки с именем файла */
    uint32_t str_len = forth_pop();
    uint32_t str_addr = forth_pop();

    if (current_forth_vm->abort_flag) return;

    /* Извлекаем имя файла из нашей Memory-Mapped памяти хоста */
    char filename[TOKEN_BUFFER_SIZE];
    if (str_len > FORTH_MAX_WORD_LEN) str_len = FORTH_MAX_WORD_LEN;

    uint32_t i;
    for (i = 0; i < str_len; i++) {
        filename[i] = (char)hw_read8(str_addr + i);
    }
    filename[str_len] = '\0';

    /* Открываем файл через стандартный Си-поток */
    FILE *f = fopen(filename, "r");
    if (!f) {
        printf("[FATFS ERROR] Could not open file: %s\n", filename);
        forth_abort("FATFS IO Error");
        return;
    }

    file_stack[file_stack_ptr++] = f;
    uint32_t is_root_file = (file_stack_ptr == 1);
    mem_metrics_t start_mem;
    if (is_root_file) {
        start_mem = get_current_mem_metrics();
        current_forth_vm->quiet_mode = 1;
    }

    char line_buf[STRING_BUFFER_SIZE];
    uint32_t line_num = 1;

    while (fgets(line_buf, sizeof(line_buf), f) != NULL) {
        forth_interpret_line(line_buf);
        if (current_forth_vm->abort_flag) break;
        line_num++;
    }

    fclose(f);
    file_stack_ptr--;

    if (current_forth_vm->abort_flag) {
        if (is_root_file) {
            printf("\n==================================================\n");
            printf("[CRITICAL SCRIPT ERROR] Exec aborted in '%s' at line %u\n", filename, line_num - 1);
            printf("==================================================\n");
            current_forth_vm->state = 0;
            current_forth_vm->quiet_mode = 0;
            current_forth_vm->abort_flag = 0;
            file_stack_ptr = 0;
        }
        return;
    }

    if (is_root_file) {
        current_forth_vm->quiet_mode = 0;
        mem_metrics_t end_mem = get_current_mem_metrics();
        printf("\n--------------------------------------------------\n");
        printf("[SUCCESS] Automation Engine successfully synchronized.\n");
        printf("  Root script:               '%s'\n", filename);
        printf("  SPI-RAM Dictionary size:   %u bytes\n", end_mem.spi_ram_used);
        printf("--------------------------------------------------\n\n");
    }
}

void forth_cmd_included(void) {
    /*
     * Это слово вызывается рантаймом. Нам нужно считать имя файла.
     * Но поскольку имя файла идет следующим токеном в этой же строке,
     * для простоты симулятора мы заставим программиста передавать имя файла
     * через стек или временно разрешим `main.c` обрабатывать вложенность.
     *
     * Чтобы сделать это КРИСТАЛЬНО ЧИСТО и без усложнения парсера,
     * давайте просто разрешим слову `included` брать имя файла со стека
     * в виде строки, либо сделаем так, чтобы в файле использовался Си-стиль.
     */
    printf("[FORTH] Standard INCLUDED requires string descriptors.\n");
}

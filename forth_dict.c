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

/* Массив скрытых примитивов ядра. Индекс в массиве равен XT_ID. */
static const void (*kernel_primitive_handlers[])(void) = {
    [XT_NONE]    = NULL,
    [XT_LIT]     = forth_primitive_lit,
    [XT_BRANCH]  = forth_primitive_branch,
    [XT_0BRANCH] = forth_primitive_0branch,
};

#define KERNEL_PRIMITIVES_COUNT (sizeof(kernel_primitive_handlers) / sizeof(kernel_primitive_handlers[0]))

/* Внимание разработчику: массив ДОЛЖЕН быть строго отсортирован по ASCII возрастанию! */
static const forth_word_builtin_t builtin_words[] = {
    /* { name, handler, xt_id, is_immediate } */
	{ "!", forth_primitive_store, XT_STORE, 0 },
	{ ".\"", NULL, 0, 0 }, // временная заглушка для точки-кавычки ." ToDo: реализовать фнкционал!
	{ "*",		forth_mul, 				XT_MUL, 0},
    { "+",     forth_add,    			XT_ADD,  0 },
    { "-",     forth_sub,    			XT_SUB,  0 },
	{ "->", forth_dual_to, XT_CMD_TO, 1 }, /* перехват в парсере */
	{ ".",		forth_dot, 				XT_DOT, 0},
	{ ".\\", forth_dual_dot_quote, XT_DOT_QUOTE, 1},
	{ ":",	forth_cmd_colon, XT_COLON, 1 },
	{ ";",	forth_cmd_semicolon, XT_SEMICOLON, 0 },
	{ "<", forth_less_than, XT_LESS_THAN, 0},
	{ "<>", forth_not_equal, XT_NOT_EQUAL, 0},
	{ "\\", forth_cmd_backslash, XT_BACKSLASH, 1}, /* комментарий - команда, которая только доходит до конца разбираемой строки и больше ничего не делает */
	{ "=", forth_equal, XT_EQUAL, 0},
	{ ">", forth_greater_than, XT_GREATER_THAN, 0},
	{ ">R", forth_primitive_to_r, XT_TO_R, 0},
	{ "@", forth_primitive_fetch, XT_FETCH, 0 },
	{ "[CHAR]", forth_dual_bracket_char, XT_CMD_BRACKET_CHAR, 1}, /* костыль парсера */
	{ "0=", forth_0equal, XT_0EQUAL, 0},
    { "ABORT", forth_primitive_abort,  XT_ABORT,  0 },  //
	{ "AGAIN", forth_cmd_again, XT_CMD_AGAIN, 1},
	{ "ALLOC-CHUNK", forth_cmd_alloc_chunk, XT_ALLOC_CHUNK, 0 },
	{ "ALLOCATE", forth_cmd_allocate, XT_ALLOCATE, 0 },
	{ "AND", forth_primitive_and, XT_AND, 0 },
	{ "ARSHIFT", forth_primitive_arshift, XT_ARSHIFT, 0 },
    { "BEGIN", forth_cmd_begin,        XT_CMD_BEGIN,  1 },
	{ "C@", forth_primitive_c_fetch, XT_C_FETCH, 0 },
	{ "C!", forth_primitive_c_store, XT_C_STORE, 0 },
	{ "CHAR", forth_dual_char, XT_CMD_CHAR, 1 },
	{ "CR",    forth_cr,				XT_CR,	0 },
	{ "COUNT", forth_primitive_count, XT_COUNT, 0 },
	{ "CONSTANT", forth_dual_constant, XT_PRIMITIVE_CONSTANT_RT, 0 },
	{ "DUP", forth_dup, XT_DUP, 0 },
	{ "DROP", forth_drop,        XT_DROP,  0 },
	{ "ELSE", forth_cmd_else, XT_CMD_ELSE, 1 },
	{ "EMIT", forth_primitive_emit, XT_EMIT, 0 },
	{ "IF", forth_cmd_if, XT_CMD_IF, 1 },
	{ "INCLUDED", forth_primitive_included, XT_INCLUDED, 0 },
	{ "INVERT", forth_primitive_invert, XT_INVERT, 0 },
	{ "F-CLOSE", forth_primitive_f_close, XT_F_CLOSE, 0 },
	{ "F-CREATE", forth_primitive_f_create, XT_F_CREATE, 0 },
	{ "F-OPEN", forth_primitive_f_open, XT_F_OPEN, 0 },
	{ "F-READ", forth_primitive_f_read, XT_F_READ, 0 },
	{ "F-WRTE", forth_primitive_f_write, XT_F_WRITE, 0 },
	{ "FAST-CELL-FREE", forth_cmd_fast_cell_free, XT_FAST_CELL_FREE, 0 },
	{ "FAST-CELL", forth_cmd_fast_cell,  XT_FAST_CELL, 0},
	{ "FREE-CHUNK", forth_cmd_free_chunk, XT_FREE_CHUNK, 0 },
	{ "FREE", forth_cmd_free, XT_FREE, 0 },
	{ "LSHIFT", forth_primitive_lshift, XT_LSHIFT, 0 },
	{ "MEM-DUMP", forth_cmd_mem_dump, XT_MEM_DUMP, 0 },
	{ "OR", forth_primitive_or, XT_OR, 0 },
	{ "OVER", forth_over,        XT_OVER,  0 },
	{ "R>", forth_primitive_from_r, XT_FROM_R, 0 },
	{ "RSHIFT", forth_primitive_rshift, XT_LSHIFT, 0 },
	{ "S\"", forth_dual_s_quote, XT_S_QUOTE, 1 },
	{ "SWAP", forth_swap,        XT_SWAP,  0 },
	{ "THEN", forth_cmd_then, XT_CMD_THEN, 1 },
	{ "TO", forth_dual_to, XT_CMD_TO, 1 }, /* синоним "стрелки" -> перехват в парсере */
	{ "TYPE", forth_primitive_type, XT_TYPE , 0 },
	{ "VALUE", dummy_xt, XT_CMD_VALUE, 0 }, /* не ошибка ли? */
	{ "VARIABLE", forth_dual_variable, XT_PRIMITIVE_VAR_RT, 0 },
    { "UNTIL", forth_cmd_until,        XT_CMD_UNTIL,  1 },
	{ "H.",	forth_primitive_dot_hex, XT_DOT_HEX, 0 },
	{ "U.", forth_primitive_dot_unsigned, XT_DOT_UNSIGNED, 0 },
	{ "B.", forth_primitive_dot_binary, XT_DOT_BINARY, 0 },
	{ "Q.", forth_primitive_dot_formatted, XT_DOT_FORMATTED, 0 },
	{ "XOR", forth_primitive_xor, XT_XOR, 0 },
};

#define BUILTIN_WORDS_COUNT (sizeof(builtin_words) / sizeof(builtin_words[0]))

/* Компаратор для двоичного поиска */
static int compare_builtin_nodes(const void *key, const void *element) {
    const char *search_name = (const char *)key;
    const forth_word_builtin_t *word = (const forth_word_builtin_t *)element;
    return strcmp(search_name, word->name);
}

/* Высокоскоростной двоичный поиск за O(log N) */
const forth_word_builtin_t* find_builtin(const char *token) {
    return (const forth_word_builtin_t*) bsearch(
        token,
        builtin_words,
        BUILTIN_WORDS_COUNT,
        sizeof(forth_word_builtin_t),
        compare_builtin_nodes
    );
}

/* ЮНИТ-ТЕСТ: Защита от кривых рук при добавлении новых слов */
void forth_dict_validate_sorting(void) {
    for (size_t i = 0; i < BUILTIN_WORDS_COUNT - 1; i++) {
        if (strcmp(builtin_words[i].name, builtin_words[i + 1].name) >= 0) {
            // Нарушение порядка! Выводим ошибку и аварийно останавливаемся
            char error_buf[128];
            // Безопасно форматируем без динамической аллокации
            forth_abort_with_context("[CRITICAL]: Flash dictionary sorting broken!");
            return;
        }
    }
}

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

void forth_bracket_char (void) {
	const char *p = current_forth_vm->parse_line_ptr;
	char ch = get_next_char_from_line(&p);
	if (ch != '\0') {
		forth_push((uint32_t)ch);
	} else {
		forth_abort("CHAR missing argument");
	}
}

void forth_dual_char(void) {
    if (current_forth_vm->state == FORTH_STATE_RUNTIME) return; // Безопасность

    const char *p = current_forth_vm->parse_line_ptr;
    char token[FORTH_MAX_WORD_LEN];

    // Выкусываем аргумент (например, "A")
    if (!extract_next_token(&p, token)) {
        forth_abort("CHAR missing argument");
        return;
    }
    current_forth_vm->parse_line_ptr = p;
    char ch = token[0]; // Берем первый символ токена

    if (current_forth_vm->state == FORTH_STATE_COMPILE) {
        /* Запекаем пару [ XT_LIT ] -> [ код_символа ] */
        uint32_t free_ptr = current_forth_vm->dict_free_ptr;
        hw_write32(free_ptr, XT_LIT);
        hw_write32(free_ptr + 4, (uint32_t)ch);
        current_forth_vm->dict_free_ptr = free_ptr + 8;
    } else {
        /* REPL: Кладем код ASCII на стек данных */
        forth_push((uint32_t)ch);
    }
}

void forth_dual_bracket_char (void) {
    if (current_forth_vm->state != FORTH_STATE_COMPILE) {
        forth_abort("[CHAR] can only be used inside colon definitions");
        return;
    }

    const char *p = current_forth_vm->parse_line_ptr;
    char token[FORTH_MAX_WORD_LEN];

    if (!extract_next_token(&p, token)) {
        forth_abort("[CHAR] missing argument");
        return;
    }
    current_forth_vm->parse_line_ptr = p;
    char ch = token[0];

    /* Запекаем литерал */
    uint32_t free_ptr = current_forth_vm->dict_free_ptr;
    hw_write32(free_ptr, XT_LIT);
    hw_write32(free_ptr + 4, (uint32_t)ch);
    current_forth_vm->dict_free_ptr = free_ptr + 8;
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

void forth_dual_constant(void) {
    if (current_forth_vm->state == FORTH_STATE_RUNTIME) return; // Защита

    // 1. Выкусываем имя переменной из строки парсера (например, "MY-VAR")
    const char *p = current_forth_vm->parse_line_ptr;
    char var_name[FORTH_MAX_WORD_LEN];
    parse_next_token_to_buf(&p, var_name);
    current_forth_vm->parse_line_ptr = p;

    // 2. Выделяем 4 байта в SPI-RAM под хранение значения этой переменной
    uint32_t var_data_addr = current_forth_vm->dict_free_ptr;
    hw_write32(var_data_addr, 0); // Инициализируем нулем
    current_forth_vm->dict_free_ptr += 4;

    // 3. Регистрируем новое слово в пользовательском словаре в SPI-RAM.
    // Передаем имя, рантайм-примитив выполнения переменных (XT_PRIMITIVE_VARIABLE_RT)
    // и адрес её данных как payload.
    dict_add_user_word(var_name, XT_PRIMITIVE_VAR_RT, var_data_addr);
}

void forth_dual_variable(void) {
    if (current_forth_vm->state == FORTH_STATE_RUNTIME) return; // Защита

    // 1. Выкусываем имя переменной из строки парсера (например, "MY-VAR")
    const char *p = current_forth_vm->parse_line_ptr;
    char var_name[FORTH_MAX_WORD_LEN];
    parse_next_token_to_buf(&p, var_name);
    current_forth_vm->parse_line_ptr = p;

    // 2. Выделяем 4 байта в SPI-RAM под хранение значения этой переменной
    uint32_t var_data_addr = current_forth_vm->dict_free_ptr;
    hw_write32(var_data_addr, 0); // Инициализируем нулем
    current_forth_vm->dict_free_ptr += 4;

    // 3. Регистрируем новое слово в пользовательском словаре в SPI-RAM.
    // Передаем имя, рантайм-примитив выполнения переменных (XT_PRIMITIVE_VARIABLE_RT)
    // и адрес её данных как payload.
    dict_add_user_word(var_name, XT_PRIMITIVE_VAR_RT, var_data_addr);
}

void forth_primitive_var_runtime(void) {
    // В шитом коде сразу за XT_PRIMITIVE_VAR_RT компилятор сохранил адрес данных
    uint32_t data_addr = hw_spi_ram_read32(current_forth_vm->ip);
    current_forth_vm->ip += 4;

    // Просто кладем адрес ячейки памяти на стек данных
    forth_push(data_addr);
}

void forth_dual_to(void) {
    if (current_forth_vm->state == FORTH_STATE_RUNTIME) {
        /* 1️⃣ РАНТАЙМ: Извлекаем адрес и пишем туда значение со стека */
        uint32_t target_addr = hw_spi_ram_read32(current_forth_vm->ip);
        current_forth_vm->ip += 4;

        uint32_t value = forth_pop();
        hw_write32(target_addr, value);
        return;
    }

    /* 2️⃣ ПАРСИНГ ТЕКСТА: Ищем, кому присвоить значение */
    const char *p = current_forth_vm->parse_line_ptr;
    char target_name[FORTH_MAX_WORD_LEN];
    parse_next_token_to_buf(&p, target_name);
    current_forth_vm->parse_line_ptr = p;

    // Ищем переменную в словаре, чтобы узнать адрес её ячейки памяти
    uint32_t var_data_addr = dict_find_variable_address(target_name);
    if (var_data_addr == 0) {
        forth_abort("Error: Unknown variable target for TO");
        return;
    }

    if (current_forth_vm->state == FORTH_STATE_COMPILE) {
        /* Режим компиляции: запекаем XT_TO и адрес назначения */
        uint32_t free_ptr = current_forth_vm->dict_free_ptr;
        hw_write32(free_ptr, XT_CMD_TO); // Запекаем себя же для рантайма
        hw_write32(free_ptr + 4, var_data_addr);
        current_forth_vm->dict_free_ptr += 8;
    } else {
        /* Режим REPL: Выполняем присваивание немедленно */
        uint32_t value = forth_pop();
        hw_write32(var_data_addr, value);
    }
}

void forth_dual_s_quote (void) {
	if (!current_forth_vm) return;
   switch (current_forth_vm->state) {

		case FORTH_STATE_RUNTIME: {
			/*
			 * 1️⃣ СИТУАЦИЯ: Крутится шитый код.
			 * Текста нет, забираем длину и байты строки из SPI-RAM по указателю 'ip'.
			 */
			uint32_t len = hw_spi_ram_read32(current_forth_vm->ip);
			uint32_t str_addr = current_forth_vm->ip + 4;

			forth_push(str_addr);
			forth_push(len);

			uint32_t aligned_len = (len + 3) & ~3;
			current_forth_vm->ip += 4 + aligned_len;
			break;
		}

		case FORTH_STATE_COMPILE:
		case FORTH_STATE_REPL: {
			/*
			 * 2️⃣ СИТУАЦИЯ: Работает текстовый парсер.
			 * Выделяем логику разбора строки из текстового потока в общий блок.
			 */
			const char *p = current_forth_vm->parse_line_ptr;
			if (*p == ' ') p++;

			static char str_buf[STRING_BUFFER_SIZE];
			uint32_t len = 0;

			while (*p != '\0' && len < FORTH_MAX_STRING_LEN) {
				if (*p == '\\' && (*(p + 1) == '"' || *(p + 1) == '\\')) {
					p++;
					str_buf[len++] = *p++;
					continue;
				}
				if (*p == '"') break;
				str_buf[len++] = *p++;
			}
			if (*p == '"') p++;
			str_buf[len] = '\0';

			current_forth_vm->parse_line_ptr = p; // Сохраняем указатель текста

			// А теперь смотрим, что делать с распарсенным текстом: запечь или выдать на стек
			if (current_forth_vm->state == FORTH_STATE_COMPILE) {
				/* Ветка компиляции текста в код */
				uint32_t free_ptr = current_forth_vm->dict_free_ptr;

				hw_write32(free_ptr, XT_S_QUOTE); // Запекаем единый XT_ID слова
				hw_write32(free_ptr + 4, len);
				hw_spi_ram_write_buf(free_ptr + 8, (const uint8_t *)str_buf, len);

				uint32_t aligned_len = (len + 3) & ~3;
				current_forth_vm->dict_free_ptr = free_ptr + 8 + aligned_len;
			} else {
				/* Ветка интерактивного REPL выполнения текста */
				uint32_t temp_str_addr = EXT_SPI_RAM_BASE + EXT_SPI_RAM_SIZE - STRING_BUFFER_SIZE;
				hw_spi_ram_write_buf(temp_str_addr, (const uint8_t *)str_buf, len);

				forth_push(temp_str_addr);
				forth_push(len);
			}
			break;
		}
	}
}

void forth_dual_dot_quote (void) {
	if (!current_forth_vm) return;
    uint32_t state = current_forth_vm->state;
    const char *p = current_forth_vm->parse_line_ptr;

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

    *current_forth_vm->parse_line_ptr = p;

    if (state == 1) {
        uint32_t free_ptr = current_forth_vm->dict_free_ptr;
        hw_write32(free_ptr, XT_PRIMITIVE_P_DOT_QUOTE);
        hw_write32(free_ptr + 4, len);

        hw_spi_ram_write_buf(free_ptr + 8, (const uint8_t *)str_buf, len);

        uint32_t aligned_len = (len + 3) & ~3;
        current_forth_vm->dict_free_ptr = free_ptr + 8 + aligned_len;
    } else {
        printf("%s", str_buf);
    }}




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
	current_forth_vm->ip = forth_r_pop();
}

/* Реализация примитивов управления логикой выполнения */
void forth_primitive_lit(void) {
    uint32_t literal_val = hw_read32(current_forth_vm->ip);
    forth_push(literal_val);
    current_forth_vm->ip += 4;
}

void forth_primitive_branch(void) {
	current_forth_vm->ip += (int32_t)hw_read32(current_forth_vm->ip);
}

void forth_primitive_0branch(void) {
    // 1. Достаем флаг со стека данных (результат операции сравнения внутри цикла)
    uint32_t flag = forth_pop();

    // 2. Читаем адрес перехода, который запечен следом в памяти
    uint32_t target = hw_spi_ram_read32(current_forth_vm->ip);

    if (flag == 0) {
        // Если 0 (Ложь) — цикл продолжается, прыгаем назад на BEGIN
        current_forth_vm->ip = target;
    } else {
        // Если Истина — выходим из цикла, шагаем через ячейку адреса дальше
        current_forth_vm->ip += 4;
    }
}

void forth_primitive_emit(void) {
    char ch = (char)forth_pop();
    if (!current_forth_vm->abort_flag) {
        putchar(ch);
    }
}

/* Си-обработчик для слова обратного слэша: взводит флаг тишины до конца текущей строки */
void forth_cmd_backslash(void) {
    // Нам не важен state — комментарий просто пропускается и в REPL, и в компиляции
    const char *p = current_forth_vm->parse_line_ptr;

    // Двигаем указатель до конца строки или терминатора
    while (*p != '\0' && *p != '\n' && *p != '\r') {
        p++;
    }

    // Возвращаем обновленный указатель обратно в VM — парсер продолжит уже со следующей строки
    current_forth_vm->parse_line_ptr = p;
}

#define MAX_OPEN_FILES  4
/* Изолированный массив 64-битных Си-указателей хоста */
static FILE *sys_file_table[MAX_OPEN_FILES] = { NULL };

void purge_sys_file_table (void) {
	/* экстренная чистка таблицы подвешенных файлов */
    for (uint32_t i = 0; i < MAX_OPEN_FILES; i++) {
        if (sys_file_table[i] != NULL) {
            fclose(sys_file_table[i]);
            sys_file_table[i] = NULL; /* заземление подвисшего файла */
        }
    }
}

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
    if (current_forth_vm->state != FORTH_STATE_COMPILE) {
        forth_abort("BEGIN used outside colon definition");
        return;
    }
    // Запоминаем текущий адрес свободной памяти в SPI-RAM и кладем его на стек компиляции
    forth_push(current_forth_vm->dict_free_ptr);
}

void forth_cmd_until(void) {
    if (current_forth_vm->state != FORTH_STATE_COMPILE) {
        forth_abort("UNTIL used outside colon definition");
        return;
    }
    // Достаем со стека адрес, который там оставил BEGIN
    uint32_t target_address = forth_pop();

    uint32_t free_ptr = current_forth_vm->dict_free_ptr;
    // 1. Запекаем примитив условного перехода (бранч по нулю)
    hw_write32(free_ptr, XT_0BRANCH);
    // 2. Сразу за ним запекаем физический адрес возврата
    hw_write32(free_ptr + 4, target_address);

    // Сдвигаем границу памяти на 8 байт (токен + адрес)
    current_forth_vm->dict_free_ptr = free_ptr + 8;
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

void execute_native_id(forth_xt_t xt) {
    /* ВЕТКА А: Это скрытый примитив ядра (ID находится в диапазоне ядра) */
    if (xt >= XT_KERNEL_START && xt < XT_KERNEL_END) {
        // Мгновенный прямой прыжок по индексу массива за 1 такт процессора!
        if (kernel_primitive_handlers[xt] != NULL) {
            kernel_primitive_handlers[xt]();
            return;
        }
    }

    /* ВЕТКА Б: Это публичное встроенное слово (Примитив или Дуал) */
    // Так как рантайм-вызов происходит из бинарного шитого кода в SPI-RAM,
    // нам нужно быстро запустить Си-коллбэк этого публичного слова.
    // Для этого при старте системы мы можем один раз построить небольшую
    // SRAM-таблицу указателей для публичных XT_ID (как мы проектировали ранее),
    // чтобы рантайм вообще никогда не делал поисков по циклу.

    void (*public_handler)(void) = get_public_handler_by_id(xt);
    if (public_handler != NULL) {
        public_handler();
        return;
    }

    forth_abort("Runtime Error: Unknown Execution Token");
}

/* Диспетчер выполнения шитого кода */
void forth_execute_compiled_xt(uint32_t xt_address) {
    // Сохраняем старый режим (это может быть FORTH_STATE_REPL или FORTH_STATE_COMPILE, если мы вызвали слово внутри компиляции)
    uint32_t previous_state = current_forth_vm->state;
    uint32_t previous_ip = current_forth_vm->ip;

    // Переключаем рубильник системы в чистый РАНТАЙМ
    current_forth_vm->state = FORTH_STATE_RUNTIME;
    current_forth_vm->ip = xt_address;

    while (current_forth_vm->ip != 0) {
        forth_xt_t current_xt = hw_spi_ram_read32(current_forth_vm->ip);
        current_forth_vm->ip += 4;

        execute_native_id(current_xt);
    }

    // Восстанавливаем исходный режим текстового парсера
    current_forth_vm->state = previous_state;
    current_forth_vm->ip = previous_ip;
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
	printf("[SYSTEM] %u system format constants successfully secured.\n", (uint32_t)INIT_CONSTANTS_COUNT);
}

uint32_t dict_find(const char *name, forth_xt_t *out_builtin_xt) {
#if 0 /* <-- код полностью нерабочий и требует перестройки заново */
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
#endif
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

	const forth_word_builtin_t *word = find_builtin(token_name);

    if (word != NULL) {
        // 2. Универсальное правило Форта:
        // Вызываем функцию, если слово IMMEDIATE *ИЛИ* мы в режиме REPL
        if (word->is_immediate || current_forth_vm->state == 0) {
            word->handler(); // Единый вызов Си-функции!
        } else {
            // Иначе — мы в режиме компиляции, и это обычное слово.
            // Просто пишем его XT в SPI-RAM, Си-функцию вызывать НЕ НАДО.
        	hw_write32 (current_forth_vm->dict_free_ptr, word->xt_id); /* адрес указывает в область "SPI-память", функция сама поймёт */
            current_forth_vm->dict_free_ptr += 4;
        }
        return;
    }

	/* проверяем, не является ли токен числом (в любом базисе) */
	uint32_t numeric_val = 0;
	if (parse_forth_number(token_name, &numeric_val)) {
	    switch (current_forth_vm->state) {
	        case FORTH_STATE_REPL:
	            // В режиме REPL число просто прыгает на стек данных
	            forth_push(numeric_val);
	            break;

	        case FORTH_STATE_COMPILE:
	            // В режиме компиляции запекаем пару: [ примитив литерала ] -> [ само значение ]
	            uint32_t free_ptr = current_forth_vm->dict_free_ptr;
	            hw_write32(free_ptr, XT_LIT);
	            hw_write32(free_ptr + 4, numeric_val);
	            current_forth_vm->dict_free_ptr = free_ptr + 8;
	            break;

	        case FORTH_STATE_RUNTIME:
	            // Виртуальная машина никогда не передает строки с числами в парсер во время рантайма,
	            // так как в SPI-RAM числа уже лежат в виде бинарных данных за XT_LIT.
	            break;
	    }
	}

	/* ЕСЛИ ЭТО НЕ ЧИСЛО — ищем в пользовательском словаре (SPI-RAM) */

#if 0 /* * Здесь пока всё ещё далеко до работоспособного состояния!!! */
    uint32_t state = current_forth_vm->state;
    forth_xt_t builtin_xt = NULL;
    uint32_t xt = dict_find(token_name, &builtin_xt);

    if (xt != 0) {
        uint8_t is_immediate = 0;


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
#endif
}

void forth_interpret_line(const char *line) {
    current_forth_vm->parse_line_ptr = line;

    while (*current_forth_vm->parse_line_ptr != '\0') {
        char token[TOKEN_BUFFER_SIZE];
        if (!extract_next_token(&current_forth_vm->parse_line_ptr, token)) {
            break; // Строка пуста или закончилась
        }

        // Шаг 1: Ищем двоичным поиском во флэш-таблице встроенных слов
        const forth_word_builtin_t *word = find_builtin(token);
        if (word != NULL) {
            // Единое жесткое правило диспетчеризации:
            if (word->is_immediate || current_forth_vm->state == FORTH_STATE_REPL) {
                word->handler(); // Выполняем Си-код (для примитивов в REPL, команд и дуалов)
            } else {
                // Обычный примитив в режиме компиляции — просто списываем его XT_ID в память
                hw_write32(current_forth_vm->dict_free_ptr, word->xt_id);
                current_forth_vm->dict_free_ptr += 4;
            }
            continue;
        }

        // Шаг 2: Если не встроенное — ищем в пользовательском динамическом словаре в SPI-RAM
        // ...

        // Шаг 3: Если не нашли слово — проверяем, число ли это
        uint32_t numeric_value;
        if (try_parse_number(token, &numeric_value)) {
            process_numeric_token(numeric_value);
            continue;
        }

        // Шаг 4: Если ничего не подошло — паникуем
        forth_abort_unknown_token(token);
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

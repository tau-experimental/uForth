#ifndef FORTH_DICT_H
#define FORTH_DICT_H

#include <stdint.h>

/* Максимальная длина имени Forth-слова (без учета нулевого терминатора) */
#define FORTH_MAX_WORD_LEN    33
#define TOKEN_BUFFER_SIZE     36 // с запасом и для выравнивания, было: (FORTH_MAX_WORD_LEN + 1)

#define FORTH_MAX_STRING_LEN    255
#define STRING_BUFFER_SIZE     (FORTH_MAX_STRING_LEN + 1)

/*typedef uint32_t forth_xt_t;*/
/* Перечисление уникальных ID для встроенных Си-примитивов (Execution Tokens) */
typedef enum {
    XT_NONE = 0,

    /* --- СКРЫТЫЕ ПРИМИТИВЫ: Строго по порядку для прямой индексации --- */
    XT_KERNEL_START,
    XT_LIT = XT_KERNEL_START, // ID = 1
    XT_BRANCH,               // ID = 2
    XT_0BRANCH,              // ID = 3
    XT_KERNEL_END,

    /* --- ПУБЛИЧНЫЕ ТОКЕНЫ: Начинаются сразу после скрытых --- */
	XT_ABORT = XT_KERNEL_END,                 /* abort */
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
    XT_S_QUOTE,               /* dual: и команда, и скрытый рантайм-примитив (s") */
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
	//XT_CMD_VARIABLE,          /* variable keyword compiler token */
	XT_PRIMITIVE_VAR_RT,   /* The hidden execution-time worker token */
	XT_PRIMITIVE_CONSTANT_RT, /* команда создаёт константу, примитив возвращает её значение */
    //XT_CMD_CONSTANT,            /* constant */
    //XT_PRIMITIVE_P_CONSTANT,     /* Скрытый рантайм-обработчик константы */
	XT_CMD_VALUE,            /* value */
    XT_CMD_TO,               /* to / стрелка */
    XT_PRIMITIVE_P_VALUE,     /* Скрытый рантайм-обработчик значения */
	XT_F_OPEN,              /* s" filename" f-open  ( c-addr len -- file-id ) */
	XT_F_CREATE,              /* s" filename" f-create  ( c-addr len -- file-id ) */
	XT_F_WRITE,               /* buf-addr len file-id f-write ( -- )              */
	XT_F_READ,                /* buf-addr max file-id f-read  ( -- actual-len )   */
	XT_F_CLOSE                /* file-id f-close              ( -- ) */
} forth_xt_t;

/* Монолитная 12-байтовая структура для Flash-резидентных примитивов */
typedef struct {
    const char *name;          // 4 байта: Указатель на строку в Flash
    void (*handler)(void);     // 4 байта: Прямой Си-коллбэк выполнения

    // Битовые поля компилятора: занимают ровно 4 байта в сумме
    uint32_t xt_id : 31;       // Числовой Execution Token для записи в SPI-RAM
    uint32_t is_immediate : 1; // Флаг немедленного исполнения (1 = выполнять при state=1)
} forth_word_builtin_t;

/* Экспортируемые интерфейсы словаря */
const forth_word_builtin_t* find_builtin(const char *token);
void forth_dict_validate_sorting(void); // Наш встроенный юнит-тест

/* Инициализация подсистемы словаря */
void dict_init(void);

/* экстренная очистка таблиы трансляции файлов */
void purge_sys_file_table (void);

/* Поиск слова по имени (сначала ищет в SPI-RAM, затем во встроенном словаре Flash) */
uint32_t dict_find(const char *name, forth_xt_t *out_builtin_xt);

/* Регистрация нового слова во внешней SPI-RAM */
//void dict_add_word(const char *name, uint32_t xt_address, uint8_t flags);
void dict_add_word(const char *name);

/* Главная функция REPL: обработка текстовой строки */
void forth_interpret_line(const char *line);

/* Функция запуска шитого кода пользовательского слова */
void forth_execute_xt(uint32_t xt);

/* прототипы встроенных слов */
void forth_cmd_backslash(void);
void forth_primitive_abort(void);
void forth_cmd_semicolon(void);
void forth_cmd_colon(void);
void forth_cmd_if(void);
void forth_cmd_else(void);
void forth_cmd_then(void);

void forth_primitive_store(void);
void forth_primitive_fetch(void);
void forth_primitive_c_store(void);
void forth_primitive_c_fetch(void);
void forth_primitive_to_r(void);
void forth_primitive_from_r(void);
void forth_primitive_exit(void);
void forth_primitive_lit(void);
void forth_primitive_branch(void);
void forth_primitive_0branch(void);
void forth_primitive_emit(void);

void forth_dup(void);
void forth_drop(void);
void forth_swap(void);
void forth_over(void);

void forth_cmd_allocate(void);
void forth_cmd_free(void);

void forth_cmd_if(void);
void forth_cmd_then(void);
void forth_cmd_else(void);
void forth_cmd_begin(void);
void forth_cmd_until(void);
void forth_cmd_again(void);

void forth_cmd_included(void);
void forth_primitive_included(void);
void forth_add(void);
void forth_sub(void);
void forth_mul(void);
void forth_primitive_and(void);
void forth_primitive_or(void);
void forth_primitive_xor(void);
void forth_primitive_invert(void);
void forth_primitive_lshift(void);
void forth_primitive_rshift(void);
void forth_primitive_arshift(void);

void forth_0equal(void);
void forth_equal(void);
void forth_not_equal(void);
void forth_less_than(void);
void forth_greater_than(void);

void forth_cr(void);
void forth_dot(void);
void forth_primitive_dot_unsigned(void);
void forth_primitive_dot_hex(void);
void forth_primitive_dot_binary(void);
void forth_primitive_dot_formatted(void);

void forth_primitive_count(void);
void forth_primitive_type(void);

void forth_dual_constant(void);
void forth_dual_value(void);
void forth_dual_to(void);
void forth_dual_variable(void);

void forth_dual_dot_quote(void);

void forth_dual_char (void);
void forth_dual_bracket_char (void);

void forth_dual_s_quote(void);

void forth_primitive_f_open(void); /* открыть на чтение */
void forth_primitive_f_create(void); /* открыть на запись */
void forth_primitive_f_write(void);
void forth_primitive_f_read(void);
void forth_primitive_f_close(void);


void forth_cmd_mem_dump(void);



#endif /* FORTH_DICT_H */

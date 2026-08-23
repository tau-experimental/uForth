#include "forth_debug.h"
#include "hardware.h"
#include "forth_vm.h"
#include "forth_cell_pool.h"
#include "forth_chunk_pool.h"
#include "forth_heap.h"
#include "forth_dict.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t debug_get_chunk_free_head(void); /* Опережающее объявление */
uint8_t debug_get_cell_bitmap_byte(uint32_t byte_idx); /* Опережающее объявление */

void forth_export_memory_csv(const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) return;
    fprintf(f, "Pool,Element_Index,Physical_Address,Status,Content_Or_Next_Free_Pointer,Raw_Hex_Data_Bytes_0_to_15\n");
    uint32_t i, j;
    for (i = 0; i < CELL_BITMAP_SIZE; i++) {
        uint8_t bitmap_byte = debug_get_cell_bitmap_byte(i);
        for (j = 0; j < 8; j++) {
            uint32_t cell_idx = (i * 8) + j;
            uint32_t cell_addr = CELL_DATA_BASE + (cell_idx * CELL_SIZE);
            int is_allocated = (bitmap_byte & (1 << j)) != 0;
            uint32_t content = hw_read32(cell_addr);
            fprintf(f, "FAST_CELL_BITMAP,%u,0x%08X,%s,%d (0x%08X),%02X%02X%02X%02X\n",
                    cell_idx, cell_addr, is_allocated ? "ALLOCATED" : "FREE",
                    (int32_t)content, content, hw_read8(cell_addr), hw_read8(cell_addr+1), hw_read8(cell_addr+2), hw_read8(cell_addr+3));
        }
    }
    uint32_t free_head = debug_get_chunk_free_head();

    for (i = 0; i < CHUNK_COUNT; i++) {
        uint32_t chunk_addr = CHUNK_POOL_BASE + (i * CHUNK_SIZE);
        int is_free = 0; uint32_t curr = free_head;
        while (curr != 0) { if (curr == chunk_addr) { is_free = 1; break; } curr = hw_read32(curr); }
        fprintf(f, "STRUCT_CHUNKS,%u,0x%08X,%s,", i, chunk_addr, is_free ? "FREE_IN_CHAIN" : "ALLOCATED");
        if (is_free) {
            uint32_t next_free = hw_read32(chunk_addr);
            fprintf(f, "0x%08X (Next Free),", next_free);
        } else fprintf(f, "User Data,");
        for (j = 0; j < 16; j++) fprintf(f, "%02X", hw_read8(chunk_addr + j));
        fprintf(f, "\n");
    }
    uint32_t heap_curr = HEAP_BASE_ADDR; uint32_t heap_idx = 0;
    while (heap_curr != 0) {
        uint32_t size_flags = hw_read32(heap_curr + 0); uint32_t block_size = size_flags & 0xFFFFFFFE;
        int is_busy = size_flags & 0x00000001; uint32_t next_block = hw_read32(heap_curr + 4);
        fprintf(f, "EXTERNAL_HEAP,%u,0x%08X,%s,Size: %u | Next: 0x%08X,", heap_idx++, heap_curr, is_busy ? "ALLOCATED" : "FREE", block_size, next_block);
        if (is_busy) { uint32_t p_val = hw_read32(heap_curr + 8); fprintf(f, "%02X%02X%02X%02X\n", (p_val>>24)&0xFF, (p_val>>16)&0xFF, (p_val>>8)&0xFF, p_val&0xFF); }
        else fprintf(f, "00000000\n");
        heap_curr = next_block;
    }
    fclose(f);
}

/* ========================================================================= */
/* НОВЫЙ ИНСТРУМЕНТ: НЕРАЗРУШАЮЩИЙ СНАПШОТ РАНТАЙМА (СТЕКИ И СЛОВАРЬ)         */
/* ========================================================================= */
void forth_export_runtime_snapshot_csv(const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        fprintf(stderr, "Debug Error: Could not open file %s for snapshot export\n", filename);
        return;
    }

    /* --- СЕКЦИЯ 1: СИСТЕМНЫЕ РЕГИСТРЫ ВМ --- */
    fprintf(f, "=== VM SYSTEM REGISTERS ===\n");
    fprintf(f, "Register_Name\tValue_Hex\tMeaning\n");

    /* Внутри forth_export_runtime_snapshot_csv */
    //fprintf(f, "ADDR_LATEST_WORD\t0x%08X\t0x%08X\t%u\tLatest Compiled Word...\n", 0, current_forth_vm->latest_word, current_forth_vm->latest_word);


    fprintf(f, "SP\t0x%08X\tData Stack Pointer (Top of Stack)\n", current_forth_vm->sp);
    fprintf(f, "RP\t0x%08X\tReturn Stack Pointer\n", current_forth_vm->rp);
    fprintf(f, "IP\t0x%08X\tInstruction Pointer (Runtimes Loop)\n", current_forth_vm->ip);
    fprintf(f, "LATEST_WORD\t0x%08X\tLatest Compiled Word Address\n", current_forth_vm->latest_word);
    fprintf(f, "DICT_FREE_PTR\t0x%08X\tDictionary Free Pointer (Compilation Frontier)\n", current_forth_vm->dict_free_ptr);
    fprintf(f, "STATE\t0x%02X\t%s\n\n", current_forth_vm->state, ((current_forth_vm->state == 1) ? "COMPILATION MODE" : "EXECUTION MODE"));

    /* --- СЕКЦИЯ 2: НЕРАЗРУШАЮЩИЙ СНАПШОТ СТЕКА ДАННЫХ (DATA STACK) --- */
    fprintf(f, "=== DATA STACK SNAPSHOT ===\n");
    fprintf(f, "Stack_Index\tValue_Hex\tPosition\n");

    uint32_t sp_walker = 0;
    uint32_t stack_idx = 0;
    if (current_forth_vm->sp == 0) {
    	fprintf(f, "N/A\tN/A\tSTACK IS EMPTY\n");
    } else {
    	for (uint32_t i = 0; i < current_forth_vm->sp; i++) {
			uint32_t val = current_forth_vm->data_stack[i];

			fprintf(f, "%u\t0x%08X\t%s\n",
					i, val,
					(i == current_forth_vm->sp - 1) ? "TOP OF STACK (TOS)" : "In Stack");
		}
    }
    fprintf(f, "\n");

    /* --- СЕКЦИЯ 3: НЕРАЗРУШАЮЩИЙ СНАПШОТ СТЕКА ВОЗВРАТОВ (RETURN STACK) --- */
    fprintf(f, "=== RETURN STACK SNAPSHOT ===\n");
    fprintf(f, "Stack_Index\tValue_Hex\tPosition\n");

    if (current_forth_vm->rp == 0) {
            fprintf(f, "N/A\tN/A\tRETURN STACK IS EMPTY\n");
        } else {
            for (uint32_t i = 0; i < current_forth_vm->rp; i++) {
                uint32_t val = current_forth_vm->return_stack[i];

                fprintf(f, "%u\t0x%08X\t%s\n",
                        i, val,
                        (i == current_forth_vm->rp - 1) ? "TOP OF RETURN STACK" : "In Stack");
            }
        }
    /* --- СЕКЦИЯ 4: КАРТА ПОЛЬЗОВАТЕЛЬСКОГО СЛОВАРЯ (ВНЕШНЯЯ SPI-RAM) --- */
    fprintf(f, "=== USER DICTIONARY CHAIN (SPI-RAM) ===\n");
    fprintf(f, "Word_Index\tWord_Header_Addr\tLink_Field_Addr\tPrev_Word_Ptr_Hex\tFlags_Raw\tName_Len\tImmediate_Flag\tWord_Name\tBody_Code_Addr_XT\tBody_Hex_Dump_First_16_Bytes\n");

    uint32_t curr_word = current_forth_vm->latest_word;
    uint32_t u_word_idx = 0;
    if (curr_word == 0) {
        fprintf(f, "N/A\tN/A\tN/A\tN/A\tN/A\tN/A\tN/A\tSPI-RAM DICTIONARY IS EMPTY\tN/A\tN/A\n");
    } else {
        while (curr_word != 0) {
            uint32_t link_addr = curr_word;
            uint32_t prev_word_ptr = hw_read32(link_addr);
            uint8_t flags_len = hw_read8(curr_word + 4);
            uint32_t word_len = flags_len & 0x1F; /* LEN_MASK */
            int is_immediate = (flags_len & 0x80) != 0; /* FLAG_IMMEDIATE */

            char name_buf[32];
            uint32_t k;
            for (k = 0; k < word_len && k < 31; k++) {
                name_buf[k] = (char)hw_read8(curr_word + 5 + k);
            }
            name_buf[word_len] = '\0';

            /* Вычисляем адрес начала тела (XT) с учетом 4-байтового выравнивания */
            uint32_t body_addr = (curr_word + 5 + word_len + 3) & ~3;

            fprintf(f, "%u\t0x%08X\t0x%08X\t0x%08X\t0x%02X\t%u\t%s\t\"%s\"\t0x%08X,",
                    u_word_idx++, curr_word, link_addr, prev_word_ptr, flags_len, word_len,
                    is_immediate ? "IMMEDIATE" : "NORMAL", name_buf, body_addr);

            /* Выгружаем первые 16 байт (или 4 токена) тела слова для инспекции шитого кода */
            for (k = 0; k < 16; k++) {
                fprintf(f, "%02X", hw_read8(body_addr + k));
            }
            fprintf(f, "\n");

            curr_word = prev_word_ptr; /* Переходим по цепочке к предыдущему слову */
        }
    }
    fprintf(f, "\n");

    /* --- СЕКЦИЯ 5: КАРТА ВСТРОЕННОГО СЛОВАРЯ (FLASH ЯДРА НА СИ) --- */
    /* --- В самом конце функции forth_export_runtime_snapshot_csv --- */
    fprintf(f, "=== BUILTIN CORE DICTIONARY (FLASH) ===\n");
    /* Заменяем запятые на \t в заголовке */
    fprintf(f, "Builtin_Index\tBuiltin_Word_Struct_Addr\tPrev_Word_Builtin_Ptr\tFlags_Raw\tName_Len\tImmediate_Flag\tWord_Name\tExecution_Token_ID_Or_Addr\n");

    /*
     * Так как встроенный словарь — это нативные Си-структуры в памяти ПК хоста,
     * мы обходим его через обычные Си-указатели, проверяя правильность флагов ядра.
     */
    extern const forth_word_builtin_t w_colon; /* Наше корневое слово */
    const forth_word_builtin_t *b_walker = &w_colon;
    uint32_t b_word_idx = 0;

    while (b_walker != NULL) {
		uint8_t flags_len = b_walker->flags_len;
		uint32_t word_len = flags_len & 0x1F;
		int is_immediate = (flags_len & 0x80) != 0;

		/*
		 * ИСПРАВЛЕНИЕ: Убираем опасные кавычки, меняем разделители "," на "\t".
		 * Теперь строки с кавычками .", s" и точкой с запятой ; запишутся идеально!
		 */
		fprintf(f, "%u\t%p\t%p\t0x%02X\t%u\t%s\t%s\t0x%08X\n",
				b_word_idx++, (void*)b_walker, (void*)(b_walker->link), flags_len, word_len,
				is_immediate ? "IMMEDIATE" : "NORMAL", b_walker->name, (uint32_t)(uintptr_t)(b_walker->xt));

		b_walker = b_walker->link;
	}

    fclose(f);
    printf("[DEBUG SNAPSHOT] Full runtime snapshot successfully exported to '%s'\n", filename);
}

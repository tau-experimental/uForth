#ifndef FORTH_DEBUG_H
#define FORTH_DEBUG_H

/* Экспорт полной карты памяти в CSV-файл */
void forth_export_memory_csv(const char *filename);

/* НОВЫЙ ИНСТРУМЕНТ: Полный снапшот рантайма (Стеки, Регистры, Словарь) */
void forth_export_runtime_snapshot_csv(const char *filename);

#endif /* FORTH_DEBUG_H */

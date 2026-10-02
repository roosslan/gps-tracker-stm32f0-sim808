# Запускается при каждой сборке прошивки (cmake -P).
# Копирует src/allowed_numbers.h в сгенерированный заголовок OUT, а если файла
# нет — пишет туда пустой список и предупреждает. Файл OUT перезаписывается
# только при изменении содержимого, поэтому main.c пересобирается ровно тогда,
# когда белый список появился, изменился или пропал.
#
#   -DSRC=<путь к src/allowed_numbers.h>  -DOUT=<путь к сгенерированному заголовку>

if(EXISTS "${SRC}")
    file(READ "${SRC}" content)
else()
    message(WARNING
        "src/allowed_numbers.h not found: the tracker will reply to ANY number.\n"
        "Copy src/allowed_numbers.example.h to src/allowed_numbers.h and add your numbers.")
    set(content "/* src/allowed_numbers.h не найден: трекер отвечает любому номеру. */\n#define CFG_ALLOWED_NUMBERS NULL\n")
endif()

file(WRITE "${OUT}.tmp" "${content}")
file(COPY_FILE "${OUT}.tmp" "${OUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUT}.tmp")

    .section    .init
    .global     _start
    .type       _start, @function

_start:
    set.creg    22, 0
    mv          sp, __stack
    set.creg    4, 0
    jmp         _main

    .size _start, .-_start

/* Minimal Cortex-M4 startup for QEMU mps2-an386 */
    .syntax unified
    .cpu cortex-m4
    .thumb

    .section .isr_vector, "a", %progbits
    .type g_pfnVectors, %object
g_pfnVectors:
    .word _estack
    .word Reset_Handler
    .word NMI_Handler
    .word HardFault_Handler
    .word MemManage_Handler
    .word BusFault_Handler
    .word UsageFault_Handler
    .word 0
    .word 0
    .word 0
    .word 0
    .word SVC_Handler
    .word DebugMon_Handler
    .word 0
    .word PendSV_Handler
    .word SysTick_Handler
    /* External interrupts — all default */
    .rept 240
    .word Default_Handler
    .endr

    .text
    .global Reset_Handler
    .thumb_func
    .type Reset_Handler, %function
Reset_Handler:
    ldr   r0, =_estack
    msr   msp, r0

    /* Copy .data from flash to RAM */
    ldr   r0, =_sdata
    ldr   r1, =_edata
    ldr   r2, =_sidata
    movs  r3, #0
    b     copy_data_check
copy_data_loop:
    ldr   r4, [r2, r3]
    str   r4, [r0, r3]
    adds  r3, r3, #4
copy_data_check:
    adds  r4, r0, r3
    cmp   r4, r1
    bcc   copy_data_loop

    /* Zero .bss */
    ldr   r2, =_sbss
    ldr   r4, =_ebss
    movs  r3, #0
    b     zero_bss_check
zero_bss_loop:
    str   r3, [r2]
    adds  r2, r2, #4
zero_bss_check:
    cmp   r2, r4
    bcc   zero_bss_loop

    bl    main
    b     .

    .thumb_func
    .weak NMI_Handler
NMI_Handler:
    b .

    .thumb_func
    .weak HardFault_Handler
HardFault_Handler:
    b .

    .thumb_func
    .weak MemManage_Handler
MemManage_Handler:
    b .

    .thumb_func
    .weak BusFault_Handler
BusFault_Handler:
    b .

    .thumb_func
    .weak UsageFault_Handler
UsageFault_Handler:
    b .

    .thumb_func
    .weak DebugMon_Handler
DebugMon_Handler:
    b .

    .thumb_func
    .weak Default_Handler
Default_Handler:
    b .

    /* FreeRTOS handlers — defined by the CM4 port; declared weak here */
    .weak SVC_Handler
    .thumb_set SVC_Handler, Default_Handler

    .weak PendSV_Handler
    .thumb_set PendSV_Handler, Default_Handler

    .weak SysTick_Handler
    .thumb_set SysTick_Handler, Default_Handler

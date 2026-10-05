/* Reset handler and vector table for the nRF5340 application core. No interrupt is ever enabled
 * (main.c polls), so the table ends after the core exceptions. */
#include <stdint.h>
#include <string.h>

extern uint32_t __stack_top, __data_load, __data_start, __data_end, __bss_start, __bss_end;

void SystemInit(void);
int main(void);

void Reset_Handler(void)
{
    memcpy(&__data_start, &__data_load, (size_t)((char *)&__data_end - (char *)&__data_start));
    memset(&__bss_start, 0, (size_t)((char *)&__bss_end - (char *)&__bss_start));
    SystemInit();
    main();
    for (;;) {
    }
}

static void fault(void)
{
    for (;;) {
    }
}

__attribute__((section(".vectors"), used))
static void (*const vectors[16])(void) = {
    (void (*)(void))&__stack_top,
    Reset_Handler,
    fault, /* NMI */
    fault, /* HardFault */
    fault, /* MemManage */
    fault, /* BusFault */
    fault, /* UsageFault */
    fault, /* SecureFault */
    0,
    0,
    0,
    fault, /* SVCall */
    fault, /* DebugMonitor */
    0,
    fault, /* PendSV */
    fault, /* SysTick */
};

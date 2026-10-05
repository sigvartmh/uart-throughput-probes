/*
 * UART throughput test for the nRF5340 DK's application core: what does the on-board debug
 * probe's virtual COM port pass at 1 Mbaud, in each direction?
 *
 * UARTE0 on the DK's UART pins: TX P0.20, RX P0.22; built with FLOW=1 also RTS P0.19, CTS P0.21.
 * The firmware is never the bottleneck, and it uses no interrupts:
 *   - reception is EasyDMA into one buffer that a shortcut restarts (the data is discarded);
 *   - received bytes are counted in hardware (RXDRDY -> DPPI -> TIMER0 in counter mode);
 *   - transmission is EasyDMA, 4 KiB at a time.
 *
 * It answers each burst from the host once the line has been idle for IDLE_US:
 *   one byte          -> STREAM_BYTES of a byte counter (0, 1, .. 255, 0, ..), a pause, then
 *                        "\r\ntx <bytes> B in <ms> ms = <rate> B/s, errorsrc <n>\r\n"
 *   anything longer   -> "rx <bytes> B in <ms> ms = <rate> B/s, errorsrc <n>\r\n"
 * The times are this chip's (crystal): for rx from the first to the last byte, for tx how long
 * the stream took to leave. The line rate is 100000 B/s. errorsrc is the UARTE's ERRORSRC since
 * the last report: 1 overrun, 2 parity, 4 framing, 8 break.
 */
#include <nrfx.h>
#include <hal/nrf_clock.h>
#include <hal/nrf_dppi.h>
#include <hal/nrf_gpio.h>
#include <hal/nrf_timer.h>
#include <hal/nrf_uarte.h>

#ifndef FLOW
#define FLOW 0 /* 1: RTS/CTS */
#endif

#define PIN_TX  NRF_GPIO_PIN_MAP(0, 20)
#define PIN_RX  NRF_GPIO_PIN_MAP(0, 22)
#define PIN_RTS NRF_GPIO_PIN_MAP(0, 19)
#define PIN_CTS NRF_GPIO_PIN_MAP(0, 21)
#define PIN_LED NRF_GPIO_PIN_MAP(0, 28) /* LED1: toggles per report */

#define STREAM_BYTES (1024 * 1024)
#define IDLE_US      300000  /* a burst ends after this long without a byte */
#define DRAIN_US     1000000 /* after the stream: lets a probe that fell behind catch up, so the
                              * "tx" line is not lost with the bytes it dropped */

#define UART    NRF_UARTE0
#define COUNTER NRF_TIMER0 /* received bytes */
#define CLOCK   NRF_TIMER1 /* microseconds, free-running */

static uint8_t rx_sink[256];
static uint8_t pattern[4096]; /* a multiple of 256: the counter continues across pieces */

static uint32_t capture(NRF_TIMER_Type *timer)
{
    nrf_timer_task_trigger(timer, NRF_TIMER_TASK_CAPTURE0);
    return nrf_timer_cc_get(timer, NRF_TIMER_CC_CHANNEL0);
}

/* Blocking; with FLOW it waits for CTS as long as it takes. EasyDMA reads from RAM only. */
static void send(const uint8_t *data, size_t len)
{
    nrf_uarte_event_clear(UART, NRF_UARTE_EVENT_ENDTX);
    nrf_uarte_tx_buffer_set(UART, data, len);
    nrf_uarte_task_trigger(UART, NRF_UARTE_TASK_STARTTX);
    while (!nrf_uarte_event_check(UART, NRF_UARTE_EVENT_ENDTX)) {
    }
}

static char *put_str(char *p, const char *s)
{
    while (*s) {
        *p++ = *s++;
    }
    return p;
}

static char *put_u32(char *p, uint32_t v)
{
    char digits[10];
    int n = 0;
    do {
        digits[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n) {
        *p++ = digits[--n];
    }
    return p;
}

static void report(const char *what, uint32_t bytes, uint32_t us, uint32_t errorsrc)
{
    char line[96];
    char *p = put_str(line, what);
    p = put_u32(p, bytes);
    p = put_str(p, " B in ");
    p = put_u32(p, us / 1000);
    p = put_str(p, " ms = ");
    p = put_u32(p, us ? (uint32_t)((uint64_t)bytes * 1000000 / us) : 0);
    p = put_str(p, " B/s, errorsrc ");
    p = put_u32(p, errorsrc);
    p = put_str(p, "\r\n");
    send((const uint8_t *)line, (size_t)(p - line));
    nrf_gpio_pin_toggle(PIN_LED);
}

int main(void)
{
    /* The crystal: the baud rate and the time base are as accurate as it is. */
    nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_HFCLKSTART);
    while (!nrf_clock_event_check(NRF_CLOCK, NRF_CLOCK_EVENT_HFCLKSTARTED)) {
    }

    for (size_t i = 0; i < sizeof pattern; i++) {
        pattern[i] = (uint8_t)i;
    }
    nrf_gpio_cfg_output(PIN_LED);

    nrf_timer_mode_set(CLOCK, NRF_TIMER_MODE_TIMER);
    nrf_timer_bit_width_set(CLOCK, NRF_TIMER_BIT_WIDTH_32);
    nrf_timer_prescaler_set(CLOCK, 4); /* 16 MHz / 2^4 = 1 MHz */
    nrf_timer_task_trigger(CLOCK, NRF_TIMER_TASK_START);

    /* Every received byte (RXDRDY) steps COUNTER, through DPPI channel 0. */
    nrf_timer_mode_set(COUNTER, NRF_TIMER_MODE_COUNTER);
    nrf_timer_bit_width_set(COUNTER, NRF_TIMER_BIT_WIDTH_32);
    nrf_timer_subscribe_set(COUNTER, NRF_TIMER_TASK_COUNT, 0);
    nrf_timer_task_trigger(COUNTER, NRF_TIMER_TASK_START);
    nrf_uarte_publish_set(UART, NRF_UARTE_EVENT_RXDRDY, 0);
    nrf_dppi_channels_enable(NRF_DPPIC, 1u << 0);

    /* Pull-ups: the probe releases its pins while no host has the port open. */
    nrf_gpio_pin_set(PIN_TX);
    nrf_gpio_cfg_output(PIN_TX);
    nrf_gpio_cfg_input(PIN_RX, NRF_GPIO_PIN_PULLUP);
    nrf_uarte_txrx_pins_set(UART, PIN_TX, PIN_RX);
#if FLOW
    nrf_gpio_pin_set(PIN_RTS);
    nrf_gpio_cfg_output(PIN_RTS);
    nrf_gpio_cfg_input(PIN_CTS, NRF_GPIO_PIN_PULLUP);
    nrf_uarte_hwfc_pins_set(UART, PIN_RTS, PIN_CTS);
#endif
    nrf_uarte_config_t config = {
        .hwfc = FLOW ? NRF_UARTE_HWFC_ENABLED : NRF_UARTE_HWFC_DISABLED,
        .parity = NRF_UARTE_PARITY_EXCLUDED,
        .stop = NRF_UARTE_STOP_ONE,
    };
    nrf_uarte_configure(UART, &config);
    nrf_uarte_baudrate_set(UART, NRF_UARTE_BAUDRATE_1000000);
    nrf_uarte_enable(UART);

    /* Receive for ever: ENDRX restarts reception into the same buffer at once, so with FLOW
     * this side's RTS stays asserted. */
    nrf_uarte_rx_buffer_set(UART, rx_sink, sizeof rx_sink);
    nrf_uarte_shorts_enable(UART, NRF_UARTE_SHORT_ENDRX_STARTRX);
    nrf_uarte_task_trigger(UART, NRF_UARTE_TASK_STARTRX);

    uint32_t seen = 0, first = 0, last = 0;
    for (;;) {
        uint32_t now = capture(CLOCK);
        uint32_t count = capture(COUNTER);
        if (count != seen) {
            if (seen == 0) {
                first = now;
            }
            seen = count;
            last = now;
        } else if (seen && now - last > IDLE_US) {
            uint32_t errorsrc = nrf_uarte_errorsrc_get_and_clear(UART);
            if (seen == 1) {
                uint32_t start = capture(CLOCK);
                for (uint32_t sent = 0; sent < STREAM_BYTES; sent += sizeof pattern) {
                    send(pattern, sizeof pattern);
                }
                uint32_t end = capture(CLOCK);
                while (capture(CLOCK) - end < DRAIN_US) {
                }
                report("\r\ntx ", STREAM_BYTES, end - start, errorsrc);
            } else {
                report("rx ", seen, last - first, errorsrc);
            }
            nrf_timer_task_trigger(COUNTER, NRF_TIMER_TASK_CLEAR);
            seen = 0;
        }
    }
}

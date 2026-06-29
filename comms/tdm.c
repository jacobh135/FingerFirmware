#include "tdm.h"

#include <stddef.h>

#include "lsm6dsv16b.h"
#include "stm32g431_min.h"

/* ------------------------------------------------------------------------- *
 * Peripheral definitions not present in stm32g431_min.h.
 * Addresses/bit positions verified against ST CMSIS (stm32g431xx.h) and the
 * STM32G4 reference manual. Kept local to this module so the shared minimal
 * header doesn't need to change.
 *
 * ROLE: the LSM6DSV16B is the TDM *slave* -- it does not generate BCLK/WCLK,
 * it shifts accelerometer data out in response to clocks supplied by the host.
 * So the STM32 SAI is the TDM *master receiver*: it drives BCLK on PA8 and
 * WCLK on PA9 (outputs) and clocks the sensor's TDMout in on PA10.
 * ------------------------------------------------------------------------- */
#define SAI1_BASE             (0x40015400UL)            /* APB2 + 0x5400 */
#define SAI1_BLOCK_A_BASE     (SAI1_BASE + 0x004UL)
#define DMA1_BASE             (0x40020000UL)            /* AHB1PERIPH_BASE */
#define DMA1_CHANNEL1_BASE    (DMA1_BASE + 0x008UL)
#define DMAMUX1_BASE          (0x40020800UL)
#define DMAMUX1_CHANNEL0_BASE (DMAMUX1_BASE)

typedef struct {
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t FRCR;
    volatile uint32_t SLOTR;
    volatile uint32_t IMR;
    volatile uint32_t SR;
    volatile uint32_t CLRFR;
    volatile uint32_t DR;
} SAI_Block_TypeDef;

typedef struct {
    volatile uint32_t CCR;
    volatile uint32_t CNDTR;
    volatile uint32_t CPAR;
    volatile uint32_t CMAR;
} DMA_Channel_TypeDef;

typedef struct {
    volatile uint32_t CCR;
} DMAMUX_Channel_TypeDef;

#define SAI1_Block_A     ((SAI_Block_TypeDef *)SAI1_BLOCK_A_BASE)
#define DMA1_Channel1    ((DMA_Channel_TypeDef *)DMA1_CHANNEL1_BASE)
#define DMAMUX1_Channel0 ((DMAMUX_Channel_TypeDef *)DMAMUX1_CHANNEL0_BASE)

/* RCC enable bits (positions verified against CMSIS) */
#define RCC_AHB1ENR_DMA1EN    (1UL << 0)
#define RCC_AHB1ENR_DMAMUX1EN (1UL << 2)
#define RCC_APB2ENR_SAI1EN    (1UL << 21)

/* SAI block CR1 */
#define SAI_CR1_MODE_MASTER_RX (1UL << 0)    /* MODE = 01: master receiver       */
#define SAI_CR1_DS_16BIT       (4UL << 5)    /* DS  = 100: 16-bit data           */
#define SAI_CR1_CKSTR          (1UL << 9)    /* clock strobing edge (see notes)  */
#define SAI_CR1_OUTDRIV        (1UL << 13)   /* drive outputs as soon as enabled */
#define SAI_CR1_SAIEN          (1UL << 16)
#define SAI_CR1_DMAEN          (1UL << 17)
#define SAI_CR1_NODIV          (1UL << 19)   /* no MCLK; SCK = ker_ck / MCKDIV   */
#define SAI_CR1_MCKDIV(n)      ((uint32_t)(n) << 20)
/* SAI block CR2 */
#define SAI_CR2_FTH_QUARTER    (1UL << 0)    /* FIFO threshold 1/4               */
/* SAI block FRCR */
#define SAI_FRCR_FSPOL         (1UL << 17)   /* FS active high (WCLK rising)     */
/* SAI block SLOTR */
#define SAI_SLOTR_SLOTSZ_16    (1UL << 6)    /* SLOTSZ = 01: 16-bit slots        */

/* Bit-clock divider. SAI1 kernel clock = SYSCLK = HSI16 = 16 MHz (reset
   default; SAI1SEL = 00). With NODIV=1, BCLK = 16 MHz / MCKDIV.
   MCKDIV=8 -> BCLK = 2.0 MHz, WCLK = BCLK/128 = 15.625 kHz -- slightly under
   the nominal 16 kHz because 16 MHz can't divide to exactly 2.048 MHz, but
   fine to get the link running. For exact 16 kHz, source SAI1 from the PLL
   (RCC_CCIPR SAI1SEL) -- left as a later refinement.
   NOTE: if frame_count climbs at ~half the expected rate, the silicon is using
   SCK = ker/(2*MCKDIV); set this to 4. */
#define SAI_MCKDIV_VALUE 8U

/* DMA channel CCR */
#define DMA_CCR_EN       (1UL << 0)
#define DMA_CCR_CIRC     (1UL << 5)
#define DMA_CCR_MINC     (1UL << 7)
#define DMA_CCR_PSIZE_16 (1UL << 8)         /* PSIZE = 01: 16-bit               */
#define DMA_CCR_MSIZE_16 (1UL << 10)        /* MSIZE = 01: 16-bit               */

#define DMAMUX_REQ_SAI1_A 108U              /* DMAMUX request line for SAI1_A   */

/* GPIO: PA8=SAI1_SCK_A (BCLK out), PA9=SAI1_FS_A (WCLK out),
   PA10=SAI1_SD_A (TDMout in), all AF14 */
#define SAI_AF      14U
#define SAI_SCK_PIN 8U
#define SAI_FS_PIN  9U
#define SAI_SD_PIN  10U

/* 128-BCLK frame (BCLK / WCLK = 128). We enable only slots 0,1,2 (axis order
   Z,Y,X per TDM_CFG1), so the DMA receives 3 x int16 per frame. Buffer holds
   TDM_FRAMES frames as a circular ring. */
#define TDM_SLOTS_PER_FRAME 3U
#define TDM_FRAMES          128U
#define TDM_BUF_SAMPLES     (TDM_FRAMES * TDM_SLOTS_PER_FRAME)

/* One-shot capture burst: snapshots (x,y,z) from the live circular buffer into
   a separate RAM buffer at full TDM rate, until full, for later slow drain
   over UART (115200 baud can't keep up with live 3-axis streaming -- see
   tdm_capture_start() for the bandwidth math). Kept deliberately SMALL: the
   Phase-2 FFT needs a healthy stack (float butterfly math + nested loops),
   and on this 32 KB chip a larger capture buffer starved it into a crash.
   2048 frames (~131 ms) leaves ~13 KB for the stack. Burst capture is a
   secondary path now -- continuous texture streaming is the main one. */
#define TDM_CAPTURE_FRAMES (2048U)

static volatile int16_t tdm_buf[TDM_BUF_SAMPLES];
static tdm_status_t status = TDM_STATUS_DISABLED;
static uint32_t total_samples;
static uint32_t last_cndtr;

/* Capture-burst state. capture_xyz holds interleaved x,y,z per frame so the
   drain step (and the Python side) can read it straight as "x y z" triples. */
static int16_t capture_xyz[TDM_CAPTURE_FRAMES][3];
static uint32_t capture_count;
static bool capture_active;
static uint32_t capture_last_seen_frames;

static void gpio_set_af(GPIO_TypeDef *gpio, uint32_t pin, uint32_t af)
{
    /* alternate-function mode */
    gpio->MODER &= ~(3UL << (pin * 2U));
    gpio->MODER |= (2UL << (pin * 2U));
    /* very-high speed (clean edges at 2 MHz BCLK) */
    gpio->OSPEEDR |= (3UL << (pin * 2U));
    /* no pull-up/pull-down */
    gpio->PUPDR &= ~(3UL << (pin * 2U));
    /* AF number (pins 8..15 live in AFRH) */
    if (pin < 8U) {
        gpio->AFRL &= ~(0xFUL << (pin * 4U));
        gpio->AFRL |= (af << (pin * 4U));
    } else {
        uint32_t shift = (pin - 8U) * 4U;
        gpio->AFRH &= ~(0xFUL << shift);
        gpio->AFRH |= (af << shift);
    }
}

static void tdm_gpio_init(void)
{
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    (void)RCC->AHB2ENR;

    gpio_set_af(GPIOA, SAI_SCK_PIN, SAI_AF);  /* BCLK out  */
    gpio_set_af(GPIOA, SAI_FS_PIN, SAI_AF);   /* WCLK out  */
    gpio_set_af(GPIOA, SAI_SD_PIN, SAI_AF);   /* TDMout in */
}

static void tdm_sai_init(void)
{
    RCC->APB2ENR |= RCC_APB2ENR_SAI1EN;
    (void)RCC->APB2ENR;

    /* Make sure the block is disabled while we configure it. */
    SAI1_Block_A->CR1 &= ~SAI_CR1_SAIEN;

    /* Master receiver: the SAI generates BCLK (PA8) and WCLK (PA9) and clocks
       in the sensor's TDMout (PA10). Free protocol, 16-bit data, MSB-first,
       no MCLK output, bit clock = kernel/MCKDIV, DMA enabled. CKSTR selects the
       sampling edge (see bring-up notes). */
    SAI1_Block_A->CR1 = SAI_CR1_MODE_MASTER_RX |
                        SAI_CR1_DS_16BIT |
                        SAI_CR1_NODIV |
                        SAI_CR1_MCKDIV(SAI_MCKDIV_VALUE) |
                        SAI_CR1_OUTDRIV |
                        SAI_CR1_CKSTR |
                        SAI_CR1_DMAEN;

    SAI1_Block_A->CR2 = SAI_CR2_FTH_QUARTER;

    /* Frame: 128 bit clocks per WCLK period (FRL = 128-1). FS active high
       (WCLK rising starts the frame), FS aligned to first bit of slot 0
       (FSOFF = 0), frame-start only (FSDEF = 0). FSALL is a master-mode
       parameter; set to one slot for completeness. */
    SAI1_Block_A->FRCR = (127UL << 0) |     /* FRL  = 127 -> 128-bit frame */
                         (15UL << 8) |      /* FSALL = 15 (not used in slave) */
                         SAI_FRCR_FSPOL;    /* FSPOL = 1 (active high) */

    /* 8 slots of 16 bits (NBSLOT = 8-1), enable only slots 0,1,2 (Z,Y,X). The
       sensor only ever drives 3 of its 8 possible TDM slots when configured
       for 3-axis output (TDM_CFG1 = 0xE0); slots 3-7 are left tristated by
       the sensor, so we don't capture them. */
    SAI1_Block_A->SLOTR = SAI_SLOTR_SLOTSZ_16 |
                          (7UL << 8) |        /* NBSLOT = 7 -> 8 slots */
                          (0x0007UL << 16);   /* SLOTEN slots 0,1,2 */
}

static void tdm_dma_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_DMA1EN | RCC_AHB1ENR_DMAMUX1EN;
    (void)RCC->AHB1ENR;

    /* Disable the channel before (re)programming addresses. */
    DMA1_Channel1->CCR = 0U;

    DMA1_Channel1->CPAR = (uint32_t)(&SAI1_Block_A->DR);
    DMA1_Channel1->CMAR = (uint32_t)tdm_buf;
    DMA1_Channel1->CNDTR = TDM_BUF_SAMPLES;

    /* Route SAI1_A requests to DMA1 channel 1 (DMAMUX channel 0). */
    DMAMUX1_Channel0->CCR = DMAMUX_REQ_SAI1_A;

    /* Peripheral-to-memory, circular, 16-bit both sides, increment memory. */
    DMA1_Channel1->CCR = DMA_CCR_MINC |
                         DMA_CCR_CIRC |
                         DMA_CCR_PSIZE_16 |
                         DMA_CCR_MSIZE_16;
    DMA1_Channel1->CCR |= DMA_CCR_EN;

    last_cndtr = TDM_BUF_SAMPLES;
    total_samples = 0U;
    capture_active = false;
    capture_count = 0U;
    capture_last_seen_frames = 0U;
}

void tdm_init(void)
{
    status = TDM_STATUS_DISABLED;

    /* Turn on the IMU's audio channel over I2C (configures TDM_CFG0/1/2 and
       switches the accelerometer into high-performance TDM mode). As a TDM
       slave it now waits for the host to supply BCLK/WCLK. */
    if (!lsm6dsv16b_enable_tdm_audio()) {
        status = TDM_STATUS_ERROR;
        return;
    }

    tdm_gpio_init();
    tdm_dma_init();
    tdm_sai_init();

    /* Start the master clocks + reception. */
    SAI1_Block_A->CR1 |= SAI_CR1_SAIEN;

    status = TDM_STATUS_STREAMING;
}

void tdm_update(void)
{
    if (status != TDM_STATUS_STREAMING) {
        return;
    }

    /* Track how far the circular DMA has advanced so callers can tell the
       stream is live. CNDTR counts down and reloads to TDM_BUF_SAMPLES. */
    uint32_t cndtr = DMA1_Channel1->CNDTR;
    uint32_t consumed;

    if (cndtr <= last_cndtr) {
        consumed = last_cndtr - cndtr;
    } else {
        consumed = last_cndtr + (TDM_BUF_SAMPLES - cndtr);
    }

    total_samples += consumed;
    last_cndtr = cndtr;

    /* Capture-burst: while active, grab every newly-completed frame from the
       live circular buffer into capture_xyz until it's full. Called from the
       main loop, so we may see more than one new frame per call if the loop
       runs slower than the ~15.6 kHz frame rate -- walk forward through all
       of them rather than just sampling "latest" so we don't silently drop
       frames inside the capture window.

       TIMING ASSUMPTION: the live circular buffer (TDM_BUF_SAMPLES, 128
       frames) wraps every ~8.2 ms. tdm_update() must be called more often
       than that during a capture or frames get overwritten before this loop
       reads them -- with the main loop running tens of thousands of
       iterations/sec (see the `loops=` heartbeat counter), this has a wide
       margin today, but don't add a blocking call (e.g. a long UART write,
       a multi-ms delay) anywhere in the main loop while a capture is armed. */
    if (capture_active) {
        uint32_t frames_now = total_samples / TDM_SLOTS_PER_FRAME;

        while ((capture_last_seen_frames < frames_now) && (capture_count < TDM_CAPTURE_FRAMES)) {
            uint32_t frame_index = capture_last_seen_frames;
            uint32_t base = (frame_index * TDM_SLOTS_PER_FRAME) % TDM_BUF_SAMPLES;

            int16_t z_raw = tdm_buf[base + 0U];
            int16_t y_raw = tdm_buf[base + 1U];
            int16_t x_raw = tdm_buf[base + 2U];

            capture_xyz[capture_count][0] = x_raw;
            capture_xyz[capture_count][1] = y_raw;
            capture_xyz[capture_count][2] = z_raw;

            capture_count++;
            capture_last_seen_frames++;
        }

        if (capture_count >= TDM_CAPTURE_FRAMES) {
            capture_active = false;
        }
    }
}

tdm_status_t tdm_status(void)
{
    return status;
}

uint32_t tdm_frame_count(void)
{
    return total_samples / TDM_SLOTS_PER_FRAME;
}

bool tdm_latest_xyz(int16_t *x, int16_t *y, int16_t *z)
{
    if ((status != TDM_STATUS_STREAMING) || (total_samples < TDM_SLOTS_PER_FRAME)) {
        return false;
    }

    /* Index the DMA is about to write next. */
    uint32_t widx = TDM_BUF_SAMPLES - DMA1_Channel1->CNDTR;

    /* Step back to the most recently completed frame boundary. */
    uint32_t frame_end = (widx / TDM_SLOTS_PER_FRAME) * TDM_SLOTS_PER_FRAME;
    uint32_t base = (frame_end + TDM_BUF_SAMPLES - TDM_SLOTS_PER_FRAME) % TDM_BUF_SAMPLES;

    int16_t z_raw = tdm_buf[base + 0U];
    int16_t y_raw = tdm_buf[base + 1U];
    int16_t x_raw = tdm_buf[base + 2U];

    if (x != NULL) { *x = x_raw; }
    if (y != NULL) { *y = y_raw; }
    if (z != NULL) { *z = z_raw; }

    return true;
}

/* DIAGNOSTIC: copy the most recent complete 8-slot frame so the caller can see
   which slots carry real data. Only the first 3 are ever live in the current
   3-slot config; slots 3-7 will read back as whatever was last in tdm_buf
   (stale, not meaningful) since the SAI no longer writes them. Kept only for
   re-diagnosing if TDM ever needs re-bringup; not used in normal operation. */
bool tdm_dump_frame(int16_t out[8])
{
    if ((status != TDM_STATUS_STREAMING) || (total_samples < TDM_SLOTS_PER_FRAME)) {
        return false;
    }

    uint32_t widx = TDM_BUF_SAMPLES - DMA1_Channel1->CNDTR;
    uint32_t frame_end = (widx / TDM_SLOTS_PER_FRAME) * TDM_SLOTS_PER_FRAME;
    uint32_t base = (frame_end + TDM_BUF_SAMPLES - TDM_SLOTS_PER_FRAME) % TDM_BUF_SAMPLES;
    uint32_t n = (TDM_SLOTS_PER_FRAME < 8U) ? TDM_SLOTS_PER_FRAME : 8U;

    for (uint32_t i = 0U; i < n; i++) {
        out[i] = tdm_buf[base + i];
    }
    for (uint32_t i = n; i < 8U; i++) {
        out[i] = 0;
    }

    return true;
}

/* --------------------------- capture-burst API --------------------------- *
 * One-shot full-rate 3-axis capture into RAM, for later slow drain over UART.
 * 115200 baud carries ~11.5 KB/s of payload; full-rate 3-axis ASCII streaming
 * needs on the order of 280 KB/s (15625 frames/s x 3 axes x ~6 ASCII
 * chars/sample), about 24x too much. So: capture at full rate into RAM first
 * (cheap -- this is just a memcpy-rate loop, no UART involved), then drain
 * the finished buffer out slowly afterward where the baud limit no longer
 * matters because nothing is being lost.
 * -------------------------------------------------------------------------- */

void tdm_capture_start(void)
{
    if (status != TDM_STATUS_STREAMING) {
        return;
    }

    capture_count = 0U;
    capture_last_seen_frames = total_samples / TDM_SLOTS_PER_FRAME;
    capture_active = true;
}

bool tdm_capture_active(void)
{
    return capture_active;
}

bool tdm_capture_ready(void)
{
    return (!capture_active) && (capture_count >= TDM_CAPTURE_FRAMES);
}

uint32_t tdm_capture_count(void)
{
    return capture_count;
}

uint32_t tdm_capture_total_frames(void)
{
    return TDM_CAPTURE_FRAMES;
}

bool tdm_capture_get(uint32_t index, int16_t *x, int16_t *y, int16_t *z)
{
    if (index >= capture_count) {
        return false;
    }

    if (x != NULL) { *x = capture_xyz[index][0]; }
    if (y != NULL) { *y = capture_xyz[index][1]; }
    if (z != NULL) { *z = capture_xyz[index][2]; }

    return true;
}

/* --------------------------- continuous stream --------------------------- */

void tdm_stream_cursor_init(tdm_stream_cursor_t *cursor)
{
    if (cursor == NULL) {
        return;
    }
    cursor->next_frame = 0U;
    cursor->primed = false;
}

uint32_t tdm_stream_pull(tdm_stream_cursor_t *cursor,
                         int16_t *x, int16_t *y, int16_t *z,
                         uint32_t max_frames)
{
    if ((cursor == NULL) || (status != TDM_STATUS_STREAMING)) {
        return 0U;
    }

    uint32_t frames_now = total_samples / TDM_SLOTS_PER_FRAME;

    /* On the first pull, start from "now" so we don't try to replay stale
       history that may already have been overwritten in the ring. */
    if (!cursor->primed) {
        cursor->next_frame = frames_now;
        cursor->primed = true;
        return 0U;
    }

    if (frames_now <= cursor->next_frame) {
        return 0U;   /* nothing new */
    }

    uint32_t available = frames_now - cursor->next_frame;

    /* The circular buffer only holds the most recent TDM_FRAMES frames. If the
       consumer fell behind by more than that, the oldest unread frames are
       gone -- skip forward to the oldest frame still resident so we return
       real (if discontiguous) data rather than garbage. The caller can notice
       the jump because fewer frames came back than wall-clock implies. */
    if (available > TDM_FRAMES) {
        cursor->next_frame = frames_now - TDM_FRAMES;
        available = TDM_FRAMES;
    }

    uint32_t to_copy = (available < max_frames) ? available : max_frames;

    for (uint32_t i = 0U; i < to_copy; i++) {
        uint32_t frame_index = cursor->next_frame + i;
        uint32_t base = (frame_index * TDM_SLOTS_PER_FRAME) % TDM_BUF_SAMPLES;

        /* Slot order is Z,Y,X (per TDM_CFG1 axis order). */
        if (z != NULL) { z[i] = tdm_buf[base + 0U]; }
        if (y != NULL) { y[i] = tdm_buf[base + 1U]; }
        if (x != NULL) { x[i] = tdm_buf[base + 2U]; }
    }

    cursor->next_frame += to_copy;
    return to_copy;
}

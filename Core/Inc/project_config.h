#ifndef PROJECT_CONFIG_H
#define PROJECT_CONFIG_H

#include <stdint.h>

/* Reference-only LCD demo path:
 * LiangHaoCai-IPS-2.8inch_SPI_Module_MSP2807_User_Manual_CN
 * Do not modify or include that demo project directly.
 */

#define AD9834_MCLK_HZ              75000000UL
#define DDS_FREQ_MIN_HZ             1UL
#define DDS_FREQ_MAX_HZ             10000000UL
#define DDS_DEFAULT_FREQ_HZ         1000UL

#define ADC_REF_MV                  3300UL
#define WAVE_SAMPLE_COUNT           1024U
#define WAVE_DMA_BUFFER_COUNT       (WAVE_SAMPLE_COUNT * 2U)
#define WAVE_SAMPLE_RATE_LOW_HZ     20000UL
#define WAVE_SAMPLE_RATE_MID_HZ     100000UL
#define WAVE_SAMPLE_RATE_HIGH_HZ    600000UL
#define WAVE_FREQ_LOW_BAND_HZ       600UL
#define WAVE_FREQ_LOW_EXIT_HZ       1200UL
#define WAVE_FREQ_HIGH_ENTER_HZ     20000UL
#define WAVE_FREQ_HIGH_EXIT_HZ      12000UL
#define BODE_AVG_SAMPLES            96U
#define SWEEP_SETTLE_MS             120U
#define SWEEP_INITIAL_SETTLE_MS     1500U
#define SWEEP_START_FREQ_HZ         100UL
#define SWEEP_STOP_FREQ_HZ          10000000UL
#define SWEEP_MAX_POINTS            81U

/* AD8307 typical slope is 25 mV/dB, so dB = delta_mV * 100 / 25
 * when the result is stored as centi-dB.
 */
#define AD8307_MV_PER_DB            25L
/* Set to 1 if PA3/PA4 are physically wired opposite to the logical Bode
 * channels. Logical REF must be the signal before the DUT, logical DUT must be
 * the signal after the DUT.
 */
#define BODE_SWAP_REF_DUT           0U
/* Add this offset after the AD8307 voltage-difference calculation.
 * Calibrate with a through connection: if the displayed gain is +8.00 dB,
 * set BODE_GAIN_OFFSET_CDB to -800.
 */
#define BODE_GAIN_OFFSET_CDB        100L

#define LCD_WIDTH                   240U
#define LCD_HEIGHT                  320U
#define LCD_DEFAULT_ROTATION        1U
/* ILI9341 MADCTL bits: MY=0x80, MX=0x40, MV=0x20, BGR=0x08.
 * If text is mirrored, tune these four values only; do not change drawing code.
 * The default landscape value includes MX to correct the observed mirror.
 */
#define LCD_MADCTL_ROT0             0x48U
#define LCD_MADCTL_ROT1             0x68U
#define LCD_MADCTL_ROT2             0x88U
#define LCD_MADCTL_ROT3             0xA8U
/* Number of NOP cycles inserted between software-SPI SCK edges.
 * With 72 MHz HCLK, 5 cycles gives ~3.5 MHz SPI clock. ILI9341 max is 10 MHz.
 * Increase if the LCD shows pixel errors or fails to init; decrease for speed.
 */
#define LCD_SOFT_SPI_DELAY_CYCLES   5U
#define UI_FIELD_REFRESH_MS         500U

/* Crest factor windows. These are intentionally wide for noisy real signals. */
#define CREST_SQUARE_MIN_X1000      900U
#define CREST_SQUARE_MAX_X1000      1150U
#define CREST_SINE_MIN_X1000        1280U
#define CREST_SINE_MAX_X1000        1560U
#define CREST_TRIANGLE_MIN_X1000    1600U
#define CREST_TRIANGLE_MAX_X1000    1950U

#define WAVE_MIN_RMS_MV             25U
#define WAVE_MIN_VPP_MV             80U
#define WAVE_LOW_WARN_VPP_MV        180U
#define WAVE_CLIP_LOW_RAW           8U
#define WAVE_CLIP_HIGH_RAW          4087U
#define WAVE_CLIP_COUNT_LIMIT       8U

#define UI_REFRESH_MS               100U
#define INPUT_SCAN_MS               10U
#define ENCODER_REVERSE_GUARD_MS    80U
#define ENCODER_COUNTS_PER_STEP     4
/* Set INPUT_HARDWARE_ENABLE to 1 after keys/encoder are physically connected.
 * Floating no-pull inputs can generate false events during LCD-only bring-up.
 */
#define INPUT_HARDWARE_ENABLE       1U

#if WAVE_SAMPLE_COUNT > 1024U
#error "Keep WAVE_SAMPLE_COUNT <= 1024 on STM32F103C8T6 RAM."
#endif

#endif /* PROJECT_CONFIG_H */

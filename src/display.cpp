/**
 * display.cpp - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * SSD1306 128x64 OLED driver over I2C1 + DisplayTask.
 *
 * Design notes
 * ------------
 * - The panel is written through a 1024 byte RAM frame buffer (128 x 64
 *   pixels, one byte per vertical eight-pixel column strip) and flushed one
 *   page at a time.  Rendering therefore never touches the I2C peripheral
 *   while pixels are being composed, and a partial screen can never be seen
 *   by the user.
 * - Section 26: only DisplayTask renders and flushes.  The driver itself has
 *   no internal locking because the single-owner rule makes one unnecessary.
 *   InputTask (milestone 8) honours that rule too: it publishes the page it
 *   selected on modeQueue and never calls into this file.
 * - Section 27/29: the screen is one page at a time - title, value, and the
 *   ROOM MONITOR banner above the rule - chosen by DisplayMode.
 * - The glyph data is the Adafruit GFX 'classic' 5x7 font, kept in
 *   include/glcdfont.h; its BSD licence is in assets/LICENSE-glcdfont.txt.
 * - No Arduino or third-party display library is used - section 6 requires
 *   native STM32Cube/FreeRTOS APIs only.
 */

#include "display.h"

#include <stdio.h>
#include <string.h>

#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"
#include "event_groups.h"

#include "rtos_objects.h"
#include "input.h"

#include "glcdfont.h"

/*-----------------------------------------------------------
 * Panel geometry and wiring
 *----------------------------------------------------------*/
#define OLED_I2C_ADDRESS   0x3CU     /* 7 bit address, see board-ssd1306   */
#define OLED_WIDTH         128
#define OLED_HEIGHT        64
#define OLED_PAGES         ( OLED_HEIGHT / 8 )
#define GLYPH_WIDTH        5
#define GLYPH_ADVANCE      6         /* 5 pixel glyph + 1 pixel spacing     */
#define GLYPH_HEIGHT       7
#define I2C_TIMEOUT_MS     100U

/**
 * How long DisplayTask waits for a new sensor sample before it re-checks the
 * page queue (milestone 8).
 *
 * The wait used to be portMAX_DELAY, which was correct while a sample was the
 * only thing that could change the screen.  Now a page turn must also reach
 * the display, so the block is bounded: 50 ms is long enough that DisplayTask
 * sleeps between events (section 19 - no polling loop) and short enough that
 * a turn is on the OLED well inside the "within a second" of FT-04/FT-05.
 * Re-rendering still happens only when something actually changed, so the
 * extra wake-ups cost one queue check and nothing else.
 */
#define DISPLAY_POLL_MS     50U

static I2C_HandleTypeDef hi2c1;
static uint8_t frame_buffer[OLED_WIDTH * OLED_PAGES];
static bool display_ready = false;

/*-----------------------------------------------------------
 * Local helpers
 *----------------------------------------------------------*/
static void ssd1306_commands(const uint8_t *commands, uint8_t length);
static void ssd1306_flush(void);
static void oled_clear(void);
static void oled_pixel(int x, int y);
static void oled_char(int x, int y, char character);
static void oled_text(int x, int y, const char *text);
static void render_page(const SensorData *sample, bool have_sample,
                        DisplayMode mode, bool alarmed);

void display_init(void)
{
    hi2c1.Instance             = I2C1;
    hi2c1.Init.ClockSpeed      = 400000U;
    hi2c1.Init.DutyCycle       = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1     = 0U;
    hi2c1.Init.AddressingMode  = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode   = I2C_NOSTRETCH_DISABLE;

    if( HAL_I2C_Init(&hi2c1) != HAL_OK )
    {
        display_ready = false;
        return;
    }

    /* Init sequence from the SSD1306 application notes: turn the panel off
     * while it is configured, enable the internal charge pump, then address
     * every pixel exactly once. */
    static const uint8_t init_sequence[] =
    {
        0xAE,             /* display off                             */
        0xD5, 0x80,       /* display clock divide                    */
        0xA8, 0x3F,       /* multiplex ratio = 64                    */
        0xD3, 0x00,       /* display offset = 0                      */
        0x40,             /* start line = 0                          */
        0x8D, 0x14,       /* charge pump on                          */
        0x20, 0x00,       /* horizontal addressing mode             */
        0xA1,             /* segment remap                           */
        0xC8,             /* COM scan direction reversed             */
        0xDA, 0x12,       /* COM pins hardware configuration         */
        0x81, 0x7F,       /* contrast                                */
        0xD9, 0xF1,       /* pre-charge period                       */
        0xDB, 0x40,       /* VCOMH deselect level                    */
        0xA4,             /* resume RAM content                      */
        0xA6,             /* normal (not inverted) display           */
        0x21, 0x00, 0x7F, /* column address range 0..127             */
        0x22, 0x00, 0x07, /* page address range 0..7                 */
        0xAF              /* display on                              */
    };

    ssd1306_commands(init_sequence, (uint8_t)sizeof(init_sequence));

    /* Give the panel's charge pump time to stabilise.  Safe here: this runs
     * during hardware initialisation, before the scheduler has started, so
     * HAL_Delay() is legal (see rtos_objects.h). */
    HAL_Delay(100U);

    oled_clear();
    ssd1306_flush();
    display_ready = true;
}

bool display_is_ready(void)
{
    return display_ready;
}

/** HAL callback: I2C1 clock plus the AF open-drain pins PB6/PB7. */
void HAL_I2C_MspInit(I2C_HandleTypeDef *i2c_handle)
{
    GPIO_InitTypeDef gpio = {0};

    if( i2c_handle->Instance != I2C1 )
    {
        return;
    }

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();

    gpio.Pin   = GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode  = GPIO_MODE_AF_OD;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
}

/** Control byte 0x00 = the following bytes are commands. */
static void ssd1306_commands(const uint8_t *commands, uint8_t length)
{
    uint8_t buffer[34];

    if( length + 1U > sizeof(buffer) )
    {
        return;
    }

    buffer[0] = 0x00U;
    memcpy(&buffer[1], commands, length);

    ( void )HAL_I2C_Master_Transmit(&hi2c1, ( uint16_t )( OLED_I2C_ADDRESS << 1 ),
                                    buffer, ( uint16_t )( length + 1U ),
                                    I2C_TIMEOUT_MS);
}

/**
 * Push the whole frame buffer.  Exactly 1024 data bytes are written, which
 * returns the controller's address pointer to (0,0) so the next flush starts
 * from the top-left pixel again.
 */
static void ssd1306_flush(void)
{
    uint8_t buffer[1 + OLED_WIDTH];

    buffer[0] = 0x40U;   /* control byte: the following bytes are data */

    for( uint8_t page = 0; page < OLED_PAGES; ++page )
    {
        memcpy(&buffer[1], &frame_buffer[page * OLED_WIDTH], OLED_WIDTH);

        if( HAL_I2C_Master_Transmit(&hi2c1, ( uint16_t )( OLED_I2C_ADDRESS << 1 ),
                                    buffer, sizeof(buffer),
                                    I2C_TIMEOUT_MS) != HAL_OK )
        {
            return;
        }
    }
}

static void oled_clear(void)
{
    memset(frame_buffer, 0, sizeof(frame_buffer));
}

/** Set one pixel; the frame buffer is column-major, one byte per 8 rows. */
static void oled_pixel(int x, int y)
{
    if( ( x < 0 ) || ( x >= OLED_WIDTH ) || ( y < 0 ) || ( y >= OLED_HEIGHT ) )
    {
        return;
    }

    frame_buffer[x + ( y / 8 ) * OLED_WIDTH] |= ( uint8_t )( 1U << ( y % 8 ) );
}

/** Draw one 5x7 glyph with its top-left corner at (x, y). */
static void oled_char(int x, int y, char character)
{
    uint8_t index = ( uint8_t )character;

    for( uint8_t column = 0; column < GLYPH_WIDTH; ++column )
    {
        uint8_t bits = font[( ( uint16_t )index * GLYPH_WIDTH ) + column];

        for( uint8_t row = 0; row < GLYPH_HEIGHT; ++row )
        {
            if( ( bits & ( 1U << row ) ) != 0U )
            {
                oled_pixel(x + column, y + row);
            }
        }
    }
}

static void oled_text(int x, int y, const char *text)
{
    for( ; *text != '\0'; ++text )
    {
        oled_char(x, y, *text);
        x += GLYPH_ADVANCE;
    }
}

/**
 * Section 27: the screen layout, section 29: which page is drawn.
 *
 *   ROOM MONITOR
 *   ----------------
 *   Temperature
 *   25.40 C
 *
 * The banner and rule are common to every page; only the title and the value
 * line follow DisplayMode.  Until the first sample arrives the value line is a
 * placeholder ("--.- C" and friends), so the display never shows a
 * measurement that was not actually taken.  The Motion page reads
 * SensorData.motionDetected, which SensorTask fills from MotionTask's PIR
 * poll (milestone 10), so it shows "Detected" only while the sensor's OUT
 * pin is actually high.
 *
 * alarmed (section 35's EVENT_ALARM, sampled by DisplayTask) appends a marker
 * to the temperature value so the OLED shows why the buzzer is ringing: the
 * page switches between "%.2f C" and "%.2f C ALARM" as AlarmTask enters and
 * leaves the out-of-range state.  The marker is on the temperature page only
 * because that is the page whose value the decision was made on; the other
 * pages keep their own units untouched.
 */
static void render_page(const SensorData *sample, bool have_sample,
                        DisplayMode mode, bool alarmed)
{
    char line[22];
    const char *title = "Temperature";

    oled_clear();

    oled_text(0, 0, "ROOM MONITOR");

    for( int x = 0; x < OLED_WIDTH; ++x )
    {
        oled_pixel(x, 11);
    }

    switch( mode )
    {
        case DisplayMode::TEMPERATURE:
            title = "Temperature";
            if( have_sample )
            {
                snprintf(line, sizeof(line),
                         alarmed ? "%.2f C ALARM" : "%.2f C",
                         sample->temperature);
            }
            else
            {
                snprintf(line, sizeof(line), "--.- C");
            }
            break;

        case DisplayMode::HUMIDITY:
            title = "Humidity";
            if( have_sample )
            {
                snprintf(line, sizeof(line), "%.2f %%", sample->humidity);
            }
            else
            {
                snprintf(line, sizeof(line), "--.- %%");
            }
            break;

        case DisplayMode::LIGHT:
            title = "Light";
            if( have_sample )
            {
                snprintf(line, sizeof(line), "%d %%", sample->lightLevel);
            }
            else
            {
                snprintf(line, sizeof(line), "-- %%");
            }
            break;

        case DisplayMode::MOTION:
        default:
            title = "Motion";
            if( have_sample )
            {
                snprintf(line, sizeof(line), "%s",
                         sample->motionDetected ? "Detected" : "None");
            }
            else
            {
                snprintf(line, sizeof(line), "--");
            }
            break;
    }

    oled_text(0, 20, title);
    oled_text(0, 36, line);

    ssd1306_flush();
}

/**
 * Sections 26/27/28: the single owner of the OLED.
 *
 * Three sources can change the screen, so the loop handles all three and
 * redraws at most once per pass - a sample and a page turn and an alarm edge
 * that arrive together produce one coherent frame:
 *
 *   - displayQueue, bounded wait (see DISPLAY_POLL_MS), carries sensor data;
 *   - modeQueue, checked with a zero timeout so this call never delays the
 *     sample path, carries the page InputTask selected;
 *   - EVENT_ALARM (section 35), sampled once per pass, marks the temperature
 *     page while it is set, so the marker appears and disappears with
 *     AlarmTask's own transitions even when no new sample has arrived.
 *
 * The INACTIVE reduction (section 34) sits alongside those three sources:
 * while EVENT_ACTIVE is clear the OLED is blanked exactly once and
 * no further frame is drawn, so an empty room costs one I2C clear instead of
 * a redraw every 2 s.  The two queue receives stay unconditional so the
 * length-one queues keep draining and the latest sample and page are waiting
 * in the local variables; the moment EVENT_ACTIVE returns the current frame
 * is re-rendered with both (a reactivated system must come back showing what
 * it was showing, not a cleared screen).
 *
 * With the bounded wait the task still blocks when nothing is happening - it
 * is not a polling loop - and section 19 is satisfied.
 *
 * Priority 1 (sections 38-39): presentation has no deadline in section 7,
 * so this is the task that "can tolerate latency" - a redraw may be late
 * without anything breaking, while a preempted SensorTask or AlarmTask
 * would cost freshness the alarm cannot get back.  Its ~100 ms I2C flush is
 * the largest unprivileged stretch of work in the system, which is exactly
 * why it must sit below the producers.  The full table is on the xTaskCreate
 * cluster in main.cpp.
 */
void DisplayTask(void *argument)
{
    ( void )argument;

    SensorData sample = {};
    DisplayMode mode = DisplayMode::TEMPERATURE;
    bool have_sample = false;
    bool active = ( ( xEventGroupGetBits(systemEvents) & EVENT_ACTIVE ) != 0U );
    bool alarmed = ( ( xEventGroupGetBits(systemEvents) & EVENT_ALARM ) != 0U );

    if( active )
    {
        render_page(&sample, false, mode, alarmed);
    }

    for( ;; )
    {
        bool redraw = false;
        DisplayMode published = mode;
        bool now_active, now_alarmed;

        if( xQueueReceive(displayQueue, &sample,
                          pdMS_TO_TICKS(DISPLAY_POLL_MS)) == pdTRUE )
        {
            have_sample = true;
            redraw = true;
        }

        if( xQueueReceive(modeQueue, &published, 0) == pdTRUE )
        {
            if( published != mode )
            {
                mode = published;
                redraw = true;
            }
        }

        now_alarmed = ( ( xEventGroupGetBits(systemEvents) & EVENT_ALARM ) != 0U );
        if( now_alarmed != alarmed )
        {
            alarmed = now_alarmed;
            redraw = true;
        }

        now_active = ( ( xEventGroupGetBits(systemEvents) & EVENT_ACTIVE ) != 0U );
        if( now_active != active )
        {
            active = now_active;

            if( active )
            {
                /* Reactivation: restore the frame that was pending when the
                 * system went quiet (section 34). */
                redraw = true;
            }
            else
            {
                /* Transition to INACTIVE: one blank, then nothing (section
                 * 34).  Any redraw the queues queued above is dropped - the
                 * next ACTIVE transition forces a fresh one instead. */
                oled_clear();
                ssd1306_flush();
                redraw = false;
            }
        }

        if( active && redraw )
        {
            render_page(&sample, have_sample, mode, alarmed);
        }
    }
}

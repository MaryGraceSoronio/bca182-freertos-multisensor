/**
 * input.cpp - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * Rotary-encoder driver and InputTask (section 28: "Read the rotary encoder
 * and change the displayed page").
 *
 * How the KY-040 is read
 * ----------------------
 * The encoder is sampled as ordinary GPIO - no EXTI, no timer encoder mode -
 * because section 41 keeps InputTask a normal FreeRTOS task and section 19
 * wants every subsystem behind its own task rather than ISR logic.
 *
 * Wokwi models the KY-040 like this (docs.wokwi.com/parts/wokwi-ky-040 and
 * the KY-040 timing capture published there):
 *
 *   - both lines idle high (the board carries pull-ups, and the MCU input is
 *     configured with the internal pull-up as well),
 *   - one detent produces a low pulse on each line per detent, and
 *   - the two pulses are offset by a few milliseconds - one line leads, the
 *     other follows - so the direction is the *order* of the edges, not their
 *     number.
 *
 * The decode rule is the standard one from Wokwi's own Arduino example: look
 * at CLK's falling edge and sample DT.  DT high at that moment = clockwise,
 * DT low = counter-clockwise.  A rising edge carries no information and is
 * ignored, and if CLK and DT change in the same sample the direction is
 * ambiguous, so that sample is dropped instead of guessed.
 *
 * Sample rate (documented deviation)
 * ----------------------------------
 * The suggested period was INPUT_POLL_PERIOD_MS 10 (100 Hz).  Wokwi documents
 * the KY-040's two pulses as only "a few milliseconds" wide and offset from
 * each other by a couple of milliseconds, so at 100 Hz a sample can land
 * outside the CLK low pulse entirely - the detent is missed - or land after
 * the DT line has already moved, in which case the direction read is wrong.
 * The period has to beat the pulse width, not the detent period.
 *
 * 1 kHz (1 ms) is the obvious arithmetic answer and was measured first: it
 * stalls the Wokwi simulator.  With a 1 ms period the same firmware produced
 * less than one second of output in a 45 s run, while 2 ms and 10 ms produced
 * a complete run in the same 45 s.  2 ms (500 Hz) is therefore the fastest
 * period that keeps the simulation real-time, and it still puts at least two
 * samples inside every pulse of the documented waveform, which is exactly what
 * the two-consecutive-samples debounce needs before it trusts a level.
 *
 * The delay remains a blocking vTaskDelay(), so section 19 is respected: the
 * task does finite work and then sleeps, and at 500 wake-ups per second the
 * cost is well under a tenth of one percent of the CPU.
 *
 * Pin map (see include/input.h):
 *   PB12 - CLK   PB13 - DT   PB14 - SW
 */

#include "input.h"

#include <stdio.h>

#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"

#include "rtos_objects.h"   /* modeQueue, serial_write(), systemEvents */

/*-----------------------------------------------------------
 * Pin assignment and tuning constants
 *----------------------------------------------------------*/

#define ENCODER_CLK_PORT  GPIOB
#define ENCODER_CLK_PIN   GPIO_PIN_12

#define ENCODER_DT_PORT   GPIOB
#define ENCODER_DT_PIN    GPIO_PIN_13

#define ENCODER_SW_PORT   GPIOB
#define ENCODER_SW_PIN    GPIO_PIN_14

/** Poll period.  See the sample-rate note in the file header: 2 ms is a
 *  measured deviation from the suggested 10 ms (1 ms stalls the simulator). */
#define INPUT_POLL_PERIOD_MS  2U

/** A pin must read the same level on this many consecutive samples before the
 *  level is considered settled - the section 28 debounce. */
#define INPUT_STABLE_SAMPLES  2U

/*-----------------------------------------------------------
 * GPIO bring-up
 *----------------------------------------------------------*/

/**
 * PB12/PB13/PB14 as inputs with the internal pull-up.
 *
 * STM32F1 HAL has no GPIO_MODE_INPUT_PULLUP - on this family mode and pull
 * are separate fields - so GPIO_MODE_INPUT + GPIO_PULLUP is the pull-up input
 * configuration.  SWJ debug pins live on PA13/PA14/PA15/PB3/PB4, so none of
 * these three lines needs an AFIO remap or a JTAG disable, and none is done.
 */
void input_init( void )
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin   = ENCODER_CLK_PIN | ENCODER_DT_PIN | ENCODER_SW_PIN;
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    HAL_GPIO_Init(ENCODER_CLK_PORT, &gpio);
}

static bool encoder_pin_high( GPIO_TypeDef *port, uint16_t pin )
{
    return ( HAL_GPIO_ReadPin(port, pin) == GPIO_PIN_SET );
}

/*-----------------------------------------------------------
 * Page navigation (section 29)
 *----------------------------------------------------------*/

DisplayMode nextDisplayMode( DisplayMode mode )
{
    switch( mode )
    {
        case DisplayMode::TEMPERATURE: return DisplayMode::HUMIDITY;
        case DisplayMode::HUMIDITY:    return DisplayMode::LIGHT;
        case DisplayMode::LIGHT:       return DisplayMode::MOTION;
        case DisplayMode::MOTION:      return DisplayMode::TEMPERATURE;
    }

    /* Unreachable while the enum has four members; kept so a compiler that
     * does not treat the switch as exhaustive stays quiet. */
    return DisplayMode::TEMPERATURE;
}

DisplayMode previousDisplayMode( DisplayMode mode )
{
    switch( mode )
    {
        case DisplayMode::TEMPERATURE: return DisplayMode::MOTION;
        case DisplayMode::HUMIDITY:    return DisplayMode::TEMPERATURE;
        case DisplayMode::LIGHT:       return DisplayMode::HUMIDITY;
        case DisplayMode::MOTION:      return DisplayMode::LIGHT;
    }

    return DisplayMode::TEMPERATURE;
}

/** Human-readable page name for the section 28 serial report. */
static const char *page_name( DisplayMode mode )
{
    switch( mode )
    {
        case DisplayMode::TEMPERATURE: return "Temperature";
        case DisplayMode::HUMIDITY:    return "Humidity";
        case DisplayMode::LIGHT:       return "Light";
        case DisplayMode::MOTION:      return "Motion";
    }

    return "Temperature";
}

/**
 * Publish the new page: the display is told first (it is the consumer of the
 * page, section 26), then the turn is reported on the serial console so it
 * can be checked from the log.
 *
 * `mode` is const: publish_page() reads the page and hands it to the queue,
 * it never rewrites it.  Declaring that here is what cppcheck's
 * constParameterPointer asks for (section 45), and it documents the contract
 * for the caller as well.
 */
static void publish_page( const DisplayMode *mode )
{
    char line[24];

    ( void ) xQueueOverwrite(modeQueue, mode);

    snprintf(line, sizeof(line), "Page: %s\r\n", page_name(*mode));
    serial_write(line);
}

/*-----------------------------------------------------------
 * InputTask - section 28
 *----------------------------------------------------------*/

/**
 * Decode the rotary encoder and select the displayed page.
 *
 * Sections 28-29: clockwise walks Temperature -> Humidity -> Light -> Motion
 * and wraps, counter-clockwise walks back and wraps the other way.  Only the
 * page changes here - the OLED, the I2C bus and the sensor samples are not
 * touched, so DisplayTask remains the single owner of the display (section
 * 26).
 *
 * Priority 3 (sections 38-39): joint highest in the system with MotionTask
 * and StateTask, because a human turn of the knob is a one-shot event - if
 * this task were queued behind SensorTask's two-second cycle a page change
 * would appear late or not at all.
 * The cost of that priority is bounded by the design above: two GPIO reads, a
 * couple of comparisons and one 2 ms blocking delay per iteration, so the task
 * has nothing to do between turns and cannot starve the sensor or display.
 */
void InputTask( void *argument )
{
    DisplayMode mode = DisplayMode::TEMPERATURE;

    bool candidate_clk, candidate_dt;
    bool stable_clk, stable_dt;
    uint8_t clk_samples, dt_samples;

    ( void ) argument;

    /* Prime both filters from the real pin levels.  Without this a line that
     * happens to read low at start-up would look like the first detent and
     * move the page before anyone touched the knob. */
    candidate_clk = stable_clk = encoder_pin_high(ENCODER_CLK_PORT, ENCODER_CLK_PIN);
    candidate_dt  = stable_dt  = encoder_pin_high(ENCODER_DT_PORT,  ENCODER_DT_PIN);
    clk_samples   = INPUT_STABLE_SAMPLES;
    dt_samples    = INPUT_STABLE_SAMPLES;

    /* Start on the temperature page (sections 27-28) and make sure the
     * display has the page before the first loop turn. */
    publish_page(&mode);

    for( ;; )
    {
        bool previous_clk = stable_clk;
        bool previous_dt  = stable_dt;
        bool clk_fell, dt_moved;

        bool raw_clk = encoder_pin_high(ENCODER_CLK_PORT, ENCODER_CLK_PIN);
        bool raw_dt  = encoder_pin_high(ENCODER_DT_PORT,  ENCODER_DT_PIN);

        /* Independent two-sample stability filter per line.  Restarting the
         * counter on any level change is what makes contact bounce (on real
         * hardware) and millisecond-scale skew (in Wokwi) harmless: a level
         * that does not hold still for INPUT_STABLE_SAMPLES reads never
         * reaches stable_clk/stable_dt. */
        if( raw_clk != candidate_clk )
        {
            candidate_clk = raw_clk;
            clk_samples   = 1U;
        }
        else if( clk_samples < INPUT_STABLE_SAMPLES )
        {
            clk_samples++;
        }

        if( raw_dt != candidate_dt )
        {
            candidate_dt = raw_dt;
            dt_samples   = 1U;
        }
        else if( dt_samples < INPUT_STABLE_SAMPLES )
        {
            dt_samples++;
        }

        if( clk_samples >= INPUT_STABLE_SAMPLES )
        {
            stable_clk = candidate_clk;
        }

        if( dt_samples >= INPUT_STABLE_SAMPLES )
        {
            stable_dt = candidate_dt;
        }

        clk_fell  = ( previous_clk && ( !stable_clk ) );
        dt_moved  = ( previous_dt != stable_dt );

        /* One detent = one CLK falling edge, so a single turn can never
         * advance twice.  The falling edge is the only edge that carries the
         * direction (rising edges repeat the same state), and a sample where
         * both lines moved is ambiguous - dropped rather than guessed, so a
         * fast spin cannot invent an extra page.
         *
         * Section 33: while EVENT_ACTIVE is clear (section 34's INACTIVE)
         * the detent is consumed but ignored - the page cannot change and
         * nothing is published, so no phantom turn is waiting for someone
         * when the system comes back.  The filters and edge detection above
         * keep running throughout: dropping them would freeze the two
         * stability levels mid-transition and let one stale CLK low level
         * look like the first detent after reactivation. */
        if( clk_fell && ( !dt_moved ) &&
            ( ( xEventGroupGetBits(systemEvents) & EVENT_ACTIVE ) != 0U ) )
        {
            mode = stable_dt ? nextDisplayMode(mode)
                             : previousDisplayMode(mode);
            publish_page(&mode);
        }

        vTaskDelay(pdMS_TO_TICKS(INPUT_POLL_PERIOD_MS));
    }
}



/**
 * sensors.cpp - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * DHT22 single-wire driver, LDR/ADC sampling and SensorTask.
 *
 * Timing source
 * -------------
 * The DHT22 protocol needs microsecond resolution (a data bit is 26-28 us
 * for '0' and about 70 us for '1').  HAL_GetTick() only offers 1 ms, so the
 * driver uses the ARM DWT cycle counter, which Wokwi simulates for the
 * STM32F103.  The number of cycles per microsecond is derived from
 * SystemCoreClock, so the driver keeps working if the clock configuration
 * falls back to the 8 MHz HSI.
 *
 * No Arduino library is used - section 6 prohibits the Arduino framework, so
 * the protocol is implemented from the datasheet timing.
 */

#include "sensors.h"

#include <stdio.h>

#include "stm32f1xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"

#include "rtos_objects.h"
#include "motion.h"         /* motion_detected() - the PIR level */

/*-----------------------------------------------------------
 * Pin assignment
 *----------------------------------------------------------*/
#define LDR_ADC_CHANNEL       ADC_CHANNEL_0   /* PA0 = ADC1_IN0 */
#define LDR_TIMEOUT_MS        10U
#define LDR_FULL_SCALE        4095U           /* 12 bit, right aligned */

#define DHT22_GPIO_PORT   GPIOA
#define DHT22_GPIO_PIN    GPIO_PIN_1
#define DHT22_CYCLES_PER_US  ( SystemCoreClock / 1000000U )

/* Protocol timing (values from the DHT22 datasheet).  The datasheet's phase
 * lengths are shown in the comments; the timeouts are deliberately several
 * times longer, because a timeout only bounds how long dht_wait_level() may
 * look for a transition that never arrives - it never delays a transition
 * that does.  The margin also absorbs the SysTick/PendSV bursts that run
 * while the scheduler is suspended for the duration of the frame. */
#define DHT22_START_LOW_US   10000U  /* start signal: datasheet asks for
                                      * >= 1 ms; 10 ms is used so every
                                      * read succeeds in simulation     */
#define DHT22_START_RELEASE_US   30U  /* release before sensor answers */
#define DHT22_RESPONSE_US      300U  /* around the 80 us phases       */
#define DHT22_PREAMBLE_US      200U  /* around the 50 us low          */
#define DHT22_BIT_THRESHOLD_US  40U  /* midpoint between 28 us and 70 us */

/*-----------------------------------------------------------
 * Local helpers
 *----------------------------------------------------------*/
static void dwt_init(void);
static void dht_delay_us(uint32_t microseconds);
static int  dht_wait_level(GPIO_PinState level, uint32_t timeout_us);
static void dht22_gpio_init(void);
static void dht_pin_output(void);
static void dht_pin_input(void);
static void ldr_adc_init(void);

static ADC_HandleTypeDef hadc1;
static bool ldr_ready = false;

void sensors_init(void)
{
    dwt_init();
    dht22_gpio_init();
    ldr_adc_init();
}

/** PA1 as open-drain with the internal pull-up; the 10 k resistor in the
 *  diagram provides the strong pull-up the single-wire bus needs. */
static void dht22_gpio_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();

    dht_pin_output();

    /* Release the line (open drain driving '1' = high impedance). */
    HAL_GPIO_WritePin(DHT22_GPIO_PORT, DHT22_GPIO_PIN, GPIO_PIN_SET);
}

/** Drive the single-wire bus: only the host's start signal ever needs this
 *  end of the line actively pulled low. */
static void dht_pin_output(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin   = DHT22_GPIO_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT22_GPIO_PORT, &gpio);
}

/** Release the bus so the sensor owns it.  The line is an input for the
 *  whole answer and frame: a GPIO left configured as an output keeps the bus
 *  under MCU control, and the sensor then cannot pull it down - which reads
 *  as a missing response. */
static void dht_pin_input(void)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin   = DHT22_GPIO_PIN;
    gpio.Mode  = GPIO_MODE_INPUT;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(DHT22_GPIO_PORT, &gpio);
}

/**
 * ADC bring-up for the photoresistor module.
 *
 * The factory-start-up calibration (HAL_ADCEx_Calibration_Start) is
 * deliberately not called: it waits for a calibration-done flag with no
 * timeout parameter, and Wokwi only implements "ADC1 basic conversion".  A
 * simulated converter already reports an ideal result, so the calibration
 * would buy nothing on the target of this laboratory activity and could
 * stall startup forever.  On real hardware it should be re-enabled.
 */
static void ldr_adc_init(void)
{
    ADC_ChannelConfTypeDef channel = {0};

    hadc1.Instance                  = ADC1;
    hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
    hadc1.Init.ScanConvMode          = ADC_SCAN_DISABLE;
    hadc1.Init.ContinuousConvMode    = DISABLE;
    hadc1.Init.NbrOfConversion       = 1;
    hadc1.Init.DiscontinuousConvMode = DISABLE;
    hadc1.Init.NbrOfDiscConversion   = 1;
    hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;

    if( HAL_ADC_Init(&hadc1) != HAL_OK )
    {
        ldr_ready = false;
        return;
    }

    channel.Channel      = LDR_ADC_CHANNEL;
    channel.Rank         = ADC_REGULAR_RANK_1;
    channel.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;

    if( HAL_ADC_ConfigChannel(&hadc1, &channel) != HAL_OK )
    {
        ldr_ready = false;
        return;
    }

    ldr_ready = true;
}

/**
 * HAL callback: clock the ADC and put PA0 in analog mode.
 *
 * The ADC clock comes from PCLK2 divided by 6, which keeps it at 12 MHz for
 * the 72 MHz system clock and inside the 14 MHz limit of the reference
 * manual.  Section 23 also rules out DMA, so no DMA channel is requested
 * here.
 */
void HAL_ADC_MspInit(ADC_HandleTypeDef *adc_handle)
{
    RCC_PeriphCLKInitTypeDef periph_clock = {0};
    GPIO_InitTypeDef gpio = {0};

    if( adc_handle->Instance != ADC1 )
    {
        return;
    }

    __HAL_RCC_ADC1_CLK_ENABLE();

    periph_clock.PeriphClockSelection = RCC_PERIPHCLK_ADC;
    periph_clock.AdcClockSelection     = RCC_ADCPCLK2_DIV6;
    ( void )HAL_RCCEx_PeriphCLKConfig(&periph_clock);

    gpio.Pin   = GPIO_PIN_0;
    gpio.Mode  = GPIO_MODE_ANALOG;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &gpio);
}

bool ldr_read_raw(uint16_t *raw_value)
{
    if( ( !ldr_ready ) || ( raw_value == nullptr ) )
    {
        return false;
    }

    if( HAL_ADC_Start(&hadc1) != HAL_OK )
    {
        return false;
    }

    bool converted = ( HAL_ADC_PollForConversion(&hadc1, LDR_TIMEOUT_MS) == HAL_OK );

    if( converted )
    {
        *raw_value = ( uint16_t )HAL_ADC_GetValue(&hadc1);
    }

    ( void )HAL_ADC_Stop(&hadc1);

    return converted;
}

uint8_t ldr_to_light_percent(uint16_t raw_value)
{
    if( raw_value > LDR_FULL_SCALE )
    {
        raw_value = ( uint16_t )LDR_FULL_SCALE;
    }

    /* Inverted on purpose: a low reading means a bright scene.  The
     * +2047 term rounds to nearest instead of truncating. */
    uint32_t scaled = ( ( uint32_t )( LDR_FULL_SCALE - raw_value ) * 100U + 2047U ) /
                      LDR_FULL_SCALE;

    return ( uint8_t )scaled;
}

static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

static void dht_delay_us(uint32_t microseconds)
{
    uint32_t start  = DWT->CYCCNT;
    uint32_t cycles = microseconds * DHT22_CYCLES_PER_US;

    while( ( DWT->CYCCNT - start ) < cycles )
    {
    }
}

/** Wait until the data line reaches @p level, or give up after @p timeout_us.
 *  @return 0 on success, -1 on timeout. */
static int dht_wait_level(GPIO_PinState level, uint32_t timeout_us)
{
    uint32_t start  = DWT->CYCCNT;
    uint32_t cycles = timeout_us * DHT22_CYCLES_PER_US;

    while( HAL_GPIO_ReadPin(DHT22_GPIO_PORT, DHT22_GPIO_PIN) != level )
    {
        if( ( DWT->CYCCNT - start ) >= cycles )
        {
            return -1;
        }
    }

    return 0;
}

bool dht22_read(float *temperature_c, float *humidity_percent)
{
    uint8_t data[5] = {0, 0, 0, 0, 0};
    bool frame_ok = false;

    /* Host start signal.  Holding the line low does not depend on
     * uninterrupted execution - the output register keeps the level while
     * the task is preempted - so the 10 ms pulse runs with the scheduler
     * still running and only the answer, which is time-critical, is taken
     * under the suspension window below. */
    dht_pin_output();
    HAL_GPIO_WritePin(DHT22_GPIO_PORT, DHT22_GPIO_PIN, GPIO_PIN_RESET);
    dht_delay_us(DHT22_START_LOW_US);

    /* One transaction = one suspension window.  See sensors.h for why. */
    vTaskSuspendAll();
    {
        /* Release: ODR = 1 first (otherwise the input pull would be a
         * pull-down), then let the sensor own the line, then wait out the
         * datasheet's 20-40 us before it answers. */
        HAL_GPIO_WritePin(DHT22_GPIO_PORT, DHT22_GPIO_PIN, GPIO_PIN_SET);
        dht_pin_input();
        dht_delay_us(DHT22_START_RELEASE_US);

        /* Sensor answers with 80 us low, then 80 us high, then bit 0. */
        if( dht_wait_level(GPIO_PIN_RESET, DHT22_RESPONSE_US) == 0 &&
            dht_wait_level(GPIO_PIN_SET,  DHT22_RESPONSE_US) == 0 &&
            dht_wait_level(GPIO_PIN_RESET, DHT22_RESPONSE_US) == 0 )
        {
            frame_ok = true;

            for( int bit = 0; bit < 40 && frame_ok; ++bit )
            {
                /* Every bit starts with a 50 us low; measure the high part. */
                if( dht_wait_level(GPIO_PIN_SET, DHT22_PREAMBLE_US) != 0 )
                {
                    frame_ok = false;
                    break;
                }

                uint32_t high_start = DWT->CYCCNT;

                if( dht_wait_level(GPIO_PIN_RESET, DHT22_RESPONSE_US) != 0 )
                {
                    frame_ok = false;
                    break;
                }

                uint32_t high_cycles = DWT->CYCCNT - high_start;

                if( high_cycles > ( DHT22_BIT_THRESHOLD_US * DHT22_CYCLES_PER_US ) )
                {
                    data[bit >> 3] |= ( uint8_t )( 1U << ( 7 - ( bit & 7 ) ) );
                }
            }
        }
    }
    ( void )xTaskResumeAll();

    if( !frame_ok )
    {
        return false;
    }

    /* Bytes 0..3 are humidity-high, humidity-low, temperature-high,
     * temperature-low; byte 4 is the sum of the first four. */
    uint8_t checksum = ( uint8_t )( data[0] + data[1] + data[2] + data[3] );

    if( checksum != data[4] )
    {
        return false;
    }

    uint16_t raw_humidity    = ( ( uint16_t )data[0] << 8 ) | data[1];
    uint16_t raw_temperature = ( ( uint16_t )( data[2] & 0x7FU ) << 8 ) | data[3];

    float temperature = ( float )raw_temperature / 10.0F;

    /* Bit 15 of the temperature word is the sign bit. */
    if( ( data[2] & 0x80U ) != 0U )
    {
        temperature = -temperature;
    }

    if( humidity_percent != nullptr )
    {
        *humidity_percent = ( float )raw_humidity / 10.0F;
    }

    if( temperature_c != nullptr )
    {
        *temperature_c = temperature;
    }

    return true;
}

/**
 * Section 22 - the periodic producer of the system.
 *
 * vTaskDelayUntil() rather than vTaskDelay(): the wake-up instants are
 * derived from one fixed base (last_wake), so every period is exactly
 * 2000 ms in kernel time and the sampling instants do not drift, even when
 * an individual read takes longer than usual.  vTaskDelay() would restart
 * the 2000 ms count from the moment the work finished and the period would
 * slowly stretch.
 *
 * Section 25 - each sample is written to displayQueue and alarmQueue with
 * xQueueOverwrite(), so the two consumers both see every reading (the
 * deviation from the single-queue diagram is documented in rtos_objects.h).
 *
 * Section 19 - the loop performs finite work and then blocks, so it never
 * monopolises the CPU.
 */
void SensorTask(void *argument)
{
    ( void )argument;

    TickType_t last_wake_time = xTaskGetTickCount();
    SensorData sample = {};
    uint16_t   raw_light = 0U;
    char       line[48];

    for( ;; )
    {
        if( dht22_read(&sample.temperature, &sample.humidity) )
        {
            snprintf(line, sizeof(line), "Temperature: %.2f C\r\n", sample.temperature);
            serial_write(line);

            snprintf(line, sizeof(line), "Humidity: %.2f %%\r\n", sample.humidity);
            serial_write(line);
        }
        else
        {
            serial_write("DHT22 read failed\r\n");
        }

        if( ldr_read_raw(&raw_light) )
        {
            sample.lightLevel = ldr_to_light_percent(raw_light);

            snprintf(line, sizeof(line), "Light level: %d %%\r\n", sample.lightLevel);
            serial_write(line);
        }
        else
        {
            serial_write("LDR read failed\r\n");
        }

        /* Section 31 - MotionTask owns the PIR line; fold its latest level
         * into the sample so DisplayTask and AlarmTask receive one
         * self-contained message (sensors.h documents the field). */
        sample.motionDetected = motion_detected();

        ( void )xQueueOverwrite(displayQueue, &sample);
        ( void )xQueueOverwrite(alarmQueue, &sample);

        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(SENSOR_SAMPLE_PERIOD_MS));
    }
}

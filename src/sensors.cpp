/**
 * sensors.cpp - BCA182 Laboratory Activity 1
 * Real-Time Multisensor Room Monitoring System
 *
 * DHT22 single-wire driver + SensorTask.
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

/*-----------------------------------------------------------
 * Pin assignment
 *----------------------------------------------------------*/
#define DHT22_GPIO_PORT   GPIOA
#define DHT22_GPIO_PIN    GPIO_PIN_1
#define DHT22_CYCLES_PER_US  ( SystemCoreClock / 1000000U )

/* Protocol timing (values from the DHT22 datasheet). */
#define DHT22_START_LOW_US    1200U  /* host start signal, >= 1 ms  */
#define DHT22_START_RELEASE_US   30U  /* release before sensor answers */
#define DHT22_RESPONSE_US      100U  /* slack around the 80 us phases */
#define DHT22_PREAMBLE_US       70U  /* slack around the 50 us low    */
#define DHT22_BIT_THRESHOLD_US  40U  /* midpoint between 28 us and 70 us */

/*-----------------------------------------------------------
 * Local helpers
 *----------------------------------------------------------*/
static void dwt_init(void);
static void dht_delay_us(uint32_t microseconds);
static int  dht_wait_level(GPIO_PinState level, uint32_t timeout_us);
static void dht22_gpio_init(void);

void sensors_init(void)
{
    dwt_init();
    dht22_gpio_init();
}

/** PA1 as open-drain with the internal pull-up; the 10 k resistor in the
 *  diagram provides the strong pull-up the single-wire bus needs. */
static void dht22_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin   = DHT22_GPIO_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_OD;
    gpio.Pull  = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT22_GPIO_PORT, &gpio);

    /* Release the line (open drain driving '1' = high impedance). */
    HAL_GPIO_WritePin(DHT22_GPIO_PORT, DHT22_GPIO_PIN, GPIO_PIN_SET);
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

    /* One transaction = one suspension window.  See sensors.h for why. */
    vTaskSuspendAll();
    {
        HAL_GPIO_WritePin(DHT22_GPIO_PORT, DHT22_GPIO_PIN, GPIO_PIN_RESET);
        dht_delay_us(DHT22_START_LOW_US);

        HAL_GPIO_WritePin(DHT22_GPIO_PORT, DHT22_GPIO_PIN, GPIO_PIN_SET);
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
 * Section 20 / section 22: sample the DHT22 and report the result over the
 * serial monitor before anything else in the system is wired to it.
 *
 * Section 19 - the loop performs finite work and then blocks for two
 * seconds, so it never monopolises the CPU.  Milestone 6 replaces the delay
 * with vTaskDelayUntil() and publishes the reading to the sensor queues.
 */
void SensorTask(void *argument)
{
    ( void )argument;

    float temperature = 0.0F;
    float humidity    = 0.0F;
    char  line[48];

    for( ;; )
    {
        if( dht22_read(&temperature, &humidity) )
        {
            snprintf(line, sizeof(line), "Temperature: %.2f C\r\n", temperature);
            serial_write(line);

            snprintf(line, sizeof(line), "Humidity: %.2f %%\r\n", humidity);
            serial_write(line);
        }
        else
        {
            serial_write("DHT22 read failed\r\n");
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

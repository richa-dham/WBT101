/* Blink an LED with a frequency of 2 Hz */

#include "sparcommon.h"
#include "wiced_transport.h"
#include "wiced_hal_platform.h"
#include "wiced_bt_dev.h"
#include "wiced_timer.h"

//GJL TEMP
#define WICED_LED1 WICED_P26

/*****************************    Constants   *****************************/
/* Timer will expire every 250ms so that the LED frequency will be 500ms = 2 Hz */
#define TIMEOUT_IN_MS (250)

/*****************************    Variables   *****************************/
/* HCI interface configuration */
const wiced_transport_cfg_t  transport_cfg =
{
    WICED_TRANSPORT_UART,
    { WICED_TRANSPORT_UART_HCI_MODE, 115200 },
    { 0, 0},
    NULL,
    NULL,
    NULL
};

wiced_timer_t ms_timer;               	/* timer structure */

/*****************************    Function Prototypes   *******************/
wiced_result_t bt_cback( wiced_bt_management_evt_t event, wiced_bt_management_evt_data_t *p_event_data);
void timer_cback( uint32_t arg );

/*****************************    Functions   *****************************/
/*  Main application. This just starts the BT stack and provides the callback function.
 *  The actual application initialization will happen when stack reports that BT device is ready. */
APPLICATION_START( )
{
    wiced_transport_init( &transport_cfg ); /* Initialize HCI interface */
	wiced_bt_stack_init( bt_cback, NULL, NULL ); /* Register BT stack callback */
}


/* Callback function for BlueTooth events */
wiced_result_t bt_cback( wiced_bt_management_evt_t event, wiced_bt_management_evt_data_t *p_event_data)
{
    wiced_result_t result = WICED_SUCCESS;

    switch( event )
    {
        /* BlueTooth  stack enabled */
        case BTM_ENABLED_EVT:
            /* Initialize WICED functions */
            wiced_bt_app_init();

            /* Configure GPIO P2 to drive high so that the reset to the shield is not pulled down */
            wiced_hal_gpio_configure_pin( WICED_P02, GPIO_OUTPUT_ENABLE, GPIO_PIN_OUTPUT_HIGH);

            /* Start a timer to control blinking */
            if ( wiced_init_timer(&ms_timer, &timer_cback, 0, WICED_MILLI_SECONDS_PERIODIC_TIMER ) == WICED_SUCCESS )
			{
				wiced_start_timer( &ms_timer, TIMEOUT_IN_MS );
			}
            break;
        default:
            break;
    }
    return result;
}


/* The function invoked on timeout of the timer. */
void timer_cback( uint32_t arg )
{
	/* Read current set value for the LED pin and invert it */
	wiced_hal_gpio_set_pin_output( WICED_LED1, !wiced_hal_gpio_get_pin_output(WICED_LED1));
}


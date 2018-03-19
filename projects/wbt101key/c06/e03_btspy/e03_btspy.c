/* Setup the BLE to connect and provide a custom service:
 *   CapSense  (custom profile - UUID )
 *
 * The CapSense data is read from the shield board over I2C every 100ms.
 *
 * Notifications are sent whenever the data changes if the CCCD is set for the service.
 * */

#include "wiced.h"
#include "wiced_platform.h"
#include "sparcommon.h"
#include "wiced_bt_dev.h"
#include "wiced_bt_cfg.h"
#include "wiced_bt_trace.h"
#include "wiced_bt_ble.h"
#include "wiced_bt_gatt.h"
#include "wiced_bt_uuid.h"
#include "wiced_bt_trace.h"
#include "wiced_timer.h"
#include "wiced_hal_i2c.h"
#include "wiced_transport.h"
// Project specific include file
#include "ble_gatt.h"

/*****************************    Constants   *****************************/
/* Timer for reading I2C data from the PSoC */
#define TIMEOUT_IN_MS (100)

#define I2C_ADDRESS  (0x42)
/* Offset for the register that holds CapSense button values */
#define BUTTON_REG  (0x06)
/* Number of buttons - we only use 3 instead of 4 because the phone app only supports 3 buttons */
#define NUM_BUTTONS (3)
/* Mask for the button values from the I2C register (lower 3 bits of the button register) */
#define BUTTON_MASK (0x07)
/* The characteristic we send is a 3 byte array: number of buttons, mask (unused), button data */
#define BUTTON_CHAR_SIZE (3)
#define BUTTON_HEADER_POS (0)
#define BUTTON_MASK_POS (1)
#define BUTTON_VALUE_POS (2)

#ifdef ENABLE_HCI_TRACE
const wiced_transport_cfg_t transport_cfg =
{
    WICED_TRANSPORT_UART,
    { WICED_TRANSPORT_UART_HCI_MODE, HCI_UART_DEFAULT_BAUD},
    { 0, 0},
    NULL,
    NULL,
    NULL
};
#endif


extern const wiced_bt_cfg_settings_t wiced_bt_cfg_settings;
extern const wiced_bt_cfg_buf_pool_t wiced_bt_cfg_buf_pools[];

/*****************************    Variables   *****************************/
/* Bluetooth variables */
uint16_t  connection_id;    // connection ID referenced by the stack

uint8_t device_name[]          = WICED_DEVICE_NAME;
uint8_t appearance_name[2]     = { BIT16_TO_8(WICED_DEVICE_APPEARANCE) };
char    char_mfr_name_value[]  = { "Cypress" };
char    char_model_num_value[] = { "1234" };
uint8_t char_system_id_value[] = { 0x12, 0x34, 0x56, 0xff, 0xee, 0x9a, 0xbc, 0xde};

/* Data variables and notification flags */
/* CapSense characteristic */
uint8_t capSenseVal[BUTTON_CHAR_SIZE] =        {NUM_BUTTONS, 0};
uint8_t capSenseValPrev[BUTTON_CHAR_SIZE] =    {NUM_BUTTONS, 0};
uint16_t capSenseCCCD = 0;    // Start with notifications/indications off

/*
 * This is the GATT database for the application.  It defines services,
 * characteristics and descriptors. Each attribute in the database has
 * a handle (characteristic has two, one for the characteristic itself,
 * another for the value).  The handles are used by the peer to access
 * attributes, and can be used locally by the application - for example,
 * to retrieve data written by the peer.  Definition of characteristics
 * and descriptors has GATT Properties (read, write, notify...) but also
 * has permissions which identify if and how peer is allowed to read or
 * write into it.  All handles do not need to be sequential, but need to
 * be in ascending order.
 */
const uint8_t gatt_database[]=
{
    // Declare mandatory GATT service
    PRIMARY_SERVICE_UUID16( HANDLE_GATT_SERVICE, UUID_SERVICE_GATT ),

    // Declare mandatory GAP service. Device Name and Appearance are mandatory
    // characteristics of GAP service
    PRIMARY_SERVICE_UUID16( HANDLE_GAP_SERVICE, UUID_SERVICE_GAP ),

        // Declare mandatory GAP service characteristic: Dev Name
        CHARACTERISTIC_UUID16( HANDLE_GAP_SERVICE_CHAR_DEV_NAME, HANDLE_GAP_SERVICE_CHAR_DEV_NAME_VAL,
            UUID_CHARACTERISTIC_DEVICE_NAME, LEGATTDB_CHAR_PROP_READ, LEGATTDB_PERM_READABLE ),

        // Declare mandatory GAP service characteristic: Appearance
        CHARACTERISTIC_UUID16( HANDLE_GAP_SERVICE_CHAR_DEV_APPEARANCE, HANDLE_GAP_SERVICE_CHAR_DEV_APPEARANCE_VAL,
            UUID_CHARACTERISTIC_APPEARANCE, LEGATTDB_CHAR_PROP_READ, LEGATTDB_PERM_READABLE ),

    // Declare proprietary CapSense Service with 128 bit UUID
    PRIMARY_SERVICE_UUID128( HANDLE_CAPSENSE_SERVICE, UUID_CAPSENSE_SERVICE ),

        // Declare characteristic used to notify a change
        CHARACTERISTIC_UUID128( HANDLE_CAPSENSE_SERVICE_CHAR_BUTTONS, HANDLE_CAPSENSE_SERVICE_CHAR_BUTTONS_VAL,
            UUID_CAPSENSE_CHARACTERISTIC, LEGATTDB_CHAR_PROP_READ | LEGATTDB_CHAR_PROP_NOTIFY, LEGATTDB_PERM_READABLE ),

            // Declare client characteristic configuration descriptor.
            // Value of the descriptor can be modified by the client
            // Value modified shall be retained during connection and across connection
            // for bonded devices.  Setting value to 1 tells this application to send notification
            // when value of the characteristic changes.  Value 2 is to allow indications.
            CHAR_DESCRIPTOR_UUID16_WRITABLE( HANDLE_CAPSENSE_SERVICE_CHAR_CFG_DESC, UUID_DESCRIPTOR_CLIENT_CHARACTERISTIC_CONFIGURATION,
            LEGATTDB_PERM_READABLE | LEGATTDB_PERM_WRITE_REQ | LEGATTDB_PERM_AUTH_READABLE | LEGATTDB_PERM_AUTH_WRITABLE),

    // Declare Device info service
    PRIMARY_SERVICE_UUID16( HANDLE_DEV_INFO_SERVICE, UUID_SERVICE_DEVICE_INFORMATION ),

        // Characteristic Manufacturer Name
        CHARACTERISTIC_UUID16( HANDLE_DEV_INFO_SERVICE_CHAR_MFR_NAME, HANDLE_DEV_INFO_SERVICE_CHAR_MFR_NAME_VAL,
            UUID_CHARACTERISTIC_MANUFACTURER_NAME_STRING, LEGATTDB_CHAR_PROP_READ, LEGATTDB_PERM_READABLE ),

        // Characteristic Model Number
        CHARACTERISTIC_UUID16( HANDLE_DEV_INFO_SERVICE_CHAR_MODEL_NUM, HANDLE_DEV_INFO_SERVICE_CHAR_MODEL_NUM_VAL,
            UUID_CHARACTERISTIC_MODEL_NUMBER_STRING, LEGATTDB_CHAR_PROP_READ, LEGATTDB_PERM_READABLE ),

        // Characteristic System ID
        CHARACTERISTIC_UUID16( HANDLE_DEV_INFO_SERVICE_CHAR_SYSTEM_ID, HANDLE_DEV_INFO_SERVICE_CHAR_SYSTEM_ID_VAL,
            UUID_CHARACTERISTIC_SYSTEM_ID, LEGATTDB_CHAR_PROP_READ, LEGATTDB_PERM_READABLE ),
};

/* Structure for a GATT characteristic (attributes) */
typedef struct
{
    uint16_t handle;
    uint16_t attr_len;
    void     *p_attr;
} attribute_t;

/* GATT Database Characteristic (Attribute) Array */
attribute_t gauAttributes[] =
{
    { HANDLE_GAP_SERVICE_CHAR_DEV_NAME_VAL,       sizeof(device_name),         device_name },
    { HANDLE_GAP_SERVICE_CHAR_DEV_APPEARANCE_VAL, sizeof(appearance_name),       appearance_name },
    { HANDLE_CAPSENSE_SERVICE_CHAR_BUTTONS_VAL, sizeof(capSenseVal), capSenseVal},
    { HANDLE_CAPSENSE_SERVICE_CHAR_CFG_DESC, sizeof(capSenseCCCD), &capSenseCCCD},
    { HANDLE_DEV_INFO_SERVICE_CHAR_MFR_NAME_VAL,  sizeof(char_mfr_name_value),   char_mfr_name_value },
    { HANDLE_DEV_INFO_SERVICE_CHAR_MODEL_NUM_VAL, sizeof(char_model_num_value),  char_model_num_value },
    { HANDLE_DEV_INFO_SERVICE_CHAR_SYSTEM_ID_VAL, sizeof(char_system_id_value),  char_system_id_value },
};

/* Timer structure used to periodically read data from the shield over I2C */
wiced_timer_t ms_timer;

/* I2C variables */
uint8_t i2cOffset = BUTTON_REG;

struct {
    uint8_t buttonVal;
} __attribute__((packed)) i2cReadBuf;

/*****************************    Function Prototypes   *******************/
static void                    application_init( void );  /* Initialize the application */
#ifdef ENABLE_HCI_TRACE
static void                    hci_trace_cback( wiced_bt_hci_trace_type_t type, uint16_t length, uint8_t* p_data);
#endif
static wiced_result_t          bt_cback( wiced_bt_management_evt_t event, wiced_bt_management_evt_data_t *p_event_data);
static wiced_bt_gatt_status_t  gatts_callback( wiced_bt_gatt_evt_t event, wiced_bt_gatt_event_data_t *p_data);
static wiced_bt_gatt_status_t  gatts_req_cb( wiced_bt_gatt_attribute_request_t *p_data );
static wiced_bt_gatt_status_t  gatts_req_read_handler( uint16_t conn_id, wiced_bt_gatt_read_t * p_read_data );
static wiced_bt_gatt_status_t  gatts_req_write_handler( uint16_t conn_id, wiced_bt_gatt_write_t * p_data );
static attribute_t *           gatts_get_attribute( uint16_t handle );
static void                    bt_set_advertisement_data( void ); /* Setup BLE advertisement packets */
static void                    getPsocI2c(uint32_t arg);  /* This is a timer callback function used to get I2C data from PSoC */
static void                    send_message( void );

/*****************************    Function Implementations   ***********************/
/***********************************************************************************/
/*  Main application. This just starts the BT stack and provides the callback function.
 *  The actual application initialization will happen when stack reports that BT device is ready. */
APPLICATION_START( )
{
    /* Send DEBUG messages over the PUART or HCI UART depending on whether ENABLE_HCI_TRACE is defined in the make file*/
    #ifdef ENABLE_HCI_TRACE
    wiced_transport_init( &transport_cfg );
    wiced_set_debug_uart (WICED_ROUTE_DEBUG_TO_WICED_UART);
    wiced_bt_dev_register_hci_trace( hci_trace_cback );
    #else
    wiced_set_debug_uart( WICED_ROUTE_DEBUG_TO_PUART );
    #endif
    wiced_bt_stack_init( bt_cback, &wiced_bt_cfg_settings, wiced_bt_cfg_buf_pools ); /* Start up BLE stack */
}

/***********************************************************************************/
/* Callback function for HCI Trace messages */
#ifdef ENABLE_HCI_TRACE
static void hci_trace_cback( wiced_bt_hci_trace_type_t type, uint16_t length, uint8_t* p_data)
{
     wiced_transport_send_hci_trace(NULL, type, length, p_data);
}
#endif

/***********************************************************************************/
/* This function initializes the application.
 * It is executed in the BTM_ENABLED_EVT management callback. */
void application_init( void )
{
    wiced_bt_gatt_status_t gatt_status;
    wiced_result_t         result;

    /* Initialize wiced app */
    wiced_bt_app_init();

    /* Initialize I2C interface to the PSoC */
    /* Configure I2C block */
    wiced_hal_i2c_init();
    wiced_hal_i2c_set_speed(I2CM_SPEED_400KHZ);
    /* Write the offset for the button register */
    wiced_hal_i2c_write(&i2cOffset , 1, I2C_ADDRESS);

    /* Start a timer to read the I2C data every 100ms */
    if ( wiced_init_timer(&ms_timer, &getPsocI2c, 0, WICED_MILLI_SECONDS_PERIODIC_TIMER ) == WICED_SUCCESS )
    {
        wiced_start_timer( &ms_timer, TIMEOUT_IN_MS );
    }

    /* Register with stack to receive GATT callback */
    gatt_status = wiced_bt_gatt_register( gatts_callback );

    WICED_BT_TRACE( "\tGATT Init: wiced_bt_gatt_register: %d\r\n", gatt_status );

    /*  Tell stack to use our GATT databse */
    gatt_status =  wiced_bt_gatt_db_init( gatt_database, sizeof(gatt_database) );

    WICED_BT_TRACE("\tGATT Init: wiced_bt_gatt_db_init %d\r\n", gatt_status);

    /* Set the advertising params and make the device discoverable */
    bt_set_advertisement_data();

    /* Start connectable high duty cycle advertising */
    result =  wiced_bt_start_advertisements( BTM_BLE_ADVERT_UNDIRECTED_HIGH, 0, NULL );
    WICED_BT_TRACE("\t\tStart Advertisements: %d\r\n", result );

}


/***********************************************************************************/
/* Callback function for Bluetooth Stack events */
wiced_result_t bt_cback( wiced_bt_management_evt_t event, wiced_bt_management_evt_data_t *p_event_data)
{
    wiced_result_t                    result = WICED_BT_SUCCESS;
    wiced_bt_ble_advert_mode_t       *p_mode;

    WICED_BT_TRACE("BT_Callback: %d:\t", event );

    switch( event )
    {
        /* Bluetooth stack enabled */
        case BTM_ENABLED_EVT:
            WICED_BT_TRACE( "Bluetooth Enabled\n\r");
            application_init(); /* Initialize and start other application functionality */
            break;

        /* IO capabilities of the device requested by the client */
        case BTM_PAIRING_IO_CAPABILITIES_BLE_REQUEST_EVT:
            WICED_BT_TRACE( "BT_Callback: Pairing Requested\n\r");
            p_event_data->pairing_io_capabilities_ble_request.local_io_cap  = BTM_IO_CAPABILITIES_NONE;
            p_event_data->pairing_io_capabilities_ble_request.oob_data      = BTM_OOB_NONE;
            p_event_data->pairing_io_capabilities_ble_request.auth_req      = BTM_LE_AUTH_REQ_SC_ONLY;
            break;

        /* Security request from the client */
        case BTM_SECURITY_REQUEST_EVT:
            WICED_BT_TRACE( "Security Requested\n\r");
            wiced_bt_ble_security_grant( p_event_data->security_request.bd_addr, WICED_BT_SUCCESS );
            break;

        /* Advertising state change */
        case BTM_BLE_ADVERT_STATE_CHANGED_EVT:
            p_mode = &p_event_data->ble_advert_state_changed;
            WICED_BT_TRACE( "Advertisement State Change: %d\n\r", *p_mode);
            /* If advertising is off and we are not connected, turn it back on */
            if ( *p_mode == BTM_BLE_ADVERT_OFF && !connection_id)
            {
                result =  wiced_bt_start_advertisements( BTM_BLE_ADVERT_UNDIRECTED_HIGH, 0, NULL );
            }
            break;

        default:
            WICED_BT_TRACE( "Default\n\r");
            break;
    }
    return result;
}

/***********************************************************************************/
/* Callback function for Bluetooth GATT events */
wiced_bt_gatt_status_t gatts_callback( wiced_bt_gatt_evt_t event, wiced_bt_gatt_event_data_t *p_data)
{
    wiced_result_t result = WICED_BT_GATT_SUCCESS;

    /* This structure holds the connection status information */
    wiced_bt_gatt_connection_status_t *p_status;
    p_status = &(p_data->connection_status);

    switch(event)
    {
        case GATT_CONNECTION_STATUS_EVT: /* Process connect/disconnect events */
            /* Check to see if this is a connect or disconnect */
            if ( p_status->connected )
            {
                /* On a connect, stop advertising, and initialize everything */
                WICED_BT_TRACE( "\tGATT Callback: connection_up %B id:%d\r\n", p_status->bd_addr, p_status->conn_id);

                /* Update the connection handler.  Save address of the connected device. */
                connection_id = p_status->conn_id;

                //GJL add to start pairing when connection happens. Otherwise the peer has to initiate it
                //wiced_bt_dev_sec_bond( p_status->bd_addr,p_status->addr_type,p_status->transport, 0, NULL );

                /* Stop advertising */
                result =  wiced_bt_start_advertisements( BTM_BLE_ADVERT_OFF, 0, NULL );
                WICED_BT_TRACE( "\t\tStop Advertisements%d\r\n", result );
            }
            else
            {
                /* On a disconnect, reset everything and restart advertising */
                WICED_BT_TRACE( "\tGATT Callback: connection_down conn_id:%d reason:%d\r\n", connection_id, p_status->reason );

                /* Resetting the device info */
                connection_id = 0;
                capSenseCCCD = 0;

                /* Restart advertisements */
                result =  wiced_bt_start_advertisements( BTM_BLE_ADVERT_UNDIRECTED_HIGH, 0, NULL );
                WICED_BT_TRACE( "\t\tStart Advertisements: %d\r\n", result );
            }
            break;

        case GATT_ATTRIBUTE_REQUEST_EVT: /* Process GATT data requests */
            result = gatts_req_cb( &p_data->attribute_request );
            break;

        default:
            break;
    }
    return result;
}


/**************************************************************************************/
/* Process GATT requests from the peer - we will process read and write requests only */
wiced_bt_gatt_status_t gatts_req_cb( wiced_bt_gatt_attribute_request_t *p_data )
{
    WICED_BT_TRACE( "\tGATT Request: gatts_req_cb. connection %d, type %d\r\n", p_data->conn_id, p_data->request_type );

    wiced_result_t result = WICED_BT_GATT_INVALID_PDU;

    switch ( p_data->request_type )
    {
        case GATTS_REQ_TYPE_READ:
            result = gatts_req_read_handler( p_data->conn_id, &(p_data->data.read_req) );
            break;

        case GATTS_REQ_TYPE_WRITE:
            result = gatts_req_write_handler( p_data->conn_id, &(p_data->data.write_req) );
            break;

        default:
            break;
    }

    return result;
}

/***********************************************************************************/
/* Process Read request or command from peer device */
wiced_bt_gatt_status_t gatts_req_read_handler( uint16_t conn_id, wiced_bt_gatt_read_t * p_read_data )
{
    attribute_t *puAttribute;
    int          attr_len_to_copy;

    if ( ( puAttribute = gatts_get_attribute(p_read_data->handle) ) == NULL)
    {
        WICED_BT_TRACE("\t\tGATT Read: read_hndlr attr not found hdl:%x\r\n", p_read_data->handle );
        return WICED_BT_GATT_INVALID_HANDLE;
    }

    attr_len_to_copy = puAttribute->attr_len;

    WICED_BT_TRACE("\t\tGATT Read: read_hndlr conn_id:%d hdl:%x offset:%d len:%d\r\n", conn_id, p_read_data->handle, p_read_data->offset, attr_len_to_copy );

    if ( p_read_data->offset >= puAttribute->attr_len )
    {
        attr_len_to_copy = 0;
    }

    if ( attr_len_to_copy != 0 )
    {
        uint8_t *from;
        int      to_copy = attr_len_to_copy - p_read_data->offset;


        if ( to_copy > *p_read_data->p_val_len )
        {
            to_copy = *p_read_data->p_val_len;
        }

        from = ((uint8_t *)puAttribute->p_attr) + p_read_data->offset;
        *p_read_data->p_val_len = to_copy;

        memcpy( p_read_data->p_val, from, to_copy);
    }

    return WICED_BT_GATT_SUCCESS;
}


/***********************************************************************************/
/* Process write request or write command from peer device */
wiced_bt_gatt_status_t gatts_req_write_handler( uint16_t conn_id, wiced_bt_gatt_write_t * p_data )
{
    wiced_bt_gatt_status_t result    = WICED_BT_GATT_SUCCESS;
    uint8_t                *p_attr   = p_data->p_val;
    uint8_t                nv_update = WICED_FALSE;

    WICED_BT_TRACE("\t\tGATT Write: write_handler: conn_id:%d hdl:0x%x prep:%d offset:%d len:%d\r\n", conn_id, p_data->handle, p_data->is_prep, p_data->offset, p_data->val_len );

    switch ( p_data->handle )
    {
    /* By writing into Characteristic Client Configuration descriptor
     * peer can enable or disable notification or indication */
    case HANDLE_CAPSENSE_SERVICE_CHAR_CFG_DESC:
        if ( p_data->val_len != 2 )
        {
            return WICED_BT_GATT_INVALID_ATTR_LEN;
        }
        capSenseCCCD = p_attr[0] | ( p_attr[1] << 8 );
        break;

    default:
        result = WICED_BT_GATT_INVALID_HANDLE;
        break;
    }

    return result;
}

/***********************************************************************************/
/* Find attribute description by handle */
attribute_t * gatts_get_attribute( uint16_t handle )
{
    int i;
    for ( i = 0; i <  sizeof( gauAttributes ) / sizeof( gauAttributes[0] ); i++ )
    {
        if ( gauAttributes[i].handle == handle )
        {
            return ( &gauAttributes[i] );
        }
    }
    WICED_BT_TRACE( "\t\tAttribute not found:%x\r\n", handle );
    return NULL;
}

/***********************************************************************************/
/* Set the Advertisement packet data */
void bt_set_advertisement_data( void )
{
    wiced_bt_ble_advert_elem_t adv_elem[3];
    uint8_t num_elem = 0;
    uint8_t flag = BTM_BLE_GENERAL_DISCOVERABLE_FLAG | BTM_BLE_BREDR_NOT_SUPPORTED;
    uint8_t capsense_service_uuid[LEN_UUID_128] = { UUID_CAPSENSE_SERVICE };


    adv_elem[num_elem].advert_type  = BTM_BLE_ADVERT_TYPE_FLAG;
    adv_elem[num_elem].len          = sizeof(uint8_t);
    adv_elem[num_elem].p_data       = &flag;
    num_elem++;

    adv_elem[num_elem].advert_type  = BTM_BLE_ADVERT_TYPE_NAME_COMPLETE;
    adv_elem[num_elem].len          = strlen( (char *)wiced_bt_cfg_settings.device_name );
    adv_elem[num_elem].p_data       = ( uint8_t* )wiced_bt_cfg_settings.device_name ;
    num_elem++;

    adv_elem[num_elem].advert_type  = BTM_BLE_ADVERT_TYPE_128SRV_COMPLETE;
    adv_elem[num_elem].len          = LEN_UUID_128;
    adv_elem[num_elem].p_data       = capsense_service_uuid;
    num_elem++;

    wiced_bt_ble_set_raw_advertisement_data(num_elem, adv_elem);
}

/***********************************************************************************/
/* The function invoked on timeout of the timer.
 * It gets temperature and CapSense button data from the PSoC*/
void getPsocI2c( uint32_t arg )
{
    /* Read I2C data */
    wiced_hal_i2c_read((char *) &i2cReadBuf , sizeof(i2cReadBuf), I2C_ADDRESS);

    /* Copy Button values from I2C register to the CapSense GATT array */
    capSenseVal[BUTTON_VALUE_POS] = (i2cReadBuf.buttonVal & BUTTON_MASK);

    /* Check to see if any button values have changed and if so send the data */
    if(capSenseValPrev[BUTTON_VALUE_POS] != capSenseVal[BUTTON_VALUE_POS])
    {
        capSenseValPrev[BUTTON_VALUE_POS] = capSenseVal[BUTTON_VALUE_POS];
        WICED_BT_TRACE("\t\t\tCapSense Button Value Change: %x\r\n",capSenseVal[BUTTON_VALUE_POS]);
        send_message();
    }
}

/***********************************************************************************/
/* Check if client has registered for notification and send message if appropriate */
void send_message( void )
{
    /* Check if connection is up. If not, do nothing */
    if ( connection_id != 0)
    {
        /* If client has registered for notifications, send the value */
        if ( capSenseCCCD & GATT_CLIENT_CONFIG_NOTIFICATION )
        {
            wiced_bt_gatt_send_notification(connection_id, HANDLE_CAPSENSE_SERVICE_CHAR_BUTTONS_VAL, sizeof(capSenseVal), capSenseVal );
            WICED_BT_TRACE( "\tSend Notification: sending CapSense value\r\n");
        }
    }
}

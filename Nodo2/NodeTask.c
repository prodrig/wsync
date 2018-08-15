/***** Includes *****/
/* XDCtools Header files */ 
#include <xdc/std.h>
#include <xdc/runtime/System.h>

/* BIOS Header files */ 
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Event.h>
#include <ti/sysbios/knl/Clock.h>

/* TI-RTOS Header files */ 
#include <ti/drivers/PIN.h>
#include <ti/display/Display.h>
#include <ti/display/DisplayExt.h>

/* Board Header files */
#include "Board.h"

/* Application Header files */ 
#include "SceAdc.h"
#include "NodeTask.h"
#include "NodeRadioTask.h"

#ifdef FEATURE_BLE_ADV
#include "ble_adv/BleAdv.h"
#endif

/***** Defines *****/
#define NODE_TASK_STACK_SIZE 1024
#define NODE_TASK_PRIORITY   4

#define NODE_EVENT_ALL                  0xFFFFFFFF
#define NODE_EVENT_NEW_ADC_VALUE    (uint32_t)(1 << 0)
#define NODE_EVENT_UPDATE_LCD       (uint32_t)(1 << 1)

/* A change mask of 0xFF0 means that changes in the lower 4 bits does not trigger a wakeup. */
#define NODE_ADCTASK_CHANGE_MASK                    0xFF0

/* Minimum slow Report interval is 50s (in units of samplingTime)*/
//#define NODE_ADCTASK_REPORTINTERVAL_SLOW                50
/* Minimum fast Report interval is 1s (in units of samplingTime) for 30s*/
#define NODE_ADCTASK_REPORTINTERVAL_FAST                5
//#define NODE_ADCTASK_REPORTINTERVAL_FAST_DURIATION_MS   30000


#define NUM_EDDYSTONE_URLS      5

/***** Variable declarations *****/
static Task_Params nodeTaskParams;
Task_Struct nodeTask;    /* Not static so you can see in ROV */
static uint8_t nodeTaskStack[NODE_TASK_STACK_SIZE];
Event_Struct nodeEvent;  /* Not static so you can see in ROV */
static Event_Handle nodeEventHandle;
static uint16_t latestAdcValue;

/* Pin driver handle */
static PIN_Handle buttonPinHandle;
static PIN_Handle ledPinHandle;
static PIN_State buttonPinState;
static PIN_State ledPinState;

/* Enable the 3.3V power domain used by the LCD */
PIN_Config pinTable[] = {
#if !defined __CC1350STK_BOARD_H__
    NODE_ACTIVITY_LED | PIN_GPIO_OUTPUT_EN | PIN_GPIO_LOW | PIN_PUSHPULL | PIN_DRVSTR_MAX,
#endif
    PIN_TERMINATE
};

/*
 * Application button pin configuration table:
 *   - Buttons interrupts are configured to trigger on falling edge.
 */
PIN_Config buttonPinTable[] = {
    Board_PIN_BUTTON0  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
//#ifdef FEATURE_BLE_ADV
    Board_PIN_BUTTON1  | PIN_INPUT_EN | PIN_PULLUP | PIN_IRQ_NEGEDGE,
//#endif
    PIN_TERMINATE
};

//static uint8_t nodeAddress = 0;

#ifdef FEATURE_BLE_ADV
static BleAdv_Stats bleAdvStats = {0};
#endif

/***** Prototypes *****/
static void nodeTaskFunction(UArg arg0, UArg arg1);
static void adcCallback(uint16_t adcValue);
static void buttonCallback(PIN_Handle handle, PIN_Id pinId);


/***** Function definitions *****/
void NodeTask_init(void)
{

    /* Create event used internally for state changes */
    Event_Params eventParam;
    Event_Params_init(&eventParam);
    Event_construct(&nodeEvent, &eventParam);
    nodeEventHandle = Event_handle(&nodeEvent);

    /* Create the node task */
    Task_Params_init(&nodeTaskParams);
    nodeTaskParams.stackSize = NODE_TASK_STACK_SIZE;
    nodeTaskParams.priority = NODE_TASK_PRIORITY;
    nodeTaskParams.stack = &nodeTaskStack;
    Task_construct(&nodeTask, nodeTaskFunction, &nodeTaskParams, NULL);
}

#ifdef FEATURE_BLE_ADV
void NodeTask_advStatsCB(BleAdv_Stats stats)
{
    memcpy(&bleAdvStats, &stats, sizeof(BleAdv_Stats));

    /* Post LCD update event */
    Event_post(nodeEventHandle, NODE_EVENT_UPDATE_LCD);
}
#endif

static void nodeTaskFunction(UArg arg0, UArg arg1)
{
    /* Open LED pins */
    ledPinHandle = PIN_open(&ledPinState, pinTable);
    if (!ledPinHandle)
    {
        System_abort("Error initializing board 3.3V domain pins\n");
    }

    /* Start the SCE ADC task with 1s sample period and reacting to change in ADC value. */
    SceAdc_init(0x00010000, NODE_ADCTASK_REPORTINTERVAL_FAST, NODE_ADCTASK_CHANGE_MASK);
    SceAdc_registerAdcCallback(adcCallback);
    SceAdc_start();

    buttonPinHandle = PIN_open(&buttonPinState, buttonPinTable);
    if (!buttonPinHandle)
    {
        System_abort("Error initializing button pins\n");
    }

    /* Setup callback for button pins */
    if (PIN_registerIntCb(buttonPinHandle, &buttonCallback) != 0)
    {
        System_abort("Error registering button callback function");
    }

    while (1)
    {
        /* Wait for event */
        uint32_t events = Event_pend(nodeEventHandle, 0, NODE_EVENT_ALL, BIOS_WAIT_FOREVER);

        /* If new ADC value, send this data */
        if (events & NODE_EVENT_NEW_ADC_VALUE) {
            /* Toggle activity LED */
#if !defined __CC1350STK_BOARD_H__
            PIN_setOutputValue(ledPinHandle, NODE_ACTIVITY_LED,!PIN_getOutputValue(NODE_ACTIVITY_LED));
#endif

            /* Send ADC value to concentrator */
            NodeRadioTask_sendAdcData(latestAdcValue);

            /* Update display */
            //updateLcd();
        }
    }
}

static void adcCallback(uint16_t adcValue)
{
    /* Save latest value */
    latestAdcValue = adcValue;

    /* Post event */
    Event_post(nodeEventHandle, NODE_EVENT_NEW_ADC_VALUE);
}

/*
 *  ======== buttonCallback ========
 *  Pin interrupt Callback function board buttons configured in the pinTable.
 */
static void buttonCallback(PIN_Handle handle, PIN_Id pinId)
{
    /* Debounce logic, only toggle if the button is still pushed (low) */
    CPUdelay(8000*50);


    if (PIN_getInputValue(Board_PIN_BUTTON1) == 0)
    {
        nodeRadioTask_addNodeAddr();
    }
    else if (PIN_getInputValue(Board_PIN_BUTTON0) == 0)
    {
        nodeRadioTask_subNodeAddr();

    }
}

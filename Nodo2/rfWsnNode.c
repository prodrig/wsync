/* XDCtools Header files */
#include <xdc/std.h>
#include <xdc/runtime/System.h>

/* BIOS Header files */
#include <ti/sysbios/BIOS.h>
#include <ti/display/Display.h>
#include <ti/drivers/Power.h>
#include <ti/drivers/power/PowerCC26XX.h>

/* Board Header files */
#include "Board.h"

/* Application Header files */ 
#include "NodeRadioTask.h"
#include "NodeTask.h"


/*
 *  ======== main ========
 */
int main(void)
{
    /* Call driver init functions. */
    Board_initGeneral();

    /* Initialize sensor node tasks */
    NodeRadioTask_init();
    NodeTask_init();

    /* Start BIOS */
    BIOS_start();

    return (0);
}

/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include <linux/kernel.h>
#include <linux/module.h>
#include <stdarg.h>

#include "csr_pmem.h"
#include "csr_util.h"

/*------------------------------------------------------------------*/
/* Base conversion */
/*------------------------------------------------------------------*/
/* Convert signed 32 bit (or less) integer to string */
void CsrUInt16ToHex(u16 number, char *str)
{
    u16 index;
    u16 currentValue;

    for (index = 0; index < 4; index++)
    {
        currentValue = (u16) (number & 0x000F);
        number >>= 4;
        str[3 - index] = (char) (currentValue > 9 ? currentValue + 55 : currentValue + '0');
    }
    str[4] = '\0';
}

MODULE_DESCRIPTION("CSR Operating System Kernel Abstraction");
MODULE_AUTHOR("Cambridge Silicon Radio Ltd.");
MODULE_LICENSE("GPL and additional rights");

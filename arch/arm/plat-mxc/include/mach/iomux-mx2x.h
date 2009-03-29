/*
* Copyright (C) 2008 by Sascha Hauer <kernel@pengutronix.de>
* Copyright (C) 2009 by Holger Schurig <hs4233@mail.mn-solutions.de>
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
* MA 02110-1301, USA.
*/

#ifndef _MXC_IOMUX_MX2x_H
#define _MXC_IOMUX_MX2x_H

#ifndef GPIO_PORTA
#error Please include mach/iomux.h
#endif


/* Primary GPIO pin functions */

#define PA5_PF_LSCLK            (GPIO_PORTA | GPIO_PF | GPIO_OUT | 5)
#define PA6_PF_LD0              (GPIO_PORTA | GPIO_PF | GPIO_OUT | 6)
#define PA7_PF_LD1              (GPIO_PORTA | GPIO_PF | GPIO_OUT | 7)
#define PA8_PF_LD2              (GPIO_PORTA | GPIO_PF | GPIO_OUT | 8)
#define PA9_PF_LD3              (GPIO_PORTA | GPIO_PF | GPIO_OUT | 9)
#define PA10_PF_LD4             (GPIO_PORTA | GPIO_PF | GPIO_OUT | 10)
#define PA11_PF_LD5             (GPIO_PORTA | GPIO_PF | GPIO_OUT | 11)
#define PA12_PF_LD6             (GPIO_PORTA | GPIO_PF | GPIO_OUT | 12)
#define PA13_PF_LD7             (GPIO_PORTA | GPIO_PF | GPIO_OUT | 13)
#define PA14_PF_LD8             (GPIO_PORTA | GPIO_PF | GPIO_OUT | 14)
#define PA15_PF_LD9             (GPIO_PORTA | GPIO_PF | GPIO_OUT | 15)
#define PA16_PF_LD10            (GPIO_PORTA | GPIO_PF | GPIO_OUT | 16)
#define PA17_PF_LD11            (GPIO_PORTA | GPIO_PF | GPIO_OUT | 17)
#define PA18_PF_LD12            (GPIO_PORTA | GPIO_PF | GPIO_OUT | 18)
#define PA19_PF_LD13            (GPIO_PORTA | GPIO_PF | GPIO_OUT | 19)
#define PA20_PF_LD14            (GPIO_PORTA | GPIO_PF | GPIO_OUT | 20)
#define PA21_PF_LD15            (GPIO_PORTA | GPIO_PF | GPIO_OUT | 21)
#define PA22_PF_LD16            (GPIO_PORTA | GPIO_PF | GPIO_OUT | 22)
#define PA23_PF_LD17            (GPIO_PORTA | GPIO_PF | GPIO_OUT | 23)
#define PA24_PF_REV             (GPIO_PORTA | GPIO_PF | GPIO_OUT | 24)
#define PA25_PF_CLS             (GPIO_PORTA | GPIO_PF | GPIO_OUT | 25)
#define PA26_PF_PS              (GPIO_PORTA | GPIO_PF | GPIO_OUT | 26)
#define PA27_PF_SPL_SPR         (GPIO_PORTA | GPIO_PF | GPIO_OUT | 27)
#define PA28_PF_HSYNC           (GPIO_PORTA | GPIO_PF | GPIO_OUT | 28)
#define PA29_PF_VSYNC           (GPIO_PORTA | GPIO_PF | GPIO_OUT | 29)
#define PA30_PF_CONTRAST        (GPIO_PORTA | GPIO_PF | GPIO_OUT | 30)
#define PA31_PF_OE_ACD          (GPIO_PORTA | GPIO_PF | GPIO_OUT | 31)
#define PB4_PF_SD2_D0           (GPIO_PORTB | GPIO_PF | 4)
#define PB5_PF_SD2_D1           (GPIO_PORTB | GPIO_PF | 5)
#define PB6_PF_SD2_D2           (GPIO_PORTB | GPIO_PF | 6)
#define PB7_PF_SD2_D3           (GPIO_PORTB | GPIO_PF | 7)
#define PB8_PF_SD2_CMD          (GPIO_PORTB | GPIO_PF | 8)
#define PB9_PF_SD2_CLK          (GPIO_PORTB | GPIO_PF | 9)
#define PB10_PF_CSI_D0          (GPIO_PORTB | GPIO_PF | GPIO_OUT | 10)
#define PB11_PF_CSI_D1          (GPIO_PORTB | GPIO_PF | GPIO_OUT | 11)
#define PB12_PF_CSI_D2          (GPIO_PORTB | GPIO_PF | GPIO_OUT | 12)
#define PB13_PF_CSI_D3          (GPIO_PORTB | GPIO_PF | GPIO_OUT | 13)
#define PB14_PF_CSI_D4          (GPIO_PORTB | GPIO_PF | GPIO_OUT | 14)
#define PB15_PF_CSI_MCLK        (GPIO_PORTB | GPIO_PF | GPIO_OUT | 15)
#define PB16_PF_CSI_PIXCLK      (GPIO_PORTB | GPIO_PF | GPIO_OUT | 16)
#define PB17_PF_CSI_D5          (GPIO_PORTB | GPIO_PF | GPIO_OUT | 17)
#define PB18_PF_CSI_D6          (GPIO_PORTB | GPIO_PF | GPIO_OUT | 18)
#define PB19_PF_CSI_D7          (GPIO_PORTB | GPIO_PF | GPIO_OUT | 19)
#define PB20_PF_CSI_VSYNC       (GPIO_PORTB | GPIO_PF | GPIO_OUT | 20)
#define PB21_PF_CSI_HSYNC       (GPIO_PORTB | GPIO_PF | GPIO_OUT | 21)
#define PB23_PF_USB_PWR         (GPIO_PORTB | GPIO_PF | 23)
#define PB24_PF_USB_OC          (GPIO_PORTB | GPIO_PF | 24)
#define PB26_PF_USBH1_FS        (GPIO_PORTB | GPIO_PF | 26)
#define PB27_PF_USBH1_OE        (GPIO_PORTB | GPIO_PF | 27)
#define PB28_PF_USBH1_TXDM      (GPIO_PORTB | GPIO_PF | 28)
#define PB29_PF_USBH1_TXDP      (GPIO_PORTB | GPIO_PF | 29)
#define PB30_PF_USBH1_RXDM      (GPIO_PORTB | GPIO_PF | 30)
#define PB31_PF_USBH1_RXDP      (GPIO_PORTB | GPIO_PF | 31)
#define PC14_PF_TOUT            (GPIO_PORTC | GPIO_PF | 14)
#define PC15_PF_TIN             (GPIO_PORTC | GPIO_PF | 15)
#define PC20_PF_SSI1_FS         (GPIO_PORTC | GPIO_PF | GPIO_IN | 20)
#define PC21_PF_SSI1_RXD        (GPIO_PORTC | GPIO_PF | GPIO_IN | 21)
#define PC22_PF_SSI1_TXD        (GPIO_PORTC | GPIO_PF | GPIO_IN | 22)
#define PC23_PF_SSI1_CLK        (GPIO_PORTC | GPIO_PF | GPIO_IN | 23)
#define PC24_PF_SSI2_FS         (GPIO_PORTC | GPIO_PF | GPIO_IN | 24)
#define PC25_PF_SSI2_RXD        (GPIO_PORTC | GPIO_PF | GPIO_IN | 25)
#define PC26_PF_SSI2_TXD        (GPIO_PORTC | GPIO_PF | GPIO_IN | 26)
#define PC27_PF_SSI2_CLK        (GPIO_PORTC | GPIO_PF | GPIO_IN | 27)
#define PC28_PF_SSI3_FS         (GPIO_PORTC | GPIO_PF | GPIO_IN | 28)
#define PC29_PF_SSI3_RXD        (GPIO_PORTC | GPIO_PF | GPIO_IN | 29)
#define PC30_PF_SSI3_TXD        (GPIO_PORTC | GPIO_PF | GPIO_IN | 30)
#define PC31_PF_SSI3_CLK        (GPIO_PORTC | GPIO_PF | GPIO_IN | 31)
#define PD17_PF_I2C_DATA        (GPIO_PORTD | GPIO_PF | GPIO_OUT | 17)
#define PD18_PF_I2C_CLK         (GPIO_PORTD | GPIO_PF | GPIO_OUT | 18)
#define PD19_PF_CSPI2_SS2       (GPIO_PORTD | GPIO_PF | 19)
#define PD20_PF_CSPI2_SS1       (GPIO_PORTD | GPIO_PF | 20)
#define PD21_PF_CSPI2_SS0       (GPIO_PORTD | GPIO_PF | 21)
#define PD22_PF_CSPI2_SCLK      (GPIO_PORTD | GPIO_PF | 22)
#define PD23_PF_CSPI2_MISO      (GPIO_PORTD | GPIO_PF | 23)
#define PD24_PF_CSPI2_MOSI      (GPIO_PORTD | GPIO_PF | 24)
#define PD25_PF_CSPI1_RDY       (GPIO_PORTD | GPIO_PF | GPIO_OUT | 25)
#define PD26_PF_CSPI1_SS2       (GPIO_PORTD | GPIO_PF | GPIO_OUT | 26)
#define PD27_PF_CSPI1_SS1       (GPIO_PORTD | GPIO_PF | GPIO_OUT | 27)
#define PD28_PF_CSPI1_SS0       (GPIO_PORTD | GPIO_PF | GPIO_OUT | 28)
#define PD29_PF_CSPI1_SCLK      (GPIO_PORTD | GPIO_PF | GPIO_OUT | 29)
#define PD30_PF_CSPI1_MISO      (GPIO_PORTD | GPIO_PF | GPIO_IN | 30)
#define PD31_PF_CSPI1_MOSI      (GPIO_PORTD | GPIO_PF | GPIO_OUT | 31)
#define PE3_PF_UART2_CTS        (GPIO_PORTE | GPIO_PF | GPIO_OUT | 3)
#define PE4_PF_UART2_RTS        (GPIO_PORTE | GPIO_PF | GPIO_IN | 4)
#define PE5_PF_PWMO             (GPIO_PORTE | GPIO_PF | 5)
#define PE6_PF_UART2_TXD        (GPIO_PORTE | GPIO_PF | GPIO_OUT | 6)
#define PE7_PF_UART2_RXD        (GPIO_PORTE | GPIO_PF | GPIO_IN | 7)
#define PE8_PF_UART3_TXD        (GPIO_PORTE | GPIO_PF | GPIO_OUT | 8)
#define PE9_PF_UART3_RXD        (GPIO_PORTE | GPIO_PF | GPIO_IN | 9)
#define PE10_PF_UART3_CTS       (GPIO_PORTE | GPIO_PF | GPIO_OUT | 10)
#define PE11_PF_UART3_RTS       (GPIO_PORTE | GPIO_PF | GPIO_IN | 11)
#define PE12_PF_UART1_TXD       (GPIO_PORTE | GPIO_PF | GPIO_OUT | 12)
#define PE13_PF_UART1_RXD       (GPIO_PORTE | GPIO_PF | GPIO_IN | 13)
#define PE14_PF_UART1_CTS       (GPIO_PORTE | GPIO_PF | GPIO_OUT | 14)
#define PE15_PF_UART1_RTS       (GPIO_PORTE | GPIO_PF | GPIO_IN | 15)
#define PE16_PF_RTCK            (GPIO_PORTE | GPIO_PF | GPIO_OUT | 16)
#define PE17_PF_RESET_OUT       (GPIO_PORTE | GPIO_PF | 17)
#define PE18_PF_SD1_D0          (GPIO_PORTE | GPIO_PF | 18)
#define PE19_PF_SD1_D1          (GPIO_PORTE | GPIO_PF | 19)
#define PE20_PF_SD1_D2          (GPIO_PORTE | GPIO_PF | 20)
#define PE21_PF_SD1_D3          (GPIO_PORTE | GPIO_PF | 21)
#define PE22_PF_SD1_CMD         (GPIO_PORTE | GPIO_PF | 22)
#define PE23_PF_SD1_CLK         (GPIO_PORTE | GPIO_PF | 23)
#define PF0_PF_NRFB             (GPIO_PORTF | GPIO_PF | 0)
#define PF2_PF_NFWP             (GPIO_PORTF | GPIO_PF | 2)
#define PF4_PF_NFALE            (GPIO_PORTF | GPIO_PF | 4)
#define PF5_PF_NFRE             (GPIO_PORTF | GPIO_PF | 5)
#define PF6_PF_NFWE             (GPIO_PORTF | GPIO_PF | 6)
#define PF15_PF_CLKO            (GPIO_PORTF | GPIO_PF | 15)
#define PF21_PF_CS4             (GPIO_PORTF | GPIO_PF | 21)
#define PF22_PF_CS5             (GPIO_PORTF | GPIO_PF | 22)

/* Alternate GPIO pin functions */

#define PB26_AF_UART4_RTS       (GPIO_PORTB | GPIO_AF | GPIO_IN | 26)
#define PB28_AF_UART4_TXD       (GPIO_PORTB | GPIO_AF | GPIO_OUT | 28)
#define PB29_AF_UART4_CTS       (GPIO_PORTB | GPIO_AF | GPIO_OUT | 29)
#define PB31_AF_UART4_RXD       (GPIO_PORTB | GPIO_AF | GPIO_IN | 31)
#define PC28_AF_SLCDC2_D0       (GPIO_PORTC | GPIO_AF | 28)
#define PC29_AF_SLCDC2_RS       (GPIO_PORTC | GPIO_AF | 29)
#define PC30_AF_SLCDC2_CS       (GPIO_PORTC | GPIO_AF | 30)
#define PC31_AF_SLCDC2_CLK      (GPIO_PORTC | GPIO_AF | 31)
#define PD19_AF_USBH2_DATA4     (GPIO_PORTD | GPIO_AF | 19)
#define PD20_AF_USBH2_DATA3     (GPIO_PORTD | GPIO_AF | 20)
#define PD21_AF_USBH2_DATA6     (GPIO_PORTD | GPIO_AF | 21)
#define PD22_AF_USBH2_DATA0     (GPIO_PORTD | GPIO_AF | 22)
#define PD23_AF_USBH2_DATA2     (GPIO_PORTD | GPIO_AF | 23)
#define PD24_AF_USBH2_DATA1     (GPIO_PORTD | GPIO_AF | 24)
#define PD26_AF_USBH2_DATA5     (GPIO_PORTD | GPIO_AF | 26)
#define PE0_AF_KP_COL6          (GPIO_PORTE | GPIO_AF | 0)
#define PE1_AF_KP_ROW6          (GPIO_PORTE | GPIO_AF | 1)
#define PE2_AF_KP_ROW7          (GPIO_PORTE | GPIO_AF | 2)
#define PE3_AF_KP_COL7          (GPIO_PORTE | GPIO_AF | 3)
#define PE4_AF_KP_ROW7          (GPIO_PORTE | GPIO_AF | 4)
#define PE6_AF_KP_COL6          (GPIO_PORTE | GPIO_AF | 6)
#define PE7_AF_KP_ROW6          (GPIO_PORTE | GPIO_AF | 7)
#define PE16_AF_OWIRE           (GPIO_PORTE | GPIO_AF | 16)
#define PE18_AF_CSPI3_MISO      (GPIO_PORTE | GPIO_AF | GPIO_IN | 18)
#define PE21_AF_CSPI3_SS        (GPIO_PORTE | GPIO_AF | GPIO_OUT | 21)
#define PE22_AF_CSPI3_MOSI      (GPIO_PORTE | GPIO_AF | GPIO_OUT | 22)
#define PE23_AF_CSPI3_SCLK      (GPIO_PORTE | GPIO_AF | GPIO_OUT | 23)

/* AIN GPIO pin functions */

#define PA6_AIN_SLCDC1_DAT0     (GPIO_PORTA | GPIO_AIN | GPIO_OUT | 6)
#define PA7_AIN_SLCDC1_DAT1     (GPIO_PORTA | GPIO_AIN | GPIO_OUT | 7)
#define PA8_AIN_SLCDC1_DAT2     (GPIO_PORTA | GPIO_AIN | GPIO_OUT | 8)
#define PA0_AIN_SLCDC1_DAT3     (GPIO_PORTA | GPIO_AIN | GPIO_OUT | 0)
#define PA11_AIN_SLCDC1_DAT5    (GPIO_PORTA | GPIO_AIN | GPIO_OUT | 11)
#define PA13_AIN_SLCDC1_DAT7    (GPIO_PORTA | GPIO_AIN | GPIO_OUT | 13)
#define PA15_AIN_SLCDC1_DAT9    (GPIO_PORTA | GPIO_AIN | GPIO_OUT | 15)
#define PA17_AIN_SLCDC1_DAT11   (GPIO_PORTA | GPIO_AIN | GPIO_OUT | 17)
#define PA19_AIN_SLCDC1_DAT13   (GPIO_PORTA | GPIO_AIN | GPIO_OUT | 19)
#define PA21_AIN_SLCDC1_DAT15   (GPIO_PORTA | GPIO_AIN | GPIO_OUT | 21)
#define PA22_AIN_EXT_DMAGRANT   (GPIO_PORTA | GPIO_AIN | GPIO_OUT | 22)
#define PA24_AIN_SLCDC1_D0      (GPIO_PORTA | GPIO_AIN | GPIO_OUT | 24)
#define PA25_AIN_SLCDC1_RS      (GPIO_PORTA | GPIO_AIN | GPIO_OUT | 25)
#define PA26_AIN_SLCDC1_CS      (GPIO_PORTA | GPIO_AIN | GPIO_OUT | 26)
#define PA27_AIN_SLCDC1_CLK     (GPIO_PORTA | GPIO_AIN | GPIO_OUT | 27)
#define PB6_AIN_SLCDC1_D0       (GPIO_PORTB | GPIO_AIN | GPIO_OUT | 6)
#define PB7_AIN_SLCDC1_RS       (GPIO_PORTB | GPIO_AIN | GPIO_OUT | 7)
#define PB8_AIN_SLCDC1_CS       (GPIO_PORTB | GPIO_AIN | GPIO_OUT | 8)
#define PB9_AIN_SLCDC1_CLK      (GPIO_PORTB | GPIO_AIN | GPIO_OUT | 9)
#define PB25_AIN_SLCDC1_DAT0    (GPIO_PORTB | GPIO_AIN | GPIO_OUT | 25)
#define PB26_AIN_SLCDC1_DAT1    (GPIO_PORTB | GPIO_AIN | GPIO_OUT | 26)
#define PB27_AIN_SLCDC1_DAT2    (GPIO_PORTB | GPIO_AIN | GPIO_OUT | 27)
#define PB28_AIN_SLCDC1_DAT3    (GPIO_PORTB | GPIO_AIN | GPIO_OUT | 28)
#define PB29_AIN_SLCDC1_DAT4    (GPIO_PORTB | GPIO_AIN | GPIO_OUT | 29)
#define PB30_AIN_SLCDC1_DAT5    (GPIO_PORTB | GPIO_AIN | GPIO_OUT | 30)
#define PB31_AIN_SLCDC1_DAT6    (GPIO_PORTB | GPIO_AIN | GPIO_OUT | 31)
#define PC5_AIN_SLCDC1_DAT7     (GPIO_PORTC | GPIO_AIN | GPIO_OUT | 5)
#define PC6_AIN_SLCDC1_DAT8     (GPIO_PORTC | GPIO_AIN | GPIO_OUT | 6)
#define PC7_AIN_SLCDC1_DAT9     (GPIO_PORTC | GPIO_AIN | GPIO_OUT | 7)
#define PC8_AIN_SLCDC1_DAT10    (GPIO_PORTC | GPIO_AIN | GPIO_OUT | 8)
#define PC9_AIN_SLCDC1_DAT11    (GPIO_PORTC | GPIO_AIN | GPIO_OUT | 9)
#define PC10_AIN_SLCDC1_DAT12   (GPIO_PORTC | GPIO_AIN | GPIO_OUT | 10)
#define PC11_AIN_SLCDC1_DAT13   (GPIO_PORTC | GPIO_AIN | GPIO_OUT | 11)
#define PC12_AIN_SLCDC1_DAT14   (GPIO_PORTC | GPIO_AIN | GPIO_OUT | 12)
#define PC13_AIN_SLCDC1_DAT15   (GPIO_PORTC | GPIO_AIN | GPIO_OUT | 13)
#define PE5_AIN_PC_SPKOUT       (GPIO_PORTE | GPIO_AIN | GPIO_OUT | 5)

/* BIN GPIO pin functions */

#define PE5_BIN_TOUT2           (GPIO_PORTE | GPIO_BIN | GPIO_OUT | 5)

/* CIN GPIO pin functions */

#define PA14_CIN_SLCDC1_DAT0    (GPIO_PORTA | GPIO_CIN | GPIO_OUT | 14)
#define PA15_CIN_SLCDC1_DAT1    (GPIO_PORTA | GPIO_CIN | GPIO_OUT | 15)
#define PA16_CIN_SLCDC1_DAT2    (GPIO_PORTA | GPIO_CIN | GPIO_OUT | 16)
#define PA17_CIN_SLCDC1_DAT3    (GPIO_PORTA | GPIO_CIN | GPIO_OUT | 17)
#define PA18_CIN_SLCDC1_DAT4    (GPIO_PORTA | GPIO_CIN | GPIO_OUT | 18)
#define PA19_CIN_SLCDC1_DAT5    (GPIO_PORTA | GPIO_CIN | GPIO_OUT | 19)
#define PA20_CIN_SLCDC1_DAT6    (GPIO_PORTA | GPIO_CIN | GPIO_OUT | 20)
#define PA21_CIN_SLCDC1_DAT7    (GPIO_PORTA | GPIO_CIN | GPIO_OUT | 21)
#define PB30_CIN_UART4_CTS      (GPIO_PORTB | GPIO_CIN | GPIO_OUT | 30)
#define PE5_CIN_TOUT3           (GPIO_PORTE | GPIO_CIN | GPIO_OUT | 5)

/* AOUT GPIO pin functions */

#define PB29_AOUT_UART4_RXD     (GPIO_PORTB | GPIO_AOUT | GPIO_IN | 29)
#define PB31_AOUT_UART4_RTS     (GPIO_PORTB | GPIO_AOUT | GPIO_IN | 31)
#define PC8_AOUT_USBOTG_TXR_INT (GPIO_PORTC | GPIO_AOUT | GPIO_IN | 8)
#define PC15_AOUT_WKGD          (GPIO_PORTC | GPIO_AOUT | GPIO_IN | 15)
#define PF21_AOUT_DTACK         (GPIO_PORTF | GPIO_AOUT | GPIO_IN | 21)


#endif

#include "../comedidev.h"
#include "comedi_fc.h"
#include "amcc_s5933.h"

#include "addi-data/addi_common.h"

#define ADDIDATA_WATCHDOG 2	/*  Or shold it be something else */

#include "addi-data/addi_eeprom.c"
#include "addi-data/hwdrv_apci035.c"
#include "addi-data/addi_common.c"

static const struct addi_board apci035_boardtypes[] = {
	{
		.pc_DriverName		= "apci035",
		.i_VendorId		= PCI_VENDOR_ID_ADDIDATA,
		.i_DeviceId		= 0x0300,
		.i_IorangeBase0		= 127,
		.i_IorangeBase1		= APCI035_ADDRESS_RANGE,
		.i_PCIEeprom		= 1,
		.pc_EepromChip		= ADDIDATA_S5920,
		.i_NbrAiChannel		= 16,
		.i_NbrAiChannelDiff	= 8,
		.i_AiChannelList	= 16,
		.i_AiMaxdata		= 0xff,
		.pr_AiRangelist		= &range_apci035_ai,
		.i_Timer		= 1,
		.ui_MinAcquisitiontimeNs = 10000,
		.ui_MinDelaytimeNs	= 100000,
		.interrupt		= v_APCI035_Interrupt,
		.reset			= i_APCI035_Reset,
		.ai_config		= i_APCI035_ConfigAnalogInput,
		.ai_read		= i_APCI035_ReadAnalogInput,
		.timer_config		= i_APCI035_ConfigTimerWatchdog,
		.timer_write		= i_APCI035_StartStopWriteTimerWatchdog,
		.timer_read		= i_APCI035_ReadTimerWatchdog,
	},
};

static struct comedi_driver apci035_driver = {
	.driver_name	= "addi_apci_035",
	.module		= THIS_MODULE,
	.auto_attach	= addi_auto_attach,
	.detach		= i_ADDI_Detach,
	.num_names	= ARRAY_SIZE(apci035_boardtypes),
	.board_name	= &apci035_boardtypes[0].pc_DriverName,
	.offset		= sizeof(struct addi_board),
};

static int __devinit apci035_pci_probe(struct pci_dev *dev,
				       const struct pci_device_id *ent)
{
	return comedi_pci_auto_config(dev, &apci035_driver);
}

static void __devexit apci035_pci_remove(struct pci_dev *dev)
{
	comedi_pci_auto_unconfig(dev);
}

static DEFINE_PCI_DEVICE_TABLE(apci035_pci_table) = {
	{ PCI_DEVICE(PCI_VENDOR_ID_ADDIDATA,  0x0300) },
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, apci035_pci_table);

static struct pci_driver apci035_pci_driver = {
	.name		= "addi_apci_035",
	.id_table	= apci035_pci_table,
	.probe		= apci035_pci_probe,
	.remove		= apci035_pci_remove,
};
module_comedi_pci_driver(apci035_driver, apci035_pci_driver);

MODULE_AUTHOR("Comedi http://www.comedi.org");
MODULE_DESCRIPTION("Comedi low-level driver");
MODULE_LICENSE("GPL");

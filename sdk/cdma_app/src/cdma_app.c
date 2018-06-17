/*
 * Copyright (c) 2015 Xilinx, Inc.  All rights reserved.
 *
 * Xilinx, Inc.
 * XILINX IS PROVIDING THIS DESIGN, CODE, OR INFORMATION "AS IS" AS A
 * COURTESY TO YOU.  BY PROVIDING THIS DESIGN, CODE, OR INFORMATION AS
 * ONE POSSIBLE   IMPLEMENTATION OF THIS FEATURE, APPLICATION OR
 * STANDARD, XILINX IS MAKING NO REPRESENTATION THAT THIS IMPLEMENTATION
 * IS FREE FROM ANY CLAIMS OF INFRINGEMENT, AND YOU ARE RESPONSIBLE
 * FOR OBTAINING ANY RIGHTS YOU MAY REQUIRE FOR YOUR IMPLEMENTATION.
 * XILINX EXPRESSLY DISCLAIMS ANY WARRANTY WHATSOEVER WITH RESPECT TO
 * THE ADEQUACY OF THE IMPLEMENTATION, INCLUDING BUT NOT LIMITED TO
 * ANY WARRANTIES OR REPRESENTATIONS THAT THIS IMPLEMENTATION IS FREE
 * FROM CLAIMS OF INFRINGEMENT, IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

/*
 * helloworld.c: simple test application
 */

#include <stdio.h>
//#include "platform.h"
#include "xaxicdma.h"
#include "xdebug.h"
#include "xil_exception.h"
#include "xil_cache.h"
#include "xparameters.h"
#include "xscugic.h"

#define NUMBER_OF_TRANSFERS	2	/* Number of simple transfers to do */
#define DMA_CTRL_DEVICE_ID 	XPAR_AXICDMA_0_DEVICE_ID
#define INT_CTRL_DEVICE_ID	XPAR_SCUGIC_SINGLE_DEVICE_ID
#define DMA_CTRL_IRPT_INTR	XPAR_FABRIC_AXI_CDMA_0_CDMA_INTROUT_INTR

volatile static int Done = 0;	/* Dma transfer is done */
volatile static int Error = 0;	/* Dma Bus Error occurs */

static u32 SourceAddr 	= 0x08000000;
static u32 DestAddr 	= 0x0C000000;
static u32 MemSize		= 0x04000000;

static XAxiCdma instAxiCdma;	/* Instance of the XAxiCdma */
static XScuGic instIntCtrl;		/* Instance of the Interrupt Controller */



static int Array_3[32][16];
static int Array_4[32][16];

static int Array_1[32][16];
static int Array_2[32][16];

int const input[16] = {0xb504f33, 0xabeb4a0, 0xa267994, 0x987fbfc, 0x8e39d9c, 0x839c3cc, 0x78ad74c, 0x6d743f4, 0x61f78a8, 0x563e6a8, 0x4a5018c, 0x3e33f2c, 0x31f1704, 0x259020c, 0x1917a64, 0xc8fb2c};
/* Length of the buffers for DMA transfer */
static u32 BUFFER_BYTESIZE	= (XPAR_AXI_CDMA_0_M_AXI_DATA_WIDTH * XPAR_AXI_CDMA_0_M_AXI_MAX_BURST_LEN * 2);
static int CDMATransfer(XAxiCdma *InstancePtr, int Length, int Retries);

//Multiply and Shift
int MUL_SHIFT_30(int x, int y)
{
  return ((int) (((long long) (x) * (y)) >> 30));
}


void MULT_SHIFT_LOOP(int Value )
{
   int i, j;
   for (i = 0; i < 32; i++) {
	   //printf("---MSL %i\n\r", i);

      for (j = 0; j < 16; j++) {

    	  Array_3[i][j] = (int)((MUL_SHIFT_30(input[j],Array_1[j][i])) + Value);
    	  Array_4[i][j] = (int)((MUL_SHIFT_30(input[j],Array_2[j][i])) + Value);
      }
   }
}

void TestPattern_Initialization(void)
{
	 int i, j;
	   for (i = 0; i < 32; i++)
	   {
	      for (j = 0; j < 16; j++)
	      {
	    	  Array_1[i][j] =  (int ) ((0xA5A5A5A5 >> 1) * i );
	    	  Array_2[i][j] =  (int ) ((0xA5A5A5A5 << 1) * i );
	      }
	   }
}



static int SetupIntrSystem(XScuGic *pInst_IntCtrl, XAxiCdma *pInst_cdma, u32 ID_IntCtrl)
{
	int Status;

	/*
	 * Initialize the interrupt controller driver
	 */
	XScuGic_Config *pIntCtrl_Config;

	/*
	 * Initialize the interrupt controller driver so that it is ready to
	 * use.
	 */
	pIntCtrl_Config = XScuGic_LookupConfig(INT_CTRL_DEVICE_ID);
	if (NULL == pIntCtrl_Config) {
		return XST_FAILURE;
	}

	Status = XScuGic_CfgInitialize(pInst_IntCtrl, pIntCtrl_Config, pIntCtrl_Config->CpuBaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	// 0xA0 --> priority; 0x03: trigger mode (probably rising edge)
	XScuGic_SetPriorityTriggerType(pInst_IntCtrl, ID_IntCtrl, 0xA0, 0x3);

	/*
	 * Connect the device driver handler that will be called when an
	 * interrupt for the device occurs, the handler defined above performs
	 * the specific interrupt processing for the device.
	 */
	Status = XScuGic_Connect(pInst_IntCtrl, ID_IntCtrl, (Xil_InterruptHandler)XAxiCdma_IntrHandler, pInst_cdma);
	if (Status != XST_SUCCESS) {
		return Status;
	}

	/*
	 * Enable the interrupt for the DMA device.
	 */
	XScuGic_Enable(pInst_IntCtrl, ID_IntCtrl);

	/*
	 * Connect the interrupt controller interrupt handler to the hardware
	 * interrupt handling logic in the processor.
	 */
	Xil_ExceptionInit();
	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_IRQ_INT, (Xil_ExceptionHandler)XScuGic_InterruptHandler, pInst_IntCtrl);


	/*
	 * Enable interrupts in the Processor.
	 */
	Xil_ExceptionEnable();


	return XST_SUCCESS;
}


static void DisableIntrSystem(XScuGic *pInst_IntCtrl , u32 ID_IntCtrl)
{
		XScuGic_Disable(pInst_IntCtrl, ID_IntCtrl);
		XScuGic_Disconnect(pInst_IntCtrl, ID_IntCtrl);
}



/*****************************************************************************/
/*
* Callback function for the simple transfer. It is called by the driver's
* interrupt handler.
*
* @param	CallBackRef is the reference pointer registered through
*		transfer submission. In this case, it is the pointer to the
* 		driver instance
* @param	IrqMask is the interrupt mask the driver interrupt handler
*		passes to the callback function.
* @param	IgnorePtr is a pointer that is ignored by simple callback
* 		function
*
* @return	None
*
* @note		None
*
******************************************************************************/
static void Cdma_CallBack(void *CallBackRef, u32 IrqMask, int *IgnorePtr)
{

	if (IrqMask & XAXICDMA_XR_IRQ_ERROR_MASK) {
		Error = TRUE;
		printf("\r\n--- Transfer Error --- \r\n");
	}

	if (IrqMask & XAXICDMA_XR_IRQ_IOC_MASK) {
		printf("\r\n--- Transfer Done --- \r\n");
		Done = TRUE;
	}

}


/*****************************************************************************/
/*
*
* This function does  CDMA transfer
*
* @param	InstancePtr is a pointer to the XAxiCdma instance
* @param	Length is the transfer length
* @param	Retries is how many times to retry on submission
*
* @return
*		- XST_SUCCESS if transfer is successful
*		- XST_FAILURE if either the transfer fails or the data has
*		  error
*
* @note		None
*
******************************************************************************/
static int CDMATransfer(XAxiCdma *InstancePtr, int Length, int Retries)
{

	int Status;

	printf("Start Transfer \n\r");

	/* Try to start the DMA transfer
	 */
	Done = 0;
	Error = 0;

	/* Flush the SrcBuffer before the DMA transfer, in case the Data Cache
	 * is enabled
	 */
	Xil_DCacheFlushRange((u32)SourceAddr, Length);

	Status = XAxiCdma_SimpleTransfer(InstancePtr, (u32)SourceAddr, (u32)DestAddr, Length, Cdma_CallBack, (void *)InstancePtr);

	if (Status == XST_FAILURE) {
		printf("Error in Transfer  \n\r");
		return 1;
	}

	/* Wait until the DMA transfer is done
	 */
	while (!Done && !Error) {
		/* Wait */
	}

	if (Error) {
		return XST_FAILURE;
		return 1;
	}

	/* Invalidate the DestBuffer before receiving the data, in case the
	 * Data Cache is enabled
	 */
	Xil_DCacheInvalidateRange((u32)DestAddr, Length);

	return XST_SUCCESS;
}

int XAxiCdma_Interrupt(XScuGic *pInst_IntCtrl, XAxiCdma *pInst_cdma, u16 DeviceId_cdma, u32 IntNbr_cdma)
{
	{
		XAxiCdma_Config *CfgPtr;
		int Status;
		int SubmitTries = 1;		/* Retry to submit */
		int Tries = NUMBER_OF_TRANSFERS;
		int Index;

		/* Initialize the XAxiCdma device.
		 */
		CfgPtr = XAxiCdma_LookupConfig(DeviceId_cdma);
		if (!CfgPtr) {
			return XST_FAILURE;
		}

		Status = XAxiCdma_CfgInitialize(pInst_cdma, CfgPtr, CfgPtr->BaseAddress);
		if (Status != XST_SUCCESS) {
			return XST_FAILURE;
		}

		/* Setup the interrupt system
		 */
		Status = SetupIntrSystem(pInst_IntCtrl, pInst_cdma, IntNbr_cdma);
		if (Status != XST_SUCCESS) {
			return XST_FAILURE;
		}

		/* Enable all (completion/error/delay) interrupts
		 */
		XAxiCdma_IntrEnable(pInst_cdma, XAXICDMA_XR_IRQ_ALL_MASK);

		for (Index = 0; Index < Tries; Index++) {
			Status = CDMATransfer(pInst_cdma, BUFFER_BYTESIZE, SubmitTries);

			if(Status != XST_SUCCESS) {
				DisableIntrSystem(pInst_IntCtrl, IntNbr_cdma);
				return XST_FAILURE;
			}
		}

		/* Test finishes successfully, clean up and return
		 */
		DisableIntrSystem(pInst_IntCtrl, IntNbr_cdma);

		return XST_SUCCESS;
	}
}




int main()
{
	int Status;
	u32  *SrcPtr;
	u32  *DestPtr;
	unsigned int  Index;
	//int i, j;

    printf("\r\n--- Entering main() --- \r\n");

	/*********************************************************************************
		Step : 1 : Initialization of the Source Memory with the Specified test pattern
			   	   Clear Destination memory
	**********************************************************************************/
    TestPattern_Initialization();
    printf("Test pattern created\r\n");

    /* Initialize the source buffer bytes with a pattern and the
    	 * the destination buffer bytes to zero
   	 */
   	SrcPtr = (u32*)SourceAddr;
   	DestPtr = (u32 *)DestAddr;
   	printf("BUFFER_BYTESIZE %i\n\r", (int)BUFFER_BYTESIZE);


   	for (Index = 0; Index < MemSize/4; Index++) {
   		SrcPtr[Index] = Index;
   		DestPtr[Index] = 0;
   		if((Index & 0x0000ffff) == 0) {
   			printf("Index: %08x SrcPtr: %08x \n\r", Index, (int)(&SrcPtr[Index]));
   		}
   	}

/*
   	for (Index = 0; Index < (BUFFER_BYTESIZE/1024); Index++)
   	{
   		printf("...Index: %i\n\r", Index);
   		MULT_SHIFT_LOOP((Index*100));
   		for (i = 0; i < 32; i++)
   		{
   	   		//printf("%i..", (int)i);
   			for (j = 0; j < 16; j++)
   			{
   				SrcPtr[((i+j)*(Index+1))] 		= Array_3[i][j];
   				SrcPtr[((i+j)*(Index+1)) + 1] 	= Array_4[i][j];
   				DestPtr[((i+j)*(Index+1))] 		= 0;
   				DestPtr[((i+j)*(Index+1)) + 1]  = 0;
   			}

   		}

   	}
*/
   	printf("Source buffer filled\n\r");
	/*********************************************************************************
		Step : 2 : AXI CDMA Initialization
				   Association of the CDMA ISR with the Interrupt
				   Enable the CDMA Interrupt
			   	   Provide Source and destination location to CDMA
			   	   Specified Number of byte to be transfer to CDMA register
			       Start the CDMA
			   	   Wait for the Interrupt and return the status
	**********************************************************************************/

    Status = XAxiCdma_Interrupt( &instIntCtrl, &instAxiCdma, DMA_CTRL_DEVICE_ID, DMA_CTRL_IRPT_INTR);

   	if (Status != XST_SUCCESS) {

    		printf("XAxiCdma_Interrupt: Failed\r\n");
    		return XST_FAILURE;
    	}

    	xil_printf("XAxiCdma_Interrupt: Passed\r\n");


    /*********************************************************************************
		Step : 3 : Compare Source memory with Destination memory
				   Return the Status
	**********************************************************************************/
    	for (Index = 0; Index < (BUFFER_BYTESIZE/4); Index++)
    	{
    		if ( DestPtr[Index] != SrcPtr[Index])
    		{
    			printf("Error in Comparison : Index : %x \n\r", Index);
    			return XST_FAILURE;
    		}
    	}

    	printf("DMA Transfer is Successful \n\r");
    	return XST_SUCCESS;


    return 0;
}





/*
 *
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *
 */


#ifndef _STORVSC_API_H_
#define _STORVSC_API_H_

#include "VmbusApi.h"


/* Defines */


#define STORVSC_RING_BUFFER_SIZE			10*PAGE_SIZE
#define BLKVSC_RING_BUFFER_SIZE				20*PAGE_SIZE

#define STORVSC_MAX_IO_REQUESTS				64

/*
 * In Hyper-V, each port/path/target maps to 1 scsi host adapter.  In
 * reality, the path/target is not used (ie always set to 0) so our
 * scsi host adapter essentially has 1 bus with 1 target that contains
 * up to 256 luns.
 */

#define STORVSC_MAX_LUNS_PER_TARGET			64
#define STORVSC_MAX_TARGETS					1
#define STORVSC_MAX_CHANNELS				1


/* Fwd decl */

/* struct VMBUS_CHANNEL; */
typedef struct _STORVSC_REQUEST* PSTORVSC_REQUEST;


/* Data types */

typedef int (*PFN_ON_IO_REQUEST)(struct hv_device *Device, PSTORVSC_REQUEST Request);
typedef void (*PFN_ON_IO_REQUEST_COMPLTN)(PSTORVSC_REQUEST Request);

typedef int (*PFN_ON_HOST_RESET)(struct hv_device *Device);
typedef void (*PFN_ON_HOST_RESCAN)(struct hv_device *Device);


/* Matches Windows-end */
typedef enum _STORVSC_REQUEST_TYPE{
	WRITE_TYPE,
	READ_TYPE,
	UNKNOWN_TYPE,
} STORVSC_REQUEST_TYPE;


typedef struct _STORVSC_REQUEST {
	STORVSC_REQUEST_TYPE		Type;
	u32					Host;
	u32					Bus;
	u32					TargetId;
	u32					LunId;
	u8 *					Cdb;
	u32					CdbLen;
	u32					Status;
	u32					BytesXfer;

	unsigned char*					SenseBuffer;
	u32					SenseBufferSize;

	void *					Context;

	PFN_ON_IO_REQUEST_COMPLTN	OnIOCompletion;

	/* This points to the memory after DataBuffer */
	void *					Extension;

	MULTIPAGE_BUFFER		DataBuffer;
} STORVSC_REQUEST;


/* Represents the block vsc driver */
typedef struct _STORVSC_DRIVER_OBJECT {
	DRIVER_OBJECT			Base; /* Must be the first field */

	/* Set by caller (in bytes) */
	u32					RingBufferSize;

	/* Allocate this much private extension for each I/O request */
	u32					RequestExtSize;

	/* Maximum # of requests in flight per channel/device */
	u32					MaxOutstandingRequestsPerChannel;

	/* Set by the caller to allow us to re-enumerate the bus on the host */
	PFN_ON_HOST_RESCAN		OnHostRescan;

	/* Specific to this driver */
	PFN_ON_IO_REQUEST		OnIORequest;
	PFN_ON_HOST_RESET		OnHostReset;

} STORVSC_DRIVER_OBJECT;

typedef struct _STORVSC_DEVICE_INFO {
	unsigned int	PortNumber;
    unsigned char	PathId;
    unsigned char	TargetId;
} STORVSC_DEVICE_INFO;


/* Interface */

int
StorVscInitialize(
	DRIVER_OBJECT	*Driver
	);

int
BlkVscInitialize(
	DRIVER_OBJECT	*Driver
	);
#endif /* _STORVSC_API_H_ */

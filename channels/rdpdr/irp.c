/* -*- c-basic-offset: 8 -*-
   FreeRDP: A Remote Desktop Protocol client.
   Device Redirection - Interrupt Request Packet (IRP) Processing

   Copyright (C) Marc-Andre Moreau <marcandre.moreau@gmail.com> 2009

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rdpdr_types.h"
#include "rdpdr_constants.h"
#include "irp.h"

static void
irp_construct_common_response(IRP * irp)
{
	int size;
	char * data;

	size = irp->outputBufferLength + 4;
	data = malloc(size);
	memset(data, 0, size);
	SET_UINT32(data, 0, irp->outputBufferLength);
	if (irp->outputBufferLength > 0)
		memcpy(data + 4, irp->outputBuffer, irp->outputBufferLength);
	if (irp->outputBuffer)
		free(irp->outputBuffer);
	irp->outputBuffer = data;
	irp->outputBufferLength = size;
}

void
irp_output_device_io_completion_header(IRP* irp, char* data, int data_size)
{
	if(data_size < 16)
		return;

	SET_UINT16(data, 0, RDPDR_CTYP_CORE); /* component */
	SET_UINT16(data, 2, PAKID_CORE_DEVICE_IOCOMPLETION); /* packetID */
	SET_UINT32(data, 4, irp->dev->id); /* deviceID */
	SET_UINT32(data, 8, irp->completionID); /* completionID */
	SET_UINT32(data, 12, irp->ioStatus); /* ioStatus */
}

void
irp_process_create_request(IRP* irp, char* data, int data_size)
{
	uint32 pathLength;
	char * path;
	int size;
	uint8 information;

	irp->desiredAccess = GET_UINT32(data, 0); /* desiredAccess */
	//irp->allocationSize = GET_UINT64(data, 4); /* allocationSize */
	irp->fileAttributes = GET_UINT32(data, 12); /* fileAttributes */
	irp->sharedAccess = GET_UINT32(data, 16); /* sharedAccess */
	irp->createDisposition = GET_UINT32(data, 20); /* createDisposition */
	irp->createOptions = GET_UINT32(data, 24); /* createOptions */
	pathLength = GET_UINT32(data, 28); /* pathLength */

	size = pathLength * 3 / 2 + 1;
	path = (char *) malloc(size);
	memset(path, 0, size);
	if (pathLength > 0)
	{
		get_wstr(path, size, &data[32], pathLength);
	}

	if (!irp->dev->service->create)
	{
		irp->ioStatus = RD_STATUS_NOT_SUPPORTED;
	}
	else
	{
		irp->ioStatus = irp->dev->service->create(irp, path);
	}
	free(path);

	/* construct create response */
	irp->outputBufferLength = 5;
	irp->outputBuffer = malloc(irp->outputBufferLength);

	switch (irp->createDisposition)
	{
	case FILE_SUPERSEDE:
	case FILE_OPEN:
	case FILE_CREATE:
	case FILE_OVERWRITE:
		information = FILE_SUPERSEDED;
		break;
	case FILE_OPEN_IF:
		information = FILE_OPENED;
		break;
	case FILE_OVERWRITE_IF:
		information = FILE_OVERWRITTEN;
		break;
	default:
		information = 0;
		break;
	}

	SET_UINT32(irp->outputBuffer, 0, irp->fileID); /* fileID */
	SET_UINT8(irp->outputBuffer, 4, information); /* information */
}

void
irp_process_close_request(IRP* irp, char* data, int data_size)
{
	/* 32-byte pad */
	if (!irp->dev->service->close)
	{
		irp->ioStatus = RD_STATUS_NOT_SUPPORTED;
	}
	else
	{
		irp->ioStatus = irp->dev->service->close(irp);
	}

	/* construct close response */
	irp->outputBufferLength = 5;
	irp->outputBuffer = malloc(irp->outputBufferLength);

	SET_UINT32(irp->outputBuffer, 0, 0); /* Padding */
	SET_UINT8(irp->outputBuffer, 4, 0); /* Padding */
}

void
irp_process_read_request(IRP* irp, char* data, int data_size)
{
	irp->length = GET_UINT32(data, 0); /* length */
	irp->offset = GET_UINT64(data, 4); /* offset */
	/* 20-byte pad */

	if (!irp->dev->service->read)
	{
		irp->ioStatus = RD_STATUS_NOT_SUPPORTED;
	}
	else
	{
		irp->ioStatus = irp->dev->service->read(irp);
	}
	irp_construct_common_response(irp);
}

void
irp_process_write_request(IRP* irp, char* data, int data_size)
{
	irp->length = GET_UINT32(data, 0); /* length */
	irp->offset = GET_UINT64(data, 4); /* offset */
	/* 20-byte pad */
	irp->inputBuffer = data + 32;
	irp->inputBufferLength = irp->length;

	if (!irp->dev->service->write)
	{
		irp->ioStatus = RD_STATUS_NOT_SUPPORTED;
	}
	else
	{
		irp->ioStatus = irp->dev->service->write(irp);
	}
	if (irp->ioStatus == RD_STATUS_SUCCESS)
	{
		irp->outputBufferLength = 5;
		irp->outputBuffer = malloc(5);
		SET_UINT32(irp->outputBuffer, 0, irp->length);
		/* [MS-RDPEFS] says this is an optional padding, but unfortunately it's required! */
		SET_UINT8(irp->outputBuffer, 4, 0);
	}
	else
	{
		irp_construct_common_response(irp);
	}
}

void
irp_process_query_volume_information_request(IRP* irp, char* data, int data_size)
{
	irp->infoClass = GET_UINT32(data, 0); /* fsInformationClass */
	irp->inputBufferLength = GET_UINT32(data, 4); /* length */
	/* 24-byte pad */
	
	/* queryVolumeBuffer */
	irp->inputBuffer = data + 32;

	if (!irp->dev->service->query_volume_info)
	{
		irp->ioStatus = RD_STATUS_NOT_SUPPORTED;
	}
	else
	{
		irp->ioStatus = irp->dev->service->query_volume_info(irp);
	}
	irp_construct_common_response(irp);
}

void
irp_process_set_volume_information_request(IRP* irp, char* data, int data_size)
{
#if 0
	uint32 fsInformationClass;
	uint32 length;

	fsInformationClass = GET_UINT32(data, 0); /* fsInformationClass */
	length = GET_UINT32(data, 4); /* length */
	/* 24-byte pad */
	
	/* setVolumeBuffer */
#endif
}

void
irp_process_query_information_request(IRP* irp, char* data, int data_size)
{
	irp->infoClass = GET_UINT32(data, 0); /* fsInformationClass */
	irp->inputBufferLength = GET_UINT32(data, 4); /* length */
	/* 24-byte pad */

	/* QueryBuffer */
	irp->inputBuffer = data + 32;

	if (!irp->dev->service->query_info)
	{
		irp->ioStatus = RD_STATUS_NOT_SUPPORTED;
	}
	else
	{
		irp->ioStatus = irp->dev->service->query_info(irp);
	}
	irp_construct_common_response(irp);
}

void
irp_process_set_information_request(IRP* irp, char* data, int data_size)
{
#if 0
	uint32 fsInformationClass;
	uint32 length;

	fsInformationClass = GET_UINT32(data, 0); /* fsInformationClass */
	length = GET_UINT32(data, 4); /* length */
	/* 24-byte pad */
	
	/* setBuffer */
#endif
}

void
irp_process_directory_control_request(IRP* irp, char* data, int data_size)
{
	switch(irp->minorFunction)
	{
		case IRP_MN_QUERY_DIRECTORY:
			LLOGLN(0, ("IRP_MN_QUERY_DIRECTORY"));
			irp_process_query_directory_request(irp, data, data_size);
			break;
		
		case IRP_MN_NOTIFY_CHANGE_DIRECTORY:
			LLOGLN(0, ("IRP_MN_NOTIFY_CHANGE_DIRECTORY"));
			irp_process_notify_change_directory_request(irp, data, data_size);
			break;

		default:
			LLOGLN(0, ("IRP majorFunction=0x%x minorFunction=0x%x", irp->majorFunction, irp->minorFunction));
			irp->ioStatus = RD_STATUS_INVALID_PARAMETER;
			return;
	}
}

void
irp_process_device_control_request(IRP* irp, char* data, int data_size)
{
	//irp->outputBufferLength = GET_UINT32(data, 0); /* outputBufferLength */
	irp->inputBufferLength = GET_UINT32(data, 4); /* inputBufferLength */
	irp->ioControlCode = GET_UINT32(data, 8); /* ioControlCode */
	/* 20-byte pad */

	/* inputBuffer */
	irp->inputBuffer = data + 32;

	if (!irp->dev->service->control)
	{
		irp->ioStatus = RD_STATUS_NOT_SUPPORTED;
	}
	else
	{
		irp->ioStatus = irp->dev->service->control(irp);
	}
	irp_construct_common_response(irp);
}

void
irp_process_file_lock_control_request(IRP* irp, char* data, int data_size)
{
	uint32 numLocks;

	irp->operation = GET_UINT32(data, 0); /* operation */
	irp->waitOperation = GET_UINT8(data, 4); /* f (first bit) */
	/* pad (f + pad = 32 bits) */
	numLocks = GET_UINT32(data, 8); /* numLocks */
	irp->inputBufferLength = numLocks * 16; /* sizeof(RDP_LOCK_INFO) */
	/* 20-byte pad */
	irp->inputBuffer = data + 32;

	if (!irp->dev->service->lock_control)
	{
		irp->ioStatus = RD_STATUS_NOT_SUPPORTED;
	}
	else
	{
		irp->ioStatus = irp->dev->service->lock_control(irp);
	}
	irp_construct_common_response(irp);
}

void
irp_process_query_directory_request(IRP* irp, char* data, int data_size)
{
	uint8 initialQuery;
	uint32 pathLength;
	int size;
	char * path;

	irp->infoClass = GET_UINT32(data, 0); /* fsInformationClass */
	initialQuery = GET_UINT8(data, 4); /* initialQuery */
	pathLength = GET_UINT32(data, 5); /* pathLength */
	/* 23-byte pad */

	size = pathLength * 3 / 2 + 1;
	path = (char *) malloc(size);
	memset(path, 0, size);
	if (pathLength > 0)
	{
		get_wstr(path, size, &data[32], pathLength);
	}

	if (!irp->dev->service->query_directory)
	{
		irp->ioStatus = RD_STATUS_NOT_SUPPORTED;
	}
	else
	{
		irp->ioStatus = irp->dev->service->query_directory(irp, initialQuery, path);
	}
	free(path);

	if (irp->ioStatus == RD_STATUS_NO_MORE_FILES)
	{
		/* [MS-RDPEFS] said it's an optional padding, however it's *required* for this last query!!! */
		irp->outputBuffer = malloc(5);
		memset(irp->outputBuffer, 0, 5);
		irp->outputBufferLength = 5;
	}
	else
	{
		irp_construct_common_response(irp);
	}
}

void
irp_process_notify_change_directory_request(IRP* irp, char* data, int data_size)
{
	irp->watchTree = GET_UINT8(data, 0); /* watchQuery */
	irp->completionFilter = GET_UINT32(data, 1); /* completionQuery */
	/* 27-byte pad */

	if (!irp->dev->service->notify_change_directory)
	{
		irp->ioStatus = RD_STATUS_NOT_SUPPORTED;
	}
	else
	{
		irp->ioStatus = irp->dev->service->notify_change_directory(irp);
	}
}



#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void finished() {
	printf("\nPress (Start) to reboot...\n");
	while (aptMainLoop())
	{
		gspWaitForVBlank();
		hidScanInput();

		u32 kDown = hidKeysDown();
		if (kDown & KEY_START) break;
	}
	consoleClear();
	gfxExit();
	svcKernelSetState(7); // after killing fs, APT_HardwareResetAsync() will errdisp so we use svcKernelSetState(7) instead
}

void manageSave(bool restore){
	Result res = 0;
	printf("Terminating the fs module...\n");
	Handle fshandle;

	res = svcOpenProcess(&fshandle, 0); // fs is always pid 0
	if(R_FAILED(res)){
		printf("\x1b[31mFailed to open fs process\x1b[0m: 0x%08x.\n", (unsigned int)res);
		return;
	}

	res = svcTerminateProcess(fshandle); // ns:s cannot terminate firm modules
	if(R_FAILED(res)){
		printf("\x1b[31mFailed to terminate fs process\x1b[0m: 0x%08x.\n", (unsigned int)res);
		return;
	}

	res = svcCloseHandle(fshandle);
	if(R_FAILED(res)){
		printf("\x1b[31mFailed to close fs handle\x1b[0m: 0x%08x.\n", (unsigned int)res);
		return;
	}

	svcSleepThread(100000000LL);

	printf("Waiting for fs to terminate...\n");
	svcSleepThread(100000000LL);
	while(1){
		res = svcOpenProcess(&fshandle, 0); // fs is always pid 0
		if(R_FAILED(res)){
			printf("Terminated fs!\n\n");
			break;
		}
		res = svcCloseHandle(fshandle);
		if(R_FAILED(res)){
			printf("\x1b[31mFailed to close fs handle\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}
		svcSleepThread(100000000LL);
	}

	u32 sdmcFileHandleLower;
	u32 sdmcFileHandleUpper;
	u32 sysdataFileHandleLower;
	u32 sysdataFileHandleUpper;

	u32 sdmcArchiveHandleLower;
	u32 sdmcArchiveHandleUpper;
	u32 sysdataArchiveHandleLower;
	u32 sysdataArchiveHandleUpper;

	const char sysdata_archive_path[] = { 0, 0, 0, 0 }; //sysdata save archive

	const char sdmc_file_path[] = "/nimsavegame.bin";
	const char sysdata_file_path[] = { 0x2c, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 }; // `0001002C/00000000/`, nim savegame file; binary path, reversed byte order because endians
	//const char sysdata_file_path[] = { 0x11, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 }; // `00010011/00000000/`, fs savegame file; binary path, reversed byte order because endians

	u64 sdmcFileSize;
	u64 sdmcFileOffset;
	u8* sdmcFileBuffer;

	u64 sysdataFileSize;
	u64 sysdataFileOffset;
	u8* sysdataFileBuffer;

	printf("Getting PxiFS0 handle...\n\n");
	Handle pxifs0handle;

	res = srvGetServiceHandle(&pxifs0handle, "PxiFS0");
	if(R_FAILED(res)){
		printf("\x1b[31mFailed to get PxiFS0 handle\x1b[0m: 0x%08x.\n", (unsigned int)res);
		return;
	}

	// open sysdata archive with fspxi
	printf("Opening sysdata archive...\n");

	u32 *cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = IPC_MakeHeader(0x12,3,2); // 0x1200C2, FSPXI:OpenArchive according to 3dbrew
	cmdbuf[1] = ARCHIVE_SYSTEM_SAVEDATA2;
	cmdbuf[2] = PATH_BINARY;
	cmdbuf[3] = sizeof(sysdata_archive_path);
	cmdbuf[4] = IPC_Desc_PXIBuffer(sizeof(sysdata_archive_path), 0, 1);
	cmdbuf[5] = (uintptr_t) sysdata_archive_path;

	res = svcSendSyncRequest(pxifs0handle);
	if(R_FAILED(res)){
		printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
		return;
	}

	res = cmdbuf[1];
	if(R_FAILED(res)){
		printf("\x1b[31mFailed to open sysdata archive\x1b[0m: 0x%08x.\n", (unsigned int)res);
		return;
	}

	sysdataArchiveHandleLower = cmdbuf[2];
	sysdataArchiveHandleUpper = cmdbuf[3];

	cmdbuf = getThreadCommandBuffer();

	printf("Opening sdmc archive...\n\n");

	cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = IPC_MakeHeader(0x12,3,2); // 0x1200C2, FSPXI:OpenArchive according to 3dbrew
	cmdbuf[1] = ARCHIVE_SDMC;
	cmdbuf[2] = PATH_EMPTY;
	cmdbuf[3] = 1;
	cmdbuf[4] = IPC_Desc_PXIBuffer(1, 0, 1);
	cmdbuf[5] = (uintptr_t) "";

	res = svcSendSyncRequest(pxifs0handle);
	if(R_FAILED(res)){
		printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
		return;
	}

	res = cmdbuf[1];
	if(R_FAILED(res)){
		printf("\x1b[31mFailed to open sdmc archive\x1b[0m: 0x%08x.\n", (unsigned int)res);
		return;
	}

	sdmcArchiveHandleLower = cmdbuf[2];
	sdmcArchiveHandleUpper = cmdbuf[3];

	cmdbuf = getThreadCommandBuffer();

	if(restore){
		printf("Restoring nim savegame...\n\n");
		printf("From: nand://data/<id0>/sysdata/0001002c/00000000\n");
		printf("To: smdc://nimsavegame.bin\n\n");

		printf("Opening nim savegame backup...\n");

		cmdbuf[0] = IPC_MakeHeader(0x1,7,2); // 0x101C2, FSPXI:OpenFile according to 3dbrew
		cmdbuf[1] = 0;
		cmdbuf[2] = sdmcArchiveHandleLower;
		cmdbuf[3] = sdmcArchiveHandleUpper;
		cmdbuf[4] = PATH_ASCII;
		cmdbuf[5] = strlen(sdmc_file_path)+1;
		cmdbuf[6] = 0x01; // read
		cmdbuf[7] = 0;
		cmdbuf[8] = IPC_Desc_PXIBuffer(strlen(sdmc_file_path)+1, 0, 1);
		cmdbuf[9] = (uintptr_t) sdmc_file_path;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mCould not open nim savegame backup\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		sdmcFileHandleLower = cmdbuf[2];
		sdmcFileHandleUpper = cmdbuf[3];

		cmdbuf = getThreadCommandBuffer();

		printf("Opening nim savegame...\n");

		// nim creates this file if it does not exist
		// we should never have to create it ourselves

		cmdbuf[0] = IPC_MakeHeader(0x1,7,2); // 0x101C2, FSPXI:OpenFile according to 3dbrew
		cmdbuf[1] = 0;
		cmdbuf[2] = sysdataArchiveHandleLower;
		cmdbuf[3] = sysdataArchiveHandleUpper;
		cmdbuf[4] = PATH_BINARY;
		cmdbuf[5] = sizeof(sysdata_file_path);
		cmdbuf[6] = 0x02; // write
		cmdbuf[7] = 0;
		cmdbuf[8] = IPC_Desc_PXIBuffer(sizeof(sysdata_file_path), 0, 1);
		cmdbuf[9] = (uintptr_t) sysdata_file_path;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mCould not open nim savegame\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		sysdataFileHandleLower = cmdbuf[2];
		sysdataFileHandleUpper = cmdbuf[3];

		cmdbuf = getThreadCommandBuffer();

		printf("Getting nim savegame backup size...\n");

		cmdbuf[0] = IPC_MakeHeader(0xD,2,0); // 0xD0080, FSPXI:GetFileSize according to 3dbrew
		cmdbuf[1] = sdmcFileHandleLower;
		cmdbuf[2] = sdmcFileHandleUpper;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mCould not get nim savegame backup size\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		sdmcFileSize = cmdbuf[2] | ((u64) cmdbuf[3] << 32);

		cmdbuf = getThreadCommandBuffer();

		printf("Reading nim savegame backup to buffer...\n\n");

		sdmcFileOffset = 0;

		sdmcFileBuffer = (u8 *) malloc(sdmcFileSize);
		memset(sdmcFileBuffer, 0, sdmcFileSize);

		cmdbuf[0] = IPC_MakeHeader(0x9,5,2); // 0x90142, FSPXI:ReadFile according to 3dbrew
		cmdbuf[1] = sdmcFileHandleLower;
		cmdbuf[2] = sdmcFileHandleUpper;
		cmdbuf[3] = (u32) sdmcFileOffset;
		cmdbuf[4] = (u32) (sdmcFileOffset >> 32);
		cmdbuf[5] = sdmcFileSize;
		cmdbuf[6] = IPC_Desc_PXIBuffer(sdmcFileSize, 0, 0);
		cmdbuf[7] = (uintptr_t) sdmcFileBuffer;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mCould not read nim savegame backup to buffer\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		cmdbuf = getThreadCommandBuffer();

		printf("Writing nim savegame backup to nand...\n");

		cmdbuf[0] = IPC_MakeHeader(0xB,6,2); // 0xB0182, FSPXI:WriteFile according to 3dbrew
		cmdbuf[1] = sysdataFileHandleLower;
		cmdbuf[2] = sysdataFileHandleUpper;
		cmdbuf[3] = (u32) sdmcFileOffset;
		cmdbuf[4] = (u32) (sdmcFileOffset >> 32);
		cmdbuf[5] = 0; // FLUSH_FLAGS
		cmdbuf[6] = sdmcFileSize;
		cmdbuf[7] = IPC_Desc_PXIBuffer(sdmcFileSize, 0, 1);
		cmdbuf[8] = (uintptr_t) sdmcFileBuffer;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mCould not write nim savegame backup to nand\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		if(cmdbuf[2] != sdmcFileSize){
			printf("\x1b[31mSize written does not match nim savegame backup size\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		free(sdmcFileBuffer);

		cmdbuf = getThreadCommandBuffer();

		printf("Closing nim savegame...\n");

		cmdbuf[0] = IPC_MakeHeader(0xF,2,0); // 0xF0080, FSPXI:CloseFile according to 3dbrew
		cmdbuf[1] = sysdataFileHandleLower;
		cmdbuf[2] = sysdataFileHandleUpper;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mCould not close nim savegame\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		cmdbuf = getThreadCommandBuffer();

		printf("Closing nim savegame backup...\n");

		cmdbuf[0] = IPC_MakeHeader(0xF,2,0); // 0xF0080, FSPXI:CloseFile according to 3dbrew
		cmdbuf[1] = sdmcFileHandleLower;
		cmdbuf[2] = sdmcFileHandleUpper;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mCould not close nim savegame backup\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		cmdbuf = getThreadCommandBuffer();

		printf("Closing sysdata archive...\n\n");

		cmdbuf[0] = IPC_MakeHeader(0x16,2,0); // 0x160080, FSPXI:CloseArchive according to 3dbrew
		cmdbuf[1] = sysdataArchiveHandleLower;
		cmdbuf[2] = sysdataArchiveHandleUpper;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mFailed to close sysdata archive\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		printf("Closing sdmc archive...\n\n");

		cmdbuf[0] = IPC_MakeHeader(0x16,2,0); // 0x160080, FSPXI:OpenArchive according to 3dbrew
		cmdbuf[1] = sdmcArchiveHandleLower;
		cmdbuf[2] = sdmcArchiveHandleUpper;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mFailed to close sdmc archive\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		printf("\x1b[32mSuccessfully restored nim savegame.\x1b[0m\n");

	} else {

		printf("Backing up nim savegame...\n\n");
		printf("From: nand://data/<id0>/sysdata/0001002c/00000000\n");
		printf("To: smdc://nimsavegame.bin\n\n");

		cmdbuf = getThreadCommandBuffer();

		printf("Opening nim savegame...\n");

		cmdbuf[0] = IPC_MakeHeader(0x1,7,2); // 0x101C2, FSPXI:OpenFile according to 3dbrew
		cmdbuf[1] = 0;
		cmdbuf[2] = sysdataArchiveHandleLower;
		cmdbuf[3] = sysdataArchiveHandleUpper;
		cmdbuf[4] = PATH_BINARY;
		cmdbuf[5] = sizeof(sysdata_file_path);
		cmdbuf[6] = 0x03;//0x01; // read
		cmdbuf[7] = 0;
		cmdbuf[8] = IPC_Desc_PXIBuffer(sizeof(sysdata_file_path), 0, 1);
		cmdbuf[9] = (uintptr_t) sysdata_file_path;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mCould not open nim savegame\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		sysdataFileHandleLower = cmdbuf[2];
		sysdataFileHandleUpper = cmdbuf[3];

		cmdbuf = getThreadCommandBuffer();

		printf("Getting nim savegame size...\n");

		cmdbuf[0] = IPC_MakeHeader(0xD,2,0); // 0xD0080, FSPXI:GetFileSize according to 3dbrew
		cmdbuf[1] = sysdataFileHandleLower;
		cmdbuf[2] = sysdataFileHandleUpper;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mCould not get nim savegame size\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		sysdataFileSize = cmdbuf[2] | ((u64) cmdbuf[3] << 32);

		printf("Creating nim savegame backup...\n");

		cmdbuf[0] = IPC_MakeHeader(0x5,8,2); // 0x50202, FSPXI:CreateFile according to 3dbrew
		cmdbuf[1] = 0;
		cmdbuf[2] = sdmcArchiveHandleLower;
		cmdbuf[3] = sdmcArchiveHandleUpper;
		cmdbuf[4] = PATH_ASCII;
		cmdbuf[5] = strlen(sdmc_file_path)+1;
		cmdbuf[6] = 0;
		cmdbuf[7] = (u32) sysdataFileSize;
		cmdbuf[8] = (u32) (sysdataFileSize >> 32);
		cmdbuf[9] = IPC_Desc_PXIBuffer(strlen(sdmc_file_path)+1, 0, 1);
		cmdbuf[10] = (uintptr_t) sdmc_file_path;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];

		if(res == 0xC82044BE){
			printf("\n\x1b[31mFound existing nim savegame backup.\x1b[0m\n");

			printf("Delete? (A) Yes (B) No\n\n");
			while (aptMainLoop()){

				hidScanInput();
				u32 kDown = hidKeysDown();
					if (kDown & KEY_A) {
						cmdbuf = getThreadCommandBuffer();

						// delete nim save with fspxi
						printf("Deleting existing nim savegame backup...\n\n");

						cmdbuf[0] = IPC_MakeHeader(0x2,5,2); // 0x20142, delete file according to 3dbrew; confirmed correct by neobrain
						cmdbuf[1] = 0;
						cmdbuf[2] = sdmcArchiveHandleLower;
						cmdbuf[3] = sdmcArchiveHandleUpper;
						cmdbuf[4] = PATH_ASCII;
						cmdbuf[5] = strlen(sdmc_file_path)+1;
						cmdbuf[6] = IPC_Desc_PXIBuffer(strlen(sdmc_file_path)+1, 0, 1);
						cmdbuf[7] = (uintptr_t) sdmc_file_path;

						res = svcSendSyncRequest(pxifs0handle);
						if(R_FAILED(res)){
							printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
							return;
						}

						res = cmdbuf[1];
						if(R_FAILED(res)){
							printf("\x1b[31mFailed to delete existing nim savegame backup\x1b[0m: 0x%08x.\n", (unsigned int)res);
							return;
						}

						printf("Creating nim savegame backup...\n");

						cmdbuf[0] = IPC_MakeHeader(0x5,8,2); // 0x50202, FSPXI:CreateFile according to 3dbrew
						cmdbuf[1] = 0;
						cmdbuf[2] = sdmcArchiveHandleLower;
						cmdbuf[3] = sdmcArchiveHandleUpper;
						cmdbuf[4] = PATH_ASCII;
						cmdbuf[5] = strlen(sdmc_file_path)+1;
						cmdbuf[6] = 0;
						cmdbuf[7] = (u32) sysdataFileSize;
						cmdbuf[8] = (u32) (sysdataFileSize >> 32);
						cmdbuf[9] = IPC_Desc_PXIBuffer(strlen(sdmc_file_path)+1, 0, 1);
						cmdbuf[10] = (uintptr_t) sdmc_file_path;

						res = svcSendSyncRequest(pxifs0handle);
						if(R_FAILED(res)){
							printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
							return;
						}

						res = cmdbuf[1];
						if(R_FAILED(res)){
							printf("\x1b[31mCould not create nim savegame backup\x1b[0m: 0x%08x.\n", (unsigned int)res);
							return;
						}

						break;

					}
					if (kDown & KEY_B) {
						return;
					}

			}

		}

		if(R_FAILED(res)){
			printf("\x1b[31mCould not create nim savegame backup\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		sdmcFileHandleLower = cmdbuf[2];
		sdmcFileHandleUpper = cmdbuf[3];

		cmdbuf = getThreadCommandBuffer();

		printf("Opening nim savegame backup...\n");

		cmdbuf[0] = IPC_MakeHeader(0x1,7,2); // 0x101C2, FSPXI:OpenFile according to 3dbrew
		cmdbuf[1] = 0;
		cmdbuf[2] = sdmcArchiveHandleLower;
		cmdbuf[3] = sdmcArchiveHandleUpper;
		cmdbuf[4] = PATH_ASCII;
		cmdbuf[5] = strlen(sdmc_file_path)+1;
		cmdbuf[6] = 0x03;
		cmdbuf[7] = 0;
		cmdbuf[8] = IPC_Desc_PXIBuffer(strlen(sdmc_file_path)+1, 0, 1);
		cmdbuf[9] = (uintptr_t) sdmc_file_path;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mCould not open nim savegame backup\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		sdmcFileHandleLower = cmdbuf[2];
		sdmcFileHandleUpper = cmdbuf[3];

		cmdbuf = getThreadCommandBuffer();

		printf("Reading nim savegame to buffer...\n");

		sysdataFileOffset = 0;

		sysdataFileBuffer = (u8 *) malloc(sysdataFileSize);
		memset(sysdataFileBuffer, 0, sysdataFileSize);

		cmdbuf[0] = IPC_MakeHeader(0x9,5,2); // 0x90142, FSPXI:ReadFile according to 3dbrew
		cmdbuf[1] = sysdataFileHandleLower;
		cmdbuf[2] = sysdataFileHandleUpper;
		cmdbuf[3] = (u32) sysdataFileOffset;
		cmdbuf[4] = (u32) (sysdataFileOffset >> 32);
		cmdbuf[5] = sysdataFileSize;
		cmdbuf[6] = IPC_Desc_PXIBuffer(sysdataFileSize, 0, 0);
		cmdbuf[7] = (uintptr_t) sysdataFileBuffer;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mCould not read nim savegame to buffer\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		cmdbuf = getThreadCommandBuffer();

		printf("Writing nim savegame backup to sdmc...\n\n");

		cmdbuf[0] = IPC_MakeHeader(0xB,6,2); // 0xB0182, FSPXI:WriteFile according to 3dbrew
		cmdbuf[1] = sdmcFileHandleLower;
		cmdbuf[2] = sdmcFileHandleUpper;
		cmdbuf[3] = (u32) sysdataFileOffset;
		cmdbuf[4] = (u32) (sysdataFileOffset >> 32);
		cmdbuf[5] = 0; // FLUSH_FLAGS
		cmdbuf[6] = sysdataFileSize;
		cmdbuf[7] = IPC_Desc_PXIBuffer(sysdataFileSize, 0, 1);
		cmdbuf[8] = (uintptr_t) sysdataFileBuffer;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mCould not write nim savegame backup to sdmc\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		if(cmdbuf[2] != sysdataFileSize){
			printf("\x1b[31mSize written does not match nim savegame size\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		free(sysdataFileBuffer);

		printf("\x1b[32mSuccessfully backed up nim savegame.\x1b[0m\n\n");

		cmdbuf = getThreadCommandBuffer();

		printf("Closing nim savegame backup...\n");

		cmdbuf[0] = IPC_MakeHeader(0xF,2,0); // 0xF0080, FSPXI:CloseFile according to 3dbrew
		cmdbuf[1] = sdmcFileHandleLower;
		cmdbuf[2] = sdmcFileHandleUpper;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mCould not close nim savegame backup\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		cmdbuf = getThreadCommandBuffer();

		printf("Zeroing out nim savegame...\n\n");

		sysdataFileBuffer = (u8 *) malloc(sysdataFileSize);
		memset(sysdataFileBuffer, 0, sysdataFileSize);

		cmdbuf[0] = IPC_MakeHeader(0xB,6,2); // 0xB0182, FSPXI:WriteFile according to 3dbrew
		cmdbuf[1] = sysdataFileHandleLower;
		cmdbuf[2] = sysdataFileHandleUpper;
		cmdbuf[3] = (u32) sysdataFileOffset;
		cmdbuf[4] = (u32) (sysdataFileOffset >> 32);
		cmdbuf[5] = 0; // FLUSH_FLAGS
		cmdbuf[6] = sysdataFileSize;
		cmdbuf[7] = IPC_Desc_PXIBuffer(sysdataFileSize, 0, 1);
		cmdbuf[8] = (uintptr_t) sysdataFileBuffer;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mCould zero out nim savegame\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		if(cmdbuf[2] != sysdataFileSize){
			printf("\x1b[31mSize zeroed out does not match nim savegame size\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		free(sysdataFileBuffer);

		cmdbuf = getThreadCommandBuffer();

		printf("Closing nim savegame...\n");

		cmdbuf[0] = IPC_MakeHeader(0xF,2,0); // 0xF0080, FSPXI:CloseFile according to 3dbrew
		cmdbuf[1] = sysdataFileHandleLower;
		cmdbuf[2] = sysdataFileHandleUpper;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mCould not close nim savegame\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		cmdbuf = getThreadCommandBuffer();

		printf("Closing sdmc archive...\n\n");

		cmdbuf[0] = IPC_MakeHeader(0x16,2,0); // 0x160080, FSPXI:OpenArchive according to 3dbrew
		cmdbuf[1] = sdmcArchiveHandleLower;
		cmdbuf[2] = sdmcArchiveHandleUpper;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mFailed to close sdmc archive\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		/*

		// File cannot be deleted because nim keeps an active handle to its savegame at all times
		// Works fine with fs savegame, nim returns 0xc92044fa
		// DESCRIPTION_FAT_OPERATION_DENIED

		cmdbuf = getThreadCommandBuffer();

		printf("Deleting nim savegame...\n\n");

		cmdbuf[0] = IPC_MakeHeader(0x2,5,2); // 0x20142, delete file according to 3dbrew; confirmed correct by neobrain
		cmdbuf[1] = 0;
		cmdbuf[2] = sysdataArchiveHandleLower;
		cmdbuf[3] = sysdataArchiveHandleUpper;
		cmdbuf[4] = PATH_BINARY;
		cmdbuf[5] = sizeof(sysdata_file_path);
		cmdbuf[6] = IPC_Desc_PXIBuffer(sizeof(sysdata_file_path), 0, 1);
		cmdbuf[7] = (uintptr_t) sysdata_file_path;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mFailed to delete nim savegame\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		printf("\x1b[32mSuccessfully deleted nim savegame.\x1b[0m\n\n");

		*/

		cmdbuf = getThreadCommandBuffer();

		printf("Closing sysdata archive...\n");

		cmdbuf[0] = IPC_MakeHeader(0x16,2,0); // 0x160080, FSPXI:CloseArchive according to 3dbrew
		cmdbuf[1] = sysdataArchiveHandleLower;
		cmdbuf[2] = sysdataArchiveHandleUpper;

		res = svcSendSyncRequest(pxifs0handle);
		if(R_FAILED(res)){
			printf("\x1b[31msvcSendSyncRequest() failed\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

		res = cmdbuf[1];
		if(R_FAILED(res)){
			printf("\x1b[31mFailed to close sysdata archive\x1b[0m: 0x%08x.\n", (unsigned int)res);
			return;
		}

	}

}

int main(int argc, char **argv) {
  gfxInitDefault();
  consoleInit(GFX_TOP, NULL);
  printf("nimSM by Plailect\n\n");
	printf("This app requires external k11 hax\n");
	printf("(such as fasthax) to have been run!\n\n");
	printf("(X) Restore nim savegame from backup\n");
	printf("(Y) Backup and zero out nim savegame\n");
	printf("(B) Exit\n");

		while (aptMainLoop()){

			hidScanInput();
			u32 kDown = hidKeysDown();

				if (kDown & KEY_X) {
					consoleClear();
					printf("Restoring nim savegame from backup...\n\n");
					manageSave(true);
					finished();
				}
				if (kDown & KEY_Y) {
					consoleClear();
					printf("Backing up and deleting nim savegame...\n\n");
					manageSave(false);
					finished();
				}
				if (kDown & KEY_B) {
					printf("Exiting...\n");
					break;
				}

		}

	gfxFlushBuffers();
	gfxSwapBuffers();
	gspWaitForVBlank();

  gfxExit();
  return 0;
}

#include <string.h>
#include <malloc.h>
#include "dynamic_libs/os_functions.h"
#include "dynamic_libs/fs_functions.h"
#include "dynamic_libs/sys_functions.h"
#include "dynamic_libs/vpad_functions.h"
#include "dynamic_libs/socket_functions.h"
#include "system/memory.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include "common/common.h"
#include "fs/sd_fat_devoptab.h"
#include <dirent.h>

#define TITLE_TEXT                  "WUP installer (credit to: crediar, Dimok and Yardape8000)"

#define MCP_COMMAND_INSTALL_ASYNC   0x81
#define MAX_INSTALL_PATH_LENGTH     0x27F

char NameFolder[100][100];

typedef struct {
   
	int index;
	int count;
	int pos;
	int pos2;
} Menu;

Menu menupos;



static int installCompleted = 0;
static int installSuccess = 0;
static int installToUsb = 0;
static u32 installError = 0;
static u64 installedTitle = 0;
static int dirNum = 0;
static char installFolder[256] = "";
static char errorText1[128] = "";
static char errorText2[128] = "";
static bool folderSelect[1024] = {false};

static int IosInstallCallback(unsigned int errorCode, unsigned int * priv_data)
{
    installError = errorCode;
    installCompleted = 1;
    return 0;
}

static void InstallTitle(const char *titlePath)
{
    errorText1[0]=0;
    errorText2[0]=0;
    installCompleted = 1;
	//!---------------------------------------------------
    //! This part of code originates from Crediars MCP patcher assembly code
    //! it is just translated to C
    //!---------------------------------------------------
    unsigned int mcpHandle = MCP_Open();
    if(mcpHandle == 0)
    {
        __os_snprintf(errorText1, sizeof(errorText1), "Failed to open MCP.");
        return;
    }

    char text[256];
    unsigned int * mcpInstallInfo = (unsigned int *)OSAllocFromSystem(0x24, 0x40);
    char * mcpInstallPath = (char *)OSAllocFromSystem(MAX_INSTALL_PATH_LENGTH, 0x40);
    unsigned int * mcpPathInfoVector = (unsigned int *)OSAllocFromSystem(0x0C, 0x40);

    do
    {
		if(!mcpInstallInfo || !mcpInstallPath || !mcpPathInfoVector)
        {
            __os_snprintf(errorText1, sizeof(errorText1), "Error: Could not allocate memory.");
            break;
        }

        __os_snprintf(text, sizeof(text), titlePath);

        int result = MCP_InstallGetInfo(mcpHandle, text, mcpInstallInfo);
        if(result != 0)
        {
            __os_snprintf(errorText1, sizeof(errorText1), "Error: MCP_InstallGetInfo 0x%08X", MCP_GetLastRawError());
            __os_snprintf(errorText2, sizeof(errorText2), "Confirm complete WUP files are in the folder. Try power down.");
            break;
        }

        u32 titleIdHigh = mcpInstallInfo[0];
        u32 titleIdLow = mcpInstallInfo[1];
		int spoofFiles = 0;
        if ((titleIdHigh == 00050010)
			&&    ((titleIdLow == 0x10041000)     // JAP Version.bin
				|| (titleIdLow == 0x10041100)     // USA Version.bin
				|| (titleIdLow == 0x10041200)))   // EUR Version.bin
		{
			spoofFiles = 1;
			installToUsb = 0;
		}

        if (spoofFiles
		   || (titleIdHigh == 0x0005000E)     // game update
           || (titleIdHigh == 0x00050000)     // game
           || (titleIdHigh == 0x0005000C)     // DLC
           || (titleIdHigh == 0x00050002))    // Demo
        {
            installedTitle = ((u64)titleIdHigh << 32ULL) | titleIdLow;

			result = MCP_InstallSetTargetDevice(mcpHandle, installToUsb);
			if(result != 0)
			{
				__os_snprintf(errorText1, sizeof(errorText1), "Error: MCP_InstallSetTargetDevice 0x%08X", MCP_GetLastRawError());
				if (installToUsb)
					__os_snprintf(errorText2, sizeof(errorText2), "Possible USB HDD disconnected or failure");
				break;
			}
			result = MCP_InstallSetTargetUsb(mcpHandle, installToUsb);
			if(result != 0)
			{
				__os_snprintf(errorText1, sizeof(errorText1), "Error: MCP_InstallSetTargetUsb 0x%08X", MCP_GetLastRawError());
				if (installToUsb)
					__os_snprintf(errorText2, sizeof(errorText2), "Possible USB HDD disconnected or failure");
				break;
			}

            mcpInstallInfo[2] = (unsigned int)MCP_COMMAND_INSTALL_ASYNC;
            mcpInstallInfo[3] = (unsigned int)mcpPathInfoVector;
            mcpInstallInfo[4] = (unsigned int)1;
            mcpInstallInfo[5] = (unsigned int)0;

            memset(mcpInstallPath, 0, MAX_INSTALL_PATH_LENGTH);
            __os_snprintf(mcpInstallPath, MAX_INSTALL_PATH_LENGTH, titlePath);
            memset(mcpPathInfoVector, 0, 0x0C);

            mcpPathInfoVector[0] = (unsigned int)mcpInstallPath;
            mcpPathInfoVector[1] = (unsigned int)MAX_INSTALL_PATH_LENGTH;

			installCompleted = 0;
            result = IOS_IoctlvAsync(mcpHandle, MCP_COMMAND_INSTALL_ASYNC, 1, 0, mcpPathInfoVector, IosInstallCallback, mcpInstallInfo);
            if(result != 0)
            {
                __os_snprintf(errorText1, sizeof(errorText1), "Error: MCP_InstallTitleAsync 0x%08X", MCP_GetLastRawError());
                break;
            }

            while(!installCompleted)
            {
                memset(mcpInstallInfo, 0, 0x24);

                result = MCP_InstallGetProgress(mcpHandle, mcpInstallInfo);

                if(mcpInstallInfo[0] == 1)
                {
                    u64 installedSize, totalSize;
					totalSize = ((u64)mcpInstallInfo[3] << 32ULL) | mcpInstallInfo[4];
					installedSize = ((u64)mcpInstallInfo[5] << 32ULL) | mcpInstallInfo[6];
					int percent = (totalSize != 0) ? ((installedSize * 100.0f) / totalSize) : 0;
                    for(int i = 0; i < 2; i++)
                    {
                        OSScreenClearBufferEx(i, 0);

                        OSScreenPutFontEx(i, 0, 0, TITLE_TEXT);
						OSScreenPutFontEx(i, 0, 3, "Installing title...");
                        OSScreenPutFontEx(i, 0, 4, installFolder);

                        __os_snprintf(text, sizeof(text), "%08X%08X - %0.1f / %0.1f MB (%i%%)", titleIdHigh, titleIdLow, installedSize / (1024.0f * 1024.0f),
                                                                                                  totalSize / (1024.0f * 1024.0f), percent);
                        OSScreenPutFontEx(i, 0, 6, text);

                        if(percent == 100)
                        {
                            OSScreenPutFontEx(i, 0, 7, "Please wait...");
                        }
                        // Flip buffers
                        OSScreenFlipBuffersEx(i);
                    }
                }

                usleep(50000);
            }

            if(installError != 0)
            {
                if ((installError == 0xFFFCFFE9) && installToUsb)
				{
                    __os_snprintf(errorText1, sizeof(errorText1), "Error: 0x%08X access failed (no USB storage attached?)", installError);
                }
                else
				{
                    __os_snprintf(errorText1, sizeof(errorText1), "Error: install error code 0x%08X", installError);
					if (installError == 0xFFFBF446 || installError == 0xFFFBF43F)
						__os_snprintf(errorText2, sizeof(errorText2), "Possible missing or bad title.tik file");
					else if (installError == 0xFFFBF441)
						__os_snprintf(errorText2, sizeof(errorText2), "Possible incorrect console for DLC title.tik file");
					else if (installError == 0xFFFCFFE4)
						__os_snprintf(errorText2, sizeof(errorText2), "Possible not enough memory on target device");
					else if (installError == 0xFFFFF825)
						__os_snprintf(errorText2, sizeof(errorText2), "Possible bad SD card.  Reformat (32k blocks) or replace");
					else if ((installError & 0xFFFF0000) == 0xFFFB0000)
						__os_snprintf(errorText2, sizeof(errorText2), "Verify title.tik and WUP files are correct & complete");
                }
                break;
            }
            else
            {
                installSuccess = 1;
            }
        }
        else
        {
            __os_snprintf(errorText1, sizeof(errorText1), "Error: Not a game, game update, DLC, demo or version title");
        }
    }
    while(0);

    MCP_Close(mcpHandle);

    if(mcpPathInfoVector)
        OSFreeToSystem(mcpPathInfoVector);
    if(mcpInstallPath)
        OSFreeToSystem(mcpInstallPath);
    if(mcpInstallInfo)
        OSFreeToSystem(mcpInstallInfo);
}


static void Getmenupos(void)
{
	
	if(dirNum == 0)menupos.pos2 = 0;	
	
	if(menupos.count > 5)
	{
		if((menupos.pos2 + 5) < dirNum)menupos.pos2++;
		if(menupos.pos2 > dirNum)menupos.pos2--;
		if(dirNum == menupos.count - 1)menupos.pos2 = dirNum - 5;
	}
	menupos.pos = menupos.pos2;

}	
static void GetNameFolder(void)
{
	
	DIR *dirPtr;
	struct dirent *dirEntry;     
	dirPtr = opendir ("sd:/install/");
	
	
	if (dirPtr != NULL)
	{
		
		dirEntry = readdir(dirPtr);
		
		if (dirEntry != NULL && dirEntry->d_type == DT_DIR)
		{
			menupos.count = 0;
			
			while(true)
			{
				seekdir(dirPtr, menupos.count);
				dirEntry = readdir(dirPtr);
				
				
				if (dirEntry != NULL && dirEntry->d_type == DT_DIR)
					__os_snprintf(NameFolder[menupos.count], 10000, "%s", dirEntry->d_name);
				else
				{
					closedir (dirPtr);
					break;
				}
				menupos.count++;
			}
			
		}
	}

	
}
static void GetInstallDir(char *dest, int size)
{
	DIR *dirPtr;
	struct dirent *dirEntry;     
	dirPtr = opendir ("sd:/install/");
	int dirsFound = 0;
	
	if (dirPtr != NULL)
	{
		dirEntry = readdir(dirPtr);
		if (dirEntry != NULL && dirEntry->d_type == DT_DIR)
		{
			seekdir(dirPtr, dirNum);
			dirEntry = readdir(dirPtr);
			closedir (dirPtr);
			if (dirEntry != NULL && dirEntry->d_type == DT_DIR)
				__os_snprintf(dest, size, "install/%s", dirEntry->d_name);
			else
			{
				dirNum--;
				if (dirNum < 0)
					dirNum = 0;
			}
			dirsFound = 1;
		}
	}
	if (!dirsFound)
	{
		__os_snprintf(dest, size, "install");
		dirNum = 0;
	}
}

static void setAllFolderSelect(bool state)
{
	DIR *dirPtr;
	struct dirent *dirEntry;     
	
	for (int i = 0; i < 1023; i++)
		folderSelect[i] = false;

	if (state)
	{
		dirPtr = opendir ("sd:/install/");
		if (dirPtr != NULL)
		{
			int i = 0;
			while (1)
			{
				dirEntry = readdir(dirPtr);
				if (dirEntry == NULL || dirEntry->d_type != DT_DIR)
					break;
				folderSelect[i++] = true;
			}
			closedir (dirPtr);
		}
	}
}

static int getNextSelectedFolder(void)
{
	int i;
	for (i = 0; i < 1023; i++)
		if (folderSelect[i] == true)
			break;
	return i;
}

static int useFolderSelect(void)
{
	int i;
	int ret = 0;
	for (i = 0; i < 1023; i++)
		if (folderSelect[i] == true)
		{
			ret = 1;
			break;
		}
	return ret;
}

/* Entry point */
int Menu_Main(void)
{
    //!*******************************************************************
    //!                   Initialize function pointers                   *
    //!*******************************************************************
    //! do OS (for acquire) and sockets first so we got logging
    InitOSFunctionPointers();
    InitFSFunctionPointers();
    InitSysFunctionPointers();
    InitVPadFunctionPointers();
    //InitSocketFunctionPointers();

    //log_init("192.168.178.3");

    //!*******************************************************************
    //!                    Initialize heap memory                        *
    //!*******************************************************************
    //! We don't need bucket and MEM1 memory so no need to initialize
    memoryInitialize();
    mount_sd_fat("sd");

    VPADInit();
	
    int update_screen = 1;
	int delay = 0;
    int doInstall = 0;
    int vpadError = -1;
    VPADData vpad_data;
	char lastFolder[256];

    // Prepare screen
    int screen_buf0_size = 0;
    int screen_buf1_size = 0;

    // Init screen and screen buffers
    OSScreenInit();
    screen_buf0_size = OSScreenGetBufferSizeEx(0);
    screen_buf1_size = OSScreenGetBufferSizeEx(1);

    unsigned char *screenBuffer = MEM1_alloc(screen_buf0_size + screen_buf1_size, 0x40);

    OSScreenSetBufferEx(0, screenBuffer);
    OSScreenSetBufferEx(1, (screenBuffer + screen_buf0_size));

    OSScreenEnableEx(0, 1);
    OSScreenEnableEx(1, 1);

    // in case we are not in mii maker but in system menu we start the installation
    if (OSGetTitleID() != 0x000500101004A200 && // mii maker eur
        OSGetTitleID() != 0x000500101004A100 && // mii maker usa
        OSGetTitleID() != 0x000500101004A000)   // mii maker jpn
    {
		char folder[256];
		__os_snprintf(folder, sizeof(folder), "/vol/app_sd/%s", installFolder);
		InstallTitle(folder);

        MEM1_free(screenBuffer);
        memoryRelease();
        SYSLaunchMiiStudio(0);

        return EXIT_RELAUNCH_ON_LOAD;
    }

	folderSelect[dirNum] = false;
	
	__os_snprintf(lastFolder, sizeof(lastFolder), installFolder);
	
	if (installSuccess && useFolderSelect())
	{
		dirNum = getNextSelectedFolder();
		doInstall = 1;
		delay = 250;
	}
	
	int button_pressed = 1;
	GetNameFolder();
	
	while(1)
    {
		// print to TV and DRC
		if(update_screen)
		{
			GetInstallDir(installFolder, sizeof(installFolder));
			
			OSScreenClearBufferEx(0, 0);
			OSScreenClearBufferEx(1, 0);
			for(int i = 0; i < 2; i++)
			{
				char text[80];
				char text2[80];
				OSScreenPutFontEx(i, 0, 0, TITLE_TEXT);
				
				OSScreenPutFontEx(i, 0, 2, lastFolder);
				__os_snprintf(text, sizeof(text), "Install of title %08X-%08X ", (u32)(installedTitle >> 32), (u32)(installedTitle & 0xffffffff));
				if( installSuccess)
				{
					__os_snprintf(text, sizeof(text), "%s finished successfully.", text);
					OSScreenPutFontEx(i, 0, 3, text);
				}
				else if (installCompleted)
				{
					__os_snprintf(text, sizeof(text), "%s failed.", text);
					OSScreenPutFontEx(i, 0, 3, text);
					OSScreenPutFontEx(i, 0, 4, errorText1);
					OSScreenPutFontEx(i, 0, 5, errorText2);
				}

				if (!doInstall)
				{
					
					__os_snprintf(text, sizeof(text), "%c  %s", folderSelect[dirNum] ? '*' : ' ', installFolder);
					
					Getmenupos();
					
					for (int t = 0; t < menupos.count; t++) 
					{
						__os_snprintf(text2, sizeof(text2), "%c %s", folderSelect[menupos.pos] ? '*' : ' ', NameFolder[menupos.pos]);
						
						OSScreenPutFontEx(i, 3, 6+t, text2);
						OSScreenPutFontEx(i, 3, 6+t, text2);
						
						if(menupos.pos == dirNum)
						{
							OSScreenPutFontEx(i, 0, 6+t, "=>");
							OSScreenPutFontEx(i, 0, 6+t, "=>");
						}
						menupos.pos++;
						if(t >= 5)break;
					}
					
					OSScreenPutFontEx(i, 0, 13, "Up/Down to change folder.");
					OSScreenPutFontEx(i, 0, 14, "Left/Right to (*)select/unselect folder.");
					OSScreenPutFontEx(i, 0, 15, "+ to select all folders :  - to unselect all folders.");
					OSScreenPutFontEx(i, 0, 16, "A install NAND : X install USB : Y refresh SD.");
				}
				else
				{
					OSScreenPutFontEx(i, 0, 8, installFolder);
					__os_snprintf(text, sizeof(text), "Will install in %d", delay / 50);
					OSScreenPutFontEx(i, 0, 9, text);
					OSScreenPutFontEx(i, 0, 10, "Press B-Button to Cancel");
				}
				char text3[80];
				__os_snprintf(text3, sizeof(text3), "(%d) Titles", menupos.count);
				OSScreenPutFontEx(i, 50, 17, text3);
				OSScreenPutFontEx(i, 0, 17, "Press HOME-Button to return to HBL.");
			}

				// Flip buffers
				OSScreenFlipBuffersEx(0);
				OSScreenFlipBuffersEx(1);
		}
		update_screen = 0;

        VPADRead(0, &vpad_data, 1, &vpadError);
		u32 pressedBtns = 0;
		int yPressed = 0;

		if (!vpadError)
			pressedBtns = vpad_data.btns_d | vpad_data.btns_h;
		
		if (pressedBtns & VPAD_BUTTON_HOME)
			break;

        if (!doInstall)
        {
			if (pressedBtns & VPAD_BUTTON_A)
			{
				doInstall = 1;
				installToUsb = 0;
				if (useFolderSelect())
					dirNum = getNextSelectedFolder();
				break;
			}

			if (pressedBtns & VPAD_BUTTON_X)
			{
				doInstall = 1;
				installToUsb = 1;
				if (useFolderSelect())
					dirNum = getNextSelectedFolder();
				break;
			}

			if (pressedBtns & VPAD_BUTTON_Y)
			{
				if (!yPressed)
				{
					unmount_sd_fat("sd");
					mount_sd_fat("sd");
					setAllFolderSelect(false);
					dirNum = 0;
					update_screen = 1;
				}
				yPressed = 1;
			}
			else
				yPressed = 0;

			if (!button_pressed)
            {
				if (pressedBtns & VPAD_BUTTON_DOWN)
				{
					dirNum = (dirNum == menupos.count - 1) ? 0 : dirNum + 1;
					
				}
				if (pressedBtns & VPAD_BUTTON_UP)
				{
					dirNum = (dirNum == 0) ? menupos.count - 1 : dirNum - 1;
					
				}
			}
			else
			{
				delay = 0;
			}
			
			if (pressedBtns & (VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT))
			{
				folderSelect[dirNum] = (pressedBtns & VPAD_BUTTON_RIGHT) ? 1 : 0;
			}

			if (pressedBtns & (VPAD_BUTTON_PLUS | VPAD_BUTTON_MINUS))
			{
				setAllFolderSelect((pressedBtns & VPAD_BUTTON_PLUS) ? true : false);
			}

			// folder selection button pressed ?
			update_screen = ((pressedBtns & (VPAD_BUTTON_UP | VPAD_BUTTON_DOWN | VPAD_BUTTON_LEFT | VPAD_BUTTON_RIGHT
											| VPAD_BUTTON_PLUS | VPAD_BUTTON_MINUS)) || yPressed) ? 1 : 0;
		}
		else
		{
			if (pressedBtns & VPAD_BUTTON_B)
			{
				doInstall = 0;
				installSuccess = 0;
				update_screen = 1;
				delay = 0;
			}
			else if (--delay <= 0)
				break;
			else
				update_screen = (delay % 50 == 0) ? 1 : 0;
		}
		button_pressed = (vpad_data.btns_h & BTN_PRESSED) ? 1 : 0;
		usleep(20000);
    }
	GetInstallDir(installFolder, sizeof(installFolder));

	MEM1_free(screenBuffer);
	screenBuffer = NULL;

    //!*******************************************************************
    //!                    Enter main application                        *
    //!*******************************************************************
    unmount_sd_fat("sd");
    memoryRelease();

    if(doInstall)
    {
        installSuccess = 0;
        installedTitle = 0;
        installError = 0;

        SYSLaunchMenu();
        return EXIT_RELAUNCH_ON_LOAD;
    }
	
	setAllFolderSelect(false);
	dirNum = 0;
	installFolder[0] = 0;

    return EXIT_SUCCESS;
}


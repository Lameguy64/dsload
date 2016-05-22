#include <nds.h>
#include <dswifi9.h>
#include <fat.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <netinet/in.h>
#include <netdb.h>


#define VERSION				"0.25"

#define CRC32_REMAINDER		0xFFFFFFFF

#define SEND_CRC_CHECK		1
#define SEND_VERIFIED		2

#define DEFAULT_PORT		1500

#define MAXPATHLEN			64


// File info struct
typedef struct {
	u_int	flags;
	u_int	fileSize;
	u_int	fileCRC32;
	int		nameLen;
} FILEHEAD;


char	buff[32768];			// Data buffer
char	currentDir[MAXPATHLEN];	// Current directory
int		systemShutdown = false;	// Flag for auto-shutdown


void DoComms(int sock);
void SendReturn(int sock, int code);

bool CreatePath(const char *path);
int GetFreeDiskSpace(const char *path);

unsigned int crc32(void* buff, int bytes, unsigned int crc);


int main(void) {

	// Simple init (turn off bottom screen and set up text console)
	powerOff(PM_BACKLIGHT_BOTTOM);

	videoSetMode(MODE_0_2D);
	vramSetBankA(VRAM_A_MAIN_BG);

	consoleInit(NULL, 3, BgType_Text4bpp, BgSize_T_256x256, 31, 0, true, true);


	// Display program banner
	printf("DS File Loader Utility v%s\n", VERSION);
	printf("2016 Meido-Tek Productions\n\n");

	printf("Initializing File System...");

	if (!fatInitDefault()) {

		printf("\nERROR: Cannot init FS.");
		while(1) swiWaitForVBlank();

	}

	printf("Ok!\n");

	// Get current directory
	getcwd(currentDir, MAXPATHLEN);


	// Initialize wifi
	printf("Initializing WiFi...");

	if(!Wifi_InitDefault(WFC_CONNECT)) {

		printf("Connect Fail.\n");
		while(1) swiWaitForVBlank();

	} else {

		printf("Ok!\n\n");

		// Display wifi status
		{
			struct in_addr ip, gateway, mask, dns1, dns2;
			ip = Wifi_GetIPInfo(&gateway, &mask, &dns1, &dns2);
			printf("Device IP   : %s\n", inet_ntoa(ip) );
		}

		// Create a socket as a server and bind it
		struct sockaddr_in server,client;
		int sock,csock;
		int ret;

		sock = socket(AF_INET, SOCK_STREAM, 0);

		server.sin_family		= AF_INET;
		server.sin_port			= htons(DEFAULT_PORT);
		server.sin_addr.s_addr	= INADDR_ANY;

		ret = bind(sock, (struct sockaddr*) &server, sizeof(server));

		if (ret) {

			printf("Error %d binding socket!\n", ret);
			while(1) swiWaitForVBlank();

		} else {

			printf("Default Dir : %s\n\n", currentDir);

			printf("Waiting for connection...\n");
			printf("Free space: %.2fMB\n", (((float)GetFreeDiskSpace("/"))/1024)/1024);

			// Begin listening for incoming connections
			if ((ret = listen(sock, 5))) {

				printf("Error %d listening!\n", ret);

				while(1) swiWaitForVBlank();

			} else {

				// Main loop
				while(1) {

					// Check and accept an incoming connection
					{
						int clientlen = sizeof(client);
						csock = accept(sock, (struct sockaddr *)&client, &clientlen);
					}

					// Is socket valid?
					if (csock >= 0) {

						int isClient = false;

						// Test comms with client
						{

							char temp[6];
							memset(temp, 0, 6);
							printf("Connected with %s\n", inet_ntoa(client.sin_addr));

							// Receive an ID from the client
							if (recv(csock, temp, 5, 0) > 0) {

								// Check if message is a SNC0 ID
								if (strncmp("SYNC0", temp, 5) != 0) {

									printf("ERRROR: Client not compatible.\n");

								} else {

									printf("Protocol accepted!\n");
									isClient = true;
									SendReturn(csock, 0);

								}

							} else {

								printf("ERROR: Client ID receive fail.\n");

							}

						}

						// If client is valid, begin comms
						if (isClient) {

							DoComms(csock);
							printf("Client disconnected.\n");
							chdir(currentDir);

						}

						// Close connection
						shutdown(csock, 0);
						closesocket(csock);

						// Exit loop (to either turn off or return to loader) if shutdown flag is set
						if (systemShutdown == true)
							break;

						// Display stats
						printf("Waiting for connection...\n");
						printf("Free space: %.2fMB\n", (((float)GetFreeDiskSpace("/"))/1024)/1024);

					}

					swiWaitForVBlank();

				}

			}

		}

	}

	return 0;

}


void DoComms(int sock) {

	char cmd[5];

	while(1) {

		memset(cmd, 0x00, 5);

        if (recv(sock, cmd, 4, 0) > 0) {

			// Close connection
			if (strcmp("CLCN", cmd) == 0) {

				break;

			// System shutdown
			} else if (strcmp("STDN", cmd) == 0) {

				systemShutdown = true;
				break;

			// Change directory
			} else if (strcmp("CHDR", cmd) == 0) {

				SendReturn(sock, 0);

				int	pathLen;
				recv(sock, &pathLen, 4, 0);

                char pathName[pathLen];
				recv(sock, &pathName, pathLen, 0);

				if (chdir(pathName)) {

					printf("Unable to set current dir to:\n%s\n", pathName);
					SendReturn(sock, 1);

				} else {

					printf("Set current dir to:\n%s\n", pathName);
					SendReturn(sock, 0);

                }

			// Send file request
			} else if (strcmp("SNFL", cmd) == 0) {

                printf("File Send command received.\n");

				// Send return code
                SendReturn(sock, 0);

				// Receive file parameters
                FILEHEAD file;
                recv(sock, &file, sizeof(file), 0);

				// Receive file name
				char fileName[file.nameLen+1];
				memset(fileName, 0x00, file.nameLen+1);
				recv(sock, fileName, file.nameLen, 0);

				printf("Downloading %s...", fileName);

				SendReturn(sock, 0);

				FILE *fp = fopen(fileName, "wb");

				if (fp == NULL) {

					printf("ERROR: Cannot create file.\n");

				} else {

					int bytesLeft = file.fileSize;
					int count = 0;
					unsigned int checksum = CRC32_REMAINDER;
					unsigned int compsum;

                    while(bytesLeft > 0) {

						int bytesToReceive = bytesLeft;

						if (bytesToReceive > 32768)
							bytesToReceive = 32768;

						int ret;

						if (file.flags & SEND_VERIFIED)
							ret = recv(sock, &checksum, 4, 0);
						else
							ret = 1;

						if (ret < 0) {

							printf("ERROR: Download fail.\n");
							break;

						}

						int received;

						while(1) {

							received = 0;

							while(received < bytesToReceive) {

								ret = recv(sock, &buff[received], bytesToReceive-received, 0);

								if (ret < 0)
									break;

								received += ret;

							}

							if (ret < 0)
								break;

							if (file.flags & SEND_VERIFIED) {

								compsum = crc32(buff, received, CRC32_REMAINDER);

								if (checksum == compsum) {

									SendReturn(sock, 0);
									break;

								} else {

									SendReturn(sock, 1);
									printf("X");

								}

							} else {

								break;

							}

						}

                        if (ret < 0) {

							printf("\nERROR: Download fail.\n");
							break;

                        } else if (ret >= 0) {

							fwrite(buff, 1, received, fp);
							fflush(fp);

							bytesLeft -= received;

                        }


						if (count >= 2) {
							printf(".");
							count = 0;
						}

                        count++;


                    }

					fflush(fp);
					fclose(fp);

					fp = fopen("_$dummy", "wb");
					fwrite(buff, 1, 32768, fp);
					fflush(fp);
					fclose(fp);

                    unlink("_$dummy");

					printf("\n");

					if (bytesLeft == 0) {
						SendReturn(sock, 0);
						printf("Downloaded successful.\n");
					}

				}

			} else if (strlen(cmd) > 0) {

				printf("Unknown command: %s\n", cmd);

			}

        } else {

        	break;

        }

		swiWaitForVBlank();

	}

}

void SendReturn(int sock, int code) {

	send(sock, (char*)&code, 4, 0);

}

int GetFreeDiskSpace(const char *path) {

	struct statvfs diskStat;
    statvfs(path, &diskStat);

	return(diskStat.f_bsize*diskStat.f_bfree);

}

void _crc32_init(unsigned int* table) {

	int i,j;
	unsigned int crcVal;

	for(i=0; i<256; i++) {

		crcVal = i;

		for(j=0; j<8; j++) {

			if (crcVal&0x00000001L)
				crcVal = (crcVal>>1)^0xEDB88320L;
			else
				crcVal = crcVal>>1;

		}

		table[i] = crcVal;

	}

}

unsigned int crc32(void* buff, int bytes, unsigned int crc) {

	int	i;
	unsigned char*	byteBuff = (unsigned char*)buff;
	unsigned int	byte;
	unsigned int	crcTable[256];

    _crc32_init(crcTable);

	for(i=0; i<bytes; i++) {

		byte = 0x000000ffL&(unsigned int)byteBuff[i];
		crc = (crc>>8)^crcTable[(crc^byte)&0xff];

	}

	return(crc^0xFFFFFFFF);

}

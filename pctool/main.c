#include <stdio.h>
#include <unistd.h>
#include <winsock2.h>


#define VERSION				"0.25"

#define CRC32_REMAINDER		0xFFFFFFFF

#define	ERR_CREATE_SOCKET	-1
#define ERR_CONNECT_FAIL	-2
#define ERR_CHECK_FAIL		-3

#define SEND_CRC_CHECK		1	// Perform CRC comparison
#define SEND_VERIFIED		2	// Verifies data integrity

#define DEFAULT_PORT		1500


typedef struct {
	u_int	flags;
	u_int	fileSize;
	u_int	fileCRC32;
	int		nameLen;
} FILEHEAD;


int syncGetReturn(SOCKET sock);
int syncSendCommand(SOCKET sock, const char* cmd);
SOCKET syncConnect(const char* ip, int port);
int syncSendFile(SOCKET sock, const char* fileName, int flags);

unsigned int crc32(void* buff, int bytes, unsigned int crc);


int main(int argc, const char* argv[]) {

	// Print program banner
	printf("DSLoad PC File Loader Utility v%s\n", VERSION);
	printf("2016 Meido-Tek Productions\n\n");


	// Print usage instructions if no parameters specified
	if (argc <= 1) {
		printf("Usage:\n\n");
		printf("   dsload [-ip <ipadd>] [-noverify] [-shutdown] <file1> ...\n\n");
		printf("   -ip <ipadd>  - Specify an IP address to connect to the DS.\n");
		printf("   -noverify	- Disable data transfer verification (not recommended).\n");
		printf("   -shutdown	- Shutdown DS once transfer is complete.\n");
		printf("   -dir <dir>   - Change current directory path on the DS.\n");
		printf("                  (path must exist on DS first)\n");
		printf("\n");
		return(0);
	}


	// Parse arguments
	const char *ipAddr = NULL;
	int	noVerify = false,powerDown = false;
	const char *changeDir=NULL;

	int	fileStart = 0;

    for(int i=1; i<argc; i++) {

        if (strcmp("-ip", argv[i]) == 0) {
            i++;
            ipAddr = argv[i];
		} else if (strcmp("-noverify", argv[i]) == 0) {
			noVerify = true;
		} else if (strcmp("-shutdown", argv[i]) == 0) {
			powerDown = true;
		} else if (strcmp("-dir", argv[i]) == 0) {
			i++;
			changeDir = argv[i];
        } else {
        	fileStart = i;
        	break;
        }

    }

	// If no files specified
    if (fileStart == 0) {
		printf("No file(s) specified.\n");
		return(0);
    }

	// If IP address is not specified, check for the DSLOAD environment variable
    if (ipAddr == NULL) {

        ipAddr = getenv("DSLOAD");

		if (ipAddr == NULL) {

			printf("No IP address specified.\n");
			return(0);

		}

    }


    // Check if specified files exist
    for(int i=fileStart; i<argc; i++) {

		FILE *fp = fopen(argv[i], "rb");

		if (fp == NULL) {
			printf("%s not found.\n", argv[i]);
			return(0);
		}

		fclose(fp);

    }


	// Initialize Winsock
	WSADATA	wsa;

	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {

		printf("ERROR: Failed to initialize Winsock...\n");
		return EXIT_FAILURE;

	}

	printf("Connecting to %s... ", ipAddr);


	// Connect to DS by specified IP address and port number
	SOCKET	s;

	if ((s = syncConnect(ipAddr, DEFAULT_PORT)) < 0) {

		printf("Could not connect to device. %d\n", s);

	} else {

		printf("Ok.\n");

		if (changeDir != NULL) {

            syncSendCommand(s, "CHDR");
            syncGetReturn(s);

			int pathLen = strlen(changeDir)+1;
            send(s, (char*)&pathLen, 4, 0);
            send(s, changeDir, pathLen, 0);

            if (syncGetReturn(s) != 0) {

				printf("Could not change to specified directory on DS.\n");

				// Close connection
				if (!syncSendCommand(s, "CLCN"))
					printf("Failed to send connection close command.\n");

				// Give DS time to receive command before closing socket
				usleep(500000);
				closesocket(s);

				WSACleanup();

				return(EXIT_FAILURE);

			}

		}

		int flags = 0;

		if (noVerify == false)
			flags |= SEND_VERIFIED;

		// Send the files
		for(int i=fileStart; i<argc; i++) {

			if (syncSendFile(s, argv[i], flags) < 0)
				break;

		}

		// Send a command to either close connection or shutdown the DS
		if (powerDown == false) {

			// Close connection
			if (!syncSendCommand(s, "CLCN"))
				printf("Failed to send connection close command.\n");

			// Give DS time to receive command before closing socket
			usleep(500000);
			closesocket(s);

		} else {

			// Shutdown DS
			if (!syncSendCommand(s, "STDN"))
				printf("Failed to send shutdown command.\n");

			// Give DS time to receive command before closing socket
			usleep(500000);
			closesocket(s);

		}

	}

	// Close down Winsock
	WSACleanup();

	return(0);

}


int syncGetReturn(SOCKET sock) {

	int value=0;
	recv(sock, (char*)&value, 4, 0);

	return(value);

}

SOCKET syncConnect(const char* ip, int port) {

	SOCKET sock;

	// Create socket
	if ((sock = socket(AF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET)
		return(ERR_CREATE_SOCKET);

	// Connect to server
	struct sockaddr_in server;

	server.sin_addr.s_addr	= inet_addr(ip);
	server.sin_family		= AF_INET;
	server.sin_port			= htons(port);

	if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0)
		return(ERR_CONNECT_FAIL);

	if (send(sock, "SYNC0", 6, 0) < 0)
		return(ERR_CHECK_FAIL);

	if (syncGetReturn(sock) != 0)
		return(ERR_CHECK_FAIL);

	return(sock);

}

int syncSendCommand(SOCKET sock, const char* cmd) {

	if (send(sock, cmd, strlen(cmd), 0) < 0)
		return(false);

	return(true);

}

int syncSendFile(SOCKET sock, const char* fileName, int flags) {

	FILE *fp = fopen(fileName, "rb");

	if (fp == NULL)
		return(-1);

	// Calculate CRC32 of file as well as its size
	unsigned int checksum = CRC32_REMAINDER;
	int fileSize = 0;

	while(!feof(fp)) {

		char buff[32768];
		int ret = fread(buff, 1, 32768, fp);

		checksum = crc32(buff, ret, checksum);
		fileSize += ret;

	}

	fseek(fp, 0, SEEK_SET);


	// Send command for file send
	if (!syncSendCommand(sock, "SNFL")) {
		fclose(fp);
		return(-1);
	}

	// Wait for acknowledge return
	syncGetReturn(sock);


	// Send file details
	printf("Sending %s (%d bytes)...", fileName, fileSize);

	const char *trimName = strrchr(fileName, '\\');

	if (trimName == NULL) {

		trimName = strrchr(fileName, '/');

		if (trimName == NULL) {

			trimName = fileName;

		} else {

			trimName++;

		}

	} else {

		trimName++;

	}

	FILEHEAD file;

	file.flags		= flags;
	file.fileSize	= fileSize;
	file.fileCRC32	= checksum;
	file.nameLen	= strlen(trimName);

	send(sock, (char*)&file, sizeof(file), 0);
	send(sock, trimName, file.nameLen, 0);


	// Wait for acknowledge return
	if (syncGetReturn(sock) != 0) {
		fclose(fp);
		return(-1);
	}


	// Send the file
	while(fileSize > 0) {

		char buff[32768];
		int bytesRead = fread(buff, 1, 32768, fp);

		if (file.flags&SEND_VERIFIED) {

			checksum = crc32(buff, bytesRead, CRC32_REMAINDER);
			send(sock, (char*)&checksum, 4, 0);

			int result;

			do {

				send(sock, buff, bytesRead, 0);
				result = syncGetReturn(sock);

			} while(result > 0);

		} else {

			send(sock, buff, bytesRead, 0);

		}

		fileSize -= bytesRead;
		printf(".");

	}


	// Close file then wait for acknowledge return
	fclose(fp);

	if (syncGetReturn(sock) != 0)
		return(-1);

	printf("Ok.\n");


	// Delay to ensure stable transfer
	usleep(200000000);


	return(0);

}


// CRC32 calculation stuff
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

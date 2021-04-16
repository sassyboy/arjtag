#include "libsvfplayer.h"
#include <stdio.h>
#include <string>
#include <vector>
#include <string.h>
#include <limits.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <termios.h>
#include <assert.h>
#include <poll.h>
#include <iostream>

using namespace std;

// #define DEBUG_ON

int uart_open(char* path, speed_t baud){
    struct termios uart_opts;
    // Open the file - Remember not to use buffered I/O!
    int fd = open(path, O_RDWR | O_NOCTTY);
	
	if (fd < 0) return fd;

    // Setup the UART as a terminal interface
    // See http://man7.org/linux/man-pages/man3/termios.3.html

    // Flush data already in/out
    if (tcflush(fd,TCIFLUSH)==-1)
        goto err;
    if (tcflush(fd,TCOFLUSH)==-1)
        goto err;
    // Setup modes (8-bit data, disable control signals, readable, no-parity)
    uart_opts.c_cflag= CBAUD | CS8 | CLOCAL | CREAD;    // control modes
    uart_opts.c_iflag=IGNPAR;                           // input modes
    uart_opts.c_oflag=0;                                // output modes
    uart_opts.c_lflag=0;                                // local modes
    // Setup input buffer options: Minimum input: 1byte, no-delay
    uart_opts.c_cc[VMIN]=1;
    uart_opts.c_cc[VTIME]=0;
    // Set baud rate
    cfsetospeed(&uart_opts,baud);
    cfsetispeed(&uart_opts,baud);
    // Apply the settings
    if (tcsetattr(fd,TCSANOW,&uart_opts)==-1)
        goto err;

    return fd;

err:
    close(fd);
    return -1;
}

void drainfd(int fd) {
	pollfd pfd;
	pfd.fd = fd;
	pfd.events = POLLIN;
	while(poll(&pfd,1,100)>0) {
		if(!(pfd.revents&POLLIN)) continue;
		char buf[4096];
		read(fd,buf,sizeof(buf));
	}
}

void uart_readline(int fd, char* outbuf, int n){
    for (int i = 0 ; i < n ; i++){
        read(fd, &outbuf[i], 1);
        if (outbuf[i] == '\n') 
            return;
    }
}

void uart_send_command(int uartfd, char* cmd, int cmd_len, char* resp, int resp_len){
	write(uartfd, cmd, cmd_len);
	memset(resp, 0, resp_len);
	uart_readline(uartfd, resp, resp_len);
}

int main(int argc, char** argv) {
	//Variables for handling the SVF file and parser 
	FILE* fp;
	svfParser parser;
	svfPlayer player;
	int num_cmds; // # of commands completed
	int num_tclk; // # of JTAG clock-cycles completed
	int cur_line;
	char* line;
	size_t n;
	string sent_tms, sent_tdi, expected_tdo, received_tdo;

	// Variables for handling the UART JTAG Programmer
	int ttydevice;
	char outBuff[6];
	char resp[256];

	// Command-line syntax check
	if(argc < 3) {
	print_usage:
		fprintf(stderr,"usage: %s <input-svf-file> <uart-device-path>\n",argv[0]);
		return EXIT_FAILURE;
	}

	// Open the SVF file and the UART device
    fp = fopen(argv[1], "r");
    if (!fp){
        printf("Could not open the svf file: %s\n", argv[1]);
        goto abort;
    }
	if((ttydevice = uart_open(argv[2], B115200)) < 0) {
		perror("open");
		fprintf(stderr, "ERROR: could not open %s\n", argv[2]);
		goto abort;
	}
	
	// Talking to the JTAG Programmer I made with Arduino
	//// 1) Reset the JTAG Programmer by sending a $RST command
	uart_send_command(ttydevice, (char*)"$RST\n", 5, resp, 256);
	cout<<"Devices connected to the JTAG interface are:"<<endl<<resp<<endl;
	cout<<"Continue? (y/n): ";
	cin>>resp;
	if (strncmp(resp, "y",1))
		return EXIT_SUCCESS;
	//// 2) Send the commands from SVF	
	num_cmds=0;
	num_tclk=0;
	cur_line=1;
	parser.reset();
	player.reset();
	while(true) {
		// Read a line from the SVF file
		line=NULL;
		if(getline(&line, &n, fp)<0)
			break;
	#ifdef DEBUG_ON
        cout<<"Processing Line "<< cur_line <<": "<<line; 
	#endif

		// Parse the line until we can execute something
		parser.processLine(line,strlen(line));
		svfCommand cmd;
		while(parser.nextCommand(cmd)) {
			player.processCommand(cmd);
			num_cmds++;
		}
		cur_line = parser.lineNum;		
		num_tclk += player.outBuffer.length();

		//
		sent_tdi.clear();
		sent_tms.clear();
		expected_tdo.clear();
		received_tdo.clear();
		for (int i = 0 ; i < player.outBuffer.length(); i++){
			// Each command translates into one byte per clock cycle;
			// format of each byte:
			//	bit 0: value to put on tms
			//	bit 1: value to put on tdi
			//	bit 2: value expected on tdo
			//	bit 3: 0 if tdi is don't care, 1 otherwise
			//	bit 4: 0 if tdo is don't care, 1 otherwise
			uint8_t b = player.outBuffer[i];
			outBuff[0] = '$'; // We send this to Arduino
			// TMS
			outBuff[1] = (b & 0x1) + '0';
			sent_tms.append(1, outBuff[1]);
			// TDI
			outBuff[2] = (b & 0x8)  ? (char)((player.outBuffer[i] & 0x2)>>1) + '0' : 'x';
			sent_tdi.append(1, outBuff[2]);
			// Expected TDO
			outBuff[3] = (b & 0x10) ? (char)((player.outBuffer[i] & 0x4)>>2) + '0' : 'x';
			expected_tdo.append(1, outBuff[3]);
			// End of the command
			outBuff[4] = '\n';
		#ifdef DEBUG_ON
			cout<<"Sending TMS: " << outBuff[1] <<
					", TDI: "<< outBuff[2] <<
					", TDO? "<< outBuff[3] << endl;
		#endif
			uart_send_command(ttydevice, outBuff, 5, resp, 256);
			received_tdo.append(1, resp[5]);
		#ifdef DEBUG_ON
			printf("Response: %s\n", resp);
		#endif
			if (resp[0] == 'T' && resp[1] == 'D' &&	resp[2] == 'O' &&
				resp[3] == ':' && resp[4] == ' '){
				// Valid resp
				if (outBuff[3]!= 'x' && resp[5] != outBuff[3]){
					cout<<"Error while executing command at line "<<cur_line<<endl;
					cout<<"\tLine: "<<line;
					cout<<"\tSent: TMS<"<<sent_tms<<">, TDI<"<<sent_tdi<<">"<<endl;
					cout<<"\tExpected TDO<"<<expected_tdo<<">"<<endl;
					cout<<"\tReceived TDO<"<<received_tdo<<">"<<endl;
					return EXIT_FAILURE;
				}
			}
		}
		player.outBuffer.clear();
		free(line);
	}
	cout<<num_cmds<<" commands executed successfully; "<<endl;
	cout<<num_tclk<<" tclk cycles total"<<endl;
	return EXIT_SUCCESS;
abort:
	if (fp)
		fclose(fp);
	if (ttydevice)
		close(ttydevice);
	return EXIT_FAILURE;
} 

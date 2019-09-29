/* mbed Microcontroller Library
 * Copyright (c) 2018 ARM Limited
 * SPDX-License-Identifier: Apache-2.0
 */

/* 	
*/

#include "mbed.h"
#include "Dali.hpp"
#include "EthernetInterface.h"
#include "TCPSocket.h"
#include "SocketAddress.h"
#include <string.h>
#include "NTPClient.h"
#include "LocalFileSystem.h"

#define SECONDS 300.0
#define ADDR 0x7
#define BUFSZ 256
#define ONTIME 14
#define OFFTIME 00

class Lights {
public:
	Lights(Dali *dali, PinName pin);
	void set_address(uint8_t addr);
	void set_on_time(uint32_t hour);
	void set_off_time(uint32_t hour);
	void callback(void);
	time_t toggle(void);
	void turn_off(void);
	void turn_on(void);
	volatile bool check_time = false;
	
private:
	enum {OFF,ON} state = OFF;
	uint32_t on_hour, off_hour;
	Dali* _dali;
	uint8_t _addr;
	DigitalOut _led;
	time_t timestamp;
	struct tm *info;
	bool _override = false;
};

Lights::Lights(Dali *dali, PinName pin) : _led(pin) {
	_dali = dali;
	_led = 0;
}

time_t Lights::toggle() {
	/* Read the current time */
	timestamp = time(NULL);
	
	/* What hour is it? */
	info = gmtime(&timestamp);
	uint32_t hour = info->tm_hour;
	
	printf("Hour is %d\n\r",hour);

	/* If we are between the on time and off time, turn the lights on */
	if( ((hour >= on_hour) && (hour > off_hour)) || ((hour < on_hour) && (hour < off_hour)) ) {
		// Lights should be on
		if (_override) {
			if (_led.read() == 1) {
				// Reset the override flag
				_override = false;
			} else {
				// If override is set and we are off, don't do anything.
				goto EARLY;
			}
		}
		if (_led.read() == 0) {
			printf("Turning on\n\r");
			_dali->turn_on(_addr);
			_led = 1;
		}
	} else {
		// Lights should be off
		if (_override) {
			if (_led.read() == 0) {
				_override = false;
			} else {
				goto EARLY;
			}
		}
		if (_led.read() == 1) {
			printf("Turning off\n\r");
			_dali->turn_off(_addr);
			_led = 0;			
		}
	}
	
EARLY:
	check_time = false;
	return timestamp;
}

void Lights::turn_on() {
	_dali->turn_on(_addr);
	_led = 1;
	_override = true;
}

void Lights::turn_off() {
	_dali->turn_off(_addr);
	_led = 0;
	_override = true;
}

void Lights::callback() {
	check_time = true;
}

void Lights::set_on_time(uint32_t hour) {
	on_hour = hour;
}

void Lights::set_off_time(uint32_t hour) {
	off_hour = hour;
}

void Lights::set_address(uint8_t addr) {
	_addr = addr;
}


Dali *Dali::handler = {0};
Dali DaliMaster(p30,p29);
Serial Uart(USBTX,USBRX);
EthernetInterface eth;	
Ticker timecheck;
Ticker heartbeat;
DigitalOut led2(LED2);
DigitalOut led1(LED1);
LocalFileSystem local("local");

void hbeat() {
	led1 = !led1;
}

void disable_timers() {
	timecheck.detach();
	heartbeat.detach();
}

int main() 
{

	time_t time;
    TCPSocket server;
	TCPSocket updater;
	TCPSocket *sock;
    int remaining;
    int rcount;
    char *p;
    char *buffer = new char[BUFSZ];
	char *c_time_string;
    nsapi_size_or_error_t result;
	nsapi_error_t err;
	
	led1 = 0;
	led2 = 0;
	
	if(eth.connect() != NSAPI_ERROR_OK) {
		Uart.printf("Failed to connect to ethernet\n\r");
		while(1) ;
	} else {
		Uart.printf("Connected to ethernet, IP address = %s\n\r", eth.get_ip_address());
	}

	NTPClient ntp(&eth);
	
	/* Get the current time from t'interweb and set the RTC. Make
		sure we get a valid NTP packet back before continuing. */
	time_t ts = -1;
	while(ts < 0) {
		ts = ntp.get_timestamp(7000);
		Uart.printf("get_timestamp returned %d\n\r", ts);
	}
	set_time(ts);

	/* Set up the lighting control */
	Lights lighting(&DaliMaster, LED2);
	lighting.set_on_time(ONTIME);
	lighting.set_off_time(OFFTIME);
	lighting.set_address(ADDR);
	lighting.toggle();
	
	/* Check the time every 5 mins */
	timecheck.attach(callback(&lighting, &Lights::callback), SECONDS);
	
	/* Attach a heartbeat ticket */
	heartbeat.attach(&hbeat, 1);

	/* Setup a socket to listen for commands */
    result = server.open(&eth);
    if (result != 0) {
        printf("Error! server.open() returned: %d\n\r", result);
    }
	err = server.bind(8081);
	if (err != 0) {
		printf("Error! server.bind() returned: %d\n\r", err);
	}
	err = server.listen();
	if (err != 0) {
		printf("Error! server.listen() returned: %d\n\r", err);
	}
	
	/* Setup a socket to listen for firmware updates */
    result = updater.open(&eth);
    if (result != 0) {
        printf("Error! updater.open() returned: %d\n\r", result);
    }
	err = updater.bind(8082);
	if (err != 0) {
		printf("Error! updater.bind() returned: %d\n\r", err);
	}
	err = updater.listen();
	if (err != 0) {
		printf("Error! updater.listen() returned: %d\n\r", err);
	}
	
	/* Set a timeout on the server so that we can handle multiple tasks */
	server.set_timeout(1); // timeout in ms
	updater.set_timeout(1);
	
	/* Accept incoming connections to turn on/off the lights */
	while(1) {
		/* Check if we need to check the time */
		if (lighting.check_time) {
			time = lighting.toggle();
			c_time_string = ctime(&time);
			// REVISIT: testing only
			//lighting.turn_on();
			//Uart.printf("Current time is %s\n\r", c_time_string);
		}
		
		/* Check if we have a new firmware image to download */
		sock = updater.accept(&err);
		if (err != 0) {
			if (err == NSAPI_ERROR_WOULD_BLOCK) {
				// timeout so ignore
			} else {
				printf("Error! updater.accept() returned: %d\n\r", err);
			}
		} else {
			// Turn off the timer interrupts
			disable_timers();
			printf("Got socket connection on port 8082. Updating firmware.\n\r");
			
			// Open the file handle
			FILE *fp = fopen("/local/firm.bin", "w");
			printf("Opened file /local/firm.bin for writing.\n\r");
			
			while(1) {
				// Read 256 bytes at a time and write to the file
				remaining = BUFSZ;
				rcount = 0;
				p = buffer;
				while(remaining > 0 && 0 < (result = sock->recv(p, remaining))) {
			        p += result;
			        rcount += result;
			        remaining -= result;
				}
				if (result < 0) {
		        	printf("FW Update Error! sock.recv() returned: %d\n\r", result);
					fclose(fp);
					sock->close();
			        goto DISCONNECT;
				}
				printf("Writing %d bytes.\n\r", rcount);
				fwrite(p, rcount, 1, fp);
			}
			
			// Finish up and reset
			fclose(fp);
			sock->close();
			printf("Firmware update successful.\n\r");
			goto RESET;
		}
		
		/* Wait for a socket connection or timeout and loop again */
		sock = server.accept(&err);
		if (err != 0) {
			if (err == NSAPI_ERROR_WOULD_BLOCK) {
				// timeout so ignore
			} else {
				printf("Error! server.accept() returned: %d\n\r", err);
			}
		} else {
		    remaining = BUFSZ;
		    rcount = 0;
		    p = buffer;
		    while (remaining > 0 && 0 < (result = sock->recv(p, remaining))) {
		        p += result;
		        rcount += result;
		        remaining -= result;
		    }
		    if (result < 0) {
		        printf("Error! sock.recv() returned: %d\n\r", result);
				sock->close();
		        goto DISCONNECT;
		    }
			sock->close();
	
			/* Modify the light state. */
			if (buffer[1] == 'n') {
				lighting.turn_on();
			} else {
				lighting.turn_off();
			}
		}
	}
	
RESET:
	server.close();
	eth.disconnect();
	system_reset();
	while(1) {
		Uart.printf("bye\n\r");
		wait(2);
	};
	
DISCONNECT:
	delete[] buffer;	
    // Close the socket to return its memory and bring down the network interface
    server.close();
    // Bring down the ethernet interface
    eth.disconnect();
    printf("Done\n\r");
}

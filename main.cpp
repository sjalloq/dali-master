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


#define SECONDS 300.0
#define ADDR 0xA7


class Lights {
public:
	Lights(Dali *dali, PinName pin);
	void set_address(uint8_t addr);
	void set_on_time(uint32_t hour);
	void set_off_time(uint32_t hour);
	void callback(void);
	void toggle(void);
	volatile bool doit = false;
	
private:
	enum {OFF,ON} state = OFF;
	uint32_t on_hour, off_hour;
	Dali* _dali;
	uint8_t _addr;
	DigitalOut _led;
	time_t timestamp;
	struct tm *info;
};

Lights::Lights(Dali *dali, PinName pin) : _led(pin) {
	_dali = dali;
	_led = 0;
}

void Lights::toggle() {
	/* Read the current time */
	timestamp = time(NULL);
	
	/* What hour is it? */
	info = gmtime(&timestamp);
	uint32_t hour = info->tm_hour;
	
	/* If we are between the on time and off time, turn the lights on */
	if( ((hour >= on_hour) && (hour > off_hour)) ||
		 ((hour < on_hour) && (hour < off_hour)) ) {
		_dali->turn_on(_addr);
		_led = 1;
	} else {
		_dali->turn_off(_addr);
		_led = 0;
	}
	doit = false;
}

void Lights::callback() {
	doit = true;
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
DigitalOut led2(LED2);


int main() 
{
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
	}
	set_time(ts);

	/* Set up the lighting control */
	Lights lighting(&DaliMaster, LED1);
	lighting.set_on_time(20);
	lighting.set_off_time(06);
	lighting.set_address(ADDR);
	
	/* Check the time every 5 mins */
	timecheck.attach(callback(&lighting, &Lights::callback), SECONDS);
	
	/* Force an initial time check on power up */
	lighting.doit = true;
	
	while(1) {
		if(lighting.doit) {
			led2 = 1;
			led2 = 0;
			lighting.toggle();
		}		
	}
}

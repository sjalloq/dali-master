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

#define PORT 7070


Dali* Dali::handler = {0};

Dali DaliMaster(p30,p29);
Serial Uart(USBTX,USBRX);
BusOut Leds(LED1,LED2,LED3,LED4);


void set_leds(uint32_t leds) {
	Leds = leds & 0xF;
}


void print_timer_status() {
	Uart.printf("TIM2->IR = 0x%x\n\r", LPC_TIM2->IR);
	Uart.printf("TIM2->TCR = 0x%x\n\r", LPC_TIM2->TCR);
	Uart.printf("TIM2->MCR = 0x%x\n\r", LPC_TIM2->MCR);
	Uart.printf("TIM2->MR0 = %d\n\r", LPC_TIM2->MR0);
	Uart.printf("TIM2->MR1 = %d\n\r", LPC_TIM2->MR1);
	Uart.printf("TIM2->CCR = 0x%x\n\r", LPC_TIM2->CCR);	
	Uart.printf("TIM2->TC = %d\n\r", LPC_TIM2->TC);	
}


void client_handler_callback(TCPSocket *client) {
	Uart.printf("client_handler_callback\n\r");
	EventQueue *queue = mbed_event_queue();
	DaliMaster.client_handler = queue->event(&DaliMaster, &Dali::client_sigio, client);
}


// main() runs in its own thread in the OS
int main()
{
	nsapi_error_t err;
	
	EthernetInterface eth;	
	TCPSocket socket;
	TCPSocket *client;
	
	EventQueue *queue = mbed_event_queue();
	Event<void()> server_handler = queue->event(&DaliMaster, &Dali::server_sigio, &socket);
	
	DaliMaster.queue  = queue;
	DaliMaster.client = client;
	DaliMaster.attach_uart(&Uart);
	DaliMaster.attach_client_handler(&client_handler_callback);
	
	if(eth.connect() != NSAPI_ERROR_OK) {
		Uart.printf("Failed to connect to ethernet\n\r");
		while(1) ;
	} else {
		Uart.printf("Connected to ethernet, IP address = %s\n\r", eth.get_ip_address());
	}
	
	err = socket.open(&eth);
	if(err != NSAPI_ERROR_OK) {
		Uart.printf("Socket.open() failed with error: %d\n\r", err);
	}
	
	err = socket.bind(PORT);
	if(err != NSAPI_ERROR_OK) {
		Uart.printf("Socket.bind() failed with error: %d\n\r", err);
	}
	
	err = socket.listen();
	if(err != NSAPI_ERROR_OK) {
		Uart.printf("Socket.listen() failed with error: %d\n\r", err);
	}
	
	socket.set_blocking(false);
	socket.sigio(server_handler);
	server_handler.post();
	
	while(1) {
	}
}

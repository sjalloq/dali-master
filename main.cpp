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

EventQueue *queue = mbed_event_queue();
EventFlags evntFlags;
EthernetInterface eth;	
TCPSocket socket;
TCPSocket *client;


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


void client_sigio(TCPSocket *client, EventFlags *evnt) {
	
	static enum {
		RECEIVE,
		CLOSE
	} next_state = RECEIVE;
	
	static char buf[4];
	static char *buf_p = buf;
	static uint32_t remaining = 4;
	
	nsapi_size_or_error_t szerr;
	
	Uart.printf("client_sigio: state=%d\n\r", next_state);
	
	switch(next_state) {
		case CLOSE: {
			client->close();
			evnt->clear(FLAG_CLIENT_CONNECT);
			next_state = RECEIVE;
			break;
		}
		case RECEIVE: {
			szerr = client->recv(buf_p, remaining);
			Uart.printf("Received %0d bytes\n\r", szerr);
			if(szerr >= 0) {
				buf_p += szerr;
				remaining -= szerr;
				if(0 == remaining) {
					// Send transaction
					Uart.printf("Received transaction: buf[3]=0x%0x buf[2]=0x%0x buf[1]=0x%0x buf[0]=0x%0x ", 
						buf[3], buf[2], buf[1], buf[0]);
				}
				break;
			} else {
				if(NSAPI_ERROR_WOULD_BLOCK == szerr) {
					break;
				} else if(NSAPI_ERROR_NO_SOCKET == szerr) {
					next_state = CLOSE;
					// now fall through to default
				} else {
					Uart.printf("client_sigio:  unhandled error on socket->recv(): %d\n\r", szerr);
				}
			}
		}
		default: {
			// something went wrong so reset the buffer
			buf_p = buf;
			remaining = 4;
		}
	}
}


void server_sigio(TCPSocket *server, TCPSocket *client, EventFlags *evnt) {
	
    static enum {
        LISTENING,
        ACCEPT,
        CLOSE
    } next_state = LISTENING;

	nsapi_error_t err;
	nsapi_size_or_error_t szerr;
	
	static char buf[4];
	static char *buf_p = buf;
	static uint32_t remaining = 4;
	
	
	Uart.printf("server_sigio: state=%d\n\r", next_state);
	
	switch (next_state) {
		case LISTENING: {
			client = server->accept(&err);
			// Uart.printf("server->accept error is %d\n\r", err);
			// if(err == 0) {
			// 	szerr = client->recv(buf_p, 1);
			// 	Uart.printf("szerr is %d\n\r", szerr);
			// }
			switch(err) {
				case NSAPI_ERROR_WOULD_BLOCK:
				// Continue to listen
				break;
				case NSAPI_ERROR_OK: {
					// Accepted connection
					evnt->set(FLAG_CLIENT_CONNECT);
					Event<void()> client_handler = queue->event(client_sigio, client, evnt);
					client->set_blocking(false);
					client->sigio(client_handler);
					Uart.printf("calling client_handler()\n\r");
					client_handler.post();
					//queue->dispatch();					
					next_state = ACCEPT;
					Uart.printf("Just set state=%d\n\r", next_state);
					break;
				}
				default:
				// Error in connection phase
				Uart.printf("Dali::server_sigio: err = %d\n\r", err);	
				//next_state = CLOSE;
				break;
			}
			break;
		}
		case ACCEPT: {
			//if (err == NSAPI_ERROR_WOULD_BLOCK)
			//  break;
			// REVISIT: print debug info
			//next_state = CLOSE;
			//Uart.printf("Dali::server_sigio: about to accept to test socket\n\r");
			//server->accept(&err);
			Uart.printf("server_sigio: evntFlags=0x%0x\n\r", evnt->get());
			//Uart.printf("Dali::server_sigio: err=%d\n\r", err);
			break;
		}
		case CLOSE: {
			Uart.printf("Dali::server_sigio: CLOSE state\n\r");
			//server->close();
			//flgs->set(FLAG_SOCKET_CLOSED);
			next_state = LISTENING;
			break;
		}
		default: {
			break;
		}
	}
	Uart.printf("Return\n\r");
}


class Eventing {
public:
	Eventing(EventFlags *evnt);
	void print_event_flags();
	EventFlags *evntFlags;
};

Eventing::Eventing(EventFlags *evnt) {
	this->evntFlags = evnt;
}

void Eventing::print_event_flags() {
	Uart.printf("EventFlags=0x%0x\n\r", this->evntFlags->get());
}


// main() runs in its own thread in the OS
int main()
{
	nsapi_error_t err;
	
	Event<void()> server_handler = queue->event(server_sigio, &socket, client, &evntFlags);
	
	char buf[4];
	char *buf_p = buf;
	uint32_t remaining = 4;

	nsapi_size_or_error_t szerr;

	//Eventing evnt(&evntFlags);
	//Uart.attach(&evnt, &Eventing::print_event_flags);
	
	//DaliMaster.attach_uart(&Uart);

	
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
	
	Uart.printf("entering while loop\n\r");
	while(1) {		
		if(evntFlags.get() & FLAG_CLIENT_CONNECT) {
			szerr = client->recv(buf_p, remaining);
			Uart.printf("main(): Received %0d bytes\n\r", szerr);
			if(szerr >= 0) {
				buf_p += szerr;
				remaining -= szerr;
				if(0 == remaining) {
					// Send transaction
					Uart.printf("Received transaction: buf[3]=0x%0x buf[2]=0x%0x buf[1]=0x%0x buf[0]=0x%0x ", buf[3], buf[2], buf[1], buf[0]);
					remaining = 4;
					buf_p = buf;
				}
			}
		}
	}
}

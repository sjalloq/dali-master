#include "mbed.h"
#include "dali.hpp"
#include "EventQueue.h"

Dali::Dali(PinName rxPin, PinName txPin) : dali_rx(rxPin), dali_tx(txPin) {
	init();
}


void Dali::attach_uart(Serial *uart) {
	_uart = uart;
}


void Dali::init() {
	handler        = this;
	dali_tx        = 1;
	
	init_timer();
    NVIC_SetVector(TIMER2_IRQn,(uint32_t)&irq);
    NVIC_SetPriority(TIMER2_IRQn,1);
    NVIC_EnableIRQ(TIMER2_IRQn);
}


void Dali::server_sigio(TCPSocket *socket) {
	
    static enum {
        LISTENING,
        ACCEPT,
        CLOSE
    } next_state = LISTENING;

	nsapi_error_t err;
	
	_uart->printf("Dali::server_sigio: state=%d\n\r", next_state);
	
	switch (next_state) {
        case LISTENING:
			this->client = socket->accept(&err);
			_uart->printf("socket->accept error is %d\n\r", err);
			switch(err) {
                case NSAPI_ERROR_WOULD_BLOCK:
                    // Continue to listen
					break;
				case NSAPI_ERROR_OK: {
					// Accepted connection
					_uart->printf("Pop\n\r");
					eventFlags.set(FLAG_CLIENT_CONNECT);
					this->client->set_blocking(false);
					_uart->printf("About to callback\n\r");
					//this->client->sigio(this->client_handler);
					_uart->printf("Called back\n\r");
					next_state = ACCEPT;
					//flgs->set(FLAG_SOCKET_ACCEPT);
					break;
				}
                default:
                    // Error in connection phase
					_uart->printf("Dali::server_sigio: err = %d\n\r", err);	
					next_state = CLOSE;
					break;
            }
			break;
        case ACCEPT:
			//if (err == NSAPI_ERROR_WOULD_BLOCK)
              //  break;
			// REVISIT: print debug info
            //next_state = CLOSE;
			_uart->printf("Dali::server_sigio: about to accept to test socket\n\r");
			socket->accept(&err);
			_uart->printf("Dali::server_sigio: event flags = 0x%0x\n\r", eventFlags.get());
			_uart->printf("Dali::server_sigio: err=%d\n\r", err);
			break;
		case CLOSE:
			_uart->printf("Dali::server_sigio: CLOSE state\n\r");
			socket->close();
			//flgs->set(FLAG_SOCKET_CLOSED);
			next_state = LISTENING;
            break;
    }
}


/*
	Callback for client socket connections.
	Receives 4 bytes and constructs a dali_payload_t.
*/
void Dali::client_sigio(TCPSocket *socket) {
	
	static enum {
		RECEIVE,
		CLOSE
	} next_state = RECEIVE;
	
	static char buf[4];
	static char *buf_p = buf;
	static uint32_t remaining = 4;
	
	nsapi_size_or_error_t szerr;
	
	_uart->printf("Dali::client_sigio: state=%d\n\r", next_state);
	
	switch(next_state) {
		case CLOSE:
			socket->close();
			eventFlags.clear(FLAG_CLIENT_CONNECT);
			next_state = RECEIVE;
			break;
			
		case RECEIVE:
			szerr = socket->recv(buf_p, remaining);
			if(szerr > 0) {
				buf_p += szerr;
				remaining -= szerr;
				if(0 == remaining) {
					// Send transaction
					_uart->printf("Received transaction: buf[3]=0x%0x buf[2]=0x%0x buf[1]=0x%0x buf[0]=0x%0x ", 
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
					_uart->printf("Dali::client_sigio:  unhandled error on socket->recv(): %d\n\r", szerr);
				}
			}
			
		default:
			// something went wrong so reset the buffer
			buf_p = buf;
			remaining = 4;
	}
	
}



/*
	The Timer is used for both send and receive functionality.  This initialisation
	function sets up the defaults for receiving.  When sending a frame, we must disable
	the receive interrupts.

	SEND:
		- uses Match Register 1, MR1
			+ used to time each half cycle of a Manchester encoded bit.

	RECEIVE: 
		- uses Match Regsiter 0, MR0
			+ used to capture the end of the frame by firing after two stop bits
		- uses Capture Register 0, CR0
			+ used to capture the cycle count on both rising and falling edges of the Rx pin
*/
void Dali::init_timer() {
	// PCLK = CCLK
	LPC_SC->PCLKSEL1 |= (1<<12); 
	
	// Enable TIM2 in Power Control
	LPC_SC->PCONP |= 1 << 22;  
	 
	// Set the prescaler for 1MHz operation (uS timing resolution)
	LPC_TIM2->PR = 96;
	
	// Set up CR0 to capture and interrupt on rising/falling edges
	LPC_TIM2->CCR = 0x7;
	
	// Set up the MR0 to interrupt and reset when we get to two stop bits
	LPC_TIM2->MR0 = STP_2TE;
	LPC_TIM2->MCR = 3;
	
	// Set up MR1 to interrupt and reset on each half cycle
	LPC_TIM2->MR1 = TE;

}

/*
	Function    : timer_isr()
	Description : This interrupt routine handles the TIMER2 capture and match IRQs. We
                  get an IRQ on either edge of the Dali RX pin and also when we have seen
                  two stop bits.
                  
                    - on entry we always reset the char to zero.
                    - if the match interrupt has fired we know we're at the end of a frame
                    - if the capture interrupt has fired, we need to check if it was a pos/negedge.
                   
                  NOTE: this code is adapted from an NXP example but the polarity of the Dali
                  Manchester coding is reversed on the NXP I/OH board due to the optocoupler.
	              
	              REVISIT: enable mixed polarity
*/
void Dali::timer_isr(void) {
	/* 	Writing 0 resets the timer and we do this on each entry to the ISR
		in order to start counting straight after an edge or timer interrupt fires.	*/
	LPC_TIM2->TC = 0; 
	
	if (LPC_TIM2->IR & MR1_IRQ) { // match 1 interrupt for DALI send
		
		/**/
		if (frame_bit_pol) {
			dali_tx = 1; // DALI output pin high
		} else {
			dali_tx = 0; // DALI output pin low
		}
		
		/* DALI Frame : 0TE second half of start bit = 1 */
		if (frame_bit_idx == 0) {
			frame_bit_pol = 1;
			
		/* DALI Frame : 1TE - 32TE, so address + command */
		} else if (frame_bit_idx < 33) {
			frame_bit_pol = (forward_frame >> ((32 - frame_bit_idx)/2)) & 1;
			
			if (frame_bit_idx & 1)
				frame_bit_pol = !frame_bit_pol; // invert if first half of data bit
			
		/* DALI Frame : 33TE start of stop bit (4TE) and start of min settling time (7TE)  */
		} else if (frame_bit_idx == 33) {
			frame_bit_pol = 1;
			
		/* DALI Frame : 44TE, end stop bits + settling time 
			If the forward frame requires an answer, it must come within 9.174mS
			so we set up the match register as a watchdog and enable CR0 to receive
			the response. */
		} else if (frame_bit_idx == 44) { 
			LPC_TIM2->MR1 = 9174;  // timeout of 22TE = 9,174 msec
			LPC_TIM2->CCR = 7;     // enable rx, capture on both edges
			
		/* DALI Frame :  End of transfer. */
		} else if (frame_bit_idx == 45) {
			LPC_TIM2->TCR = 2;      // stop and reset timer
			LPC_TIM2->MCR = (3<<3); // re-enable receive monitoring after sending
			LPC_TIM2->MR1 = TE;     // set the half period
			
			if (backward_frame & 0x100) {         // backward frame (answer) completed ?
				answer = (uint8_t)backward_frame; // OK ! save answer
				f_dalirx = 1;                     // and set flag to signal application
				print = true;
			}
			
			backward_frame = 0;     // reset receive frame
			f_busy = 0;             // end of transmission
			
			if (f_repeat)     // repeat forward frame ?
				f_dalitx = 1; // yes, set flag to signal application
		}
		
		frame_bit_idx++;        // Increment half bit index
		LPC_TIM2->IR = MR1_IRQ; // Clear MR1 interrupt flag
		
	} else {

		/* MR0 IRQ - seen two stop bits */	
		if(LPC_TIM2->IR & MR0_IRQ) {  
			if(backward_frame) {
				print = true;
			}
			LPC_TIM2->IR = MR0_IRQ;   // Clear the IRQ
			
		/* CR0 IRQ - rising or falling edge */
		} else {                      
			if(!dali_rx.read()) {
				// Rising Edge 
				if(backward_frame != 0) {
					low_time = LPC_TIM2->CR0;
					dali_decode();
				} else {
					previous = 1;
					dali_shift_bit(1);
				}
			} else {  
				// Falling edge
				high_time = LPC_TIM2->CR0;       
			}
			LPC_TIM2->IR = CR0_IRQ;  // Clear the IRQ
		} 
	}
	
	if(print) {
		// _cb(backward_frame);
		// _cb(answer);
	}
}



/*
   Function    : dali_decode()
   Description : when receiving a Dali frame, the interrupt handler calls 
                 dali_decode() on the rising edge of the RX pin.
				 
				 We have previously stored the value of the Capture 
				 register in either the low_time or the high_time
				 variables depending on whether we saw a falling or 
				 rising edge.  These are then used to decode the
				 series of Manchester encoded bits received.


				 As an example, consider the bit pattern "10" in the following
				 Manchester encoded waveform.  A rising edge in the middle of
				 bit-period represents a "1" and a falling edge a "0".

				 Considering that the timer is reset on each edge, it should
				 be clear that the rising edge of bit-1 was preceded by a 
				 short low_time that is equal to TE, the Manchester coded
				 half period.  In contrast, the falling edge of bit-0 was
				 preceded by a long high_time.


				   . bit-1 . bit-0 .
				   .       .       .
				 --+   +---+---+   .
				   |   |   .   |   .
				   +---+   .   +---+--
				   .       .       .      
                   .  "1"  .  "0"  .   

 				 These timings give rise to the truth table shown below.
				    
   
                Previous                                          New     
                Half Bit  | Low Time | High Time |   Action   | Half Bit
             -------------+----------+-----------+------------+-------------
                   0      |    0     |     0     |  Shift 0   |   0
                   0      |    0     |     1     |  -ERROR-   |   *
                   0      |    1     |     0     |  Shift 0,1 |   1
                   0      |    1     |     1     |  -ERROR-   |   *
                   1      |    0     |     0     |  Shift 1   |   1
                   1      |    0     |     1     |  Shift 0   |   0
                   1      |    1     |     0     |  -ERROR-   |   *
                   1      |    1     |     1     |  Shift 0,1 |   1
             
*/
void Dali::dali_decode() {
    
	uint8_t action = previous << 2;


    if ((high_time > MIN_2TE) && (high_time < MAX_2TE)) {
        action = action | 1; // high_time = long
    
	} else if (!((high_time > MIN_TE) && (high_time < MAX_TE))) {
        backward_frame = 0; // DALI ERROR
        err = 1;
        return;
    }


    if ((low_time > MIN_2TE) && (low_time < MAX_2TE)) {	
        action = action | 2; // low_time = long
    
	} else if (!((low_time > MIN_TE) && (low_time < MAX_TE))) {
        backward_frame = 0; // DALI ERROR    
        err = 2;
        return;
    }


    switch (action) {
        case 0: dali_shift_bit(0); // short low, short high, shift 0
                break;
        case 1: backward_frame = 0; // short low, long high, ERROR
                break;
        case 2: dali_shift_bit(0); // long low, short high, shift 0,1
                dali_shift_bit(1);
                previous = 1; // new half bit is 1
                break;
        case 3: backward_frame = 0; // long low, long high, ERROR
                break;
        case 4: dali_shift_bit(1); // short low, short high, shift 1
                break;
        case 5: dali_shift_bit(0); // short low, long high, shift 0
                previous = 0; // new half bit is 0
                break;
        case 6: backward_frame = 0; // long low, short high, ERROR
                break;
        case 7: dali_shift_bit(0); // long low, long high, shift 0,1
                dali_shift_bit(1);
        default: break; // invalid
    }
}


void Dali::dali_shift_bit(uint8_t val) {
    backward_frame = (backward_frame << 1) | val;
}



/* 
	Function    : dali_send()
	Description : called once the forward_frame has been set up.  The address
	 			  and data .  
*/
void Dali::dali_send() {

	// if (f_repeat) { // repeat last command ?
	// 	f_repeat = 0;
	//
	// } else if ((forward & 0xE100) == 0xA100 || (forward & 0xE100) == 0xC100) {
	// 	if ((forward & 0xFF00) == INITIALISE || forward == RANDOMISE)
	// 		f_repeat = 1; // special command repeat < 100 ms
	//
	// } else if ((forward & 0x1FF) >= 0x120 && (forward & 0x1FF) <= 0x180) {
	// 	f_repeat = 1; // config. command repeat < 100 ms
	// }
	
	while (f_busy) ; // Wait until dali port is idle
	
	answer         = 0;
	backward_frame = 0;
	frame_bit_pol  = 0; // first half of start bit = 0
	frame_bit_idx  = 0;
	f_busy         = 1; // set transfer activate flag
	
	LPC_TIM2->CCR = 0x0000; // disable capture interrupt
	LPC_TIM2->MCR = (3<<3); // only enable MR1 during send
	LPC_TIM2->TCR = 2;      // reset timer
	LPC_TIM2->TCR = 1;      // enable timer
}


/*
	Function    : dali_put()
	Description : 
*/
void Dali::put(dali_payload_t dali_cmd) {
	if(dali_cmd.control.is_req) {
	}
}

void Dali::broadcast(uint8_t command) {
	forward_frame = (0xFF << 8) | command;
	dali_send();
}

void Dali::query_device_type(uint8_t addr) {
	forward_frame = (addr << 9) | 0x199;
	dali_send();
}

void Dali::query_short_address(void) {
	forward_frame = 0xBB00;
	dali_send();
}

void Dali::turn_on(uint8_t addr) {
	forward_frame = 0x7E00 & (addr << 9);
	forward_frame |= 0x105;
	dali_send();
}

void Dali::turn_off(uint8_t addr) {
	forward_frame = 0x7E00 & (addr << 9);
	forward_frame |= 0x100;
	dali_send();	
}
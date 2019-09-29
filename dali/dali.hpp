#ifndef MBED_DALI_H
#define MBED_DALI_H

// Dali Defines
#define TE 834/2      // half bit time = 417 usec
#define MIN_TE 350    // minimum half bit time (352)
#define MAX_TE 490    // maximum half bit time (482)
#define MIN_2TE 760   // minimum full bit time (760)
#define MAX_2TE 900   // maximum full bit time (899)
#define STP_2TE 1800  // maximum time for two stop bits

#define MR0_IRQ 1<<0
#define MR1_IRQ 1<<1
#define MR2_IRQ 1<<2
#define MR3_IRQ 1<<3
#define CR0_IRQ 1<<4
#define CR1_IRQ 1<<5

#define ALL_OFF 0x06
#define ALL_ON  0x05

#define FLAG_SOCKET_ACCEPT  (1<<0)
#define FLAG_SOCKET_CLOSED  (1<<1)
#define FLAG_CLIENT_CONNECT (1<<2)


typedef struct {
	uint8_t repeat       : 1;
	uint8_t response_req : 1;
	uint8_t is_rsp       : 1;
	uint8_t is_req       : 1;
	uint8_t              : 4;
} dali_ctrl_t;


typedef struct {
	dali_ctrl_t control;
	uint8_t     address;
	uint8_t     command;
	uint8_t     response;
} dali_payload_t;


class Dali {

public:
	/** Receives a pair of PinName variables
	* @param rxPin mbed pin to which the Dali Rx signal is connected
	* @param txPin mbed pin to which the Dali Tx signal is connected	
	*/
	Dali(PinName rxPin, PinName txPin);
	
	/* Pointer to the class instance so we can register the ISR */
	static Dali* handler;
	
	/* Called durint init() to bind the interrupt handler. */
	static void irq() {
		handler->timer_isr();
	}
	
	void server_sigio(TCPSocket *socket);
	void client_sigio(TCPSocket *socket);
	
	/* Main Dali function */
	void put(dali_payload_t dali_cmd);
	
	/* Function to pass pointer to Serial instance. */
	void attach_uart(Serial *uart);
	
	/* Low level Dali commands */
	void broadcast(uint8_t command);
	void query_device_type(uint8_t addr);
	void query_short_address(void);
	void turn_on(uint8_t addr);
	void turn_off(uint8_t addr);
	void dali_cmd_16(uint8_t addr, uint16_t data);

	TCPSocket *client;
	EventQueue *queue;
	
	
private:

	InterruptIn dali_rx;
	DigitalOut dali_tx;
	SocketAddress clientAddress;
	EventFlags eventFlags;
	Serial *_uart;
	
	uint32_t low_time;        			// captured puls low time
	uint32_t high_time;       			// captured puls high time
	uint32_t counter;        			// 
	volatile uint8_t frame_bit_pol;		// used for dali send bit
	volatile uint8_t frame_bit_idx;		// keeps track of sending bit frame_bit_idx
	volatile uint8_t previous;			// previous received bit
	uint16_t forward_frame;   			// holds the forward frame to be transmitted
	uint16_t backward_frame;			// holds received slave backward frame
	volatile uint8_t answer;           	// holds answer from slave
	volatile uint8_t f_repeat;         	// flag command shall be repeated
	volatile uint8_t f_busy;           	// flag DALI transfer busy
	uint8_t f_dalitx;
	uint8_t f_dalirx;
	uint32_t err;
	volatile uint32_t leds;
	bool print;
	uint32_t te_stop = 33;              // number of half cycles to the stop bit (changes for data width)

	
	Callback<void(TCPSocket *sock)> _client_handler;

	void init();
	void init_timer();
	void timer_isr();
	void dali_decode();
	void dali_shift_bit(uint8_t val);
	void dali_send();
	
};

#endif


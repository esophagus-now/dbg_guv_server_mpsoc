#ifndef AXISTREAMFIFO_H
#define AXISTREAMFIFO_H 1

typedef struct {
    unsigned ISR;  //Interrupt status register
    unsigned IER;  //Interrupt enable register
    unsigned TDFR; //TX FIFO Reset (use 0xA5 to reset)
    unsigned TDFV; //TX FIFO Vacancy
    unsigned TDFD; //TX FIFO Data
    unsigned TLR;  //TX Length
    unsigned RDFR; //RX FIFO Reset
    unsigned RDFO; //RX FIFO Occupancy
    unsigned RDFD; //RX FIFO Data
    unsigned RLR;  //RX Length 
    unsigned SRR;  //Reset all
    unsigned TDR;  //TX DEST
    unsigned RDR;  //RX DEST
} AXIStream_FIFO;

#define RPURE_MASK 0x80000000 //Receive Packet Underrun Error
#define RPORE_MASK 0x40000000 //Receive Packet Overrun Read Error
#define RPUE_MASK  0x20000000 //Receive Packet Underrun Error
#define TPOE_MASK  0x10000000 //Transmit Packet Overrun Error
#define TC_MASK    0x08000000 //Transmit Complete
#define RC_MASK    0x04000000 //Receive Complete
#define TSE_MASK   0x02000000 //Transmit Size Error
#define TRC_MASK   0x01000000 //Transmit Reset Complete
#define RRC_MASK   0x00800000 //Receive Reset Complete
#define TFPF_MASK  0x00400000 //Transmit FIFO Programmable Full
#define TFPE_MASK  0x00200000 //Transmit FIFO Programmable Empty
#define RFPF_MASK  0x00100000 //Receive FIFO Programmable Full
#define RFPE_MASK  0x00080000 //Receive FIFO Programmable Empty

#define TX_ERR_MASK (TPOE_MASK | TSE_MASK)
#define RX_ERR_MASK (RPURE_MASK | RPORE_MASK | RPUE_MASK)


#define ASFIFO_ERRCODES_IDENTS \
    X(ASFIFO_SUCCESS), /*This has a code of 0*/ \
    X(E_TX_FIFO_NO_ROOM), \
    X(E_RX_FIFO_EMPTY), \
    X(E_ERR_IRQ), \
    X(E_NULL_ARG)

#define X(x) x
enum {
    ASFIFO_ERRCODES_IDENTS
};
#undef X

void print_interrupt_info(unsigned ISR);

//Returns what was previously in ISR
unsigned clear_ints(volatile AXIStream_FIFO *base);

//Issues a reset to the TX logic. Returns 0 on successful reset, -1 on error
int reset_TX(volatile AXIStream_FIFO *base);

//Issues a reset to the RX logic. Returns 0 on successful reset, -1 on error
int reset_RX(volatile AXIStream_FIFO *base);

//Issues a reset to the AXI-Stream FIFO. Returns 0 on successful reset, -1 on error
int reset_all(volatile AXIStream_FIFO *base);

//Of course, the AXI Stream FIFO has bizarre behaviour for this quantity, but 
//here it is anyway. It is measured in 32-bit words
unsigned tx_fifo_word_vacancy(volatile AXIStream_FIFO *base);

//Sends buf to an AXI Stream FIFO. Does not perform any checking; just sends.
//If you're sending a bunch of 32 bit unsigneds, then unchecked_send_words has
//much better performance
void unchecked_send_buf(volatile AXIStream_FIFO *base, char *buf, int len);

//Sends an array of 32 bit values. Does not check anything; it's up to you to be
//sure that this is a legal transfer.
void unchecked_send_words(volatile AXIStream_FIFO *base, unsigned *vals, int words);

//Call this to check for errors after sending something. Clears the TX-related
//error interrupts. Returns 1 if error occurred, 0 if no error
int tx_err(volatile AXIStream_FIFO *base);

//Sends buf, but checks if there is room first, and checks for error interrupts
//Basically, clears TX-related error interrupts, combines tx_fifo_vacancy, 
//unchecked_send_buf, and check_tx_err
int send_buf(volatile AXIStream_FIFO *base, char *buf, int len);

//Sends an array of 32 bit words. It will first check if there is room, and also
//checks for error interrupts. Basically combines tx_fifo_word_vacancy, 
//unchecked_send_words, and tx_err. Returns negative error code, or 0 if 
//everything was fine.
int send_words(volatile AXIStream_FIFO *base, unsigned *vals, int words);

//Tells you how many words are in the receive FIFO (kind of; the AXI Stream 
//FIFO has very weird behaviour for this)
unsigned rx_fifo_word_occupancy(volatile AXIStream_FIFO *base);

typedef enum _asfifo_mode_t {
    CUT_THROUGH,
    STORE_AND_FORWARD
} asfifo_mode_t;

typedef enum {
    READ_WORDS_IDLE,
    READ_WORDS_TRANSFERRING
} rw_state_t;

//Reads a number of words out from the AXI-Stream FIFO. Has the same semantics
//as the read() system call; returns number of words read, and will not read 
//more than you ask for.
//
//However, there is one key difference: if this function returns 0, it doesn't
//mean it's the end of the packet. Instead, to use this function, you must do
//
//  rw_state_t   my_fifo_state = READ_WORDS_IDLE;
//  do {
//		...
//      int num_read = unchecked_read_words(fifo_base, buf, num_to_read, &my_fifo_state);
//      ...
//  while (my_fifo_state != READ_WORDS_IDLE);
//
//NOTE: you must maintain a separate state for each FIFO!
//
//Does not check if the transfer will be legal; this can cause all kinds of 
//issues! Also, does not support partial words transfers
int unchecked_read_words(volatile AXIStream_FIFO *base, unsigned *dst, int words, rw_state_t *state);

//Call this to check for errors after receiving something. Clears the RX-related
//error interrupts. Returns 1 if error occurred, 0 if no error
int rx_err(volatile AXIStream_FIFO *base);

//Same as unchecked_read_words, but checks for errors. Basically combines 
//unchecked_read_words with rx_err. Honestly this function is kind of dumb, but
//whatever.
//
//However, there is one key difference: if this function returns 0, it doesn't
//mean it's the end of the packet. Instead, to use this function, you must do
//
//  rw_state_t   my_fifo_state = READ_WORDS_IDLE;
//  do {
//		...
//      int num_read = unchecked_read_words(fifo_base, buf, num_to_read, &my_fifo_state);
//      ...
//  while (my_fifo_state != READ_WORDS_IDLE);
//
//NOTE: you must maintain a separate state for each FIFO!
//
//Also, the AXI Stream FIFO is a bit inconvenient because there is no way to
//discover if it is in store-and-forward or cut-through, so I need the user to
//pass that information in as a parameter.
int read_words(volatile AXIStream_FIFO *base, asfifo_mode_t mode, unsigned *dst, int words, rw_state_t *state);

//Get string for an error code
char const* asfifo_strerror(int code);
#endif

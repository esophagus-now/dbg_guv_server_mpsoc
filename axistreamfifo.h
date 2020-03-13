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

void print_interrupt_info(unsigned ISR);

//Returns what was previously in ISR
unsigned clear_ints(volatile AXIStream_FIFO *base);

//Issues a reset to the TX logic. Returns 0 on successful reset, -1 on error
int reset_TX(volatile AXIStream_FIFO *base);

//Issues a reset to the RX logic. Returns 0 on successful reset, -1 on error
int reset_TX(volatile AXIStream_FIFO *base);

//Issues a reset to the AXI-Stream FIFO. Returns 0 on successful reset, -1 on error
int reset_all(volatile AXIStream_FIFO *base);

#endif

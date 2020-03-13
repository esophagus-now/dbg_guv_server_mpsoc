#include <stdio.h>
#include "axistreamfifo.h"

void print_interrupt_info(unsigned ISR) {
    fprintf(stderr, "Interrupt info, ISR=0x%08x\n", ISR);
    
    //Test each bit of the register and print out associated info
    
    if (ISR & RPURE_MASK) {
        fprintf(stderr, "\t(ERROR) Tried reading RLR when it was empty\n");
    }
    if (ISR & RPORE_MASK) {
        fprintf(stderr, "\t(ERROR) Tried to read too many words from RX FIFO\n");
    }
    if (ISR & RPUE_MASK) {
        fprintf(stderr, "\t(ERROR) Tried to read from RX FIFO when it was empty\n");
    }
    if (ISR & TPOE_MASK) {
        fprintf(stderr, "\t(ERROR) Tried to write to TX FIFO when it was full\n");
    }
    if (ISR & TC_MASK) {
        fprintf(stderr, "\tTransmit complete\n");
    }
    if (ISR & RC_MASK) {
        fprintf(stderr, "\tReceive complete\n");
    }
    if (ISR & TSE_MASK) {
        fprintf(stderr, "\t(ERROR) Size given in TLR did not make sense\n");
    }
    if (ISR & TRC_MASK) {
        fprintf(stderr, "\tTransmit reset complete\n");
    }
    if (ISR & RRC_MASK) {
        fprintf(stderr, "\tReceive reset complete\n");
    }
    if (ISR & TFPF_MASK) {
        fprintf(stderr, "\tTX FIFO programmable full\n");
    }
    if (ISR & TFPE_MASK) {
        fprintf(stderr, "\tTX FIFO programmable empty\n");
    }
    if (ISR & RFPF_MASK) {
        fprintf(stderr, "\tRX FIFO programmable full\n");
    }
    if (ISR & RFPE_MASK) {
        fprintf(stderr, "\tRX FIFO programmable empty\n");
    }
}

//Returns what was previously in ISR
unsigned clear_ints(volatile AXIStream_FIFO *base) {
    unsigned ISR = base->ISR;
    base->ISR = 0xFFFFFFFF;
    return ISR;
}

//Issues a reset to the TX logic. Returns 0 on successful reset, -1 on error
int reset_TX(volatile AXIStream_FIFO *base) {
    base->ISR = TRC_MASK; //Clear Transmit Reset Complete bit
    
    base->TDFR = 0xA5; //Issue reset command
    
    //Check if reset happened succesfully
    unsigned ISR = base->ISR;
    
    if (ISR & TRC_MASK) return 0;
    else return -1;
}

//Issues a reset to the RX logic. Returns 0 on successful reset, -1 on error
int reset_RX(volatile AXIStream_FIFO *base) {
    base->ISR = RRC_MASK; //Clear Transmit Reset Complete bit
    
    base->RDFR = 0xA5; //Issue reset command
    
    //Check if reset happened succesfully
    unsigned ISR = base->ISR;
    
    if (ISR & RRC_MASK) return 0;
    else return -1;
}

//Issues a reset to the AXI-Stream FIFO. Returns 0 on successful reset, -1 on error
int reset_all(volatile AXIStream_FIFO *base) {
    base->ISR = RRC_MASK | TRC_MASK; //Clear Transmit and Receive Reset Complete bits
    
    base->SRR = 0xA5; //Issue reset command
    
    //Check if reset happened succesfully
    unsigned ISR = base->ISR;
    
    if ((ISR & RRC_MASK) && (ISR & TRC_MASK)) return 0;
    else return -1;
}

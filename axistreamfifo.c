#include <stdio.h>
#include "axistreamfifo.h"

//My naming styles are over the map
#define X(x) #x
char *ASFIFO_ERRCODE_STRINGS[] = {
    ASFIFO_ERRCODES_IDENTS
};
#undef X

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

//Of course, the AXI Stream FIFO has bizarre behaviour for this quantity, but 
//here it is anyway. It is measured in 32-bit words
unsigned tx_fifo_word_vacancy(volatile AXIStream_FIFO *base) {
    unsigned TDFV = base->TDFV;
    return TDFV & 0x1FFFF; //Why is this a 17 bit number?
}

//Sends buf to an AXI Stream FIFO. Does not perform any checking; just sends.
//If you're sending a bunch of 32 bit unsigneds, then unchecked_send_words has
//much better performance
void unchecked_send_buf(volatile AXIStream_FIFO *base, char *buf, int len) {
    int words = ((len+3)/4); //words = ceil(len/4)
    //Endianness makes our lives difficult...
    union {
        unsigned w;
        char byte[4];
    } u;
    
    //Do all words except the last, which may be partial
    int i;
    for (i = 0; i < words - 1; i++) {
        u.byte[3] = buf[0];
        u.byte[2] = buf[1];
        u.byte[1] = buf[2];
        u.byte[0] = buf[3];
        buf += 4;
        
        //Somewhere along the way, the PS reverses the order of the bytes in 
        //32-bit transfers before they get into the PL; this is why we had to
        //manually fiddle with the endianness. "A fix for a fix"...
        base->TDFD = u.w;
    }
}

//Sends an array of 32 bit values. Does not check anything; it's up to you to be
//sure that this is a legal transfer.
void unchecked_send_words(volatile AXIStream_FIFO *base, unsigned *vals, int words) {
    int i;
    for (i = 0; i < words; i++) {        
        //Somewhere along the way, the PS reverses the order of the bytes in 
        //32-bit transfers, so this is fine
        base->TDFD = vals[i];
    }
}

//Call this to check for errors after sending something. Clears the TX-related
//error interrupts. Returns 1 if error occurred, 0 if no error
int tx_err(volatile AXIStream_FIFO *base) {
    unsigned ISR = base->ISR;
    base->ISR |= TX_ERR_MASK;
    if (ISR & TX_ERR_MASK) return 1;
    else return 0;
}

//Sends buf, but checks if there is room first, and checks for error interrupts
//Basically, clears TX-related error interrupts, combines tx_fifo_vacancy, 
//unchecked_send_buf, and check_tx_err
int safe_send_buf(volatile AXIStream_FIFO *base, char *buf, int len) {
    //Check if there is enough room
    unsigned vcy = tx_fifo_word_vacancy(base);
    if (vcy < ((len+3)/4)) return -E_TX_FIFO_NO_ROOM;
    
    //Clear error interrupts so we don't get confused by old messages
    base->ISR = TX_ERR_MASK;
    
    //Actually send the buffer
    unchecked_send_buf(base, buf, len);
    
    //Check if an error occurred
    if (tx_err(base)) {
        return -E_ERR_IRQ;
    }
    
    return ASFIFO_SUCCESS;
}

//Sends an array of 32 bit words. It will first check if there is room, and also
//checks for error interrupts. Basically combines tx_fifo_word_vacancy, 
//unchecked_send_words, and tx_err. Returns negative error code, or 0 if 
//everything was fine.
int safe_send_words(volatile AXIStream_FIFO *base, unsigned *vals, int words) {
    //Check if there is enough room
    unsigned vcy = tx_fifo_word_vacancy(base);
    if (vcy < words) return -E_TX_FIFO_NO_ROOM;
    
    //Clear error interrupts so we don't get confused by old messages
    base->ISR = TX_ERR_MASK;
    
    //Actually send the buffer
    unchecked_send_words(base, vals, words);
    
    //Check if an error occurred
    if (tx_err(base)) {
        return -E_ERR_IRQ;
    }
    
    return ASFIFO_SUCCESS;
}

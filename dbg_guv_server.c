#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sched.h>
#include "axistreamfifo.h"
#include "queue.h"

char *usage = 
"Usage: dbg_guv_server 0xRX_ADDR [0xTX_ADDR]\n"
"\n"
"  Opens a server on port 5555. RX_ADDR is the address of the AXI-Stream FIFO\n"
"  that is receiving flits. TX_ADDR is the address of the AXI-Stream FIFO that\n"
"  is sending commands (only supply it if it is different from RX_ADDR\n"
;

int main(int argc, char **argv) {
    int fd = -1, sfd = -1;
    void *base_rx = MAP_FAILED;
    void *base_tx = MAP_FAILED;
    
    unsigned long rd_fifo_phys;
    unsigned long wr_fifo_phys;
    
    if (argc < 2 || argc > 4) {
        puts(usage);
        return 0;
    } else if (argc == 2) {
        //Get RX_ADDR from argv[1]
        int rc = sscanf(argv[1], "%lx", &rd_fifo_phys);
        if (rc != 1) {
            fprintf(stderr, "Error: could not parse [%s]\n", argv[1]);
            return -1;
        }
        //Check that rd_fifo_phys is in range
        if (rd_fifo_phys < 0xA0000000 || rd_fifo_phys > 0xA0FFFFFF) {
            //I don't actually know the maximum allowable address
            printf("RX_ADDR is out of range!\n");
            return -1;
        }

        //Check rd_fifo_phys has 32 bit alignment
        if (rd_fifo_phys & 0b11) {
            printf("Error! Addresses must be 32-bit aligned\n");
            return -1;
        }	
        wr_fifo_phys = rd_fifo_phys;
    } else {
        //Get RX_ADDR from argv[1]
        int rc = sscanf(argv[1], "%lx", &rd_fifo_phys);
        if (rc != 1) {
            fprintf(stderr, "Error: could not parse RX_ADDR = [%s]\n", argv[1]);
            return -1;
        }
        //Check that rd_fifo_phys is in range
        if (rd_fifo_phys < 0xA0000000 || rd_fifo_phys > 0xA0FFFFFF) {
            //I don't actually know the maximum allowable address
            printf("RX_ADDR is out of range!\n");
            return -1;
        }

        //Check rd_fifo_phys has 32 bit alignment
        if (rd_fifo_phys & 0b11) {
            printf("Error! Addresses must be 32-bit aligned\n");
            return -1;
        }	
        
        //Get TX_ADDR from argv[2]
        rc = sscanf(argv[2], "%lx", &wr_fifo_phys);
        if (rc != 1) {
            fprintf(stderr, "Error: could not parse TX_ADDR = [%s]\n", argv[2]);
            return -1;
        }
        //Check that wr_fifo_phys is in range
        if (wr_fifo_phys < 0xA0000000 || wr_fifo_phys > 0xA0FFFFFF) {
            //I don't actually know the maximum allowable address
            printf("TX_ADDR is out of range!\n");
            return -1;
        }

        //Check wr_fifo_phys has 32 bit alignment
        if (wr_fifo_phys & 0b11) {
            printf("Error! Addresses must be 32-bit aligned\n");
            return -1;
        }
    }
    
    //Before we screw around with mmap and hardware registers, get our server 
    //up and running
    
    sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) {
        perror("Could not open socket");
        goto err_nothing;
    }
    
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(5555),
        .sin_addr = {INADDR_ANY}
    };

    rc = bind(sfd, (struct sockaddr *) &server_addr, sizeof(struct sockaddr_in));
    if (rc < 0) {
        perror("Could not bind to port 5555");
        goto err_close_socket;
    }
    
    //At this point, all addresses are guaranteed safe. Proceed to open device
    //files.
    
    fd = open("/dev/mpsoc_axiregs", O_RDWR | O_SYNC);
    if (fd < 0) {
		perror("Could not open /dev/mpsoc_axiregs");
		goto err_close_socket;
    }

    volatile AXIStream_FIFO *rx_fifo, *tx_fifo;

    //Perform mmap for RX FIFO
	unsigned long pg_aligned = (rd_fifo_phys | 0xFFF) - 0xFFF; //Mask out lower bits
	unsigned long pg_off = rd_fifo_phys & 0xFFF; //Get only lower bits

	base_rx = mmap(
		0, //addr: Can be used to pick & choose virtual addresses. Ignore it.
		4096, //len: We'll (arbitrarily) map a whole page
		PROT_READ | PROT_WRITE, //prot: We want to read and write this memory
		MAP_SHARED, //flags: Allow others to use this memory
		fd, //fildes: File descriptor for device file we're mmmapping
		(pg_aligned - 0xA0000000) //off: (Page-aligned) offset into FPGA memory
	);
    if (base_rx == MAP_FAILED) {
		perror("Could not mmap RX FIFO device memory");
		goto err_close_fd;
	}
    rx_fifo = (volatile AXIStream_FIFO *) (base_rx + pg_off);

    if (wr_fifo_phys == rd_fifo_phys) {
        base_tx = base_rx;
        tx_fifo = rx_fifo;
    } else {
        //Perform separate mmap for TX FIFO
        pg_aligned = (wr_fifo_phys | 0xFFF) - 0xFFF; //Mask out lower bits
        pg_off = wr_fifo_phys & 0xFFF; //Get only lower bits

        base_tx = mmap(
            0, //addr: Can be used to pick & choose virtual addresses. Ignore it.
            4096, //len: We'll (arbitrarily) map a whole page
            PROT_READ | PROT_WRITE, //prot: We want to read and write this memory
            MAP_SHARED, //flags: Allow others to use this memory
            fd, //fildes: File descriptor for device file we're mmmapping
            (pg_aligned - 0xA0000000) //off: (Page-aligned) offset into FPGA memory
        );
        if (base_tx == MAP_FAILED) {
            perror("Could not mmap TX FIFO device memory");
            goto err_unmap_rx;
        }
        tx_fifo = (volatile AXIStream_FIFO *) (base_tx + pg_off);
    }
    
    //At this point, we have our rx_fifo and tx_fifo pointers and we can get to
    //work. First, we rest the AXI Stream FIFO cores:
    
    rc = reset_all(rx_fifo);
    if (rc != 0) puts("Warning: RX FIFO might not have reset correctly");
    rc = reset_all(tx_fifo);
    if (rc != 0) puts("Warning: TX FIFO might not have reset correctly");
    
    //We're now ready to accept incoming connections. Spin up the thread to
    //receive commands, and then a thread to send out logged flits
    
    return 0;

err_unmap_tx:
    if (base_tx != MAP_FAILED && base_tx != base_rx) munmap(base_tx, 4096);
err_unmap_rx:
    if (base_rx != MAP_FAILED) munmap(base_rx, 4096);
err_close_fd:
    if (fd != -1) close(fd);
err_close_socket:
    if (sfd != -1) close(sfd);
err_nothing:
    return -1;

}

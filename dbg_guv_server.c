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

typedef struct {
    //Never modified by the thread
    int server_sfd;
    
    //Only used by the thread; so no need to serialize access
    int client_sfd;
    int client_is_connected;
    
    queue *q;
} net_rx_info;

void net_rx_cleanup(void *arg) {
    net_rx_info *info = (net_rx_info *) arg;
    queue *q = info->q;
    
    if(info->client_is_connected) {
        close(info->client_sfd);
        info->client_is_connected = 0;
    }
    
    pthread_mutex_lock(&q->mutex);
    q->num_producers--;
    pthread_mutex_unlock(&q->mutex);
}

//Remember to increment arg->q->num_producers before spinning up this thread
void* net_rx(void *arg) {
    net_rx_info *info = (net_rx_info *) arg;
    queue *q = info->q;
    
    info->client_is_connected = 0;
    
    pthread_cleanup_push(net_rx_cleanup, arg);
    
    //Listen for and accept incoming connections
    int rc = listen(info->server_sfd, 1);
    if (rc < 0) {
        perror("Could not listen on socket");
        goto done;
    }
    
    struct sockaddr_in client_addr; //In case we ever want to use it
    unsigned client_addr_len = sizeof(client_addr);
    int client_sfd = accept(info->server_sfd, (struct sockaddr*)&client_addr, &client_addr_len);
    if (client_sfd < 0) {
        perror("Could not accept incoming connection");
        goto done;
    }
    info->client_sfd = client_sfd;
    info->client_is_connected = 1;
    
    //Now we just read in a loop, constantly filling the queue
    int len;
    char buf[64];
    while((len = read(client_sfd, buf, 64)) != 0) {
        if (len < 0) {
            perror("Error reading from network");
            goto done;
        }
        
        queue_write(q, buf, len);
    }
    
    done:
    pthread_cleanup_pop(1);
    pthread_exit(NULL);
}

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
    
    int rc;
    
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
    queue net_rx_queue = QUEUE_INITIALIZER;
    pthread_t net_rx_thread;
    
    net_rx_info net_rx_args = {
        .server_sfd = sfd,
        .q = &net_rx_queue
    }; 
    
    net_rx_queue.num_producers++;
    pthread_create(&net_rx_thread, NULL, net_rx, &net_rx_args);
    
    while (1) {
        char in_cmd[8];
        rc = nb_dequeue_n(&net_rx_queue, in_cmd, 8);
        if (rc == 0) {
            printf("Received command: 0x");
            int i;
            for (i = 0; i < 8; i++) {
                //Endianness???
                printf("%02x", in_cmd[i] & 0xFF);
            }
            printf("\n");
        }
    }
    
    pthread_join(net_rx_thread, NULL);
    if (base_tx != MAP_FAILED && base_tx != base_rx) munmap(base_tx, 4096);
    if (base_rx != MAP_FAILED) munmap(base_rx, 4096);
    if (fd != -1) close(fd);
    if (sfd != -1) close(sfd);
    
    return 0;
    
    
err_unmap_rx:
    if (base_rx != MAP_FAILED) munmap(base_rx, 4096);
err_close_fd:
    if (fd != -1) close(fd);
err_close_socket:
    if (sfd != -1) close(sfd);
err_nothing:
    return -1;

}

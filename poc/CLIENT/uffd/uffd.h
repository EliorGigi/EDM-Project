#ifndef UFFD_H
#define UFFD_H

#include <inttypes.h>
#include <sys/types.h>
#include <stdio.h>
#include <linux/userfaultfd.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <thread>
#include "../../shared/mpiEdm.h"
#include "../../shared/logger.h"

#define PRINT_AS_HEX(ADDR) std::hex << "0x" << ADDR << std::dec

#define PAGE_SIZE 4096
class Client;

class Uffd {
    private:

    long uffd;          /* userfaultfd file descriptor */
    char *addr;         /* Start of region handled by userfaultfd */
    uint64_t len;       /* Length of region handled by userfaultfd */
    std::thread thr;      /* ID of thread that handles page faults */
    MPI_EDM::MpiApp* mpi_instance;
    Client* client; 

    public:
    Uffd() = default;
    Uffd(MPI_EDM::MpiApp* mpi_instance, Client* client);
    ~Uffd() = default;
    void ListenPageFaults();
    void HandleMissPageFault(struct uffd_msg* msg);
    std::thread ActivateDM_Handler();

};
#endif
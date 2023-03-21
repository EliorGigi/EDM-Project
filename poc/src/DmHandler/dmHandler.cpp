#include <iostream>
#include "dmHandler.h"
#include "../appMonitor.h"

#define DEBUG_MODE 1

DmHandler::DmHandler(AppMonitor* client, int high_threshold, int low_threshold,pid_t pid) {
    this->len = len;
    this->addr = addr;
    this->high_threshold = high_threshold;
    this->low_threshold = low_threshold;
    this->pid = pid;

    this->uffd_son = injectUffdCreate(pid);
    this->uffd = duplicateFileDescriptor(pid, uffd_son);
    LOG(INFO) << "[DmHandler] - Userfaultfd injected to process. Usercode fd: " << this->uffd_son << "Polling duplicated fd:" << this->uffd; 
    this->client = client;
}


void DmHandler::ListenPageFaults(){

    static struct uffd_msg msg;   /* Data read from userfaultfd */
    ssize_t nread;

    /* Loop, handling incoming events on the userfaultfd
        file descriptor. */
    struct pollfd pollfd;
    pollfd.fd = uffd;
    pollfd.events = POLLIN;

    LOG(DEBUG) << "[DmHandler] - Waiting for events " << "fd: " << this->uffd;
    while (poll(&pollfd, 1, -1) > 0)
    {
        /* Read an event from the userfaultfd. */
        nread = read(uffd, &msg, sizeof(msg));
        if (nread == 0) {
            LOG(ERROR) << " [DmHandler] - EOF on userfaultfd! ";
            exit(EXIT_FAILURE);
        }
        if (nread == -1) {
            perror("read");
        }
        switch (msg.event) {
            case UFFD_EVENT_PAGEFAULT:
                HandleMissPageFault(&msg);
                break;
            case UFFD_EVENT_FORK:
                break;
            case UFFD_EVENT_REMAP:
                break;
            case UFFD_FEATURE_EVENT_REMOVE:
                break;
            default:
                break;
        }
    }
}

void DmHandler::HandleMissPageFault(struct uffd_msg* msg){

    /* Display info about the page-fault event. */
    LOG(INFO) << "\n--------------START HADNLING PAGE FAULT--------------";
    LOG(INFO) << "[DmHandler] - UFFD_EVENT_PAGEFAULT in address = " << PRINT_AS_HEX(msg->arg.pagefault.address) ;
    
    unsigned long long vaddr = msg->arg.pagefault.address;
    
    //handle race condition with lpet
    while(this->client->lspt.IsPageExist(vaddr)) {
        //busy wait. should be very short period of time
    }

    
    InvokeLpetIfNeeded();

    //for debug, wait until lpet done 
    if (DEBUG_MODE) {
        while(this->client->is_lpet_running) {}
    }
    LOG(INFO) << "[DmHandler] - send request for the page in address " << PRINT_AS_HEX(vaddr) << " from redis";
    /* thinking about error handling approach, thus:*/
    try {
        std::string str_vaddr = convertToHexRep(vaddr);
        auto request_page = RedisClient::getInstance()->redis_instance->get(str_vaddr); /* conversion should be ok*/
        /* now request_page is of type sw::Redis::OptionalString, meaning Optional<std::string>*/
        if (request_page) /* key exists*/ {
            LOG(INFO) << "[DmHandler] - copying page content from redis to address : " << PRINT_AS_HEX(vaddr);
           CopyExistingPage(vaddr,request_page.value().c_str()); /* here it's request_page since it's the key's value in Redis*/
            /* request_page is converted to const char **/
        }
        else { /* key does not exist*/
            LOG(INFO) << "[DmHandler] - copying zero page to address : " << PRINT_AS_HEX(vaddr);
            CopyZeroPage(vaddr);
        }
    }
    
    catch (...) {   /* an error has occured - catch any type of error*/  
        LOG(ERROR) << "[DmHandler] - failed to resolve page fault for address " <<  PRINT_AS_HEX(vaddr) ;

    }
    
    this->client->lspt.Add(Page(vaddr, pid));
}

void DmHandler::InvokeLpetIfNeeded(){ 
    std::unique_lock<std::mutex> lck(this->client->run_lpet_mutex);
    if (this->client->lspt.GetSize() >= high_threshold ) {
        LOG(INFO) << "[DmHandler] - reached high threshold, waking up lpet" ;
        this->client->cv.notify_all();
    }
    //avoid memory flood
    while (this->client->lspt.GetSize() >= high_threshold){
        LOG(DEBUG) << "[DmHandler] - avoid memory flood- dmhandler goes to sleep";
        this->client->cv.wait(lck);
    }
}

void DmHandler::CopyZeroPage(uintptr_t vaddr){
    struct uffdio_zeropage uffdio_zero;

    uffdio_zero.range.start =  (unsigned long) vaddr &
                        ~(PAGE_SIZE - 1);
    uffdio_zero.range.len = PAGE_SIZE;
    uffdio_zero.mode = 0;
    LOG(INFO) << "\n--------------FINISH HADNLING PAGE FAULT--------------\n\n";

    if (ioctl(uffd, UFFDIO_ZEROPAGE, &uffdio_zero) == -1)
        LOG(ERROR) << "[DmHandler] - ioctl UFFDIO_ZEROPAGE failed";
    
}

void DmHandler::CopyExistingPage(uintptr_t vaddr,const char* source_page_content){

    static char *page = NULL;
    /* Create a page that will be copied into the faulting region. */

    if (page == NULL) {
        page = (char*)mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (page == MAP_FAILED)
            LOG(ERROR) << "[DmHandler] - mmap temp page for copying failed ";
    }
    
    struct uffdio_copy uffdio_copy;
    memcpy(page, source_page_content, PAGE_SIZE);
    /* Copy the page pointed to by 'page' into the faulting
    region. */
    uffdio_copy.src = (unsigned long) page;
    /* We need to handle page faults in units of pages(!).
        So, round faulting address down to page boundary. */

    uffdio_copy.dst = (unsigned long) vaddr &
                        ~(PAGE_SIZE - 1);
    uffdio_copy.len = PAGE_SIZE;
    uffdio_copy.mode = 0;
    uffdio_copy.copy = 0;
    LOG(INFO) << "\n--------------FINISH HADNLING PAGE FAULT--------------\n\n";
    if (ioctl(uffd, UFFDIO_COPY, &uffdio_copy) == -1)
        LOG(ERROR) << "[DmHandler] - ioctl UFFDIO_COPY failed";
}


std::thread DmHandler::ActivateDM_Handler(){

    std::thread t (&DmHandler::ListenPageFaults,this);
    return t;
}
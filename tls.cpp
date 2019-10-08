// Thread Local Storage Library -- 
// By Edward Yuen | edwardyuensf@gmail.com
// Finished May 2019

// The goal of this project is to implement a library that 
// provides protected memory regions for threads, which they
// can safely use as local storage.

// This was done by implementing thread local storage functions like
// tls_create, tls_write, tls_read, tls_destroy, and tls_clone

#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <vector>
#include <string.h>
#include <iostream>
#include <signal.h>
#include <semaphore.h>

using namespace std;

const int ERROR_CODE = -1;
int pageSize = getpagesize();
bool notInitialized = true;
sem_t mutex_sem;

struct page{
	int referenceCounter;
	char* pagePointer;
};

struct TLS{
	int size;
	int numPages;
	pthread_t tid;
	vector<page*> pageTable;

	TLS(int size, pthread_t tid){
		this->size = size;
		this->tid = tid;
		if (size%pageSize == 0){
			numPages = size/pageSize;
		} else{
			numPages = size/pageSize+1;
		}
		for (int i = 0; i < numPages; i++){
			char* mmapPointer = (char*)mmap(NULL, pageSize, PROT_NONE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
			page* newPage = new page();
			newPage->referenceCounter = 1;
			newPage->pagePointer = mmapPointer;
			pageTable.push_back(newPage);
		}
	}

	TLS(){

	}
};

vector<TLS*> table;

bool tableContains(pthread_t tid){
	for (int i = 0; i < table.size(); i++){
		if (table.at(i)->tid == tid){
			return true;
		}
	}
	return false;
}

TLS* findTlsWithThreadID(pthread_t tid){
	pthread_t index = -1;
	for (int i = 0; i < table.size(); i++){
		if (table.at(i)->tid == tid){
			index = i;
		}
	}

	if (index == -1){
		return NULL;
	} else {
		return table.at(index);
	}
}

void SIGSEGV_handler(int signal, siginfo_t *info, void *ucontext){
	char* segfaultAddressPointer = (char*)info->si_addr;
	int segfaultInPage = 0;

	for (int i = 0; i < table.size(); i++){
		for (int j = 0; j < (table.at(i)->pageTable).size(); j++){
			char* startOfPage = (table.at(i)->pageTable).at(j)->pagePointer;
			char* endOfPage;
			// if on last page, modify end address of page differently
			if (j == (table.at(i)->pageTable).size()-1)
				endOfPage = startOfPage + (table.at(i)->size)%pageSize;
			else 
				endOfPage = startOfPage + pageSize;
			
			// check if the segfault address is an address inside a page
			if ((&startOfPage <= &segfaultAddressPointer) && (&segfaultAddressPointer <= &endOfPage)){
				segfaultInPage = 1;
				break;
			}
		}
	}

	// if segfault occured in a page, kill thread, otherwise, kill the whole process
	if (segfaultInPage == 1){
		pthread_exit(NULL);
	} else {
		struct sigaction sigact;
		sigemptyset(&sigact.sa_mask);
		sigact.sa_handler = SIG_DFL;
		sigact.sa_flags = 0;
		sigaction(SIGSEGV, &sigact, NULL);
		raise(SIGSEGV);

	}


}

void init(){
	sem_init(&mutex_sem, 0 ,1);
	struct sigaction sigact;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_sigaction = SIGSEGV_handler;
	sigact.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &sigact, NULL);


}


int tls_create(unsigned int size){
	if(notInitialized){
		init();
		notInitialized = false;
	}
	sem_wait(&mutex_sem);
	// if user specifies local storage of size 0, throw error
	if (size == 0){
		sem_post(&mutex_sem);
		return ERROR_CODE;
	}
	// if local storage for current thread already exists, throw error
	pthread_t currentTID = pthread_self();
	if (tableContains(currentTID)){
		sem_post(&mutex_sem);
		return ERROR_CODE;
	}

	TLS* newTLS = new TLS(size, currentTID);
	table.push_back(newTLS);
	sem_post(&mutex_sem);
	return 0;

}

int tls_write(unsigned int offset, unsigned int length, char *buffer){
	sem_wait(&mutex_sem);
	// if local storage for current thread DOESN'T exist, throw error
	pthread_t currentTID = pthread_self();
	if (!tableContains(currentTID)){
		sem_post(&mutex_sem);
		return ERROR_CODE;
	}

	// if user trying to write more data than the LSA can hold, throw error
	TLS* currentTLS = findTlsWithThreadID(currentTID);
	if (offset+length > currentTLS->size){
		sem_post(&mutex_sem);
		return ERROR_CODE;
	}

	// find page number and its offset, pageNum will be used to index into the 0-indexed vector of pages
	int pageNum = offset/pageSize;
	int pageOffset = offset%pageSize;
	int remainingMemory = pageSize - offset;

	// unprotects the page of the first write operation by changing protections to PROT_READ/PROT_WRITE
	if ((currentTLS->pageTable.at(pageNum))->referenceCounter > 1){
		((currentTLS->pageTable.at(pageNum))->referenceCounter)--;
		char* mmapPointer = (char*)mmap(NULL, pageSize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);
		
		page* newPage = new page();
		newPage->referenceCounter = 1;
		newPage->pagePointer = mmapPointer;
		mprotect((currentTLS->pageTable.at(pageNum))->pagePointer, pageSize, PROT_READ | PROT_WRITE);
		memcpy(newPage->pagePointer, (currentTLS->pageTable.at(pageNum))->pagePointer, pageSize);
		mprotect((currentTLS->pageTable.at(pageNum))->pagePointer, pageSize, PROT_NONE);
		mprotect(newPage->pagePointer, pageSize, PROT_NONE);
		currentTLS->pageTable.at(pageNum) = newPage;
	}

	mprotect((currentTLS->pageTable.at(pageNum))->pagePointer, pageSize, PROT_READ | PROT_WRITE);
	if (length <= remainingMemory){
		memcpy((currentTLS->pageTable.at(pageNum))->pagePointer + pageOffset, buffer, length);
		mprotect((currentTLS->pageTable.at(pageNum))->pagePointer, pageSize, PROT_NONE);
		sem_post(&mutex_sem);	
		return 0;
	} else {
		memcpy((currentTLS->pageTable.at(pageNum))->pagePointer + pageOffset, buffer, remainingMemory);
	}
	// reprotects the page of the first write operation by changing protections to PROT_NONE
	mprotect((currentTLS->pageTable.at(pageNum))->pagePointer, pageSize, PROT_NONE);


	int remainingLength = length - remainingMemory;
	int offsetOfBuffer = remainingMemory;
	while (remainingLength > 0){
		pageNum++;

		if ((currentTLS->pageTable.at(pageNum))->referenceCounter > 1){
			((currentTLS->pageTable.at(pageNum))->referenceCounter)--;
			char* mmapPointer = (char*)mmap(NULL, pageSize, PROT_READ | PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE, -1, 0);

			page* newPage = new page();
			newPage->referenceCounter = 1;
			newPage->pagePointer = mmapPointer;
			mprotect((currentTLS->pageTable.at(pageNum))->pagePointer, pageSize, PROT_READ | PROT_WRITE);
			memcpy(newPage->pagePointer, (currentTLS->pageTable.at(pageNum))->pagePointer, pageSize);
			mprotect((currentTLS->pageTable.at(pageNum))->pagePointer, pageSize, PROT_NONE);
			mprotect(newPage->pagePointer, pageSize, PROT_NONE);
			currentTLS->pageTable.at(pageNum) = newPage;			
		}

		mprotect((currentTLS->pageTable.at(pageNum))->pagePointer, pageSize, PROT_READ | PROT_WRITE);

		if (remainingLength > 4096){
			memcpy((currentTLS->pageTable.at(pageNum))->pagePointer, buffer + offsetOfBuffer, 4096);
			offsetOfBuffer += 4096;
			remainingLength -= 4096;
		} else {
			memcpy((currentTLS->pageTable.at(pageNum))->pagePointer, buffer + offsetOfBuffer, remainingLength);
			remainingLength = 0; // or change to break 
		}

		mprotect((currentTLS->pageTable.at(pageNum))->pagePointer, pageSize, PROT_NONE);

	}

	sem_post(&mutex_sem);
	return 0;
}

int tls_read(unsigned int offset, unsigned int length, char *buffer){
	sem_wait(&mutex_sem);
	pthread_t currentTID = pthread_self();
	if (!tableContains(currentTID)){
		sem_post(&mutex_sem);
		return ERROR_CODE;
	}

	// if user trying to write more data than the LSA can hold, throw error
	TLS* currentTLS = findTlsWithThreadID(currentTID);
	if (offset+length > currentTLS->size){
		sem_post(&mutex_sem);
		return ERROR_CODE;
	}

	// find page number and its offset, pageNum will be used to index into the 0-indexed vector of pages
	int pageNum = offset/pageSize;
	int pageOffset = offset%pageSize;
	int remainingMemory = pageSize - offset;

	mprotect((currentTLS->pageTable.at(pageNum))->pagePointer, pageSize, PROT_READ | PROT_WRITE);
	if (length <= remainingMemory){
		memcpy(buffer, (currentTLS->pageTable.at(pageNum))->pagePointer + pageOffset, length);
		mprotect((currentTLS->pageTable.at(pageNum))->pagePointer, pageSize, PROT_NONE);	
		sem_post(&mutex_sem);
		return 0;
	} else {
		memcpy(buffer, (currentTLS->pageTable.at(pageNum))->pagePointer + pageOffset, remainingMemory);
	}
	// reprotects the page of the first write operation by changing protections to PROT_NONE
	mprotect((currentTLS->pageTable.at(pageNum))->pagePointer, pageSize, PROT_NONE);

	int remainingLength = length - remainingMemory;
	int offsetOfBuffer = remainingMemory;
	
	while (remainingLength > 0){
		pageNum++;
		mprotect((currentTLS->pageTable.at(pageNum))->pagePointer, pageSize, PROT_READ | PROT_WRITE);

		if (remainingLength > 4096){
			memcpy(buffer + offsetOfBuffer, (currentTLS->pageTable.at(pageNum))->pagePointer, 4096);
			offsetOfBuffer += 4096;
			remainingLength -= 4096;
		} else {
			memcpy(buffer + offsetOfBuffer, (currentTLS->pageTable.at(pageNum))->pagePointer, remainingLength);
			remainingLength = 0; // or change to break 
		}

		mprotect((currentTLS->pageTable.at(pageNum))->pagePointer, pageSize, PROT_NONE);
	}
	sem_post(&mutex_sem);
	return 0;
}

int tls_destroy(){
	sem_wait(&mutex_sem);
	pthread_t currentTID = pthread_self();
	if (!tableContains(currentTID)){
		sem_post(&mutex_sem);
		return ERROR_CODE;
	}

	int index = -1;
	for (int i = 0; i < table.size(); i++){
		if (table.at(i)->tid == currentTID){
			index = i;
		}
	}

	TLS* TLS_toDestroy = table.at(index);

	// frees memory of each page in the TLS
	for (int i = 0; i < TLS_toDestroy->numPages; i++){
		if (TLS_toDestroy->pageTable.at(i)->referenceCounter == 1){
			munmap((TLS_toDestroy->pageTable.at(i))->pagePointer, pageSize);
		} else {
			((TLS_toDestroy->pageTable.at(i))->referenceCounter)--;
		}
	}
	// frees the TLS in the heap and removes from global table of TLS
	delete(TLS_toDestroy);
	table.erase(table.begin()+index);
	sem_post(&mutex_sem);
	return 0;
}

int tls_clone(pthread_t tid){
	sem_wait(&mutex_sem);
	// if target thread doesn't have a LSA, throw an error
	if (!tableContains(tid)){
		sem_post(&mutex_sem);
		return ERROR_CODE;
	}

	// if current thread already has an LSA
	pthread_t currentTID = pthread_self();
	if (tableContains(currentTID)){
		sem_post(&mutex_sem);
		return ERROR_CODE;
	}

	// TLS of the target thread
	TLS* targetTLS = findTlsWithThreadID(tid);
	// new TLS for current thread
	TLS* newTLS = new TLS();
	newTLS->size = targetTLS->size;
	newTLS->numPages = targetTLS->numPages;
	newTLS->tid = currentTID;

	for (int i = 0; i < targetTLS->numPages; i++){
		((targetTLS->pageTable.at(i))->referenceCounter)++;
		newTLS->pageTable.push_back(targetTLS->pageTable.at(i));
	}
	table.push_back(newTLS);

	sem_post(&mutex_sem);
	return 0;
}

// function used for testing
void* tls_get_internal_start_address(){

	pthread_t currentTID = pthread_self();
	if (!tableContains(currentTID)){
		NULL;
	}
	TLS* currentTLS = findTlsWithThreadID(currentTID);
	char* pointerToFirstPage = ((currentTLS->pageTable).at(0))->pagePointer;
	return pointerToFirstPage;
}

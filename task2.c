#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

#define TOTAL_MEMORY_SIZE (1 * 1024 ) // 1 MB
#define SLICE_SIZE 1 // 1 KB
#define BUFFER_SIZE 100 // Maximum number of requests that can be queued
#define MAX_TIME_SLICE 10 // Maximum time slice in units
#define DEALLOCATE_INTERVAL 3 // Interval for deallocation
#define NUM_CONSUMERS 5 // Number of consumer threads
#define NUM_PRODUCERS 5 // Number of producer threads
typedef enum { FIRST_FIT, NEXT_FIT, WORST_FIT } AllocationStrategy;

typedef struct MemoryRequest {
    int id;
    int size; // in KB
    int timeSlice; // Duration of the request in time units
    int allocationTime; // Time when the request was allocated
    struct MemoryRequest *next;
} MemoryRequest;


typedef struct Node {
    bool isHole;
    int startBlockID;
    int slices;
    int requestId; // ID of the request occupying the memory block
    int timeSlice; // Duration of the request in time units
    int allocationTime; // Time when the request was allocated
    struct Node *next;
} Node;
sem_t mutex;
sem_t not_empty;//semaphore for not empty
sem_t not_full;//semaphore for not full
Node *memoryHead = NULL, *lastAllocated = NULL;
MemoryRequest *bufferHead = NULL;
int bufferCount = 0;
int currentTime = 0;
int requestSpace=100;
int idCount=1;
bool flag=true;
AllocationStrategy strategy = FIRST_FIT; // Default allocation strategy
bool requestNotAllocated = false;//lobal variable to track if any request was not allocated

// Function Declarations
void initializeMemory();
Node* createNode(int startBlockID, int slices, bool isHole, int requestId, int timeSlice, int allocationTime);
MemoryRequest* createRequest(int id, int size, int timeSlice);
void enqueueRequest(MemoryRequest *request);
MemoryRequest* dequeueRequest();
void processRequests();

void allocateMemory(MemoryRequest *request);
void firstFitAllocation(MemoryRequest *request);
void nextFitAllocation(MemoryRequest *request);
void worstFitAllocation(MemoryRequest *request);
void allocateToNode(Node *node, MemoryRequest *request, int requiredSlices);

void deallocateMemory(int requestID);
void compactMemory();
void displayMemoryState();
void displayBufferState();
void setAllocationStrategy(AllocationStrategy strat);
void simulateTimeProgression();

void initializeMemory() {
    memoryHead = createNode(0, TOTAL_MEMORY_SIZE / SLICE_SIZE, true, -1, 0, -1);
}

Node* createNode(int startBlockID, int slices, bool isHole, int requestId, int timeSlice, int allocationTime) {
    Node *node = (Node *)malloc(sizeof(Node));
    node->startBlockID = startBlockID;
    node->slices = slices;
    node->isHole = isHole;
    node->requestId = requestId;
    node->timeSlice = timeSlice;
    node->allocationTime = allocationTime;
    node->next = NULL;
    return node;
}

MemoryRequest* createRequest(int id, int size, int timeSlice) {
    MemoryRequest *request = (MemoryRequest *)malloc(sizeof(MemoryRequest));
    request->id = id;
    request->size = size;
    request->timeSlice = timeSlice;
    request->allocationTime = -1; // Not allocated yet
    request->next = NULL;
    return request;
}

void enqueueRequest(MemoryRequest *request) {
    if (bufferCount >= BUFFER_SIZE) {
        printf("Buffer is full. Dropping request ID %d\n", request->id);
        free(request);
        return;
    }
    if (bufferHead == NULL) {
        bufferHead = request;
    } else {
        MemoryRequest *temp = bufferHead;
        while (temp->next != NULL) {
            temp = temp->next;
        }
        temp->next = request;
    }
    bufferCount++;
}

MemoryRequest* dequeueRequest() {
    if (bufferHead == NULL) {
        return NULL;
    }
    MemoryRequest *request = bufferHead;
    bufferHead = bufferHead->next;
    bufferCount--;
    return request;
}

void allocateMemory(MemoryRequest *request) {
    switch (strategy) {
        case FIRST_FIT:
            firstFitAllocation(request);
            break;
        case NEXT_FIT:
            nextFitAllocation(request);
            break;
        case WORST_FIT:
            worstFitAllocation(request);
            break;
    }
}


void firstFitAllocation(MemoryRequest *request) {
    Node *current = memoryHead;

    while (current != NULL) {
        int requiredSlices = (request->size + SLICE_SIZE - 1) / SLICE_SIZE;
        if (current->isHole && current->slices >= requiredSlices) {
            // Allocate to this node
            allocateToNode(current, request, requiredSlices);
            return;
        }
        current = current->next;
    }
}

void nextFitAllocation(MemoryRequest *request) {
    Node* current = memoryHead;
    if(lastAllocated!=NULL){
        Node* current = lastAllocated->next;//start from the last allocated node
    }
   while (current != NULL)
   {
    int requiredSlices = (request->size + SLICE_SIZE - 1) / SLICE_SIZE;
    if(current->isHole && current->slices >= requiredSlices)
    {
        // Allocate to this node
        allocateToNode(current, request, requiredSlices);
        lastAllocated = current;//update last allocated node
        return;
    }
    current = current->next;
   }
   
}

void worstFitAllocation(MemoryRequest *request) {
    Node *current = memoryHead;
    Node *worstFit = NULL;
    int maxSlices = 0;

    while (current != NULL) {//finding the worst fit
        int requiredSlices = (request->size + SLICE_SIZE - 1) / SLICE_SIZE;
        if (current->isHole && current->slices >= requiredSlices && current->slices > maxSlices) {
            worstFit = current;
            maxSlices = current->slices;
        }
        current = current->next;
    }

    if (worstFit != NULL) {//if worst fit found
        allocateToNode(worstFit, request, (request->size + SLICE_SIZE - 1) / SLICE_SIZE);
    }
}


void allocateToNode(Node *node, MemoryRequest *request, int requiredSlices) {
    node->isHole = false;
    node->requestId = request->id;
    node->timeSlice = request->timeSlice;
    node->allocationTime = currentTime;

    int remainingSlices = node->slices - requiredSlices;
    node->slices = requiredSlices;

    // Update request's allocation time to indicate successful allocation
    request->allocationTime = currentTime;

    if (remainingSlices > 0) {
        Node *newNode = createNode(node->startBlockID + requiredSlices, remainingSlices, true, -1, 0, -1);
        newNode->next = node->next;
        node->next = newNode;
    }
}



void deallocateMemory(int requestID) {
    Node *current = memoryHead, *prev = NULL;

    while (current != NULL) {
        if (!current->isHole && current->requestId == requestID) {
            current->isHole = true;
            current->requestId = -1;
            
            if (prev && prev->isHole) {
                prev->slices += current->slices;
                prev->next = current->next;
                free(current);
                current = prev->next;
            } else {
                Node *next = current->next;
                if (next && next->isHole) {
                    current->slices += next->slices;
                    current->next = next->next;
                    free(next);
                } else {
                    current = current->next;
                }
            }
        } else {
            prev = current;
            current = current->next;
        }
    }
}

void compactMemory() {
    Node *HeadA=NULL,*HeadB=NULL,*current=memoryHead,*tempB=NULL;//HeadA is head for holes list and HeadB is head for allocated memory list
    Node *temp=NULL; 
    Node *prev = NULL;
    while(current!=NULL){ //seperating holes and allocated memory (nodes)
        if(current->isHole){
            if(HeadA==NULL){//if first hole
                HeadA=current;
                HeadA->startBlockID=0;//startBlockID of first hole is 0
            }
            else{
                HeadA->slices+=current->slices;//adding slices of holes
                Node *next = current->next;//next node
                free(current);//freeing the hole
                current=next;//current node is next node
                if (prev != NULL) {//if previous node is not null
                    prev->next = current;
                }
                continue;
            }
        }
        else{
            if(HeadB==NULL)//if first allocated memory
            {
                HeadB=current;
                tempB=HeadB;
            }
            else{
                tempB->next=current;//adding allocated memory to tempB
                tempB=current;
            }
        }
        prev = current;//previous node is current node. Recode the pr
        current=current->next;//current node is next node
    }
    if(HeadB!=NULL){
        tempB->next=NULL;
        HeadA->next=HeadB;//adding allocated memory head to holes tail
    }
    else{
        HeadA->next=NULL;
    }
    memoryHead=HeadA;
    temp=memoryHead;
    int i=0;
    while(temp!=NULL){
        temp->startBlockID=i;//updating startBlockID
        i=i+temp->slices;
        temp=temp->next;
    }
}

void displayMemoryState() {
    printf("Memory State at time %d:\n", currentTime);
    Node *current = memoryHead;
    while (current != NULL) {
        printf("Request ID: %d, Block ID: %d, Slices: %d, %s, Time Slice: %d, Allocation Time: %d\n", current->requestId,
               current->startBlockID, current->slices,
               current->isHole ? "Free" : "Allocated",
               current->timeSlice, current->allocationTime);
        current = current->next;
    }
}

void displayBufferState() {
    printf("Buffer State: %d requests in buffer\n", bufferCount);
    MemoryRequest *current = bufferHead;
    while (current != NULL) {
        printf("Request ID: %d, Size: %d KB, Time Slice: %d, Allocation Time: %d\n",
               current->id, current->size, current->timeSlice, current->allocationTime);
        current = current->next;
    }
}

void setAllocationStrategy(AllocationStrategy strat) {
    strategy = strat;
    printf("Allocation Strategy set to %s\n", (strat == FIRST_FIT) ? "First Fit" : (strat == NEXT_FIT) ? "Next Fit" : "Worst Fit");
}

void processRequests() {
    MemoryRequest **requestRef = &bufferHead;  // Pointer to the pointer to the current request
    
    while (*requestRef != NULL) {
        MemoryRequest *currentRequest = *requestRef;
        allocateMemory(currentRequest);

        // If request was allocated, remove it from the buffer
        if (currentRequest->allocationTime != -1) {
            *requestRef = currentRequest->next;  // Skip the allocated request
            free(currentRequest);  // Free the allocated request
            bufferCount--;  // Decrement buffer count
        } else {
            // Move to the next request if the current one couldn't be allocated
            requestRef = &(currentRequest->next);
        }
    }
}
void *producer(void *arg)
{
    for(int i=0;i<(rand()%10000+1);i++){
        if(sem_getvalue(&not_full,&requestSpace)==0 && requestSpace==0){
            break;
        }
        sem_wait(&not_full); 
        sem_wait(&mutex);
        MemoryRequest *request=createRequest(idCount, (rand() % (50 - 2 + 1)) + 2, rand() % MAX_TIME_SLICE + 1);
        idCount++;
        enqueueRequest(request);
        printf("Producer ID: %zu,Request ID: %d, Size: %d KB, Time Slice: %d, Allocation Time: %d\n",
               pthread_self(),request->id, request->size, request->timeSlice, request->allocationTime);
        sem_post(&not_empty);
        sem_post(&mutex);

    }
}
void *consumer(void *arg)
{
    while(flag){
        int not_empty_value;
        sem_getvalue(&not_empty, &not_empty_value);
        if(not_empty_value == 0){
            flag=false;
            break;
        }
    sem_wait(&not_empty);
    sem_wait(&mutex);
    MemoryRequest *request=dequeueRequest();
    allocateMemory(request);
    currentTime++;       
    if(request->allocationTime!=-1)
    {
        
        printf("Consumer ID: %zu, Request ID: %d, Size: %d KB, Time Slice: %d, Allocation Time: %d\n",
               pthread_self(),request->id, request->size, request->timeSlice, request->allocationTime);
               free(request);
    }
    else{
 
        enqueueRequest(request);
    }
    if (currentTime % DEALLOCATE_INTERVAL == 0) {
            Node *current = memoryHead;
            while (current != NULL) {
                if (!current->isHole && currentTime - current->allocationTime >= current->timeSlice) {
                    deallocateMemory(current->requestId);
                }
                current = current->next;
            }
            printf("Consumer ID: %zu, Deallocated Memory at Time: %d\n",pthread_self(),currentTime);
            printf("\n--- After Compaction at Time: %d ---\n", currentTime);
    }
    sem_post(&not_full);
    sem_post(&mutex);
    
    }

}
void simulateTimeProgressionWithThreads(){
        sem_init(&mutex,0,1);
        sem_init(&not_full,0,requestSpace);
        sem_init(&not_empty,0,0);

        while(flag){
            //currentTime++;
            pthread_t producerThreads[NUM_PRODUCERS];
            pthread_t consumerThreads[NUM_CONSUMERS];
            for(int i=0;i<NUM_PRODUCERS;i++){
                pthread_create(&producerThreads[i],NULL,producer,NULL);
            }
            for(int i=0;i<NUM_CONSUMERS;i++){
                pthread_create(&consumerThreads[i],NULL,consumer,NULL);
            }

            sleep(1);
            for(int i=0;i<NUM_PRODUCERS;i++){
                pthread_join(producerThreads[i],NULL);
            }
            for(int i=0;i<NUM_CONSUMERS;i++){
                pthread_join(consumerThreads[i],NULL);
            }
        }
        printf("No pending requests and no allocations. Stopping simulation.\n");
        sem_destroy(&mutex);
        sem_destroy(&not_empty);
        sem_destroy(&not_full);

}
void simulateTimeProgression() {
    printf("\n--- Current Time: %d ---\n", currentTime);
    displayBufferState();
    processRequests();
    displayMemoryState();

    while (true) {
       
        processRequests();
        
        currentTime++;

        // Perform deallocation and compaction
        if (currentTime % DEALLOCATE_INTERVAL == 0) {
            Node *current = memoryHead;
            while (current != NULL) {
                if (!current->isHole && currentTime - current->allocationTime >= current->timeSlice) {
                    deallocateMemory(current->requestId);
                }
                current = current->next;
            }
            compactMemory();
            printf("\n--- After Compaction at Time: %d ---\n", currentTime);
            displayMemoryState();
            displayBufferState(); // Display buffer state after compaction

            processRequests(); // Retry allocation for all requests in the buffer
        }

        // Stop the simulation if no pending requests and no allocations
        if (bufferHead == NULL) {
            bool hasAllocations = false;
            Node *current = memoryHead;
            while (current != NULL) {
                if (!current->isHole) {
                    hasAllocations = true;
                    break;
                }
                current = current->next;
            }
            if (!hasAllocations) {
                printf("No pending requests and no allocations. Stopping simulation.\n");
                break;
            }
        }
    }
}


int main() {
    srand(time(NULL)); // Seed for random number generation
    initializeMemory();
    setAllocationStrategy(FIRST_FIT); // Set the desired strategy here
    simulateTimeProgressionWithThreads();
    compactMemory();
    displayMemoryState();
    displayBufferState();
    return 0;
}
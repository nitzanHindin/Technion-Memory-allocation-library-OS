#include "stdlib.h"
#include "unistd.h"

#define ERROR ((void*)-1)
#define MAX_SIZE ((size_t)(1e8))


void* smalloc(size_t size) {
    if(size <= 0 || size > MAX_SIZE) // size <= or only == 0 ?
        return nullptr;
    
    void* ptr = sbrk(size);
    if(ptr == ERROR)
        return nullptr;
    
    return ptr;
}
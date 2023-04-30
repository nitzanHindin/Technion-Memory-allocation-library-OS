#include "stdlib.h"
#include "unistd.h"
#include <cstring>

#define MDSIZE sizeof(MallocMetadata)
#define ERROR ((void*)-1)
#define MAX_SIZE ((size_t)(1e8))

struct MallocMetadata {
    MallocMetadata* next;
    MallocMetadata* prev;
    size_t size;
    bool is_free;
};

size_t _num_free_blocks();
size_t _num_free_bytes();
size_t _num_allocated_blocks();
size_t _num_allocated_bytes();
size_t _num_meta_data_bytes();
size_t _size_meta_data();

MallocMetadata* head = nullptr;


size_t num_free_blocks = 0;
size_t num_free_bytes = 0;
size_t num_allocated_blocks = 0;
size_t num_allocated_bytes = 0;

void* smalloc(size_t size)
{
    if(size == 0 || size > MAX_SIZE) // size <= or only == 0 ?
        return nullptr;
    
    // search the metadata
    MallocMetadata* it = head;
    MallocMetadata* prev = head;
    while(it)
    {
        if(it->is_free)
        {
            if(it->size >= size)
            {
                it->is_free = false;
                num_free_blocks--;
                num_free_bytes -= it->size;
                /*int prev_free_space = it->size;
                if(prev_free_space - size > MDSIZE)
                {
                    it->size = size;
                    MallocMetadata* ptr = it+it->size+MDSIZE;
                    ptr->is_free = true;
                    ptr->next = it->next;
                    ptr->prev = it;
                    ptr->size = prev_free_space - (size+MDSIZE);
                    it->next = ptr;
                    num_free_blocks++;
                    num_free_bytes += ptr->size;
                    num_allocated_blocks++;
                }*/
                return (void*)((size_t)it+_size_meta_data());
            }
        }
        if(it != head)
            prev = prev->next;
        it = it->next;
    }

    // if not found
    void* start_of_new_data = sbrk(_size_meta_data()+size);
    if(start_of_new_data == ERROR)
        return nullptr;
    
    MallocMetadata* new_data = (MallocMetadata*)start_of_new_data;
    if(prev){
        prev->next = new_data;
    }
    else{
        head = new_data;
    }
    new_data->is_free = false;
    new_data->prev = prev;
    new_data->next = nullptr;
    new_data->size = size;
    
    num_allocated_blocks++;
    num_allocated_bytes += new_data->size;
    return (void*)((size_t)new_data+_size_meta_data());
}

void* scalloc(size_t num, size_t size)
{
    void* ptr = smalloc(num*size);
    if(!ptr)
        return nullptr;
    
    memset(ptr, 0, num*size);

    return ptr;
}

void sfree(void* p)
{
    if(!p)
        return;
    //bool* is_free = (bool*)((size_t)p - sizeof(bool));
    //size_t* size = (size_t*)((size_t)p - sizeof(bool) - sizeof(size_t));
    MallocMetadata* MD = (MallocMetadata*)((size_t)p - _size_meta_data());
    if(MD->is_free == true)
        return;
    num_free_blocks++;
    num_free_bytes += MD->size;
    MD->is_free = true; 
}

void* srealloc(void* oldp, size_t size)
{
    if(!oldp)
    {
        void* ptr = smalloc(size);
        return ptr;
    }
    MallocMetadata* MD = (MallocMetadata*)((size_t)oldp - _size_meta_data());
    if(size <= MD->size)
        return oldp;
    
    void* newp = smalloc(size);
    if(!newp)
        return nullptr;
    
    memmove(newp, oldp, MD->size);
    sfree(oldp);
    return newp;
}

size_t _num_free_blocks()
{
    return num_free_blocks;
}

size_t _num_free_bytes()
{
    return num_free_bytes;
}

size_t _num_allocated_blocks()
{
    return num_allocated_blocks;
}

size_t _num_allocated_bytes()
{
    return num_allocated_bytes;
}

size_t _num_meta_data_bytes()
{
    return _size_meta_data() * _num_allocated_blocks();
}

size_t _size_meta_data()
{
    return MDSIZE;
}

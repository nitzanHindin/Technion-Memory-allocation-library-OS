#include "stdlib.h"
#include "unistd.h"
#include <cstring>
#include <sys/mman.h>

#define MDSIZE sizeof(MallocMetadata)
#define ERROR ((void*)-1)
#define LARGE_BLOCK ((size_t)128)
#define MAX_SIZE ((size_t)(1e8))
#define LARGE_ALLOCATION ((size_t)(128*1024))

int32_t cookie_val = rand(); // verify this

struct MallocMetadata {
public:
    //MallocMetadata() : cookie(cookie_val) {}

    int32_t cookie = cookie_val;
    size_t size;
    bool is_free;
    MallocMetadata* next_sorted_size;
    MallocMetadata* prev_by_address;
    MallocMetadata* next_by_address;

    /*MallocMetadata* operator->()
    {
        assert(0==1);
        if(cookie_val != this->cookie)
            exit(0xdeadbeef);
        return this;
    }*/
};

size_t _num_free_blocks();
size_t _num_free_bytes();
size_t _num_allocated_blocks();
size_t _num_allocated_bytes();
size_t _num_meta_data_bytes();
size_t _size_meta_data();

void insertToFreeList(MallocMetadata* to_insert);
void removeFromFreeList(MallocMetadata* to_remove);
void handleLargeBlock(MallocMetadata* md, size_t size);
void mergeNextFreeBlock(MallocMetadata* md);
void mergeFreeBlocks(MallocMetadata* md);
void* enlargeLastBlock(size_t size, MallocMetadata* top_of_heap);

MallocMetadata* head_sorted_size = nullptr;
MallocMetadata* tail_address = nullptr;

size_t num_free_blocks = 0;
size_t num_free_bytes = 0;
size_t num_allocated_blocks = 0;
size_t num_allocated_bytes = 0;

void exitOnCorruption(MallocMetadata* md)
{
    if(!md)
        return;
    if(md->cookie != cookie_val)
        exit(0xdeadbeef);
}

void* smalloc(size_t size)
{
    if(size <= (size_t)0 || size > MAX_SIZE) // size <= or only == 0 ?
        return nullptr;
    
    // search the metadata
    MallocMetadata* it = head_sorted_size;
    MallocMetadata* prev_in_sort_list = head_sorted_size;

    while(it)
    {
        exitOnCorruption(it);
        if(it->size >= size)
        {
            num_free_blocks--;
            num_free_bytes -= it->size;
            it->is_free = false;
            if(head_sorted_size == it)
            {
                MallocMetadata* tmp = it->next_sorted_size;
                exitOnCorruption(tmp);
                it->next_sorted_size = nullptr;
                head_sorted_size = tmp; 
                handleLargeBlock(it, size);
                return (void*)((size_t)it+_size_meta_data());
            }
            exitOnCorruption(prev_in_sort_list);
            prev_in_sort_list->next_sorted_size = it->next_sorted_size;
            it->next_sorted_size = nullptr;
            handleLargeBlock(it, size);
            
            return (void*)((size_t)it+_size_meta_data());
        }

        if(it != head_sorted_size)
        {
            exitOnCorruption(prev_in_sort_list);
            prev_in_sort_list = prev_in_sort_list->next_sorted_size;
        }
        it = it->next_sorted_size;
    }

    // if not found

    MallocMetadata* top_of_heap = tail_address;
    if(top_of_heap == ERROR)
        return nullptr;

    exitOnCorruption(top_of_heap);
    if(top_of_heap && top_of_heap->is_free)
        return enlargeLastBlock(size, top_of_heap);

    void* start_of_new_data;
    MallocMetadata* new_data;
    if(size >= LARGE_ALLOCATION)
    {
        void* start_of_new_data = mmap(NULL, size+_size_meta_data() , PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if(start_of_new_data == MAP_FAILED)
            return nullptr;
        new_data = (MallocMetadata*)start_of_new_data;
        new_data->cookie = cookie_val;
        //return (void*)((size_t)start_of_new_data+_size_meta_data());
    }
    else{
        start_of_new_data = sbrk(_size_meta_data()+size);
        if(start_of_new_data == ERROR)
            return nullptr;
        new_data = (MallocMetadata*)start_of_new_data;
        new_data->cookie = cookie_val;
        if(tail_address)
        {
            exitOnCorruption(tail_address);
            tail_address->next_by_address = new_data;
        }
        exitOnCorruption(new_data);
        new_data->prev_by_address = tail_address;
        new_data->next_by_address = nullptr;
        tail_address = new_data;
    }
    exitOnCorruption(new_data);
    new_data->size = size;
    new_data->next_sorted_size = nullptr; // not a part of the list, because not free
    num_allocated_blocks++;
    num_allocated_bytes += new_data->size;
    new_data->is_free = false;

    
    return (void*)((size_t)new_data+_size_meta_data());
}

void* enlargeLastBlock(size_t size, MallocMetadata* top_of_heap)
{
    exitOnCorruption(top_of_heap);
    if(size <= top_of_heap->size)
        return nullptr;
    void* addition = sbrk(size - top_of_heap->size);
    if(addition == ERROR)
        return nullptr;
    top_of_heap->is_free = false;
    num_free_blocks--;
    num_free_bytes -= top_of_heap->size;
    num_allocated_bytes += size - top_of_heap->size;
    top_of_heap->size = size;
    removeFromFreeList(top_of_heap);
    return (void*)((size_t)top_of_heap+_size_meta_data());
}

void* scalloc(size_t num, size_t size)
{
	if(num < 0 || size < 0)
		return nullptr;
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
    MallocMetadata* MD = (MallocMetadata*)((size_t)p - _size_meta_data());
    exitOnCorruption(MD);
    MD->is_free = true;
    if(MD->size >= LARGE_ALLOCATION)
    {
        num_allocated_blocks--;
        num_allocated_bytes -= MD->size;
        munmap(MD, MD->size+_size_meta_data());
        return;
    }
    num_free_blocks++;
    num_free_bytes += MD->size;
    insertToFreeList(MD);
    mergeFreeBlocks(MD);
    return;
}

void* srealloc(void* oldp, size_t size)
{
    if(size == 0 || size > MAX_SIZE)
        return nullptr;

    if(!oldp)
    {
        void* ptr = smalloc(size);
        return ptr;
    }

    MallocMetadata* MD = (MallocMetadata*)((size_t)oldp - _size_meta_data());
    exitOnCorruption(MD);

    if(MD->size >= LARGE_ALLOCATION) // case mmap
    {
        if(size > LARGE_ALLOCATION)
        {
            if(size <= MD->size)
                return oldp;
            
            num_allocated_bytes -= (MD->size + _size_meta_data());
            munmap(MD, MD->size+_size_meta_data());
            void* start_of_new_data = mmap(NULL, size+_size_meta_data() , PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if(start_of_new_data == MAP_FAILED)
                return nullptr;
            MallocMetadata* new_data = (MallocMetadata*)start_of_new_data;
            new_data->cookie = cookie_val;
            new_data->size = size;
            new_data->next_sorted_size = nullptr;
            num_allocated_bytes += (new_data->size + _size_meta_data());
            new_data->is_free = false;
            return (void*)((size_t)new_data + _size_meta_data());
        }
        else
        {
            // exitOnCorruption(MD);
            // num_allocated_bytes -= (MD->size + _size_meta_data());
            // munmap(MD, MD->size+_size_meta_data());
            return oldp;
        }
    }

    if(size <= MD->size) { // case a
        handleLargeBlock(MD, size);
        return oldp;
    }

    exitOnCorruption(MD->prev_by_address);
    exitOnCorruption(MD->next_by_address);

    if(tail_address == MD)
    {
        if((MD->prev_by_address) && (MD->prev_by_address->is_free)) // case b wilderness
        {
            MallocMetadata* src = MD;
            MallocMetadata* dst = MD->prev_by_address;
            size_t copy = src->size;
            src->is_free = true;
            //num_free_bytes += src->size;
            insertToFreeList(src);
            num_free_bytes -= (dst->size + _size_meta_data());
            mergeNextFreeBlock(dst);

            MallocMetadata* top_of_heap = tail_address;
            if(top_of_heap == ERROR)
                return nullptr;
            exitOnCorruption(top_of_heap);

            if(size > top_of_heap->size)
            {
                num_free_blocks++;
                num_free_bytes += top_of_heap->size;
                enlargeLastBlock(size, top_of_heap); // shouldn't happen if there's enough memory in tail_address
            }

            removeFromFreeList(dst);
            //num_free_bytes -= dst->size;

            memmove((void*)((size_t)dst + _size_meta_data()), (void*)((size_t)src + _size_meta_data()), copy);
            dst->is_free = false;
            handleLargeBlock(dst, size);
            return (void*)((size_t)dst + _size_meta_data());
        }
        else // case c
        {
            MallocMetadata* top_of_heap = tail_address;
            if(top_of_heap == ERROR)
                return nullptr;
            exitOnCorruption(top_of_heap);

            if(size > top_of_heap->size)
            {
                num_free_blocks++;
                num_free_bytes += top_of_heap->size;

                enlargeLastBlock(size, top_of_heap); // shouldn't happen if there's enough memory in tail_address
            }
            return (void*)((size_t)MD + _size_meta_data());
        }
    }
    // not tail address

    if((MD->prev_by_address) && (MD->prev_by_address->is_free)) // case b && !tail_address
    {
        if(size <= MD->size + MD->prev_by_address->size + _size_meta_data())
        {
            MD->is_free = true;
            num_free_bytes += MD->size;
            insertToFreeList(MD);
            mergeNextFreeBlock(MD->prev_by_address); 
            num_free_bytes -= MD->prev_by_address->size;
            removeFromFreeList(MD->prev_by_address);
            size_t copy = MD->size;
            MallocMetadata* src = MD;
            MallocMetadata* dst = MD->prev_by_address;
            memmove((void*)((size_t)dst + _size_meta_data()), (void*)((size_t)src + _size_meta_data()) ,copy);
            dst->is_free = false;
            handleLargeBlock(dst, size);
            return (void*)((size_t)dst + _size_meta_data());
        }
    }
    
    if((MD->next_by_address) && (MD->next_by_address->is_free)) //case d
    {
        if(size <= MD->size + MD->next_by_address->size + _size_meta_data())
        {
            //MD->is_free = true;
            num_free_bytes += MD->size;
            insertToFreeList(MD);
            mergeNextFreeBlock(MD); 
            num_free_bytes -= MD->size; // that's a different value than row num 238
            removeFromFreeList(MD);
            //memmove(MD + _size_meta_data(), MD + _size_meta_data(), size); // is this needed?
            MD->is_free = false;
            handleLargeBlock(MD, size);
            return (void*)((size_t)MD + _size_meta_data());
        }
    }

    if(MD->next_by_address && (MD->next_by_address->is_free) &&
      (MD->prev_by_address) && (MD->prev_by_address->is_free)) //case e
    {
        if(size <= MD->size + MD->next_by_address->size + MD->prev_by_address->size + 2*_size_meta_data())
        {
            MallocMetadata* src = MD;
            MallocMetadata* dst = MD->prev_by_address;
            size_t copy = src->size;
            src->is_free = true;
            num_free_bytes += src->size;
            insertToFreeList(src);
            mergeFreeBlocks(src);
            num_free_bytes -= dst->size;
            removeFromFreeList(dst);
            memmove((void*)((size_t)dst + _size_meta_data()), (void*)((size_t)src + _size_meta_data()), copy);
            dst->is_free = false;
            handleLargeBlock(dst, size);
            return (void*)((size_t)dst + _size_meta_data());
        }
    }

    if(MD->next_by_address && MD->next_by_address->is_free) // case f
    {
        if(MD->prev_by_address && MD->prev_by_address->is_free) // case fi as in case e  + enlargment
        {
            MallocMetadata* src = MD;
            MallocMetadata* dst = MD->prev_by_address;
            size_t copy = src->size;
            src->is_free = true;
            num_free_bytes += src->size;
            insertToFreeList(src);
            mergeFreeBlocks(src);
            num_free_bytes -= dst->size;
            removeFromFreeList(dst);

            exitOnCorruption(tail_address);
            if(size > tail_address->size)
            {
                num_free_blocks++;
                num_free_bytes += tail_address->size;
                enlargeLastBlock(size, tail_address);
            }

            memmove((void*)((size_t)dst + _size_meta_data()), (void*)((size_t)src + _size_meta_data()), copy);
            dst->is_free = false;
            handleLargeBlock(dst, size);
            return (void*)((size_t)dst + _size_meta_data());
        }
        else // case fii as in case d + enlargment
        {
            num_free_bytes += MD->size;
            insertToFreeList(MD);
            mergeNextFreeBlock(MD); 
            num_free_bytes -= MD->size; // that's a different value than row num 349
            removeFromFreeList(MD);
            exitOnCorruption(tail_address);
            if(size > tail_address->size)
            {
                num_free_blocks++;
                num_free_bytes += tail_address->size;
                enlargeLastBlock(size, tail_address);
            }
            MD->is_free = false;
            handleLargeBlock(MD, size);
            return (void*)((size_t)MD + _size_meta_data());
        }
    }

    void* newp = smalloc(size); // cases g + h
    if(!newp)
        return nullptr;
    
    memmove(newp, oldp, size);
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

void handleLargeBlock(MallocMetadata* md, size_t size)
{                   
    exitOnCorruption(md);
    if(md->size < size)
        return;
    if(md->size - size < _size_meta_data())
        return;

    size_t remainder = md->size - size - _size_meta_data();
    if(remainder < LARGE_BLOCK)
        return;

    md->size = size;
    MallocMetadata* new_md = (MallocMetadata*)((size_t)md + md->size + _size_meta_data());
    new_md->cookie = cookie_val;
    new_md->size = remainder;
    //num_free_bytes -= _size_meta_data();
    num_free_bytes += remainder;
    num_allocated_bytes -= _size_meta_data();
    num_allocated_blocks++;
    num_free_blocks++;
    if(md == tail_address)
        tail_address = new_md;
    new_md->is_free = true;
    new_md->prev_by_address = md;
    new_md->next_by_address = md->next_by_address;
    if(new_md->next_by_address)
        new_md->next_by_address->prev_by_address = new_md;
    md->next_by_address = new_md;
    insertToFreeList(new_md);
    //mergeFreeBlocks(new_md); // might be not needed
}

void insertToFreeList(MallocMetadata* to_insert)
{
    exitOnCorruption(to_insert);
    MallocMetadata* curr = head_sorted_size;
    exitOnCorruption(curr);

    if(curr == nullptr)
    {
        head_sorted_size = to_insert;
        head_sorted_size->next_sorted_size = nullptr;
        return;
    }

    if(curr->next_sorted_size == nullptr)
    {
        if(curr->size <= to_insert->size){
            if(curr->size == to_insert->size)
            {
                if((size_t)curr > (size_t)to_insert)
                {
                    to_insert->next_sorted_size = curr;
                    curr->next_sorted_size = nullptr;
                    head_sorted_size = to_insert;
                    return;
                }
            }
            curr->next_sorted_size = to_insert;
            to_insert->next_sorted_size = nullptr;
            return;
        }
    }

    if(curr->size > to_insert->size){
        MallocMetadata *tmp = curr; //=head
        head_sorted_size = to_insert;
        to_insert->next_sorted_size = tmp;
        return;
    }

    exitOnCorruption(curr->next_sorted_size);
    while(curr->next_sorted_size && (curr->next_sorted_size->size <= to_insert->size))
    {
        exitOnCorruption(curr->next_sorted_size);
        while(curr->next_sorted_size && curr->next_sorted_size->size == to_insert->size)
        {
            exitOnCorruption(curr->next_sorted_size);
            if((size_t)(curr->next_sorted_size) > (size_t)to_insert)
            {
                MallocMetadata* tmp = curr->next_sorted_size;
                curr->next_sorted_size = to_insert;
                to_insert->next_sorted_size = tmp;
                return;
            }
            curr = curr->next_sorted_size;
            exitOnCorruption(curr);
        }
        if(curr->next_sorted_size == nullptr)
            break;
        curr = curr->next_sorted_size;
        exitOnCorruption(curr);
    }

    exitOnCorruption(curr);
    MallocMetadata* past_next = curr->next_sorted_size;
    curr->next_sorted_size = to_insert;
    to_insert->next_sorted_size = past_next;
}

void mergeNextFreeBlock(MallocMetadata* md)
{
    exitOnCorruption(md);
    if(!md->next_by_address)
        return;
    exitOnCorruption(md->next_by_address);
    if(md->next_by_address->is_free){
        removeFromFreeList(md->next_by_address);
        removeFromFreeList(md);
        if(md->next_by_address == tail_address)
            tail_address = md;
        md->size += md->next_by_address->size + _size_meta_data();
        exitOnCorruption(md->next_by_address->next_by_address);
        if(md->next_by_address->next_by_address)
            md->next_by_address->next_by_address->prev_by_address = md;
        md->next_by_address = md->next_by_address->next_by_address;
        insertToFreeList(md);
        num_free_blocks--;
        num_allocated_blocks--;
        num_free_bytes += _size_meta_data();
        num_allocated_bytes += _size_meta_data();
    }
    return;
}

void mergeFreeBlocks(MallocMetadata* md){
    exitOnCorruption(md);
    mergeNextFreeBlock(md);
    exitOnCorruption(md->prev_by_address);
    if(md->prev_by_address && md->prev_by_address->is_free){
        mergeNextFreeBlock(md->prev_by_address);
    }
    return;
}

void removeFromFreeList(MallocMetadata* to_remove)
{
    exitOnCorruption(to_remove);
    MallocMetadata* it = head_sorted_size;
    exitOnCorruption(it);
    if(it == nullptr)
        return;
    if(to_remove == head_sorted_size) {
        head_sorted_size = to_remove->next_sorted_size;
        to_remove->next_sorted_size = nullptr;
        return;
    }
    MallocMetadata* prev = head_sorted_size;
    exitOnCorruption(prev);
    it = it->next_sorted_size;
    while(it)
    {
        exitOnCorruption(it);
        exitOnCorruption(prev);
        if(it == to_remove){
            prev->next_sorted_size = it->next_sorted_size;
            it->next_sorted_size = nullptr;
            return;
        }
        prev = prev->next_sorted_size;
        it = it->next_sorted_size;
    }
}
# TPS API

## data structures

we defined two structs for this phase: Page and TPS. Every memory page
created by _mmap()_ corresponds to a page struct, which contains the page 
memory address as well as the current number of threads referencing the page.
Each thread corresponds to a unique TPS struct that stores the thread’s TID 
and the reference to a page struct. We used a queue to manage all the TPS 
structs.

## high level implementation

_tpsInit()_: we start off by creating an empty queue. If the parameter segv 
equals to 1, we will set up the seg fault interrupt handler to allow TPS 
protection error to be detected. The global variable init will be set to 1 so
that future calls to this function will be invalid.

_tpsCreate()_: first checks if the current thread has already acquired a TPS 
area. This is done by calling our helper function _hasTPSBeenAllocated()_, 
which takes in a TID number and iterate through the TPS queue to find a 
match. If a match exists, _tps_create()_ will return with -1. Otherwise, it 
will initialize and allocate space for a new TPS struct and a new Page 
struct. It will also call _mmap()_ to allocate one page of memory space with 
the access permission set to PROT_NONE. Lastly, it will add the newly created 
TPS struct to the queue. The last step is protected by the provided critical 
section functions because the queue could be modified by multiple threads at 
any time.

_tpsRead()_: first verifies 1. if the given offset, length, buffer is valid 
for a read operation, 2. If the calling thread has a TPS area. These could be 
done by calling the helper function _isTPSReadWriteValid()_, which verifies 
the buffer and the boundaries. If the verification fails, _tpsRead()_ will 
return -1. Otherwise, it calls _mprotect()_ to grant read permission for the 
associated page memory and calls _memcpy()_ to write contents to the buffer 
before calling _mprotect()_ again to reset the access permission. The last 
few steps are wrapped into a critical section because the page memory can be 
shared with multiple threads, meaning that a race condition can occur at any 
time.

_tpsClone()_: first verifies 1. the calling thread has not acquired a TPS 
area, 2. the thread with TID (given as parameter) has a valid TPS area. It 
then creates a new TPS struct for the calling thread. The new TPS struct 
shares the same Page struct referenced by thread TID. The refCount field of 
the Page struct will be incremented by one. Lastly, the new TPS struct will 
be added to the queue. The logic flow is similar to _tpsCreate()_, except 
that here we don’t allocate a new page memory.

_tpsWrite()_: The initial verification process is exactly same as that of 
_tpsRead()_. Before writing, the calling thread checks if the refCount field 
of the associated page struct exceeds one. If yes, then we need to perform 
the naïve clone by copying the page contents to a new memory space. We then 
create a new Page struct to manage this page memory. The calling thread will 
refer to the new Page struct in the future. The refCount field of the old 
Page struct will be decremented by one. Finally, we start copying the 
contents of the buffer to the page memory by calling _mprotect()_ and _memcpy
()_ appropriately.

_tpsDestroy()_: first checks if a TPS area has ever been allocated. It then 
verifies if the associated page memory is shared by other threads. If the 
calling thread is the sole owner, it can safely release the memory by calling
_munmap()_. The TPS and Page struct will also be removed by calling 
_queueDelete()_ and _free()_.

## testing

Apart from using tps.c as provided, we also created three additional tester 
programs. tpsErrorHandle.c checks if our implementation properly handles 
errors by verifying the return values. For example, the second call to 
_tpsInit()_ should return -1. tpsProtection.c checks if our implementation 
can distinguish the TPS protection error from regular seg faults. 
tpsCopyOnWrite.c checks for the internal implementation of copy-on-write 
cloning. By creating a wrapper for _mmap()_, we can retrieve the latest 
address of the page memory created . Initially, two additional threads in the 
program will share the page memory with the main thread. Then main will 
perform a write operation, which should trigger a copy on write. To verify 
this, we compared latestMmapAddr to its previous values and noticed that the 
value has been modified, meaning that _mmap()_ was called before the writing 
process started. We also printed out its content to show that the newly 
created page memory was a clone of the original one.

## external references

we referred to the web links in the assignment prompt, files in the canvass 
discussion folder, as well as many posts on piazza.

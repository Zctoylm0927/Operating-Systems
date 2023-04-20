#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ZKJ",
    /* First member's full name */
    "Zhou Kaijun",
    /* First member's email address */
    "2020200671@ruc.edu.cn",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* Show idle link list & Simple separation adaptation */

/* Basic constants and macros */
#define WSIZE       4       /* Word and header/footer size (bytes) */ //line:vm:mm:beginconst
#define DSIZE       8       /* Double word size (bytes) */
#define CHUNKSIZE  (1<<12)  /* Extend heap by this amount (bytes) */  //line:vm:mm:endconst 
#define MINSIZE     24      /* minimum bytes of a block */
#define MAXLEVEL    20      /* number of free_lists */

#define MAX(x, y) ((x) > (y)? (x) : (y))  
#define ALIGNMENT 8
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

/* Pack a size and allocated bit into a word */
#define PACK(size, alloc)  ((size) | (alloc)) //line:vm:mm:pack

/* Read and write a word at address p */
#define GET(p)       (*(unsigned int *)(p))            //line:vm:mm:get
#define PUT(p, val)  (*(unsigned int *)(p) = (val))    //line:vm:mm:put

/* Read the size and allocated fields from address p */
#define GET_SIZE(p)  (GET(p) & ~0x7)                   //line:vm:mm:getsize
#define GET_ALLOC(p) (GET(p) & 0x1)                    //line:vm:mm:getalloc

/* Given block ptr bp, compute address of its header and footer */
#define HDRP(bp)       ((char *)(bp) - WSIZE)                      //line:vm:mm:hdrp
#define FTRP(bp)       ((char *)(bp) + GET_SIZE(HDRP(bp)) - DSIZE) //line:vm:mm:ftrp

/* Given block ptr bp, compute address of next and previous blocks */
#define NEXT_BLKP(bp)  ((char *)(bp) + GET_SIZE(((char *)(bp) - WSIZE))) //line:vm:mm:nextblkp
#define PREV_BLKP(bp)  ((char *)(bp) - GET_SIZE(((char *)(bp) - DSIZE))) //line:vm:mm:prevblkp
/* $end mallocmacros */

/* Free-Blocks used */
#define PREV_LST(p) ((char *)(p))         // ret pred ptr of p in free_list
#define SUCC_LST(p) ((char *)(p) + DSIZE) // ret succ ptr of p in free_list
#define PREV(p) (*(char **)(p))           // ret block's add prev ptr of p in free_list
#define SUCC(p) (*(char **)(SUCC_LST(p))) // ret block's add succ ptr of p in free_list
#define SET(p,P) (*(unsigned long *)(p) = (unsigned long)(P))

/* Global variables */
void* free_list[MAXLEVEL];

static void *extend_heap(size_t words);
static void *place(void *bp, size_t asize);
static void *find_fit(size_t asize);
static void *coalesce(void *bp);
void Insert_free_list(void *bp, size_t size);
void Delete_free_list(void *bp, size_t size);

int mm_init(void) 
{
	char *heap_listp;
    /* Create the initial empty heap */
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void *)-1) //line:vm:mm:begininit
        return -1;
    PUT(heap_listp, 0);                          /* Alignment padding */
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); /* Prologue header */ 
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); /* Prologue footer */ 
    PUT(heap_listp + (3*WSIZE), PACK(0, 1));     /* Epilogue header */
    heap_listp += (2*WSIZE);                     //line:vm:mm:endinit      
	for(int i=0;i<MAXLEVEL;++i) free_list[i]=NULL;
    if (extend_heap(CHUNKSIZE) == NULL) 
        return -1;
    return 0;
}

void *mm_malloc(size_t size) 
{
    if(!size)
        return NULL;
    size_t asize = MAX(MINSIZE,ALIGN(size+DSIZE)); // alloc_size+footer&header -> align to 8
    void *bp=NULL;

    if((bp=find_fit(asize))!=NULL) 
        return place(bp, asize);

    /* No fit found. Get more memory and place the block */
    size_t extendsize = MAX(asize,CHUNKSIZE);
    if ((bp = extend_heap(extendsize)) == NULL)  
        return NULL;                                  
    return place(bp, asize);
} 

void mm_free(void *bp)
{
    if (bp == NULL) 
        return;
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    Insert_free_list(bp,size);
    coalesce(bp);
}

void *mm_realloc(void *ptr, size_t size)
{  
    size_t oldsize;
    void *newptr;

    if(size == 0) {
        mm_free(ptr);
        return NULL;
    }
    
    size = MAX(MINSIZE,ALIGN(size+DSIZE));    
    if(ptr == NULL) {
        return mm_malloc(size);
    }

    oldsize = GET_SIZE(HDRP(ptr));
    if(oldsize>=size) return ptr;

    if(GET_ALLOC(HDRP(NEXT_BLKP(ptr)))) {
        newptr = mm_malloc(size);
        if(!newptr) return NULL;
        oldsize=MAX(size,oldsize);
        memcpy(newptr, ptr, oldsize);
        mm_free(ptr);
    }
    else {
        size_t add_size = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        if(!add_size)
            if(extend_heap(MAX(size-oldsize,CHUNKSIZE)) == NULL) return NULL;
        size_t new_size = GET_SIZE(HDRP(NEXT_BLKP(ptr)));
        if(new_size+oldsize>=size) {
            Delete_free_list(NEXT_BLKP(ptr),new_size);
            PUT(HDRP(ptr),PACK(new_size+oldsize,1));
            PUT(FTRP(ptr),PACK(new_size+oldsize,1));
            newptr=ptr;
        }
    }
    return newptr;
}

static void *coalesce(void *bp) 
{
    int prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp)));
    int next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp)));
    size_t size = GET_SIZE(HDRP(bp));

    if (prev_alloc && next_alloc) {            /* Case 1 */
        return bp;
    }

    else if (prev_alloc && !next_alloc) {      /* Case 2 */
        Delete_free_list(bp,size);
        Delete_free_list(NEXT_BLKP(bp),GET_SIZE(HDRP(NEXT_BLKP(bp))));
        size += GET_SIZE(HDRP(NEXT_BLKP(bp)));
        PUT(HDRP(bp), PACK(size, 0));
        PUT(FTRP(bp), PACK(size,0));
    }

    else if (!prev_alloc && next_alloc) {      /* Case 3 */
        Delete_free_list(bp,size);
        Delete_free_list(PREV_BLKP(bp),GET_SIZE(HDRP(PREV_BLKP(bp))));
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }

    else {                                     /* Case 4 */
        Delete_free_list(bp,size);
        Delete_free_list(PREV_BLKP(bp),GET_SIZE(HDRP(PREV_BLKP(bp))));
        Delete_free_list(NEXT_BLKP(bp),GET_SIZE(HDRP(NEXT_BLKP(bp))));
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + 
            GET_SIZE(FTRP(NEXT_BLKP(bp)));
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0));
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0));
        bp = PREV_BLKP(bp);
    }
    Insert_free_list(bp,size); 
    bp = coalesce(bp);
    return bp;
}

static void *extend_heap(size_t size) {
    char *bp;
    size = ALIGN(size);
    if ((long)(bp = mem_sbrk(size)) == -1)  
        return NULL;                                       

    /* Initialize free block header/footer and the epilogue header */
    PUT(HDRP(bp), PACK(size, 0));         /* Free block header */   //line:vm:mm:freeblockhdr
    PUT(FTRP(bp), PACK(size, 0));         /* Free block footer */   //line:vm:mm:freeblockftr
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1)); /* New epilogue header */ //line:vm:mm:newepihdr

    /* Coalesce if the previous block was free */
    Insert_free_list(bp,size);
    return coalesce(bp); 
}

void* place(void *bp, size_t asize)
{
    size_t csize = GET_SIZE(HDRP(bp));   
    size_t res = csize - asize;
    Delete_free_list(bp,csize);

    if (res < MINSIZE) { 
        PUT(HDRP(bp), PACK(csize, 1));
        PUT(FTRP(bp), PACK(csize, 1));
        return bp;
    }
    else if(res > 8*asize) {
        PUT(HDRP(bp), PACK(res, 0));
        PUT(FTRP(bp), PACK(res, 0));
        PUT((HDRP(NEXT_BLKP(bp))) , PACK(asize,1) );
        PUT((FTRP(NEXT_BLKP(bp))) , PACK(asize,1) );
        Insert_free_list(bp, res);
        return NEXT_BLKP(bp);
    }
    else { 
        PUT(HDRP(bp), PACK(asize, 1));
        PUT(FTRP(bp), PACK(asize, 1));
        void* p = NEXT_BLKP(bp);
        PUT(HDRP(p), PACK(res, 0));
        PUT(FTRP(p), PACK(res, 0));
        Insert_free_list(p,res);
        return bp;
    }
}

int get_level(size_t words) {
    int level=0;
    while(words>1 && level < MAXLEVEL-1) {
        words>>=1;
        level++;
    }
    return level;
}

static void *find_fit(size_t asize)
{
    /* Best-fit search */
    void *bp=NULL;
    for(int i=get_level(asize/MINSIZE);i<MAXLEVEL;++i) {
        void* cur=free_list[i];
        size_t res = 1<<30;
        while(cur) {
            size_t now_size=GET_SIZE(HDRP(cur));
            if(now_size < asize) cur = SUCC(cur);
            else {
                if((now_size-asize) < res) bp=cur,res=now_size-asize;
                cur = SUCC(cur);
            }
        }
        if(bp) return bp;
    }
    return NULL;
}

void Insert_free_list(void *bp, size_t size) {
    int level=get_level(size/MINSIZE);
    void* cur = free_list[level];
    if(cur==NULL) {
        SET(PREV_LST(bp),NULL);
        SET(SUCC_LST(bp),NULL);
    }
    else {
        SET(SUCC_LST(bp),cur);
        SET(PREV_LST(bp),NULL); 
        SET(PREV_LST(cur),bp);
    }
    free_list[level]=bp;    
}

void Delete_free_list(void *bp, size_t size) {
    int level=get_level(size/MINSIZE);
    void *pre=PREV(bp),*suc=SUCC(bp);
    if(pre==NULL && suc==NULL) 
        free_list[level]=NULL;
    else if(pre==NULL && suc) {
        SET(PREV_LST(suc),NULL);
        free_list[level]=suc;
    }
    else if(pre && suc==NULL) 
        SET(SUCC_LST(pre),NULL);
    else {
        SET(SUCC_LST(pre),suc);
        SET(PREV_LST(suc),pre);
    }
}

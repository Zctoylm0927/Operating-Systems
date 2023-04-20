/*
Filesystem Lab disigned and implemented by Zhou Kaijun,RUC
*/
#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <fuse.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "disk.h"

#define DEBUG 0

#define DIRMODE S_IFDIR|0755
#define REGMODE S_IFREG|0644
#define INODE_NUM 32768
#define SUPBER_BLOCK 0
#define DATA_BITMAP_START 0
#define DATA_BITMAP_END 2
#define INODE_BITMAP_START 2
#define INODE_BITMAP_END 3
#define INODE_BLOCK_START 3
#define INODE_BLOCK_NUM (INODE_NUM*sizeof(inode_t)/BLOCK_SIZE)
#define INODE_BLOCK_END (INODE_BLOCK_START+INODE_BLOCK_NUM)
#define DATABLOCK_START (INODE_BLOCK_END)
#define DATABLOCK_END 65535
#define DATABLOCK_NUM (DATABLOCK_END-DATABLOCK_START)
#define POINTER_NUM (BLOCK_SIZE/sizeof(block_id_t))

typedef uint16_t block_id_t;
typedef uint16_t inode_id_t;
typedef struct {
	time_t  atime;
	time_t  mtime;
	time_t  ctime;
  	int size;
    bool mode; 
    block_id_t ptr_block;
} inode_t;

#define MAXFILENAME 24

#define ROOT 0

int if_error = 0;


typedef char block_t[BLOCK_SIZE];
typedef uint64_t inode_bitmap_t[1*BLOCK_SIZE/sizeof(uint64_t)];
typedef uint64_t block_bitmap_t[2*BLOCK_SIZE/sizeof(uint64_t)];

typedef struct  {
   unsigned long  bsize; 
   fsblkcnt_t     blocks;
   fsblkcnt_t     bfree; 
   fsblkcnt_t     bavail;
   fsfilcnt_t     files; 
   fsfilcnt_t     ffree; 
   fsfilcnt_t     favail;
   unsigned long  namemax;
}vfs_t;

void read_block(block_id_t block_id, block_t block) {
    disk_read(block_id,block);
}

void write_block(block_id_t block_id, block_t block) {
    disk_write(block_id,block);
}

void read_blocks(block_id_t start, block_id_t end, block_t addr) {
    for(block_id_t block=start;block<end;++block)
        read_block(block,addr+(block-start)*sizeof(block_t));
}

void write_blocks(block_id_t start, block_id_t end, block_t addr) {
    for(block_id_t block=start;block<end;++block)
        write_block(block,addr+(block-start)*sizeof(block_t));
}

void read_inode_bitmap(inode_bitmap_t inode_bitmap) {
    read_blocks(INODE_BITMAP_START, INODE_BITMAP_END, (void *)(inode_bitmap));
}

void write_inode_bitmap(inode_bitmap_t inode_bitmap) {
    write_blocks(INODE_BITMAP_START, INODE_BITMAP_END, (void *)(inode_bitmap));
}

void read_data_bitmap(block_bitmap_t block_bitmap) {
    read_blocks(DATA_BITMAP_START, DATA_BITMAP_END, (void *)(block_bitmap));
}

void write_data_bitmap(block_bitmap_t block_bitmap) {
    write_blocks(DATA_BITMAP_START, DATA_BITMAP_END, (void *)(block_bitmap));
}

void clear_block(block_id_t block_id) {
    static char c[BLOCK_SIZE];
    write_block(block_id,c);
}

int get_bitmap(uint64_t *bitmap,int i) {
    int pos=i/64,off=i%64;
    if(bitmap[pos]&(1<<off)) return 1;
    return 0;
}

void set_bitmap(uint64_t *bitmap,int i,bool flag) {
    int pos=i/64,off=i%64;
    bitmap[pos]|=1<<off;
    if(!flag) bitmap[pos]^=1<<off;
}

void delete_block(block_id_t *block_id,int cnt) {
    if(!cnt) return;
    block_bitmap_t bitmap;
    read_data_bitmap(bitmap);
    for(int i=0;i<cnt;++i) {
        set_bitmap(bitmap,block_id[i],0);
        clear_block(block_id[i]);
        block_id[i]=0;
    }
    write_data_bitmap(bitmap);
}

int new_block() {
    block_bitmap_t bitmap;
    read_data_bitmap(bitmap);
    for(block_id_t i=DATABLOCK_START;i<DATABLOCK_END;++i)
        if(!get_bitmap(bitmap,i)) {
            set_bitmap(bitmap,i,1);
            write_data_bitmap(bitmap);
            return i;
        }
    if_error=1;
}

void new_blocks(int n,block_id_t block[n]) {
    if(!n) return;
    block_bitmap_t bitmap;
    read_data_bitmap(bitmap);
    int res=0;
    for(block_id_t i=DATABLOCK_START;i<DATABLOCK_END;++i)
        if(!get_bitmap(bitmap,i)) {
            set_bitmap(bitmap,i,1);
            block[res++]=i;
            if(res==n) break;
        }
    write_data_bitmap(bitmap);
}

int blocks_count(int size) {
    int s=size/BLOCK_SIZE;
    if(size%BLOCK_SIZE) s++;
    return s;
}

int count_bitmap(uint64_t *bitmap) {
    int cnt=0;
    for(int i=0;i<sizeof(bitmap)*8;++i)
        if(get_bitmap(bitmap,i)) cnt++;
    return cnt;
}

void locate_inode(inode_id_t inode_id, block_id_t *block_id, int *offset) {
    int off=(int)inode_id*sizeof(inode_t);
    *block_id=INODE_BLOCK_START+off/BLOCK_SIZE;
    *offset=off-(*block_id-INODE_BLOCK_NUM)*BLOCK_SIZE;
}

inode_t read_inode(inode_id_t inode_id) {
    block_id_t block_id;
    int offset;
    locate_inode(inode_id, &block_id, &offset);
    block_t block;
    read_block(block_id, block);
    return *(inode_t*)(block+offset);
}

void write_inode(inode_id_t inode_id,inode_t inode) {
    block_id_t block_id;
    int offset;
    locate_inode(inode_id, &block_id, &offset);
    block_t block;
    read_block(block_id, block);
    *(inode_t*)(block+offset) = inode;
    write_block(block_id,block);
}

void delete_inode(inode_id_t inode_id) {
    inode_bitmap_t bitmap;
    read_inode_bitmap(bitmap);
    set_bitmap(bitmap,inode_id,0);
    write_inode_bitmap(bitmap);
}

inode_id_t new_inode() {
    inode_bitmap_t bitmap;
    read_inode_bitmap(bitmap);
    for(inode_id_t i=1;i<INODE_NUM;++i) {
        if(!get_bitmap(bitmap,i)) {
            set_bitmap(bitmap,i,1);
            write_inode_bitmap(bitmap);
            return i;
        }
    }
}

void fill_inode(inode_id_t inode_id, bool flag) {
    inode_t tmp;
	tmp.atime = tmp.mtime = tmp.ctime = time(NULL);
	tmp.mode = flag;
	tmp.size = 0;
	tmp.ptr_block = new_block();
	write_inode(inode_id,tmp);
}

void move_inode(inode_id_t inode_id) {
    inode_t inode=read_inode(inode_id);
    block_t block;
    read_block(inode.ptr_block,block);
    delete_block((block_id_t*)block,blocks_count(inode.size));
    delete_block(&inode.ptr_block,1);
    delete_inode(inode_id);
}

typedef struct {
	char file_name[MAXFILENAME];
	inode_id_t inode_id;
}directory;

directory* read_dir(inode_t dir_inode, directory *buf) {
    static directory dir[INODE_NUM];
    block_t block;
    read_block(dir_inode.ptr_block,block);
    block_id_t *block_id = (block_id_t*)block;
    if(!buf) buf=dir;
    char* p=(char*)buf;
    for(int i=0;i<blocks_count(dir_inode.size);++i)
        read_block(block_id[i],p),p+=BLOCK_SIZE;
    return buf;
}

void extend_dir(inode_t dir_inode) {
    int c1=blocks_count(dir_inode.size);
    int c2=blocks_count(dir_inode.size+1);
    if(c1==c2) return;
    block_t block;
    read_block(dir_inode.ptr_block,block);
    block_id_t *block_id = (block_id_t*)block;
    block_id[c2-1]=new_block();
    write_block(dir_inode.ptr_block,block);
}

void write_dir(inode_t dir_inode, directory* dirs) {
    block_t block;
    read_block(dir_inode.ptr_block,block);
    block_id_t *block_id = (block_id_t*)block;
    char *p=(char*)dirs;
    int i;
    for(i=0;i<blocks_count(dir_inode.size);++i)
        write_block(block_id[i],p),p+=BLOCK_SIZE;
    int j=i,cnt=0;
    for(;i<POINTER_NUM&&block_id[i];++i) ++cnt;
    delete_block(&block_id[j],cnt);
    write_block(dir_inode.ptr_block,block);
}

int count_dir(inode_t dir_inode) {
    return dir_inode.size/sizeof(directory);
}

directory* find_dir(directory *dir,int count,const char* name,int len) {
    char file_name[MAXFILENAME];
    memset(file_name,0,MAXFILENAME);
    memcpy(file_name,name,len);
    for(directory* dirs=dir;dirs!=dir+count;++dirs) {
        if(memcmp(dirs->file_name,file_name,MAXFILENAME)==0)
            return dirs;
    }
    return NULL;
}

inode_t find_path(const char* path, inode_id_t* inode_id) {
    inode_t inode = read_inode(ROOT);
    if(inode_id) *inode_id = ROOT;
    while(233) {
        if(*path == '/') path++;
        if(!(*path)) break;
        int len=strchrnul(path,'/')-path;
        //while(*path!='/'&&*path!='\0') path++,len++;
        if(!inode.mode) {
            if_error = 1;
            break;
        }
        directory *dir=find_dir(read_dir(inode,NULL),count_dir(inode),path,len);
        if(dir) {
            inode = read_inode(dir->inode_id);
            if(inode_id) *inode_id = dir->inode_id;
            path+=len;
        }
        else {
            if_error = 1;
            break;
        }
    }
    return inode;
}

inode_t find_parent_path(const char *path,inode_id_t *dir_inode_id,char buff[MAXFILENAME+1], bool *exists) {
    inode_t dir_inode; 
    inode_t inode=read_inode(ROOT);
    inode_id_t inode_id=ROOT;
    if(DEBUG) printf("say hi\n");
    if(exists) *exists=1;
    bool flag=0;
    while(233){
        if(*path=='/') path++;
        if(!(*path)) break;
        if(flag) {
            if_error = 1;
            break;
        }
        int len= strchrnul(path,'/') - path;
        //while(*path!='/'&&*path!='\0') path++,len++;
        if(!inode.mode) {
            if_error = 1;
            break;
        }
        directory *dir=find_dir(read_dir(inode,NULL),count_dir(inode),path,len);
        dir_inode=inode;
        if(dir_inode_id)*dir_inode_id=inode_id;
        if(dir) {
            inode=read_inode(dir->inode_id);
            if(dir_inode_id) inode_id=dir->inode_id;
        }
        else flag=1;
        memset(buff,0,MAXFILENAME+1);
        strncpy(buff,path,len);
        buff[len]='\0';
        path+=len;
    }
    return dir_inode;
}

int make_dir(const char *path, bool flag) {
    inode_id_t dir_inode_id;
    char file_name[MAXFILENAME+1];
    bool exists;
    inode_t dir_inode=find_parent_path(path, &dir_inode_id, file_name, &exists);
    int count=count_dir(dir_inode);
    directory *dir=read_dir(dir_inode, NULL);
    extend_dir(dir_inode);
    dir[count].inode_id=new_inode();
    memcpy(dir[count].file_name,file_name,MAXFILENAME);
    fill_inode(dir[count].inode_id,flag);

    dir_inode.mtime=dir_inode.ctime=time(NULL);
    dir_inode.size+=sizeof(directory);
    write_inode(dir_inode_id, dir_inode);
    write_dir(dir_inode, dir);
    return 0;
}

int remove(const char* path) {
    inode_id_t dir_inode_id;
    char name[MAXFILENAME+1];
    inode_t dir_inode=find_parent_path(path,&dir_inode_id,name,NULL);
    directory *dirs=read_dir(dir_inode,NULL);
    directory *dir=find_dir(dirs,count_dir(dir_inode),name,strlen(name));
    if(!dir) return -ENOENT;
    move_inode(dir->inode_id);

    int cnt=(dirs+count_dir(dir_inode)-(dir+1)); //
    memmove(dir,dir+1,cnt*sizeof(directory));
    dir_inode.size-=sizeof(directory);
    dir_inode.atime=dir_inode.mtime=time(NULL);
    write_dir(dir_inode,dirs);
    write_inode(dir_inode_id,dir_inode);
    return 0;
}

//Format the virtual block device in the following function
int mkfs()
{
    //super block
    if(DEBUG) printf("%d\n",sizeof(inode_t));

    vfs_t vfs;
	vfs.bsize = BLOCK_SIZE;
	vfs.blocks = BLOCK_NUM;
	vfs.bfree = vfs.bavail = DATABLOCK_NUM;
	vfs.files = 1;
	vfs.ffree = vfs.favail = INODE_NUM;
	vfs.namemax = MAXFILENAME;
	//disk_write(SUPBER_BLOCK,&vfs);
    
    fill_inode(ROOT,1);
	return 0;
}

//Filesystem operations that you need to implement
int fs_getattr (const char *path, struct stat *attr)
{
    if(DEBUG) printf("say hi from getattr\n");
    inode_t inode = find_path(path,0);
	if(if_error == 1) return -ENOENT;
    attr->st_mode = inode.mode ? DIRMODE : REGMODE;
    attr->st_nlink = 1;
    attr->st_uid = getuid();
    attr->st_gid = getgid();
    attr->st_size = inode.size;
    attr->st_atime = inode.atime;
    attr->st_mtime = inode.mtime;
    attr->st_ctime = inode.ctime;
	return 0;
}

int fs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    if(DEBUG) printf("say hi from readdir\n");
    inode_id_t inode_id;
    inode_t inode=find_path(path,&inode_id);
    if(!inode.mode) return -ENOENT;
    directory *dir=read_dir(inode,NULL);
    for(directory *dirs=dir;dirs<dir+count_dir(inode);dirs++) {
        static char file_name[MAXFILENAME+1];
        memcpy(file_name,dirs->file_name,MAXFILENAME);
        filler(buffer,file_name,NULL,0);
    }
    inode.atime=time(NULL);
    write_inode(inode_id,inode);
	return 0;
}

int fs_mknod (const char *path, mode_t mode, dev_t dev)
{
    if(DEBUG) printf("say hi from mknod\n");
	return make_dir(path,0);
}

int fs_mkdir (const char *path, mode_t mode)
{
    if(DEBUG) printf("say hi from mkdir\n");
	return make_dir(path,1);
}

int fs_rmdir (const char *path)
{
    if(DEBUG) printf("say hi from rmdir\n");
	return remove(path);
}

int fs_unlink (const char *path)
{
    if(DEBUG) printf("say hi from unlink\n");
	return remove(path);
}

int fs_rename (const char *oldpath, const char *newpath)
{
    if(DEBUG) printf("say hi from rename\n");
    fs_unlink(newpath);
    inode_id_t src_inode_id,dst_inode_id;
    char src_file_name[MAXFILENAME+1],dst_file_name[MAXFILENAME+1];
    inode_t src_inode=find_parent_path(oldpath,&src_inode_id,src_file_name,NULL);
    inode_t dst_inode=find_parent_path(newpath,&dst_inode_id,dst_file_name,NULL);

    static directory src_dirs[INODE_NUM];
    read_dir(src_inode,src_dirs);

    directory* src_dir=find_dir(src_dirs,count_dir(src_inode),src_file_name,strlen(src_file_name));
    src_inode.mtime=src_inode.atime=time(NULL);
    if(!src_dir) return -ENOENT;
    if(src_inode_id==dst_inode_id) {//no need move
        memcpy(src_dir->file_name,dst_file_name,MAXFILENAME);
        write_dir(src_inode,src_dirs);
        write_inode(src_inode_id,src_inode);
    }
    else {
        static directory dst_dirs[INODE_NUM];
        read_dir(dst_inode,dst_dirs);
        extend_dir(dst_inode);

        int cnt=count_dir(dst_inode);
        memcpy(dst_dirs[cnt].file_name,dst_file_name,MAXFILENAME);
        dst_dirs[cnt].inode_id=src_dir->inode_id;
        dst_inode.mtime=dst_inode.atime=time(NULL);
        dst_inode.size+=sizeof(directory);
        write_dir(dst_inode,dst_dirs);
        write_inode(dst_inode_id,dst_inode);

        cnt=src_dirs+count_dir(src_inode)-(src_dir+1);
        memmove(src_dir,src_dir+1,cnt*sizeof(directory));
        src_inode.size-=sizeof(directory);
        write_dir(src_inode,src_dirs);
        write_inode(src_inode_id,src_inode);
    }
	return 0;
}

#define FOR_DATA_BLOCKS(sz) \
    for (int i=offset/BLOCK_SIZE,offset_in_block=offset%BLOCK_SIZE,sz=size,size_in_block; \
            size_in_block=MIN(BLOCK_SIZE-offset_in_block,sz); \
            i++, sz-=size_in_block,offset_in_block=0)

#define MIN(a,b) (a<b?a:b)
#define MAX(a,b) (a>b?a:b)

int fs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
    if(DEBUG) printf("say hi from read\n");
    inode_id_t inode_id;
    inode_t inode=find_path(path,&inode_id);
    block_t block;
    read_block(inode.ptr_block,block);
    block_id_t *block_id=(block_id_t*)block;
    FOR_DATA_BLOCKS(sz) {
        block_t newblock;
        read_block(block_id[i],newblock);
        memcpy(buffer,&newblock[offset_in_block],size_in_block);
        buffer+=size_in_block;
    }
    inode.atime=time(NULL);
    write_inode(inode_id,inode);
	return size;
}

int fs_write (const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
    if(DEBUG) printf("say hi from write\n");
    inode_id_t inode_id;
    inode_t inode=find_path(path,&inode_id);
    block_t block;
    read_block(inode.ptr_block,block);
    block_id_t *block_id=(block_id_t*)block;
    int ned=0;
    FOR_DATA_BLOCKS(sz) {
        if(!block_id[i]) ned++;
    }
    block_id_t tmp[ned];
    new_blocks(ned,tmp);
    FOR_DATA_BLOCKS(sz) {
        if(!block_id[i]) block_id[i]=tmp[--ned];
        block_t newblock;
        read_block(block_id[i],newblock);
        memcpy(&newblock[offset_in_block],buffer,size_in_block);
        buffer+=size_in_block;
        write_block(block_id[i],newblock);
    }
    write_block(inode.ptr_block,block);
    inode.ctime=inode.atime=time(NULL);
    inode.size=MAX(inode.size,size+offset);
    write_inode(inode_id,inode);
	return size;
}

int fs_truncate (const char *path, off_t size)
{
    if(DEBUG) printf("say hi from truncate\n");
    inode_id_t inode_id;
    inode_t inode=find_path(path,&inode_id);
    block_t block;
    read_block(inode.ptr_block,block);
    block_id_t *block_id=(block_id_t*)block;
    int c1=blocks_count(inode.size);
    int c2=blocks_count(size);
    if(size<=inode.size) {
        int ned=c2-c1;
        block_id_t tmp[ned];
        new_blocks(ned,tmp);
        for(block_id_t i=c1;i<c2;++i)
            block_id[i]=tmp[--ned];
    }
    else {
        int c=size%BLOCK_SIZE;
        if(c) {
            block_t tmp;
            read_block(block_id[c2-1],tmp);
            memset(tmp+c,0,BLOCK_SIZE-c);
            write_block(block_id[c2-1],tmp);
        }
        delete_block(&block_id[c2],c1-c2);
    }
    inode.size=size;
    inode.ctime=time(NULL);
    write_block(inode.ptr_block,block);
    write_inode(inode_id,inode);
	return 0;
}

int fs_utime (const char *path, struct utimbuf *buffer)
{
    if(DEBUG) printf("say hi from utime\n");
    inode_id_t inode_id;
    inode_t inode=find_path(path,&inode_id);
    inode.ctime=time(NULL);
    inode.atime=buffer->actime;
    inode.mtime=buffer->modtime;
    write_inode(inode_id,inode);
	return 0;
}

int fs_statfs (const char *path, struct statvfs *stat)
{
    if(DEBUG) printf("say hi from statfs\n");
    block_bitmap_t block_bitmap;
    read_data_bitmap(block_bitmap);
    inode_bitmap_t inode_bitmap;
    read_inode_bitmap(inode_bitmap);
    stat->f_bsize=BLOCK_SIZE;
    stat->f_blocks=DATABLOCK_NUM;
    stat->f_bfree=stat->f_bavail=DATABLOCK_NUM-count_bitmap(block_bitmap);
    stat->f_files=INODE_NUM;
    stat->f_ffree=stat->f_favail=INODE_NUM-1-count_bitmap(inode_bitmap);
    stat->f_namemax=MAXFILENAME;
	return 0;
}

int fs_open (const char *path, struct fuse_file_info *fi)
{
	printf("Open is called:%s\n",path);
	return 0;
}

//Functions you don't actually need to modify
int fs_release (const char *path, struct fuse_file_info *fi)
{
	printf("Release is called:%s\n",path);
	return 0;
}

int fs_opendir (const char *path, struct fuse_file_info *fi)
{
	printf("Opendir is called:%s\n",path);
	return 0;
}

int fs_releasedir (const char * path, struct fuse_file_info *fi)
{
	printf("Releasedir is called:%s\n",path);
	return 0;
}

static struct fuse_operations fs_operations = {
	.getattr    = fs_getattr,
	.readdir    = fs_readdir,
	.read       = fs_read,
	.mkdir      = fs_mkdir,
	.rmdir      = fs_rmdir,
	.unlink     = fs_unlink,
	.rename     = fs_rename,
	.truncate   = fs_truncate,
	.utime      = fs_utime,
	.mknod      = fs_mknod,
	.write      = fs_write,
	.statfs     = fs_statfs,
	.open       = fs_open,
	.release    = fs_release,
	.opendir    = fs_opendir,
	.releasedir = fs_releasedir
};

int main(int argc, char *argv[])
{
	if(disk_init())
		{
		printf("Can't open virtual disk!\n");
		return -1;
		}
	if(mkfs())
		{
		printf("Mkfs failed!\n");
		return -2;
		}
    return fuse_main(argc, argv, &fs_operations, NULL);
}
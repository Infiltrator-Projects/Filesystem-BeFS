/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <infiltratr/filesystem_module_v2.h>
#include "../Libraries/tests/filesystem_allocation_pages.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum{B=1024,N=256,MAX=B*N};
static uint8_t disk[MAX],saved[MAX];static unsigned big,sector,allocs,fail;
static uint8_t*blk(unsigned b){return disk+b*B;}
static void p16(uint8_t*p,uint32_t n){p[big?0:1]=n>>8;p[big?1:0]=n;}
static void p32(uint8_t*p,uint32_t n){p16(p+(big?0:2),n>>16);p16(p+(big?2:0),n);}
static void p64(uint8_t*p,uint64_t n){p32(p+(big?0:4),n>>32);p32(p+(big?4:0),n);}
static void run(uint8_t*p,unsigned start,unsigned len){p32(p,0);p16(p+4,start);p16(p+6,len);}
static void used(unsigned b){unsigned word=b/32;unsigned bit=b%32;blk(1)[word*4+(big?3-bit/8:bit/8)]|=1u<<(bit%8);}
static uint8_t*ino(unsigned id,unsigned parent,unsigned mode,uint64_t bytes)
{
    uint8_t*p=blk(id);p32(p,0x3bbe0ad9);run(p+4,id,1);p32(p+12,100);p32(p+16,200);
    p32(p+20,mode);p32(p+24,1);run(p+44,parent,1);p32(p+64,B);p64(p+208,bytes);return p;
}
static void stream(uint8_t*p,unsigned start,unsigned len){run(p+72,start,len);p64(p+168,(uint64_t)len*B);}
static void node(uint8_t*p,uint64_t left,uint64_t right,uint64_t overflow,const char**names,const uint64_t*values,unsigned n)
{
    p64(p,left);p64(p+8,right);p64(p+16,overflow);p16(p+24,n);
    unsigned total=0;for(unsigned i=0;i<n;++i){unsigned len=strlen(names[i]);memcpy(p+28+total,names[i],len);total+=len;}
    p16(p+26,total);unsigned lens=(28+total+7)&~7u;
    unsigned end=0;for(unsigned i=0;i<n;++i){end+=strlen(names[i]);p16(p+lens+2*i,end);p64(p+lens+2*n+8*i,values[i]);}
}
static void tree_header(uint8_t*p,unsigned depth,unsigned size)
{p32(p,0x69f6c2e8);p32(p+4,B);p32(p+8,depth);p64(p+16,B);p64(p+24,UINT64_MAX);p64(p+32,size);}
static void fixture(void)
{
    memset(disk,0,sizeof disk);uint8_t*p=disk+(big?0:512);memcpy(p,"BEFS",4);
    p32(p+32,0x42465331);p32(p+36,0x42494745);p32(p+40,B);p32(p+44,10);p64(p+48,N);p64(p+56,41);
    p32(p+64,B);p32(p+68,0xdd121031);p32(p+72,1);p32(p+76,13);p32(p+80,1);p32(p+84,0x434c454e);
    run(p+88,2,8);p64(p+96,2);p64(p+104,2);p32(p+112,0x15b6830e);run(p+116,10,1);
    for(unsigned i=0;i<41;++i)used(i);
    p=ino(10,10,0040755,4*B);stream(p,11,4);
    tree_header(blk(11),2,4*B);
    const char*middle[]={"dir"};uint64_t m[]={2*B};node(blk(12),UINT64_MAX,UINT64_MAX,3*B,middle,m,1);
    const char*left[]={".","..","dir"};uint64_t a[]={10,10,15};node(blk(13),UINT64_MAX,3*B,UINT64_MAX,left,a,3);
    const char*right[]={"link","proof.bin"};uint64_t b[]={18,19};node(blk(14),2*B,UINT64_MAX,UINT64_MAX,right,b,2);
    p=ino(15,10,0040755,2*B);stream(p,16,2);tree_header(blk(16),1,2*B);
    const char*sub[]={".","..","child"};uint64_t c[]={15,10,20};node(blk(17),UINT64_MAX,UINT64_MAX,UINT64_MAX,sub,c,3);
    p=ino(18,10,0120777,0);memcpy(p+72,"proof.bin",9);
    p=ino(19,10,0100644,6*B+17);stream(p,21,2);
    run(p+176,23,1);p64(p+184,3*B);run(blk(23),24,1);
    run(p+192,25,1);p64(p+200,7*B);run(blk(25),26,4);run(blk(26),30,4);
    p=ino(20,15,0100644,3);stream(p,34,1);memcpy(blk(34),"yes",3);
    for(unsigned i=0;i<2*B;++i)blk(21)[i]=(uint8_t)(i*13+7);
    for(unsigned i=0;i<B;++i)blk(24)[i]=(uint8_t)(i*17+1);
    for(unsigned i=0;i<4*B;++i)blk(30)[i]=(uint8_t)(i*7+3);
    memcpy(saved,disk,sizeof disk);
}
static int rd(void*c,uint64_t off,void*out,uint32_t n)
{(void)c;if(fail||off>sizeof disk||n>sizeof disk-off||off%sector||n%sector)return -1;memcpy(out,disk+off,n);return 0;}
static void*alloc(void*c,size_t n){(void)c;void*p=malloc(n);if(p)++allocs;return p;}
static void drop(void*c,void*p){(void)c;if(p){assert(allocs);--allocs;free(p);}}
static int fixture_allocated(uint64_t block)
{
    unsigned bit=(unsigned)(block%32);
    return !!(blk(1)[block/32*4+(big?3-bit/8:bit/8)] & (1u<<(bit%8)));
}
int main(int argc,char**argv)
{
    if(argc==4&&!strcmp(argv[1],"--emit")){
        big=0;fixture();FILE*f=fopen(argv[2],"wb"),*p=fopen(argv[3],"wb");assert(f&&p);
        assert(fwrite(disk,1,sizeof disk,f)==sizeof disk);assert(fwrite(blk(21),1,2*B,p)==2*B);
        assert(fwrite(blk(24),1,B,p)==B);assert(fwrite(blk(30),1,3*B+17,p)==3*B+17);
        assert(!fclose(f)&&!fclose(p));return 0;
    }
    assert(argc==2);void*lib=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!lib){puts(dlerror());return 1;}
    IfsFsModuleApiEntryV2 entry_;*(void**)(&entry_)=dlsym(lib,IFS_FS_COMMON_EXPORT_V2);assert(entry_);
    const IfsFsModuleApiV2*api=entry_();assert(ifs_fs_api_v2_valid(api,"befs"));
    const IfsFsCoreOpsV2*core=(const void*)ifs_fs_group_v2(api,IFS_FS_GROUP_CORE);
    const IfsFsMetadataOpsV2*meta=(const void*)ifs_fs_group_v2(api,IFS_FS_GROUP_METADATA);assert(core&&meta&&!core->write);
    for(big=0;big<2;++big)for(sector=512;sector<=4096;sector*=8){
        fixture();IfsFsHostRuntimeV2 h={.abi_version=2,.structure_bytes=sizeof h,.capabilities=IFS_FS_HOST_READ_ONLY,
          .volume_bytes=sizeof disk,.logical_sector_bytes=sector,.physical_sector_bytes=sector,.maximum_transfer_bytes=4096,
          .read_at=rd,.allocate=alloc,.release=drop};
        uint32_t variant=0;int rc=core->probe(&h,&variant);if(rc)fprintf(stderr,"probe rc=%d endian=%u\n",rc,big);
        assert(!rc&&variant==big+1&&!allocs);
        void*v=NULL;IfsFsVolumeInfoV2 info={.structure_bytes=sizeof info};assert(core->mount(&h,&v,&info)==0&&v&&info.free_bytes==(N-41)*B);
        const IfsFsAllocationOpsV2 *allocation=(const void *)ifs_fs_group_v2(api,IFS_FS_GROUP_ALLOCATION);
        check_allocation_pages(allocation,v,B,h.volume_bytes,info.free_bytes,fixture_allocated);
        char name[256];IfsFsEntryV2 e={.structure_bytes=sizeof e,.name=name,.name_capacity=sizeof name};
        assert(core->directory_entry(v,"/",0,&e)==0&&!strcmp(name,"dir"));
        assert(core->directory_entry(v,"/",1,&e)==0&&!strcmp(name,"link"));
        assert(core->directory_entry(v,"/",2,&e)==0&&!strcmp(name,"proof.bin"));
        assert(core->directory_entry(v,"/",3,&e)==IFS_FS_END);
        assert(core->stat(v,"/proof.bin",&e)==0&&e.size_bytes==6*B+17);
        uint8_t out[6*B+17];assert(core->read(v,"/proof.bin",0,out,sizeof out)==0&&!memcmp(out,blk(21),2*B)&&!memcmp(out+2*B,blk(24),B)&&!memcmp(out+3*B,blk(30),3*B+17));
        assert(core->read(v,"/dir/child",0,out,3)==0&&!memcmp(out,"yes",3));
        uint32_t n;assert(meta->readlink(v,"/link",name,sizeof name,&n)==0&&n==9&&!strcmp(name,"proof.bin"));
        run(blk(26),25,4);assert(core->read(v,"/proof.bin",0,out,1)==IFS_FS_CORRUPT);memcpy(disk,saved,sizeof disk);
        p64(blk(13)+8,2*B);assert(core->stat(v,"/proof.bin",&e)==IFS_FS_CORRUPT);memcpy(disk,saved,sizeof disk);
        fail=1;assert(core->stat(v,"/proof.bin",&e)!=0);fail=0;
        assert(core->unmount(v)==0&&core->unmount(v)==IFS_FS_INVALID&&!allocs);
        assert(!memcmp(disk,saved,sizeof disk));
    }
    puts("BeFS both byte orders, B+tree, direct/indirect/double-indirect runs, inline symlink PASS");dlclose(lib);return 0;
}

/* SPDX-License-Identifier: GPL-3.0-or-later
 * Independent Be File System reader (not UnixWare BFS).
 * Disk layouts referenced from Linux v6.12 fs/befs and Haiku's public
 * BFS format documentation and BlockAllocator implementation.
 * Original format research credited to Dominic Giampaolo, Makoto Kato,
 * Will Dyson and Haiku contributors. New Common V2 engine; no VFS proxy.
 * Clean volumes only. Directory keys and data runs are checked before
 * use; the initial profile does not expose indexed attributes.
 */
#include <infiltratr/filesystem_module_v2.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
enum{MAX_NAME=255,MAX_BLOCK=8192,SLOTS=8,MAX_BLOCKS=128*1024*1024,MAX_RUNS=65536,MAX_TREE=65536,MAX_DEPTH=16};
#define INVALID UINT64_MAX
typedef struct{uint64_t start;uint32_t len;} Run;
typedef struct{uint64_t id,parent,bytes;uint32_t type,mode,uid,gid,flags;uint8_t stream[144];} Node;
typedef struct{Node node;uint32_t length;char name[MAX_NAME+1];} Entry;
typedef struct{IfsFsHostRuntimeV2 host;uint64_t blocks,free_blocks,root,log;uint32_t loglen,block,agshift,groups,bitmap_blocks,variant;unsigned be;uint8_t*map;char label[33];uintptr_t token;} Volume;
static Volume slots[SLOTS];static uintptr_t generation;
static atomic_flag gate=ATOMIC_FLAG_INIT;
static int enter(void){return !atomic_flag_test_and_set_explicit(&gate,memory_order_acquire);}
static int leave(int r){atomic_flag_clear_explicit(&gate,memory_order_release);return r;}
static void copy(void*d,const void*s,size_t n){uint8_t*a=d;const uint8_t*b=s;while(n--)*a++=*b++;}
static void zero(void*d,size_t n){uint8_t*a=d;while(n--)*a++=0;}
static int same(const void*a,const void*b,size_t n){const uint8_t*p=a,*q=b;while(n--)if(*p++!=*q++)return 0;return 1;}
static uint32_t u16(const uint8_t*p,unsigned be){return be?((uint32_t)p[0]<<8|p[1]):((uint32_t)p[1]<<8|p[0]);}
static uint32_t u32(const uint8_t*p,unsigned be){return be?u16(p,be)<<16|u16(p+2,be):u16(p+2,be)<<16|u16(p,be);}
static uint64_t u64(const uint8_t*p,unsigned be){return be?(uint64_t)u32(p,be)<<32|u32(p+4,be):(uint64_t)u32(p+4,be)<<32|u32(p,be);}
static int cancel(const Volume*v){return v->host.cancelled&&v->host.cancelled(v->host.context);}
static int rd(const Volume*v,uint64_t off,void*p,uint32_t n){return ifs_fs_host_read_bytes_v2(&v->host,off,p,n);}
static int overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m){return a<b+m&&b<a+n;}
static int allocated(const Volume*v,uint64_t b)
{return b<v->blocks&&(u32(v->map+(size_t)(b/32)*4,v->be)&(1u<<(b%32)));}
static int good(const Volume*v,Run r)
{
    if(!r.len||r.start<=v->bitmap_blocks||r.start>=v->blocks||r.len>v->blocks-r.start||
       overlap(r.start,r.len,v->log,v->loglen))return 0;
    for(uint32_t i=0;i<r.len;++i)if(!allocated(v,r.start+i))return 0;
    return 1;
}
static int brun(const Volume*v,const uint8_t*p,Run*out)
{
    uint32_t group=u32(p,v->be),start=u16(p+4,v->be),len=u16(p+6,v->be),limit=1u<<v->agshift;
    if(!len){if(group||start)return IFS_FS_CORRUPT;*out=(Run){0};return IFS_FS_OK;}
    if(group>=v->groups||start>=limit||len>limit-start)return IFS_FS_CORRUPT;
    uint64_t b=((uint64_t)group<<v->agshift)+start;
    if(b>=v->blocks||len>v->blocks-b)return IFS_FS_CORRUPT;
    *out=(Run){b,len};return IFS_FS_OK;
}
static void dispose(Volume*v){if(v->map)v->host.release(v->host.context,v->map);zero(v,sizeof(*v));}
static int inode(const Volume*v,uint64_t id,uint64_t parent,Node*out)
{
    if(!good(v,(Run){id,1}))return IFS_FS_CORRUPT;
    uint8_t p[216];int rc=rd(v,id*v->block,p,sizeof p);if(rc)return rc;
    unsigned be=v->be;Run self,up;
    if(u32(p,be)!=0x3bbe0ad9u||brun(v,p+4,&self)||self.start!=id||self.len!=1||
       brun(v,p+44,&up)||up.len!=1||!good(v,up)||(parent&&up.start!=parent)||
       u32(p+64,be)!=v->block)return IFS_FS_CORRUPT;
    Node n={.id=id,.parent=up.start,.uid=u32(p+12,be),.gid=u32(p+16,be),.mode=u32(p+20,be),.flags=u32(p+24,be)};
    if(!(n.flags&1)||(n.flags&0x14))return IFS_FS_CORRUPT;
    switch(n.mode&0170000u){
    case 0040000:n.type=IFS_FS_ENTRY_DIRECTORY;break;
    case 0100000:n.type=IFS_FS_ENTRY_FILE;break;
    case 0120000:n.type=IFS_FS_ENTRY_LINK;break;
    default:return IFS_FS_UNSUPPORTED;
    }
    copy(n.stream,p+72,sizeof n.stream);
    if(n.type==IFS_FS_ENTRY_LINK&&!(n.flags&0x40)){
        uint32_t len=0;while(len<144&&n.stream[len])++len;
        if(!len||len==144)return IFS_FS_CORRUPT;
        n.bytes=len;
    }else{
        n.bytes=u64(p+208,be);uint64_t d=u64(p+168,be),i=u64(p+184,be),dd=u64(p+200,be);
        if(d%v->block||i%v->block||dd%v->block||(i&&i<d)||(dd&&(!i||dd<i))||
           d>v->blocks*v->block||i>v->blocks*v->block||dd>v->blocks*v->block||
           n.bytes>(dd?dd:i?i:d))return IFS_FS_CORRUPT;
    }
    *out=n;return IFS_FS_OK;
}
typedef struct{const Volume*v;const Node*n;uint8_t*seen;Run*data;uint32_t count;uint64_t blocks;} Map;
/* Mark all consumed metadata and file runs in a private ownership map.
 * A repeated block is corruption, not a valid sparse or duplicate run. */
static int claim(Map*m,Run r)
{
    if(!good(m->v,r)||overlap(r.start,r.len,m->n->id,1))return IFS_FS_CORRUPT;
    for(uint32_t i=0;i<r.len;++i){
        uint64_t b=r.start+i;unsigned bit=1u<<(b%8);
        if(m->seen[b/8]&bit)return IFS_FS_CORRUPT;
        m->seen[b/8]|=(uint8_t)bit;
    }
    return IFS_FS_OK;
}
static int add(Map*m,Run r)
{
    if(m->count==MAX_RUNS)return IFS_FS_UNSUPPORTED;
    int rc=claim(m,r);if(rc)return rc;
    m->data[m->count++]=r;m->blocks+=r.len;return IFS_FS_OK;
}
static int read_table(Map*m,Run table,uint64_t target,unsigned double_level)
{
    const Volume*v=m->v;
    if(table.len>1024)return IFS_FS_UNSUPPORTED;
    int rc=claim(m,table);if(rc)return rc;
    uint8_t raw[MAX_BLOCK];
    for(uint32_t b=0;b<table.len;++b){
        if(cancel(v))return IFS_FS_BUSY;
        rc=rd(v,(table.start+b)*v->block,raw,v->block);if(rc)return rc;
        for(uint32_t pos=0;pos<v->block;pos+=8){
            Run r;rc=brun(v,raw+pos,&r);if(rc)return rc;
            if(m->blocks==target){if(r.len)return IFS_FS_CORRUPT;continue;}
            if(!r.len)return IFS_FS_CORRUPT;
            if(double_level){
                if(r.len!=4)return IFS_FS_UNSUPPORTED;
                uint64_t span=(uint64_t)r.len*(v->block/8)*4;
                uint64_t end=target-m->blocks<span?target:m->blocks+span;
                rc=read_table(m,r,end,0);
            }else{
                if(r.len>target-m->blocks)return IFS_FS_CORRUPT;
                rc=add(m,r);
            }
            if(rc)return rc;
        }
    }
    return m->blocks==target?IFS_FS_OK:IFS_FS_CORRUPT;
}
static int mapping(Map*m)
{
    const Volume*v=m->v;const Node*n=m->n;unsigned be=v->be;
    uint64_t d=u64(n->stream+96,be)/v->block,i=u64(n->stream+112,be)/v->block,dd=u64(n->stream+128,be)/v->block;
    int rc;
    for(unsigned j=0;j<12;++j){
        Run r;rc=brun(v,n->stream+j*8,&r);if(rc)return rc;
        if(!r.len){if(m->blocks!=d)return IFS_FS_CORRUPT;continue;}
        if(m->blocks==d||r.len>d-m->blocks)return IFS_FS_CORRUPT;
        rc=add(m,r);if(rc)return rc;
    }
    if(m->blocks!=d)return IFS_FS_CORRUPT;
    Run ind,dbl;rc=brun(v,n->stream+104,&ind);if(rc)return rc;
    rc=brun(v,n->stream+120,&dbl);if(rc)return rc;
    if(i>d){if(!ind.len)return IFS_FS_CORRUPT;rc=read_table(m,ind,i,0);if(rc)return rc;}
    else if(ind.len)return IFS_FS_CORRUPT;
    if(dd>i){if(!dbl.len)return IFS_FS_CORRUPT;rc=read_table(m,dbl,dd,1);if(rc)return rc;}
    else if(dbl.len)return IFS_FS_CORRUPT;
    return m->blocks==(dd?dd:i?i:d)?IFS_FS_OK:IFS_FS_CORRUPT;
}
static int file_read(const Volume*v,const Node*n,uint64_t at,void*out,uint32_t len)
{
    if((!out&&len)||at>n->bytes||len>n->bytes-at)return IFS_FS_INVALID;
    if(!len)return IFS_FS_OK;
    if(n->type==IFS_FS_ENTRY_LINK&&!(n->flags&0x40)){copy(out,n->stream+(size_t)at,len);return IFS_FS_OK;}
    Map m={.v=v,.n=n};size_t size=(size_t)((v->blocks+7)/8);
    m.seen=v->host.allocate(v->host.context,size);
    m.data=v->host.allocate(v->host.context,MAX_RUNS*sizeof(Run));
    int rc=IFS_FS_NO_MEMORY;
    if(m.seen&&m.data){
        zero(m.seen,size);rc=mapping(&m);
        if(!rc){
            uint8_t*p=out;
            for(uint32_t i=0;i<m.count&&len;++i){
                uint64_t bytes=(uint64_t)m.data[i].len*v->block;
                if(at>=bytes){at-=bytes;continue;}
                uint32_t chunk=bytes-at<len?(uint32_t)(bytes-at):len;
                rc=rd(v,m.data[i].start*v->block+at,p,chunk);if(rc)break;
                p+=chunk;len-=chunk;at=0;
            }
            if(!rc&&len)rc=IFS_FS_CORRUPT;
        }
    }
    if(m.data)v->host.release(v->host.context,m.data);
    if(m.seen)v->host.release(v->host.context,m.seen);
    return rc;
}
typedef struct{const uint8_t*p;uint32_t n;} Key;
static int compare(Key a,Key b)
{
    uint32_t n=a.n<b.n?a.n:b.n;
    for(uint32_t i=0;i<n;++i)if(a.p[i]!=b.p[i])return a.p[i]<b.p[i]?-1:1;
    return a.n==b.n?0:a.n<b.n?-1:1;
}
typedef struct{
    const Volume*v;const Node*dir;const char*name;uint32_t length,index,count,found,depth,nodesize,visited,leafdepth;
    uint64_t size,prev_leaf,next_leaf,id;uint8_t*seen,*frames;
    char last[MAX_NAME+1];uint32_t lastlen;Entry entry;
} Search;
static int nodeptr(const Search*s,uint64_t off)
{return off>=s->nodesize&&off%s->nodesize==0&&off<=s->size-s->nodesize;}
/* Traverse the actual B+tree rather than trusting only its sibling chain.
 * Every internal bound is checked, all leaves occur at a common depth,
 * and their forward/backward links must match traversal order. */
static int tree(Search*s,uint64_t off,unsigned depth,const Key*lo,const Key*hi)
{
    const Volume*v=s->v;
    if(depth>=s->depth||depth>=MAX_DEPTH||!nodeptr(s,off)||++s->visited>MAX_TREE)return IFS_FS_CORRUPT;
    uint64_t node=off/s->nodesize;unsigned bit=1u<<(node%8);
    if(s->seen[node/8]&bit)return IFS_FS_CORRUPT;
    s->seen[node/8]|=(uint8_t)bit;
    uint8_t*p=s->frames+(size_t)depth*s->nodesize;
    int rc=file_read(v,s->dir,off,p,s->nodesize);if(rc)return rc;
    uint64_t left=u64(p,v->be),right=u64(p+8,v->be),overflow=u64(p+16,v->be);
    uint32_t n=u16(p+24,v->be),keys=u16(p+26,v->be);
    uint32_t lens=(28+keys+7)&~7u;
    if(lens>s->nodesize||n>(s->nodesize-lens)/10||
       (!n&&keys)||(n&&!keys)||(left!=INVALID&&!nodeptr(s,left))||
       (right!=INVALID&&!nodeptr(s,right)))return IFS_FS_CORRUPT;
    uint32_t values=lens+2*n,prev=0;Key last={0};
    unsigned leaf=overflow==INVALID;
    if(leaf){
        if(!s->leafdepth)s->leafdepth=depth+1;
        if(s->leafdepth!=depth+1||left!=s->prev_leaf||(s->prev_leaf!=INVALID&&s->next_leaf!=off))
            return IFS_FS_CORRUPT;
        s->prev_leaf=off;s->next_leaf=right;
    }else if(!nodeptr(s,overflow))return IFS_FS_CORRUPT;
    for(uint32_t i=0;i<n;++i){
        uint32_t end=u16(p+lens+2*i,v->be);
        if(end<=prev||end>keys||end-prev>MAX_NAME)return IFS_FS_CORRUPT;
        Key k={p+28+prev,end-prev};
        if((i&&compare(last,k)>=0)||(lo&&compare(k,*lo)<=0)||(hi&&compare(k,*hi)>0))
            return IFS_FS_CORRUPT;
        for(uint32_t j=0;j<k.n;++j)if(!k.p[j]||k.p[j]=='/'||k.p[j]==92)return IFS_FS_CORRUPT;
        uint64_t val=u64(p+values+8*i,v->be);
        if(!leaf){
            rc=tree(s,val,depth+1,i?&last:lo,&k);if(rc)return rc;
        }else{
            if(s->lastlen&&compare((Key){(const uint8_t*)s->last,s->lastlen},k)>=0)return IFS_FS_CORRUPT;
            copy(s->last,k.p,k.n);s->lastlen=k.n;
            int dot=k.n==1&&k.p[0]=='.',dotdot=k.n==2&&same(k.p,"..",2);
            if(dot||dotdot){
                if(val!=(dot?s->dir->id:s->dir->parent))return IFS_FS_CORRUPT;
            }else{
                if(!good(v,(Run){val,1})||val==s->dir->id)return IFS_FS_CORRUPT;
                if(s->name?(s->length==k.n&&same(k.p,s->name,k.n)):s->index==s->count){
                    if(s->found++)return IFS_FS_CORRUPT;
                    s->id=val;s->entry.length=k.n;copy(s->entry.name,k.p,k.n);s->entry.name[k.n]=0;
                }
                if(++s->count>MAX_TREE*32)return IFS_FS_UNSUPPORTED;
            }
        }
        last=k;prev=end;
    }
    if(prev!=keys)return IFS_FS_CORRUPT;
    if(!leaf){rc=tree(s,overflow,depth+1,n?&last:lo,hi);if(rc)return rc;}
    return IFS_FS_OK;
}
static int child(const Volume*v,const Node*dir,const char*name,uint32_t length,uint32_t index,Entry*out)
{
    if(dir->type!=IFS_FS_ENTRY_DIRECTORY)return IFS_FS_NOT_FOUND;
    if(dir->bytes<40)return IFS_FS_CORRUPT;
    uint8_t p[40];int rc=file_read(v,dir,0,p,sizeof p);if(rc)return rc;
    uint32_t nodesize=u32(p+4,v->be),depth=u32(p+8,v->be);
    uint64_t root=u64(p+16,v->be),size=u64(p+32,v->be);
    if(u32(p,v->be)!=0x69f6c2e8u||u32(p+12,v->be)||nodesize!=1024||
       !depth||depth>MAX_DEPTH||size>dir->bytes||size<2*nodesize||size%nodesize||
       size/nodesize>MAX_TREE)return IFS_FS_CORRUPT;
    Search s={.v=v,.dir=dir,.name=name,.length=length,.index=index,.nodesize=nodesize,.depth=depth,.size=size,.prev_leaf=INVALID,.next_leaf=INVALID};
    size_t bytes=(size_t)((size/nodesize+7)/8);
    s.seen=v->host.allocate(v->host.context,bytes);s.frames=v->host.allocate(v->host.context,(size_t)depth*nodesize);
    rc=IFS_FS_NO_MEMORY;
    if(s.seen&&s.frames){zero(s.seen,bytes);rc=tree(&s,root,0,NULL,NULL);if(!rc&&s.next_leaf!=INVALID)rc=IFS_FS_CORRUPT;}
    if(s.seen)v->host.release(v->host.context,s.seen);
    if(s.frames)v->host.release(v->host.context,s.frames);
    if(rc)return rc;
    if(!s.found)return name?IFS_FS_NOT_FOUND:IFS_FS_END;
    rc=inode(v,s.id,dir->id,&s.entry.node);if(rc)return rc;
    *out=s.entry;return IFS_FS_OK;
}
static int candidate(const Volume*h,uint64_t at,Volume*out)
{
    Volume v=*h;uint8_t p[132];int rc=rd(&v,at,p,sizeof p);if(rc)return rc;
    if(u32(p+32,0)==0x42465331u)v.be=0;
    else if(u32(p+32,1)==0x42465331u)v.be=1;
    else return IFS_FS_NOT_FOUND;
    unsigned be=v.be;v.variant=1+be;
    if(u32(p+36,be)!=0x42494745u||u32(p+68,be)!=0xdd121031u||u32(p+112,be)!=0x15b6830eu)
        return IFS_FS_CORRUPT;
    v.block=u32(p+40,be);uint32_t shift=u32(p+44,be);v.blocks=u64(p+48,be);
    uint64_t used=u64(p+56,be);
    v.agshift=u32(p+76,be);v.groups=u32(p+80,be);
    if(shift<10||shift>13||v.block!=(1u<<shift)||u32(p+64,be)!=v.block||
       !v.blocks||v.blocks>MAX_BLOCKS||v.blocks>h->host.volume_bytes/v.block||used>v.blocks||
       v.agshift<shift+3||v.agshift>16||u32(p+72,be)!=(1u<<(v.agshift-shift-3))||
       v.groups!=(v.blocks+(1u<<v.agshift)-1)/(1u<<v.agshift))return IFS_FS_CORRUPT;
    if(u32(p+84,be)!=0x434c454eu||u64(p+96,be)!=u64(p+104,be))return IFS_FS_UNSUPPORTED;
    Run log,root;rc=brun(&v,p+88,&log);if(rc)return rc;
    rc=brun(&v,p+116,&root);if(rc)return rc;
    v.bitmap_blocks=(uint32_t)((v.blocks+v.block*8-1)/(v.block*8));
    v.log=log.start;v.loglen=log.len;v.root=root.start;
    if(!log.len||log.start<=v.bitmap_blocks||root.len!=1||v.root<=v.bitmap_blocks||
       overlap(v.root,1,log.start,log.len))return IFS_FS_CORRUPT;
    size_t bytes=(size_t)((v.blocks+31)/32)*4;
    v.map=v.host.allocate(v.host.context,bytes);if(!v.map)return IFS_FS_NO_MEMORY;
    rc=rd(&v,v.block,v.map,(uint32_t)bytes);if(rc)goto fail;
    uint64_t counted=0;
    for(uint64_t b=0;b<v.blocks;++b){
        if(!(b%32768)&&cancel(&v)){rc=IFS_FS_BUSY;goto fail;}
        if(allocated(&v,b))++counted;
    }
    if(counted!=used){rc=IFS_FS_CORRUPT;goto fail;}
    for(uint32_t i=0;i<=v.bitmap_blocks;++i)if(!allocated(&v,i)){rc=IFS_FS_CORRUPT;goto fail;}
    for(uint32_t i=0;i<v.loglen;++i)if(!allocated(&v,v.log+i)){rc=IFS_FS_CORRUPT;goto fail;}
    v.free_blocks=v.blocks-counted;
    for(unsigned i=0;i<32&&p[i];++i){if(p[i]<32||p[i]>=127){rc=IFS_FS_UNSUPPORTED;goto fail;}v.label[i]=(char)p[i];}
    Node n;rc=inode(&v,v.root,v.root,&n);if(rc)goto fail;
    if(n.type!=IFS_FS_ENTRY_DIRECTORY){rc=IFS_FS_CORRUPT;goto fail;}
    Entry e;rc=child(&v,&n,NULL,0,UINT32_MAX,&e);if(rc!=IFS_FS_END){if(!rc)rc=IFS_FS_CORRUPT;goto fail;}
    *out=v;return IFS_FS_OK;
fail:dispose(&v);return rc;
}
static int discover(const IfsFsHostRuntimeV2*h,Volume*out)
{
    if(!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
    if(h->volume_bytes<1024)return IFS_FS_NOT_FOUND;
    Volume host={0},a={0},b={0};size_t n=h->structure_bytes;if(n>sizeof host.host)n=sizeof host.host;
    copy(&host.host,h,n);host.host.structure_bytes=(uint32_t)n;
    int x=candidate(&host,0,&a),y=candidate(&host,512,&b);
    if(!x&&!y){dispose(&a);dispose(&b);return IFS_FS_CORRUPT;}
    if(x==IFS_FS_IO_ERROR||y==IFS_FS_IO_ERROR){dispose(&a);dispose(&b);return IFS_FS_IO_ERROR;}
    if(!x){*out=a;return IFS_FS_OK;}if(!y){*out=b;return IFS_FS_OK;}
    return x!=IFS_FS_NOT_FOUND?x:y;
}
static Volume *mounted(void *p)
{if(p)for(unsigned i=0;i<SLOTS;++i)if(slots[i].token==(uintptr_t)p)return &slots[i];return NULL;}
static void info(const Volume *v,IfsFsVolumeInfoV2 *o)
{*o=(IfsFsVolumeInfoV2){.structure_bytes=sizeof(*o),.variant=v->variant,.max_name_bytes=MAX_NAME,
.block_bytes=v->block,.total_bytes=(uint64_t)v->blocks*v->block,.free_bytes=(uint64_t)v->free_blocks*v->block};}
static int probe_impl(const IfsFsHostRuntimeV2 *h,uint32_t *variant)
{if(!variant)return IFS_FS_INVALID;Volume v={0};int rc=discover(h,&v);if(!rc)*variant=v.variant;dispose(&v);return rc;}
static int mount_impl(const IfsFsHostRuntimeV2 *h,void **out,IfsFsVolumeInfoV2 *o)
{
    if(!out||!o||o->structure_bytes<sizeof(*o))return IFS_FS_INVALID;
    *out=NULL;if(!ifs_fs_host_v2_valid(h))return IFS_FS_INVALID;
    if(!(h->capabilities&IFS_FS_HOST_READ_ONLY))return IFS_FS_ACCESS;
    unsigned i=0;while(i<SLOTS&&slots[i].token)++i;
    if(i==SLOTS||generation==UINTPTR_MAX)return IFS_FS_BUSY;
    int rc=discover(h,&slots[i]);if(rc)return rc;
    slots[i].token=++generation;*out=(void *)slots[i].token;info(&slots[i],o);return IFS_FS_OK;
}
static int unmount_impl(void *p)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;dispose(v);return IFS_FS_OK;}
static int resolve(const Volume *v,const char *path,Entry *out)
{
    if(!path||*path!='/')return IFS_FS_INVALID;
    Entry e={.length=1,.name="/"};int rc=inode(v,v->root,0,&e.node);if(rc)return rc;
    ++path;
    for(unsigned depth=0;*path;++depth){
        if(depth>=256)return IFS_FS_INVALID;
        uint32_t n=0;while(n<=MAX_NAME&&path[n]&&path[n]!='/')++n;
        if(!n||n>MAX_NAME||(n==1&&path[0]=='.')||(n==2&&same(path,"..",2)))return IFS_FS_INVALID;
        Node dir=e.node;rc=child(v,&dir,path,n,0,&e);if(rc)return rc;
        path+=n;if(*path=='/'){++path;if(!*path)return IFS_FS_INVALID;}
    }
    *out=e;return IFS_FS_OK;
}
static int emit(const Entry *e,IfsFsEntryV2 *o)
{
    if(!o||o->structure_bytes<sizeof(*o)||!o->name)return IFS_FS_INVALID;
    if(o->name_capacity<=e->length)return IFS_FS_NO_SPACE;
    char *name=o->name;uint32_t cap=o->name_capacity;
    *o=(IfsFsEntryV2){.structure_bytes=sizeof(*o),.type=e->node.type,.size_bytes=e->node.bytes,
      .name=name,.name_capacity=cap,.name_bytes=e->length};
    copy(name,e->name,e->length+1);return IFS_FS_OK;
}
static int stat_impl(void *p,const char *path,IfsFsEntryV2 *o)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;Entry e;int rc=resolve(v,path,&e);return rc?rc:emit(&e,o);}
static int list_impl(void *p,const char *path,uint32_t i,IfsFsEntryV2 *o)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;Entry dir,e;int rc=resolve(v,path,&dir);if(!rc)rc=child(v,&dir.node,NULL,0,i,&e);return rc?rc:emit(&e,o);}
static int read_impl(void *p,const char *path,uint64_t at,void *out,uint32_t n)
{Volume *v=mounted(p);if(!v)return IFS_FS_INVALID;Entry e;int rc=resolve(v,path,&e);if(rc)return rc;return e.node.type==IFS_FS_ENTRY_FILE?file_read(v,&e.node,at,out,n):IFS_FS_UNSUPPORTED;}
static int readlink_impl(void *p,const char *path,char *out,uint32_t cap,uint32_t *n)
{
    if(!n)return IFS_FS_INVALID;
    *n=0;Volume *v=mounted(p);if(!v||!out)return IFS_FS_INVALID;
    Entry e;int rc=resolve(v,path,&e);if(rc)return rc;
    if(e.node.type!=IFS_FS_ENTRY_LINK)return IFS_FS_INVALID;
    if(!e.node.bytes||e.node.bytes>=cap||e.node.bytes>4095)return IFS_FS_NO_SPACE;
    uint32_t len=(uint32_t)e.node.bytes;
    char target[4096];rc=file_read(v,&e.node,0,target,len);if(rc)return rc;
    for(uint32_t i=0;i<len;++i)if(!target[i])return IFS_FS_CORRUPT;
    copy(out,target,len);out[len]=0;*n=len;return IFS_FS_OK;
}
static int query_impl(void *p,IfsFsVolumeInfoV2 *o)
{Volume *v=mounted(p);if(!v||!o||o->structure_bytes<sizeof(*o))return IFS_FS_INVALID;info(v,o);return IFS_FS_OK;}
static int metadata_impl(void *p,const char *path,IfsFsMetadataV2 *o)
{
    Volume *v=mounted(p);if(!v||!o||o->structure_bytes<sizeof(*o))return IFS_FS_INVALID;
    Entry e;int rc=resolve(v,path,&e);if(rc)return rc;
    *o=(IfsFsMetadataV2){.structure_bytes=sizeof(*o),.mask=IFS_FS_META_GET_MODE,
       .mode=e.node.mode,.owner=e.node.uid,.group=e.node.gid};return IFS_FS_OK;
}
static int label_impl(void *p,char *out,uint32_t cap,uint32_t *n)
{
    if(!n)return IFS_FS_INVALID;
    *n=0;Volume *v=mounted(p);if(!v||!out)return IFS_FS_INVALID;
    uint32_t len=0;while(len<32&&v->label[len])++len;
    if(cap<=len)return IFS_FS_NO_SPACE;
    copy(out,v->label,len);out[len]=0;*n=len;return IFS_FS_OK;
}
#define WRAP(name,args,call) static int name args{if(!enter())return IFS_FS_BUSY;return leave(call);}
WRAP(probe,(const IfsFsHostRuntimeV2 *h,uint32_t *v),probe_impl(h,v))
WRAP(mount,(const IfsFsHostRuntimeV2 *h,void **p,IfsFsVolumeInfoV2 *o),mount_impl(h,p,o))
WRAP(unmount,(void *p),unmount_impl(p))
WRAP(stat_path,(void *p,const char *s,IfsFsEntryV2 *o),stat_impl(p,s,o))
WRAP(list_path,(void *p,const char *s,uint32_t i,IfsFsEntryV2 *o),list_impl(p,s,i,o))
WRAP(read_path,(void *p,const char *s,uint64_t a,void *b,uint32_t n),read_impl(p,s,a,b,n))
WRAP(readlink_path,(void *p,const char *s,char *b,uint32_t c,uint32_t *n),readlink_impl(p,s,b,c,n))
WRAP(query,(void *p,IfsFsVolumeInfoV2 *o),query_impl(p,o))
WRAP(metadata,(void *p,const char *s,IfsFsMetadataV2 *o),metadata_impl(p,s,o))
WRAP(label_path,(void *p,char *s,uint32_t c,uint32_t *n),label_impl(p,s,c,n))
static const IfsFsCoreOpsV2 core={
 .header={2,sizeof(core),IFS_FS_GROUP_CORE,0,IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT|IFS_FS_CORE_STAT|IFS_FS_CORE_LIST|IFS_FS_CORE_READ},
 .probe=probe,.mount=mount,.unmount=unmount,.stat=stat_path,.directory_entry=list_path,.read=read_path};
static const IfsFsVolumeOpsV2 volume={.header={2,sizeof(volume),IFS_FS_GROUP_VOLUME,0,IFS_FS_VOLUME_QUERY|IFS_FS_VOLUME_LABEL_GET},.query=query,.label=label_path};
static const IfsFsMetadataOpsV2 meta={.header={2,sizeof(meta),IFS_FS_GROUP_METADATA,0,IFS_FS_META_GET_MODE|IFS_FS_META_READLINK},.get=metadata,.readlink=readlink_path};
static const IfsFsGroupHeaderV2 *group(uint32_t id,uint32_t lo,uint32_t hi)
{if(lo>2||hi<2)return NULL;if(id==IFS_FS_GROUP_CORE)return &core.header;if(id==IFS_FS_GROUP_VOLUME)return &volume.header;if(id==IFS_FS_GROUP_METADATA)return &meta.header;return NULL;}
static const IfsFsModuleApiV2 api={.abi_version=2,.structure_bytes=sizeof(api),.format_id="befs",
#ifdef IFS_BOOT_FIRMWARE
 .runtime_profiles=IFS_FS_PROFILE_FREESTANDING|IFS_FS_PROFILE_FIRMWARE,
#else
 .runtime_profiles=IFS_FS_PROFILE_HOSTED,
#endif
 .group_mask=IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)|IFS_FS_GROUP_BIT(IFS_FS_GROUP_VOLUME)|IFS_FS_GROUP_BIT(IFS_FS_GROUP_METADATA),.query_group=group};
__attribute__((visibility("default")))const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void){return &api;}

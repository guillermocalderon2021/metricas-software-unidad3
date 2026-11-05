#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define CHUNK (1024U * 1024U)
#define MAX_MIB 128U

struct counters { unsigned long long rchar, wchar, read_bytes, write_bytes; };

static double elapsed_ms(const struct timespec *a, const struct timespec *b) {
    return (b->tv_sec - a->tv_sec)*1000.0 + (b->tv_nsec - a->tv_nsec)/1000000.0;
}
static void tick(struct timespec *t) { if (clock_gettime(CLOCK_MONOTONIC,t)) {perror("clock_gettime"); exit(1);} }
static void fail(const char *s) {perror(s); exit(1);}
static unsigned parse_mib(const char *s) {
    char *end=NULL; errno=0; unsigned long x=strtoul(s,&end,10);
    if (errno || !s[0] || *end || x<1 || x>MAX_MIB) {
        fprintf(stderr,"Tamano invalido: usar 1..%u MiB\n",MAX_MIB); exit(2);
    }
    return (unsigned)x;
}
static struct counters io_snapshot(void) {
    struct counters v={0}; char key[100]; unsigned long long n;
    FILE *f=fopen("/proc/self/io","r"); if (!f) fail("/proc/self/io");
    while (fscanf(f,"%99[^:]: %llu\n",key,&n)==2) {
        if (!strcmp(key,"rchar")) v.rchar=n;
        else if (!strcmp(key,"wchar")) v.wchar=n;
        else if (!strcmp(key,"read_bytes")) v.read_bytes=n;
        else if (!strcmp(key,"write_bytes")) v.write_bytes=n;
    }
    if (ferror(f)) fail("leer /proc/self/io");
    fclose(f); return v;
}
static void output_metrics(const char *tag, double ms, struct counters a, struct counters b) {
    printf("fase=%s elapsed_ms=%.3f rchar_delta=%llu read_bytes_delta=%llu wchar_delta=%llu write_bytes_delta=%llu\n",
        tag,ms,b.rchar-a.rchar,b.read_bytes-a.read_bytes,b.wchar-a.wchar,b.write_bytes-a.write_bytes);
}
static void write_all(int fd, const unsigned char *buf, size_t n) {
    size_t off=0; while(off<n){ssize_t r=write(fd,buf+off,n-off);if(r<0&&errno==EINTR)continue;if(r<=0)fail("write");off+=(size_t)r;}
}
static void fill(unsigned char *p) {
    uint32_t x=123456789U;
    for(size_t i=0;i<CHUNK;i++){ x ^= x<<13; x ^= x>>17; x ^= x<<5; p[i]=(unsigned char)x; }
}
static void prepare(const char *path,unsigned mib) {
    unsigned char *buf=malloc(CHUNK); if(!buf)fail("malloc"); fill(buf);
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0600); if(fd<0)fail("open preparar");
    for(unsigned i=0;i<mib;i++)write_all(fd,buf,CHUNK);
    if(fsync(fd))fail("fsync preparar");
    if(close(fd))fail("close preparar");
    printf("preparado=%s tamano_MiB=%u sincronizado=si\n",path,mib);free(buf);
}
static void read_experiment(const char *path, unsigned rounds, int advise) {
    unsigned char *buf=malloc(CHUNK); if(!buf)fail("malloc");
    int fd=open(path,O_RDONLY); if(fd<0)fail("open lectura");
    struct stat st; if(fstat(fd,&st))fail("fstat");
    if(!S_ISREG(st.st_mode)){fprintf(stderr,"Se requiere un archivo regular\n");exit(2);}
    printf("archivo=%s tamano_bytes=%jd rondas=%u fadvise=%s\n",path,(intmax_t)st.st_size,rounds,advise?"DONTNEED antes de primera ronda":"no");
    if(advise){
        int rc=posix_fadvise(fd,0,0,POSIX_FADV_DONTNEED);
        if(rc){fprintf(stderr,"posix_fadvise: %s\n",strerror(rc));exit(1);}
        puts("aviso_DONTNEED=aceptado (no garantiza expulsion de cache)");
    }
    for(unsigned k=0;k<rounds;k++){
        if(lseek(fd,0,SEEK_SET)<0)fail("lseek");
        struct counters before=io_snapshot();struct timespec t0,t1;
        unsigned long long bytes=0;uint64_t checksum=0;
        tick(&t0);
        while(1){ssize_t got=read(fd,buf,CHUNK);if(got<0&&errno==EINTR)continue;if(got<0)fail("read");if(!got)break;bytes+=(unsigned long long)got;
            for(ssize_t j=0;j<got;j+=4096)checksum+=buf[j];
        }
        tick(&t1);struct counters after=io_snapshot();char tag[32];snprintf(tag,sizeof(tag),"lectura_%u",k+1);
        output_metrics(tag,elapsed_ms(&t0,&t1),before,after);
        printf("bytes_leidos=%llu checksum=%" PRIu64 "\n",bytes,checksum);
        if(bytes!=(unsigned long long)st.st_size){fprintf(stderr,"Lectura incompleta\n");exit(1);}
    }
    if(close(fd))fail("close lectura");
    free(buf);
}
static void write_experiment(const char *dir, unsigned mib, int sync_it) {
    char path[4096];int n=snprintf(path,sizeof(path),"%s/escritura-%s.bin",dir,sync_it?"sync":"sin-sync");
    if(n<0 || (size_t)n>=sizeof(path)){fprintf(stderr,"Ruta demasiado larga\n");exit(2);}
    unsigned char *buf=malloc(CHUNK);if(!buf)fail("malloc");fill(buf);
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0600);if(fd<0)fail("open escritura");
    struct counters before=io_snapshot();struct timespec start,end_write,end_sync;
    tick(&start);for(unsigned i=0;i<mib;i++)write_all(fd,buf,CHUNK);tick(&end_write);
    if(sync_it && fsync(fd))fail("fsync escritura");
    tick(&end_sync);
    struct counters after=io_snapshot();
    printf("archivo=%s tamano_MiB=%u fsync=%s write_ms=%.3f fsync_ms=%.3f total_ms=%.3f\n",path,mib,sync_it?"si":"no",
        elapsed_ms(&start,&end_write),sync_it?elapsed_ms(&end_write,&end_sync):0.0,elapsed_ms(&start,&end_sync));
    output_metrics("escritura",elapsed_ms(&start,&end_sync),before,after);
    if(close(fd))fail("close escritura");
    free(buf);
}
static void usage(const char *p){fprintf(stderr,"Uso:\n  %s prepare ARCHIVO MIB\n  %s read ARCHIVO [rondas:1..5] [advise]\n  %s write DIRECTORIO MIB sync|nosync\n",p,p,p);exit(2);}
int main(int argc,char **argv){
    if(argc<2)usage(argv[0]);
    if(!strcmp(argv[1],"prepare")&&argc==4){prepare(argv[2],parse_mib(argv[3]));return 0;}
    if(!strcmp(argv[1],"read")&&(argc==3||argc==4||argc==5)){
        unsigned rounds=argc>=4?parse_mib(argv[3]):2;
        if(rounds>5){fprintf(stderr,"rondas 1..5\n");return 2;}
        if(argc==5&&strcmp(argv[4],"advise"))usage(argv[0]);
        read_experiment(argv[2],rounds,argc==5);return 0;
    }
    if(!strcmp(argv[1],"write")&&argc==5&&(!strcmp(argv[4],"sync")||!strcmp(argv[4],"nosync"))){
        write_experiment(argv[2],parse_mib(argv[3]),!strcmp(argv[4],"sync"));return 0;
    }
    usage(argv[0]);return 2;
}

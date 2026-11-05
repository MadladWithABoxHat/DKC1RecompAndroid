#include "android_saves.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#define N kDkc1AndroidSramSize
static int fsize(const char *p){ struct stat s; return stat(p,&s)?-1:(int)s.st_size; }
static void fill(uint8_t *b, uint8_t v){ memset(b,v,N); }
int main(void){
  uint8_t ram[N], disk[N];
  assert(Dkc1AndroidSavesSetRoot("/tmp/sramtest/root/"));
  /* 1: first launch */
  fill(ram,0);
  assert(Dkc1AndroidSramLoad(ram,N));
  for(int i=0;i<100;i++) Dkc1AndroidSramTick(ram,N);
  assert(fsize("root/save.srm")==-1);              /* unchanged -> no write */
  /* game writes a save over several frames (half-copied states must not persist) */
  fill(ram,1); Dkc1AndroidSramTick(ram,N);
  fill(ram,2); Dkc1AndroidSramTick(ram,N);
  for(int i=0;i<29;i++) Dkc1AndroidSramTick(ram,N);
  assert(fsize("root/save.srm")==-1);              /* still settling */
  Dkc1AndroidSramTick(ram,N); Dkc1AndroidSramTick(ram,N);
  assert(fsize("root/save.srm")==N);
  FILE*f=fopen("root/save.srm","rb"); fread(disk,1,N,f); fclose(f); assert(!memcmp(disk,ram,N));
  assert(fsize("root/save.srm.tmp")==-1);          /* no temp left behind */
  assert(fsize("root/save.srm.bak")==-1);          /* nothing to back up yet */
  /* 2: relaunch loads it */
  uint8_t r2[N]; fill(r2,0);
  assert(Dkc1AndroidSramLoad(r2,N)); assert(!memcmp(r2,ram,N));
  /* 3: second save backs up the first */
  fill(r2,7); assert(Dkc1AndroidSramFlush(r2,N));
  assert(fsize("root/save.srm.bak")==N);
  f=fopen("root/save.srm.bak","rb"); fread(disk,1,N,f); fclose(f); assert(disk[0]==2);
  /* 4: torn main file -> restored from .bak, damaged file kept */
  f=fopen("root/save.srm","wb"); fwrite("xx",1,2,f); fclose(f);
  fill(r2,0); assert(Dkc1AndroidSramLoad(r2,N)); assert(r2[0]==2);
  assert(fsize("root/save.srm.corrupt")==2);
  /* 5: torn main and no backup -> fresh start, nothing deleted */
  unlink("root/save.srm.bak"); f=fopen("root/save.srm","wb"); fwrite("yy",1,2,f); fclose(f);
  fill(r2,9); assert(Dkc1AndroidSramLoad(r2,N)); assert(r2[0]==9);
  assert(fsize("root/save.srm.corrupt")==2);
  /* 6: constantly changing RAM still gets written within the max window */
  unlink("root/save.srm"); unlink("root/save.srm.corrupt");
  fill(r2,0); Dkc1AndroidSramLoad(r2,N);
  for(int i=0;i<299;i++){ r2[0]=(uint8_t)((i%200)+1); Dkc1AndroidSramTick(r2,N);} 
  assert(fsize("root/save.srm")==-1);
  r2[0]=201; Dkc1AndroidSramTick(r2,N);
  assert(fsize("root/save.srm")==N);
  /* 7: write failure keeps going, then recovers */
  unlink("root/save.srm"); unlink("root/save.srm.bak");
  fill(r2,0); Dkc1AndroidSramLoad(r2,N);
  rename("root","root_gone");
  fill(r2,5);
  assert(!Dkc1AndroidSramFlush(r2,N));
  for(int i=0;i<400;i++) Dkc1AndroidSramTick(r2,N);   /* retries, no crash */
  rename("root_gone","root");
  for(int i=0;i<400;i++) Dkc1AndroidSramTick(r2,N);
  assert(fsize("root/save.srm")==N);
  /* 8: legacy RtlWriteSram layout (raw 2048 bytes) is compatible */
  f=fopen("root/save.srm","wb"); fill(disk,0x33); fwrite(disk,1,N,f); fclose(f);
  fill(r2,0); assert(Dkc1AndroidSramLoad(r2,N)); assert(r2[0]==0x33);
  puts("ALL SRAM TESTS PASSED");
  return 0;
}

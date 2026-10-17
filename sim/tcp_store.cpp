#include "TcpStoreHarness.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <optional>
#include <random>
#include <stdexcept>
#include <tuple>
#include <vector>
static void check(bool b,const char*s){if(!b)throw std::runtime_error(s);}
struct Data{std::vector<uint8_t> bytes;uint64_t context;};
struct Write{
    Data data;unsigned length,mode,pos=0,count=0;bool allocated=false,driving=false,complete=false;
    uint64_t lo=0,hi=0;unsigned keep=0;
};
struct Read{
    uint32_t token;unsigned offset,length,stage=0,pos=0;bool error=false;Data data;
};
struct Stored{uint32_t token;bool error;};
struct Test{
    STcpStoreHarness d;std::mt19937_64 rng{0x9293128128ULL};
    std::map<uint32_t,Data> live;std::optional<Write> writer;std::optional<Read> reader;
    std::vector<Stored> results;
    bool stallOutput=false,stallResponse=false,stallStored=false;
    using Beat=std::tuple<uint64_t,uint64_t,unsigned,bool,bool,uint32_t,uint64_t>;
    using Result=std::tuple<uint32_t,unsigned,uint64_t,bool>;
    std::optional<Beat> held;std::optional<Result> heldStore,heldResponse;
    unsigned cycles=0,reads=0,readErrors=0,releases=0,releaseMisses=0,concurrent=0,writeErrors=0,resets=0;
    Test(){reset();}
    void reset(){
        d.set_io$$allocateValid(0);d.set_io$$valid(0);d.set_io$$readValid(0);d.set_io$$releaseValid(0);
        d.set_io$$storedReady(0);d.set_io$$responseReady(0);d.set_io$$outReady(0);
        d.set_reset(1);d.step();d.step();d.set_reset(0);
        writer.reset();reader.reset();live.clear();held.reset();heldStore.reset();heldResponse.reset();
        stallOutput=stallResponse=stallStored=false;++resets;
        for(unsigned i=0;i<10;++i)step();check(d.get_io$$freeBanks()==4,"reset did not reclaim banks");
    }
    void write(unsigned size,unsigned mode=0){
        check(!writer,"overlapping test writes");Write w;w.data.context=rng();w.mode=mode;
        if(mode==5)size=300;
        w.data.bytes.resize(size);for(auto&b:w.data.bytes)b=rng();w.length=size;
        if(mode==1)++w.length;if(mode==2)--w.length;if(mode==6)w.length=65535;
        writer=std::move(w);
    }
    void read(uint32_t token,unsigned offset,unsigned length){
        check(!reader,"overlapping test reads");reader=Read{token,offset,length};
    }
    Beat beat(){return {d.get_io$$outLo(),d.get_io$$outHi(),d.get_io$$outKeep(),bool(d.get_io$$outLast()),
        bool(d.get_io$$outError()),d.get_io$$outToken(),d.get_io$$outContext()};}
    Result storeResult(){return {d.get_io$$storedToken(),d.get_io$$storedLength(),d.get_io$$storedContext(),bool(d.get_io$$storedError())};}
    Result readResult(){return {d.get_io$$responseToken(),d.get_io$$responseLength(),d.get_io$$responseContext(),bool(d.get_io$$responseError())};}
    void step(std::optional<uint32_t> release={}){
        bool av=writer&&!writer->allocated;
        d.set_io$$allocateValid(av);
        if(writer){auto&w=*writer;
            d.set_io$$length(w.allocated?unsigned(rng()%65536):w.length);
            d.set_io$$context(w.allocated?rng():w.data.context);
            if(w.allocated&&!w.complete&&!w.driving&&rng()%5){
                w.driving=true;w.count=std::min(16U,unsigned(w.data.bytes.size())-w.pos);w.lo=rng();w.hi=rng();
                for(unsigned i=0;i<w.count;++i){auto&v=i<8?w.lo:w.hi;unsigned shift=(i%8)*8;
                    v=(v&~(uint64_t(255)<<shift))|(uint64_t(w.data.bytes[w.pos+i])<<shift);}
                w.keep=(1U<<w.count)-1;if(w.mode==3&&w.pos==0)w.keep^=2;
                if(w.mode==7&&w.pos==0)w.keep=32767;
            }
            if(w.driving){d.set_io$$lo(w.lo);d.set_io$$hi(w.hi);d.set_io$$keep(w.keep);
                bool last=w.pos+w.count==w.data.bytes.size();d.set_io$$last(last);d.set_io$$error(w.mode==4&&last);}
        }
        bool iv=writer&&writer->driving;d.set_io$$valid(iv);
        bool rv=reader&&reader->stage==0;d.set_io$$readValid(rv);
        if(reader){d.set_io$$readToken(reader->token);d.set_io$$readOffset(reader->offset);d.set_io$$readLength(reader->length);}
        bool sr=!stallStored&&rng()%4!=0,rr=!stallResponse&&rng()%3!=0,oready=!stallOutput&&rng()%5<3;
        d.set_io$$storedReady(sr);d.set_io$$responseReady(rr);d.set_io$$outReady(oready);
        d.set_io$$releaseValid(bool(release));d.set_io$$releaseToken(release.value_or(0));
        bool miss=release&&!live.count(*release);
        d.step();++cycles;
        if(bool(d.get_io$$releaseMiss())!=miss){std::cerr<<"release="<<release.value_or(0)<<" live="<<live.size()<<'\n';
            throw std::runtime_error("release ownership mismatch");}
        if(held)check(d.get_io$$outValid()&&beat()==*held,"replay payload changed while stalled");
        if(heldStore)check(d.get_io$$storedValid()&&storeResult()==*heldStore,"store result changed while stalled");
        if(heldResponse)check(d.get_io$$responseValid()&&readResult()==*heldResponse,"read result changed while stalled");
        held=d.get_io$$outValid()&&!oready?std::optional<Beat>(beat()):std::nullopt;
        heldStore=d.get_io$$storedValid()&&!sr?std::optional<Result>(storeResult()):std::nullopt;
        heldResponse=d.get_io$$responseValid()&&!rr?std::optional<Result>(readResult()):std::nullopt;
        if(d.get_io$$outValid()){
            check(reader&&reader->stage==2&&!reader->error,"payload without accepted read response");auto&r=*reader;
            check(d.get_io$$outToken()==r.token&&d.get_io$$outContext()==r.data.context&&!d.get_io$$outError(),"replay context/error");
            if(oready){
                unsigned keep=d.get_io$$outKeep(),n=__builtin_popcount(keep);
                check((keep&(keep+1))==0&&r.pos+n<=r.length,"replay length/keep");
                for(unsigned i=0;i<n;++i){uint64_t word=i<8?d.get_io$$outLo():d.get_io$$outHi();
                    if(uint8_t(word>>(8*(i%8)))!=r.data.bytes[r.offset+r.pos+i]){
                        std::cerr<<"token="<<r.token<<" offset="<<r.offset<<" pos="<<r.pos+i<<'\n';
                        throw std::runtime_error("retained payload corrupted or sliced incorrectly");}}
                r.pos+=n;if(d.get_io$$outLast()){check(r.pos==r.length,"truncated replay");++reads;reader.reset();}
                else check(n==16,"nonfinal partial replay beat");
            }
        }
        if(d.get_io$$responseValid()){
            check(reader&&reader->stage==1,"unexpected read response");auto&r=*reader;
            check(d.get_io$$responseToken()==r.token&&d.get_io$$responseLength()==r.length&&
                bool(d.get_io$$responseError())==r.error&&d.get_io$$responseContext()==(r.error?0:r.data.context),"read response mismatch");
            if(rr){if(r.error){++readErrors;reader.reset();}else r.stage=2;}
        }
        if(rv&&d.get_io$$readReady()){
            check(reader&&reader->stage==0,"read command accounting");auto&r=*reader;auto it=live.find(r.token);
            r.error=it==live.end()||(release&&*release==r.token);
            if(!r.error){r.data=it->second;r.error=uint64_t(r.offset)+r.length>r.data.bytes.size();}
            r.stage=1;
        }
        if(d.get_io$$storedValid()){
            check(writer&&writer->complete,"stored result before full input validation");auto&w=*writer;
            check(d.get_io$$storedLength()==w.length&&d.get_io$$storedContext()==w.data.context&&
                bool(d.get_io$$storedError())==(w.mode!=0),"stored result mismatch");
            if(sr){uint32_t token=d.get_io$$storedToken();results.push_back({token,w.mode!=0});
                if(w.mode==0){check(!live.count(token),"live token reused");live.emplace(token,w.data);}else ++writeErrors;
                writer.reset();}
        }
        if(av&&d.get_io$$allocateReady()){check(bool(writer),"allocation accounting");writer->allocated=true;}
        if(iv&&d.get_io$$ready()){
            check(bool(writer),"write beat accounting");auto&w=*writer;w.pos+=w.count;w.driving=false;
            if(w.pos==w.data.bytes.size())w.complete=true;
            if(d.get_io$$outValid()&&oready)++concurrent;
        }
        if(release){if(miss)++releaseMisses;else{++releases;live.erase(*release);}}
    }
    void drain(){unsigned n=0;while(writer||reader){step();check(++n<3000,"store/read deadlock");}step();}
    void readAll(uint32_t token){check(live.count(token),"test token missing");read(token,0,live.at(token).bytes.size());drain();}
};
int main(){try{
    Test t;
    // Reset during a partial write; RAM is not cleared, ownership is.
    t.write(256);while(!t.writer->allocated||t.writer->pos<16)t.step();t.reset();
    // An offered store result is not an ownership transfer until accepted.
    t.stallStored=true;t.write(64);while(!t.d.get_io$$storedValid())t.step();
    uint32_t pendingToken=t.d.get_io$$storedToken();t.read(pendingToken,0,64);
    while(t.reader)t.step();t.step(pendingToken);
    t.stallStored=false;t.drain();t.readAll(pendingToken);t.step(pendingToken);t.step();
    const unsigned sizes[]={0,1,15,16,17,31,32,63,64,128,255,256};
    for(unsigned batch=0;batch<200;++batch){
        t.write(80,1+batch%7);t.drain();check(t.live.empty(),"invalid write retained a bank");
        uint32_t tokens[4];
        for(unsigned k=0;k<4;++k){t.write(sizes[(batch*4+k)%12]);t.drain();tokens[k]=t.results.back().token;}
        check(t.d.get_io$$freeBanks()==0,"full store accounting");
        // Release while a replay's output is stalled. The fifth writer must
        // remain blocked until the old payload has actually finished reading.
        t.stallOutput=true;t.read(tokens[0],0,t.live.at(tokens[0]).bytes.size());
        unsigned guard=0;while(!t.d.get_io$$outValid()){t.step();check(++guard<200,"stalled replay setup");}
        uint32_t stale=tokens[0];t.step(stale);t.write(255);
        for(unsigned k=0;k<40;++k){t.step(k==3?std::optional<uint32_t>(stale):std::nullopt);
            check(!t.d.get_io$$allocateReady()&&t.d.get_io$$freeBanks()==0,"pinned bank reused before replay completed");}
        t.stallOutput=false;t.drain();tokens[0]=t.results.back().token;
        check(tokens[0]!=stale&&(tokens[0]&3)==(stale&3),"bank reuse generation tag");
        t.read(stale,0,0);t.drain();t.step(stale);
        for(unsigned k=0;k<4;++k){
            t.readAll(tokens[k]);
            unsigned length=t.live.at(tokens[k]).bytes.size();
            for(unsigned j=0;j<4;++j){
                unsigned off=j==0?length:(length?unsigned(t.rng()%(length+1)):0);
                unsigned len=j==1?length-off:(length-off?unsigned(t.rng()%(length-off+1)):0);
                t.read(tokens[k],off,len);t.drain();
            }
        }
        t.read(tokens[1],t.live.at(tokens[1]).bytes.size()+1,0);t.drain();
        t.read(tokens[2],65535,65535);t.drain();
        t.read(tokens[3]^0x40000000U,0,1);t.drain();t.step(tokens[3]^0x40000000U);
        // Reader and writer operate in separate RAM banks concurrently.
        t.step(tokens[3]);t.write(256);t.read(tokens[0],1,254);t.drain();tokens[3]=t.results.back().token;
        t.readAll(tokens[3]);t.readAll(tokens[0]);
        // A same-cycle read and release rejects the new read, rather than
        // leaving a memory request referencing a newly free bank.
        t.read(tokens[2],0,0);t.step(tokens[2]);t.drain();
        t.step(tokens[0]);t.step(tokens[1]);t.step(tokens[3]);t.step();
        check(t.live.empty()&&t.d.get_io$$freeBanks()==4,"bank leak after releases");
        if(batch%50==0){
            t.write(256);t.drain();uint32_t token=t.results.back().token;
            t.stallResponse=true;t.read(token,7,200);while(t.reader->stage==0)t.step();
            for(unsigned j=0;j<10;++j)t.step();t.reset();t.read(token,0,0);t.drain();
            t.write(256);t.drain();token=t.results.back().token;
            t.stallOutput=true;t.read(token,3,201);while(!t.d.get_io$$outValid())t.step();t.reset();
        }
    }
    check(t.writeErrors==200&&t.reads>=4000&&t.readErrors>=1000&&t.concurrent>100,"payload store coverage");
    std::cout<<"TCP_PAYLOAD_STORE_PASS writes="<<t.results.size()<<" rejected_writes="<<t.writeErrors
        <<" replays="<<t.reads<<" rejected_reads="<<t.readErrors<<" releases="<<t.releases
        <<" stale_releases="<<t.releaseMisses<<" concurrent_read_write_cycles="<<t.concurrent
        <<" cycles="<<t.cycles<<" resets="<<t.resets
        <<" pinned_release=pass arbitrary_slice=pass generation=pass\n";
    return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}

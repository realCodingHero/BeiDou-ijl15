#include "../ezorsia/HpMpAlert.cpp"
#include "../ezorsia/QuestHelperProtocol.h"
#include <cassert>
#include <cstdio>
namespace {
int forwarded=0,received=0,fieldChanges=0;
size_t payloadLength=0;
bool accepted=false;
void __fastcall Forward(void*,void*,CInPacket*){++forwarded;}
}
bool Memory::SetHook(bool,void**,void*){return true;}
void Memory::WriteInt(DWORD,unsigned int){}
namespace BossRoomAssistDisplay {
void SetMultipliers(unsigned char,unsigned char,unsigned char){}
void ResetMultipliers(){}
bool IsReady(){return false;}
}
namespace QuestHelperUI {
bool Receive(const unsigned char* data,size_t length){++received;payloadLength=length;QuestProtocol::Snapshot snapshot;accepted=QuestProtocol::Parse(data,length,snapshot);return true;}
void FieldChange(){++fieldChanges;}
}
int main(){
    s_ProcessPacket=Forward;
    std::vector<unsigned char> bytes(4,0);
    QuestProtocol::append(bytes,uint16_t(0x1003));QuestProtocol::append(bytes,uint8_t(1));QuestProtocol::append(bytes,uint8_t(0));
    for(int value:{123,0,42,100000000})QuestProtocol::append(bytes,value);
    for(int64_t value:{int64_t(3000),int64_t(0),int64_t(0)})QuestProtocol::append(bytes,value);
    QuestProtocol::append(bytes,0);
    for(int i=0;i<3;++i)QuestProtocol::append(bytes,uint16_t(0));
    CInPacket packet{};packet.Data=bytes.data();packet.Size=static_cast<unsigned short>(bytes.size());packet.DataLen=packet.Size-4;packet.Offset=4;
    // Raw sequence shares the former erroneous 32-bit length read. Its value
    // must never increase the payload length or make a valid packet disappear.
    for(unsigned short raw:{0,1,0x8000,0xFFFF}){
        packet.RawSeq=raw;ProcessPacket_Hook(nullptr,nullptr,&packet);
        assert(accepted && payloadLength==bytes.size()-6 && forwarded==0);
    }
    packet.Size=6;ProcessPacket_Hook(nullptr,nullptr,&packet);assert(!accepted && payloadLength==0 && forwarded==0);
    packet.Size=5;ProcessPacket_Hook(nullptr,nullptr,&packet);assert(forwarded==1 && received==5);
    packet.Size=static_cast<unsigned short>(bytes.size());bytes[4]=0x7D;bytes[5]=0;
    ProcessPacket_Hook(nullptr,nullptr,&packet);assert(forwarded==2 && fieldChanges==1);
    ProcessPacket_Hook(nullptr,nullptr,nullptr);assert(forwarded==3);
    puts("PASS native receive hook: 16-bit length independent of raw sequence, UTF-8 snapshot dispatch, truncation, SetField and native forwarding");
}

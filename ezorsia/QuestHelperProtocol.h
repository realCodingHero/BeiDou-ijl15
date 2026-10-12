#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#include <windows.h>
namespace QuestProtocol {
constexpr uint16_t Opcode=0x1003;
constexpr uint8_t Version=3;
struct Row { int id=0,kind=0,flags=0,current=0,required=0; int64_t cost=0; std::wstring label,info; };
struct Snapshot {
    int view=0,sequence=0,quest=0,character=0,map=0,total=0,features=0,progress=-1,ready=-1;
    int64_t meso=0,token=0,price=0;
    std::wstring title,message;
    std::vector<Row> rows;
};
class Reader {
    const unsigned char* data; size_t size,offset=0;
public:
    Reader(const unsigned char* p,size_t n):data(p),size(n){}
    template<class T> T number() {
        if(size-offset<sizeof(T))throw std::runtime_error("truncated quest packet");
        T v;memcpy(&v,data+offset,sizeof(v));offset+=sizeof(v);return v;
    }
    std::wstring string() {
        unsigned n=number<uint16_t>();
        if(n>2048 || size-offset<n)throw std::runtime_error("invalid quest string");
        int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,reinterpret_cast<const char*>(data+offset),n,nullptr,0);
        if(n && !count)throw std::runtime_error("invalid UTF-8");
        std::wstring v(count,L'\0');
        if(count)MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,reinterpret_cast<const char*>(data+offset),n,&v[0],count);
        offset+=n;return v;
    }
    bool complete()const{return offset==size;}
};
inline bool Parse(const unsigned char* p,size_t n,Snapshot& result) {
    if(!p || n>60000)return false;
    try {
        Reader r(p,n);auto version=r.number<uint8_t>();if(version<1 || version>Version)return false;
        Snapshot s;s.view=r.number<uint8_t>();if(s.view>6)return false;
        s.sequence=r.number<int>();s.quest=r.number<int>();s.character=r.number<int>();s.map=r.number<int>();
        s.meso=r.number<int64_t>();s.token=r.number<int64_t>();s.price=r.number<int64_t>();s.total=r.number<int>();
        s.features=version>=2?r.number<uint8_t>():3;if(s.features&~3)return false;
        s.title=r.string();s.message=r.string();unsigned count=r.number<uint16_t>();
        if(count>50 || s.total<0 || s.total>100000 || s.sequence<=0 || s.character<=0)return false;
        for(unsigned i=0;i<count;++i) {
            Row row;row.id=r.number<int>();row.kind=r.number<uint8_t>();row.flags=r.number<uint8_t>();
            row.current=r.number<int>();row.required=r.number<int>();row.cost=r.number<int64_t>();row.label=r.string();row.info=r.string();
            if(row.kind>7 || row.id<0 || row.current<0 || row.required<0 || row.cost<0)return false;
            s.rows.push_back(std::move(row));
        }
        if(version>=3){s.progress=r.number<int>();s.ready=r.number<int>();if(s.progress<0 || s.ready<0 || s.progress>100000 || s.ready>100000)return false;}
        if(!r.complete())return false;
        result=std::move(s);return true;
    }catch(const std::exception&){return false;}
}
template<class T> inline void append(std::vector<unsigned char>& b,T v) {
    const auto p=reinterpret_cast<const unsigned char*>(&v);b.insert(b.end(),p,p+sizeof(v));
}
inline std::vector<unsigned char> Request(int action,int seq,int quest,int kind,int target,int map,int64_t token=0) {
    std::vector<unsigned char> b;append(b,Opcode);append(b,Version);append(b,uint8_t(action));
    append(b,seq);append(b,quest);append(b,uint8_t(kind));append(b,target);append(b,map);append(b,token);return b;
}
}

// Native Maple Helper (NPC 9900001) views; see MapleHelperUiHandler on the server.
namespace MapleProtocol {
constexpr uint16_t Opcode=0x1004;
// Version 2 adds the Lone Adventurer's Talisman page, version 3 the custom teleport tab and the
// boss-room boost page, version 4 the salon; the server answers with the client's version.
constexpr uint8_t Version=4;
enum Action:uint8_t {Hello=0,Hub=1,Feature=2,Teleports=3,Warp=4,Pet=5,PetSet=6,TalismanList=7,TalismanBorrow=8,
    CustomAdd=9,CustomRemove=10,Boost=11,BoostSet=12,BoostExit=13,
    SalonCatalog=14,SalonState=15,SalonFavorite=16,SalonApply=17,SalonUndo=18,SalonTry=19};
enum View:uint8_t {Open=0,HubView=1,TeleportView=2,PetView=3,Done=4,Error=5,TalismanView=6,BoostView=7,
    SalonCatalogView=8,SalonStateView=9};
// Salon: kinds, catalogue rows (kind = gender, min = colour bits, max = page, icon = first id) and
// state rows (kind 0-2 = favourites, then these).
enum SalonKind:int {SalonHair=0,SalonFace=1,SalonSkin=2};
enum SalonStateRow:int {SalonCurrentSkin=10,SalonPrice=11,SalonUndoable=12,SalonLimit=13};
constexpr int SalonHubFeature=84;
// Open.value selects the page (Open.extra is the exit NPC for the boost page); talisman rows carry these flags.
enum OpenPage:int {OpenHub=0,OpenTalisman=1,OpenBoost=2};
// Teleport categories shown by version 3 (Boss=0 and Event=3 remain for older clients only).
// Custom lists carry the server's per-character limit in Snapshot.extra.
enum TeleportCategory:int {LevelMaps=1,TownMaps=2,CustomMaps=4};
// Boost rows: kind 0 = price of each allocation total (id), kind 1 = paid tier (id) and paid mesos (cost).
enum BoostRow:int {BoostPrice=0,BoostPaid=1};
constexpr int BoostBudget=30;
enum TalismanFlag:int {TalismanAvailable=1,TalismanActive=2,TalismanOwnJob=4,TalismanLocked=8};
struct Row { int id=0,kind=0,flags=0,min=0,max=0,icon=0; int64_t cost=0; std::wstring label,info; };
struct Snapshot {
    int view=0,sequence=0,character=0,cash=0,maplePoint=0,credit=0,flags=0,value=0,extra=0;
    int64_t meso=0;
    std::wstring title,message;
    std::vector<Row> rows;
};
inline bool Parse(const unsigned char* p,size_t n,Snapshot& result) {
    if(!p || n>60000)return false;
    try {
        QuestProtocol::Reader r(p,n);if(r.number<uint8_t>()!=Version)return false;
        Snapshot s;s.view=r.number<uint8_t>();if(s.view>SalonStateView)return false;
        s.sequence=r.number<int>();s.character=r.number<int>();s.meso=r.number<int64_t>();
        s.cash=r.number<int>();s.maplePoint=r.number<int>();s.credit=r.number<int>();s.flags=r.number<uint8_t>();
        s.value=r.number<int>();s.extra=r.number<int>();s.title=r.string();s.message=r.string();
        unsigned count=r.number<uint16_t>();
        if(count>200 || s.sequence<0 || s.character<=0 || (s.sequence==0 && s.view!=Open && s.view!=Done))return false;
        for(unsigned i=0;i<count;++i) {
            Row row;row.id=r.number<int>();row.kind=r.number<uint8_t>();row.flags=r.number<uint8_t>();row.cost=r.number<int64_t>();
            row.min=r.number<int16_t>();row.max=r.number<int16_t>();row.icon=r.number<int>();row.label=r.string();row.info=r.string();
            if(row.cost<0)return false;
            s.rows.push_back(std::move(row));
        }
        if(!r.complete())return false;
        result=std::move(s);return true;
    }catch(const std::exception&){return false;}
}
inline std::vector<unsigned char> Request(int action,int seq,int arg=0,int arg2=0,int64_t arg3=0) {
    std::vector<unsigned char> b;QuestProtocol::append(b,Opcode);QuestProtocol::append(b,Version);QuestProtocol::append(b,uint8_t(action));
    QuestProtocol::append(b,seq);QuestProtocol::append(b,arg);QuestProtocol::append(b,arg2);QuestProtocol::append(b,arg3);return b;
}
}

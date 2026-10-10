#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#include <windows.h>
namespace QuestProtocol {
constexpr uint16_t Opcode=0x1003;
constexpr uint8_t Version=2;
struct Row { int id=0,kind=0,flags=0,current=0,required=0; int64_t cost=0; std::wstring label,info; };
struct Snapshot {
    int view=0,sequence=0,quest=0,character=0,map=0,total=0,features=0;
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
        Reader r(p,n);auto version=r.number<uint8_t>();if(version!=1 && version!=Version)return false;
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

#include "../ezorsia/QuestHelperProtocol.h"
#include <cassert>
#include <cstdio>
using namespace QuestProtocol;
int main() {
    auto request=Request(4,123,1001,2,4000000,0);
    assert(request.size()==29 && request[0]==3 && request[1]==16 && request[2]==2);
    std::vector<unsigned char> b;
    append(b,Version);append(b,uint8_t(1));append(b,123);append(b,1001);append(b,42);append(b,100000000);
    append(b,int64_t(3000));append(b,int64_t(0));append(b,int64_t(0));append(b,1);
    append(b,uint8_t(3));
    auto str=[&](const std::string& s){append(b,uint16_t(s.size()));b.insert(b.end(),s.begin(),s.end());};
    str("Quest");str("");append(b,uint16_t(1));append(b,4000000);append(b,uint8_t(2));append(b,uint8_t(2));
    append(b,5);append(b,10);append(b,int64_t(500));str("\xE7\x8C\xAA");str("5 missing");
    Snapshot s;assert(Parse(b.data(),b.size(),s) && s.character==42 && s.rows.size()==1 && s.rows[0].cost==500 && s.rows[0].label==L"\x732A");
    for(size_t n=0;n<b.size();++n){s.sequence=999;assert(!Parse(b.data(),n,s));assert(s.sequence==999);}
    auto legacy=b;legacy[0]=1;legacy.erase(legacy.begin()+46);
    assert(Parse(legacy.data(),legacy.size(),s) && s.features==3);
    auto disabled=b;disabled[46]=0;assert(Parse(disabled.data(),disabled.size(),s) && s.features==0);
    auto corrupt=b;corrupt[0]=3;assert(!Parse(corrupt.data(),corrupt.size(),s));
    corrupt=b;corrupt[46]=4;assert(!Parse(corrupt.data(),corrupt.size(),s));
    corrupt=b;corrupt.push_back(0);assert(!Parse(corrupt.data(),corrupt.size(),s));
    corrupt=b;corrupt[1]=255;assert(!Parse(corrupt.data(),corrupt.size(),s));
    corrupt=b;corrupt[48]=255;corrupt[49]=255;assert(!Parse(corrupt.data(),corrupt.size(),s));
    corrupt=b;corrupt[corrupt.size()-14]=0xFF;assert(!Parse(corrupt.data(),corrupt.size(),s));
    puts("PASS quest packet: UTF-8 snapshot, request ABI, every truncation boundary, trailing bytes and unsupported versions");
}

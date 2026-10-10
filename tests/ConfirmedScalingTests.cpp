#include "WorldViewport.h"
#include "ConfirmedScalingPolicies.h"
#include <cassert>
#include <cmath>
#include <cstdio>
static int __fastcall IsField(void*,void*,const void*) {return 1;}
int main() {
    WorldViewport::SetSceneScaleForTesting(1.0); // Retain baseline regression coverage.

    using namespace WorldViewport;
    for(int top:{-547,-548,-550})for(int cy=-412;cy<=68;++cy) {
        double shift=ForegroundTopOffset(1080,top,-40,cy);
        assert(shift<=0 && top+cy*(-0.4)+shift<=-541+1e-8);
        if(top+cy*(-0.4)<=-541)assert(shift==0);
    }
    alignas(4) unsigned char map[0x110]{};
    void* methods[19]{};methods[18]=reinterpret_cast<void*>(&IsField);
    *reinterpret_cast<void***>(map+4)=methods;void* stage=map;
    auto camera=reinterpret_cast<RECT*>(map+0xF0);
    for(const auto& p:confirmedPolicies) {
        for(int height:{900,1080,1440}) {
            const int width=height*16/9;
            for(int mutation=0;mutation<3;++mutation) {
                SetContextForTesting(&stage,map,p.bounds,width,height);
                BeginTerrain(map);BeginBackgrounds(map);
                if(p.ground) SetGroundForTesting(map,{p.caps,p.caps+p.capCount});
                for(size_t i=0;i<p.count;++i) {
                    auto b=p.layers[i];if(mutation==1 && i==0) ++b.x;
                    RegisterBackground(map,b.index,b.front,b.ry,b.x,b.rx,b.type,b.y);
                }
                RECT bounds=p.bounds;if(mutation==2) ++bounds.left;
                *camera={bounds.left+width/2,bounds.top+height/2,bounds.right-width/2,bounds.bottom-height/2};
                AdjustCamera(map);
                const auto v=GetViewForTesting();
                assert(v.scale==1 && v.width>0 && v.height>0);
                assert(GetPolicyForTesting()==(mutation ? 0:p.map));
                if(mutation) continue;
                if(p.leftIndex>=0) {
                    const double scale=std::fmax(1.,v.height/600.);
                    for(int cx=v.camera.left;cx<=v.camera.right;++cx) {
                        const double left=BackdropSideOffset(v.width,scale,p.leftEdge,-20,cx,false);
                        const double right=BackdropSideOffset(v.width,scale,p.rightEdge,-20,cx,true);
                        assert(left<=0 && right>=0);
                        assert(scale*p.leftEdge-cx*.2+left<=-v.width/2-1.99);
                        assert(scale*p.rightEdge-cx*.2+right>=v.width/2+1.99);
                    }
                }
                if(p.bottom) assert(v.camera.bottom+v.height/2<=p.bottom);
                if(p.ground) {
                    auto expected=Fit(bounds,width,height);
                    assert(ConstrainGround(expected,bounds,{p.caps,p.caps+p.capCount},true));
                    assert(v.height==expected.height && v.camera.bottom==expected.camera.bottom);
                    assert(v.height<Fit(bounds,width,height).height || v.camera.bottom<Fit(bounds,width,height).camera.bottom);
                }
                if(p.skyIndex>=0) {
                    int ry=0;for(size_t i=0;i<p.count;++i)if(p.layers[i].index==p.skyIndex)ry=p.layers[i].ry;
                    for(int cy=v.camera.top;cy<=v.camera.bottom;++cy) {
                        double sy=std::fmax(1.,v.height/600.),dy=0;
                        CoverBackdrop(v.height,p.skyTop,p.skyBottom,ry,cy,sy,dy);
                        const double parallax=cy*ry/100.;
                        assert(sy*(p.skyTop+parallax)+dy<=-v.height/2-0.99);
                        assert(sy*(p.skyBottom+parallax)+dy>=v.height/2+0.99);
                    }
                }
            }
        }
        printf("PASS confirmed map %d: exact scope, native scale, boundary coverage\n",p.map);
    }
    // Excluded maps with similar motifs or nearly identical bounds stay out.
    for(RECT b: {RECT{-400,-300,400,300},RECT{-449,-500,1164,455}}) {
        SetContextForTesting(&stage,map,b,1920,1080);BeginTerrain(map);BeginBackgrounds(map);
        *camera={b.left+960,b.top+540,b.right-960,b.bottom-540};AdjustCamera(map);
        assert(GetPolicyForTesting()==0);
    }
    const auto& p=confirmedPolicies[0];
    SetContextForTesting(&stage,map,p.bounds,1280,720);BeginTerrain(map);BeginBackgrounds(map);
    for(size_t i=0;i<p.count;++i) {const auto& b=p.layers[i];assert(!RegisterBackground(map,b.index,b.front,b.ry,b.x,b.rx,b.type,b.y));}
    RECT native{1,2,3,4};*camera=native;AdjustCamera(map);assert(EqualRect(camera,&native) && GetPolicyForTesting()==0);
}

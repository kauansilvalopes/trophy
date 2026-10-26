// Decode bounded local game artwork. stb_image is MIT / public domain.
// The side limit is 2400: 4K PS2 box art from the cover collections is about 1530 x 2160, and a decode buffer of
// 2400 x 2400 x 4 bytes (23 MB) is the most one cover may take.
#include <stdlib.h>
#define STBI_ASSERT(x) do { if(!(x)) abort(); } while(0)
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_ONLY_TGA
#define STBI_MAX_DIMENSIONS 4096
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_SIMD
#define STBI_NO_THREAD_LOCALS
#define STBI_MAX_DIMENSIONS 4096
#include "../third_party/stb/stb_image.h"
#include "cover_decode.h"
 // Bars around a letterboxed picture: instead of flat black, fill the square with the picture itself stretched
// to cover it, reduced to a 24x24 grid, stretched back with bilinear filtering (a cheap blur) and darkened, so a
// portrait box art sits on a soft version of its own colours.
static void fill_backdrop(uint32_t* pixels,unsigned edge,const unsigned char* rgba,int w,int h){
 enum{G=24,S=4};
 float grid[G][G][3];
 const float scale=((float)edge/(float)w>(float)edge/(float)h)?(float)edge/(float)w:(float)edge/(float)h;
 const float ox=((float)edge-(float)w*scale)/2.0f,oy=((float)edge-(float)h*scale)/2.0f;
 for(int gy=0;gy<G;gy++)for(int gx=0;gx<G;gx++){
  float r=0,g=0,b=0;
  for(int sy=0;sy<S;sy++)for(int sx=0;sx<S;sx++){
   float dx=((float)gx+((float)sx+0.5f)/(float)S)*(float)edge/(float)G,dy=((float)gy+((float)sy+0.5f)/(float)S)*(float)edge/(float)G;
   int x=(int)((dx-ox)/scale),y=(int)((dy-oy)/scale);
   if(x<0)x=0;if(x>=w)x=w-1;if(y<0)y=0;if(y>=h)y=h-1;
   const unsigned char* p=rgba+((size_t)y*(size_t)w+(size_t)x)*4;
   float a=(float)p[3]/255.0f;
   r+=(float)p[0]*a+22.0f*(1.0f-a);g+=(float)p[1]*a+26.0f*(1.0f-a);b+=(float)p[2]*a+32.0f*(1.0f-a);
  }
  grid[gy][gx][0]=r/(S*S);grid[gy][gx][1]=g/(S*S);grid[gy][gx][2]=b/(S*S);
 }
 for(unsigned y=0;y<edge;y++){
  float fy=((float)y+0.5f)*(float)G/(float)edge-0.5f;if(fy<0)fy=0;if(fy>G-1)fy=G-1;
  int y0=(int)fy;int y1=y0+1<G?y0+1:y0;float ty=fy-(float)y0;
  for(unsigned x=0;x<edge;x++){
   float fx=((float)x+0.5f)*(float)G/(float)edge-0.5f;if(fx<0)fx=0;if(fx>G-1)fx=G-1;
   int x0=(int)fx;int x1=x0+1<G?x0+1:x0;float tx=fx-(float)x0;
   float c[3];
   for(int k=0;k<3;k++){
    float top=grid[y0][x0][k]*(1.0f-tx)+grid[y0][x1][k]*tx,bot=grid[y1][x0][k]*(1.0f-tx)+grid[y1][x1][k]*tx;
    c[k]=(top*(1.0f-ty)+bot*ty)*0.5f;
   }
   pixels[y*edge+x]=0xff000000u|((unsigned)c[2]<<16)|((unsigned)c[1]<<8)|(unsigned)c[0];
  }
 }
}
int decode_cover(const unsigned char* bytes,size_t length,uint32_t* pixels,unsigned edge) {
 int w=0,h=0,channels=0;
 if(!bytes||!pixels||length==0||length>4u*1024u*1024u||edge==0||edge>512)return 0;
 if(!stbi_info_from_memory(bytes,(int)length,&w,&h,&channels)||w<=0||h<=0||w>2400||h>2400)return 0;
 unsigned char* rgba=stbi_load_from_memory(bytes,(int)length,&w,&h,&channels,4);
 if(!rgba)return 0;
 // Preserve the complete image with letterboxing instead of cropping artwork.
 unsigned dw=edge,dh=edge;
 if(w>h)dh=(unsigned)((uint64_t)edge*h/w);else dw=(unsigned)((uint64_t)edge*w/h);
 if(!dw)dw=1;if(!dh)dh=1;
 if(dw<edge||dh<edge)fill_backdrop(pixels,edge,rgba,w,h);
 else for(unsigned i=0;i<edge*edge;i++)pixels[i]=0xff201a16;
 unsigned ox=(edge-dw)/2,oy=(edge-dh)/2;
 for(unsigned y=0;y<dh;y++)for(unsigned x=0;x<dw;x++){
  unsigned sx=(unsigned)((uint64_t)x*w/dw),sy=(unsigned)((uint64_t)y*h/dh);
  const unsigned char* p=rgba+((size_t)sy*w+sx)*4;
  unsigned a=p[3],inv=255-a;
  unsigned red=(p[0]*a+22*inv)/255,green=(p[1]*a+26*inv)/255,blue=(p[2]*a+32*inv)/255;
  pixels[(y+oy)*edge+x+ox]=0xff000000u|(blue<<16)|(green<<8)|red;
 }
 stbi_image_free(rgba);return 1;
}
// Fills dw x dh pixels with the picture scaled to cover the whole rectangle (centre crop, box filtered). Used for
// the portrait cases of the GPU cover flow.
static int rect_decode(const unsigned char* bytes,size_t length,uint32_t* pixels,unsigned dw,unsigned dh,size_t max_length,unsigned max_dst,int max_src) {
 int w=0,h=0,channels=0;
 if(!bytes||!pixels||length==0||length>max_length||dw==0||dh==0||dw>max_dst||dh>max_dst)return 0;
 if(!stbi_info_from_memory(bytes,(int)length,&w,&h,&channels)||w<=0||h<=0||w>max_src||h>max_src)return 0;
 unsigned char* rgba=stbi_load_from_memory(bytes,(int)length,&w,&h,&channels,4);
 if(!rgba)return 0;
 // Source window with the destination's aspect ratio, centred.
 double sw=w,sh=h;
 if((double)w*dh>(double)h*dw)sw=(double)h*dw/dh;else sh=(double)w*dh/dw;
 double ox=((double)w-sw)/2.0,oy=((double)h-sh)/2.0;
 for(unsigned y=0;y<dh;y++){
  unsigned y0=(unsigned)(oy+sh*y/dh),y1=(unsigned)(oy+sh*(y+1)/dh);if(y1<=y0)y1=y0+1;if(y1>(unsigned)h)y1=(unsigned)h;
  for(unsigned x=0;x<dw;x++){
   unsigned x0=(unsigned)(ox+sw*x/dw),x1=(unsigned)(ox+sw*(x+1)/dw);if(x1<=x0)x1=x0+1;if(x1>(unsigned)w)x1=(unsigned)w;
   unsigned r=0,g=0,b=0,n=0;
   for(unsigned sy=y0;sy<y1;sy++)for(unsigned sx=x0;sx<x1;sx++){
    const unsigned char* p=rgba+((size_t)sy*w+sx)*4;
    unsigned a=p[3],inv=255-a;
    r+=(p[0]*a+22*inv)/255;g+=(p[1]*a+26*inv)/255;b+=(p[2]*a+32*inv)/255;n++;
   }
   if(!n)n=1;
   pixels[y*dw+x]=0xff000000u|((b/n)<<16)|((g/n)<<8)|(r/n);
  }
 }
 stbi_image_free(rgba);return 1;
}
int decode_cover_rect(const unsigned char* bytes,size_t length,uint32_t* pixels,unsigned dw,unsigned dh) {
 return rect_decode(bytes,length,pixels,dw,dh,4u*1024u*1024u,1024,2400);
}
// The console keeps a 3840 x 2160 PNG of each game's key art (up to ~16 MB). This reads one and reduces it to the
// wide picture of the game page: up to 32 MB in, 4096 pixels a side, 2048 out. Memory while it runs is about
// 4.5 bytes a source pixel on top of the file (75 MB for 4K), so it must run one at a time.
int decode_cover_rect_large(const unsigned char* bytes,size_t length,uint32_t* pixels,unsigned dw,unsigned dh) {
 return rect_decode(bytes,length,pixels,dw,dh,32u*1024u*1024u,2048,4096);
}

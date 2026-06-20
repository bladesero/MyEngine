#include "Core/Sha256.h"

#include <algorithm>
#include <fstream>

namespace {
constexpr uint32_t k[64] = {
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
uint32_t R(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }
}
void Sha256::Transform(const uint8_t b[64]) {
    uint32_t w[64]{};
    for(int i=0;i<16;++i) w[i]=(uint32_t(b[i*4])<<24)|(uint32_t(b[i*4+1])<<16)|(uint32_t(b[i*4+2])<<8)|b[i*4+3];
    for(int i=16;i<64;++i){uint32_t s0=R(w[i-15],7)^R(w[i-15],18)^(w[i-15]>>3);uint32_t s1=R(w[i-2],17)^R(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
    uint32_t a=m_State[0],b0=m_State[1],c=m_State[2],d=m_State[3],e=m_State[4],f=m_State[5],g=m_State[6],h=m_State[7];
    for(int i=0;i<64;++i){uint32_t S1=R(e,6)^R(e,11)^R(e,25);uint32_t ch=(e&f)^((~e)&g);uint32_t t1=h+S1+ch+k[i]+w[i];uint32_t S0=R(a,2)^R(a,13)^R(a,22);uint32_t maj=(a&b0)^(a&c)^(b0&c);uint32_t t2=S0+maj;h=g;g=f;f=e;e=d+t1;d=c;c=b0;b0=a;a=t1+t2;}
    m_State[0]+=a;m_State[1]+=b0;m_State[2]+=c;m_State[3]+=d;m_State[4]+=e;m_State[5]+=f;m_State[6]+=g;m_State[7]+=h;
}
void Sha256::Update(const void* data,size_t size){if(m_Finalized||!data)return;const auto* p=static_cast<const uint8_t*>(data);m_TotalBytes+=size;while(size){size_t n=(std::min)(size,64-m_BufferSize);std::copy(p,p+n,m_Buffer.begin()+m_BufferSize);m_BufferSize+=n;p+=n;size-=n;if(m_BufferSize==64){Transform(m_Buffer.data());m_BufferSize=0;}}}
Sha256::Digest Sha256::Final(){if(!m_Finalized){uint64_t bits=m_TotalBytes*8;m_Buffer[m_BufferSize++]=0x80;if(m_BufferSize>56){while(m_BufferSize<64)m_Buffer[m_BufferSize++]=0;Transform(m_Buffer.data());m_BufferSize=0;}while(m_BufferSize<56)m_Buffer[m_BufferSize++]=0;for(int i=7;i>=0;--i)m_Buffer[m_BufferSize++]=uint8_t(bits>>(i*8));Transform(m_Buffer.data());m_Finalized=true;}Digest out{};for(size_t i=0;i<8;++i){out[i*4]=uint8_t(m_State[i]>>24);out[i*4+1]=uint8_t(m_State[i]>>16);out[i*4+2]=uint8_t(m_State[i]>>8);out[i*4+3]=uint8_t(m_State[i]);}return out;}
std::string Sha256::ToHex(const Digest& d){static const char h[]="0123456789abcdef";std::string s(64,'0');for(size_t i=0;i<32;++i){s[i*2]=h[d[i]>>4];s[i*2+1]=h[d[i]&15];}return s;}
bool Sha256::FromHex(const std::string& s,Digest& d){if(s.size()!=64)return false;auto v=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;};for(size_t i=0;i<32;++i){int a=v(s[i*2]),b=v(s[i*2+1]);if(a<0||b<0)return false;d[i]=uint8_t((a<<4)|b);}return true;}
std::string Sha256::HashFile(const std::filesystem::path& path,std::string* error){if(error)error->clear();std::ifstream in(path,std::ios::binary);if(!in){if(error)*error="failed to open file for SHA-256: "+path.string();return{};}Sha256 hash;std::array<char,65536> b{};while(in){in.read(b.data(),b.size());hash.Update(b.data(),static_cast<size_t>(in.gcount()));}if(!in.eof()){if(error)*error="failed while hashing: "+path.string();return{};}return ToHex(hash.Final());}

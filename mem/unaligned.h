#ifndef _SYS_MEM_UNALIGNED_H
#define _SYS_MEM_UNALIGNED_H

#include <sys/compiler.h>
#include <sys/cpu.h>

#if defined(CPU_ALLOW_UNALIGNED) && defined(CPU_BIG_ENDIAN)
static inline uint
get_u16_be(const void *p) { return *(u16 *)p; } 

static inline u32
get_u32_be(const void *p) { return *(u32 *)p; } 

static inline u64
get_u64_be(const void *p) { return *(u64 *)p; }

static inline void
put_u16_be(void *p, uint x) { *(u16 *)p = x; } 

static inline void
put_u32_be(void *p, u32 x) { *(u32 *)p = x; } 

static inline void
put_u64_be(void *p, u64 x) { *(u64 *)p = x; } 

static inline u16
cpu_be16(u16 x){return x;}

static inline u16
be16_cpu(u16 x){return x;}
 
static inline u32
cpu_be32(u32 x){return x;}

static inline u32
be32_cpu(u32 x){return x;}

#else

static inline uint
get_u16_be(const void *p)
{
	const byte *c = (const byte *)p;
	return (c[0] << 8) | c[1];
}

static inline u32
get_u32_be(const void *p)
{
	const byte *c = (const byte *)p;
	return (c[0] << 24) | (c[1] << 16) | (c[2] << 8) | c[3];
}

static inline u64
get_u64_be(const void *p)
{
	return ((u64) get_u32_be(p) << 32) | get_u32_be((const byte *)p+4);
}

static inline void
put_u16_be(void *p, uint x)
{
	byte *c = (byte *)p;
	c[0] = x >> 8;
	c[1] = x;
}

static inline void
put_u32_be(void *p, u32 x)
{
	byte *c = (byte *)p;
	c[0] = x >> 24;
	c[1] = x >> 16;
	c[2] = x >> 8;
	c[3] = x;
}

static inline void
put_u64_be(void *p, u64 x)
{
	put_u32_be(p, x >> 32);
	put_u32_be((byte *)p+4, x);
}

static inline u16                                                               
cpu_be16(u16 x){u16 u; put_u16_be(&u, x); return u; }                          

static inline u32                                                               
cpu_be32(u32 x){u32 u; put_u32_be(&u, x); return u; }                          

static inline u32                                                               
cpu_be64(u64 x){u64 u; put_u64_be(&u, x); return u; }                          

static inline u16
be16_cpu(u16 x){return get_u16_be(&x); }
                                                                               
static inline u32                                                               
be32_cpu(u32 x){return get_u32_be(&x); }

static inline u64                                                               
be64_cpu(u64 x){return get_u64_be(&x); }

#endif

static inline u64
get_u40_be(const void *p) 
{
	const byte *c = (const byte *)p;
	return ((u64)c[0] << 32) | get_u32_be(c+1);
}

static inline void
put_u40_be(void *p, u64 x)
{
	byte *c = (byte *)p;
	c[0] = x >> 32;
	put_u32_be(c+1, x);
}


#if defined(CPU_ALLOW_UNALIGNED) && !defined(CPU_BIG_ENDIAN)
static inline uint get_u16_le(const void *p) { return *(u16 *)p; } 

static inline u32 get_u32_le(const void *p) { return *(u32 *)p; } 

static inline u64 get_u64_le(const void *p) { return *(u64 *)p; } 

static inline void
put_u16_le(void *p, uint x) { *(u16 *)p = x; } 

static inline void put_u32_le(void *p, u32 x) { *(u32 *)p = x; } 

static inline void put_u64_le(void *p, u64 x) { *(u64 *)p = x; } 
#else
static inline uint
get_u16_le(const void *p)
{
	const byte *c = p;
	return c[0] | (c[1] << 8);
}

static inline u32
get_u32_le(const void *p)
{
	const byte *c = p;
	return c[0] | (c[1] << 8) | (c[2] << 16) | (c[3] << 24);
}
static inline u64
get_u64_le(const void *p)
{
	return get_u32_le(p) | ((u64) get_u32_le((const byte *)p+4) << 32);
}

static inline void
put_u16_le(void *p, uint x)
{
	byte *c = p;
	c[0] = x;
	c[1] = x >> 8;
}

static inline void
put_u32_le(void *p, u32 x)
{
	byte *c = p;
	c[0] = x;
	c[1] = x >> 8;
	c[2] = x >> 16;
	c[3] = x >> 24;
}

static inline void
put_u64_le(void *p, u64 x)
{
	put_u32_le(p, x);
	put_u32_le((byte *)p+4, x >> 32);
}
#endif

static inline u64
get_u40_le(const void *p) 
{
	const byte *c = (const byte *)p;
	return get_u32_le(c) | ((u64) c[4] << 32);
}

static inline void
put_u40_le(void *p, u64 x)
{
	byte *c = (byte *)p;
	put_u32_le(c, x);
	c[4] = x >> 32;
}

#ifdef CPU_BIG_ENDIAN
static inline uint
get_u16(const void *p) { return get_u16_be(p); }

static inline u32
get_u32(const void *p) { return get_u32_be(p); }

static inline u64
get_u64(const void *p) { return get_u64_be(p); } 

static inline u64
get_u40(const void *p) { return get_u40_be(p); }

static inline void
put_u16(void *p, u16 x) { put_u16_be(p, x); }

static inline void
put_u32(void *p, u32 x) { put_u32_be(p, x); }

static inline void
put_u64(void *p, u64 x) { put_u64_be(p, x); }

static inline void
put_u40(void *p, u64 x) { put_u40_be(p, x); } 

#else
static inline uint
get_u16(const void *p) { return get_u16_le(p); }

static inline u32
get_u32(const void *p) { return get_u32_le(p); }

static inline u64
get_u64(const void *p) { return get_u64_le(p); }

static inline u64
get_u40(const void *p) { return get_u40_le(p); }

static inline void
put_u16(void *p, uint x) { put_u16_le(p, x); }

static inline void
put_u32(void *p, u32 x) { put_u32_le(p, x); }

static inline void
put_u64(void *p, u64 x) { put_u64_le(p, x); }

static inline void
put_u40(void *p, u64 x) { put_u40_le(p, x); }

static inline u32
be24_cpu(u8 bits[3])
{
	return (bits[0] << 16) | (bits[1] << 8) | (bits[2]);
}

static inline u32
le24_cpu(u8 bits[3])
{
	return (bits[2] << 16) | (bits[1] << 8) | (bits[0]);
}

#endif
static inline uint
get_u8(const void *p) { return *(const byte *)p; } 

static inline void
put_u8(void *p, uint x) { *(byte *)p = x; }

#endif

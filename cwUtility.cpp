#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwUtility.h"

void cw::printHex( const void* buf, unsigned bufByteN, bool asciiFl )
{
  const unsigned char* data = static_cast<const unsigned char*>(buf);
  const unsigned       colN = 8;
  unsigned             ci   = 0;
        
  for(unsigned i=0; i<bufByteN; ++i)
  {
    printf("%02x ", data[i] );

    ++ci;
    if( ci == colN || i+1 == bufByteN )
    {
      unsigned n = ci==colN ? colN-1 : ci-1;

      for(unsigned j=0; j<(colN-n)*3; ++j)
        printf(" ");

      if( asciiFl )
      {
        for(unsigned j=i-n; j<=i; ++j)
          if( 32<= data[j] && data[j] < 127 )
            printf("%c",data[j]);
          else
            printf(".");
      }
      
      printf("\n");
      ci = 0;
    }
  }  
}

#ifdef NOT_DEF

// TODO: rewrite to avoid copying
// this code comes via csound source ...
double 		cw::x80ToDouble( unsigned char rate[10] )
{
  	char sign;
    short exp = 0;
    unsigned long mant1 = 0;
    unsigned long mant0 = 0;
    double val;
    unsigned char* p = (unsigned char*)rate;

    exp = *p++;
    exp <<= 8;
    exp |= *p++;
    sign = (exp & 0x8000) ? 1 : 0;
    exp &= 0x7FFF;
    
    mant1 = *p++;
    mant1 <<= 8;
    mant1 |= *p++;
    mant1 <<= 8;
    mant1 |= *p++;
    mant1 <<= 8;
    mant1 |= *p++;

    mant0 = *p++;
    mant0 <<= 8;
    mant0 |= *p++;
    mant0 <<= 8;
    mant0 |= *p++;
    mant0 <<= 8;
    mant0 |= *p++;

    /* special test for all bits zero meaning zero 
       - else pow(2,-16383) bombs */
    if (mant1 == 0 && mant0 == 0 && exp == 0 && sign == 0)
      return 0.0;
    else {
      val  = ((double)mant0) * pow(2.0,-63.0);
      val += ((double)mant1) * pow(2.0,-31.0);
      val *= pow(2.0,((double) exp) - 16383.0);
      return sign ? -val : val;
    }
}

// TODO: rewrite to avoid copying
/*
 * Convert double to IEEE 80 bit floating point
 * Should be portable to all C compilers.
 * 19aug91 aldel/dpwe  covered for MSB bug in Ultrix 'cc'
 */

void cw::doubleToX80(double val, unsigned char rate[10])
{
    char sign = 0;
    short exp = 0;
    unsigned long mant1 = 0;
    unsigned long mant0 = 0;
    unsigned char* p = (unsigned char*)rate;

    if (val < 0.0)	{  sign = 1;  val = -val; }
	
    if (val != 0.0)	/* val identically zero -> all elements zero */
      {
        exp = (short)(std::log(val)/std::log(2.0) + 16383.0);
        val *= pow(2.0, 31.0+16383.0-(double)exp);
        mant1 =((unsigned)val);
        val -= ((double)mant1);
        val *= pow(2.0, 32.0);
        mant0 =((double)val);
      }
    
    *p++ = ((sign<<7)|(exp>>8));
    *p++ = (u_char)(0xFF & exp);
    *p++ = (u_char)(0xFF & (mant1>>24));
    *p++ = (u_char)(0xFF & (mant1>>16));
    *p++ = (u_char)(0xFF & (mant1>> 8));
    *p++ = (u_char)(0xFF & (mant1));
    *p++ = (u_char)(0xFF & (mant0>>24));
    *p++ = (u_char)(0xFF & (mant0>>16));
    *p++ = (u_char)(0xFF & (mant0>> 8));
    *p++ = (u_char)(0xFF & (mant0));

}


bool		cw::isPowerOfTwo( unsigned x )
{
  return !( (x < 2) || (x & (x-1)) );
}

unsigned 	cw::nextPowerOfTwo(	unsigned val )
{
  unsigned i;
	unsigned mask = 1;
	unsigned msb 	= 0;
	unsigned cnt	= 0;
	
	// if val is a power of two return it
	if( isPowerOfTwo(val) )
		return val;

	// next pow of zero is 2
	if( val == 0 )
		return 2;
	
	// if the next power of two can't be represented in 32 bits
	if( val > 0x80000000)
  {
    assert(0);
		return 0;
	}

	// find most sig. bit that is set - the number with only the next msb set is next pow 2 
	for(i=0; i<31; i++,mask<<=1)
		if( mask & val )
		{
			msb = i;
			cnt++;
		}
				
	return 1 << (msb + 1);	
}

unsigned cw::nearestPowerOfTwo( unsigned i )
{
  unsigned vh = nextPowerOfTwo(i);

  if( vh == 2 )
    return vh;

  unsigned vl = vh / 2;

  if( vh - i < i - vl )
    return vh;
  return vl;
}

#endif

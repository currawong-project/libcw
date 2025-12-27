//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#include "cwCommon.h"
#include "cwLog.h"
#include "cwCommonImpl.h"
#include "cwMath.h"
#include "cwMem.h"
#include <algorithm>


// TODO: rewrite to avoid copying
// this code comes via csound source ...
double 		cw::math::x80ToDouble( unsigned char rate[10] )
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

void cw::math::doubleToX80(double val, unsigned char rate[10])
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

bool		cw::math::isPowerOfTwo( unsigned x )
{
  return x==1 || (!( (x < 2) || (x & (x-1)) ));
}

unsigned 	cw::math::nextPowerOfTwo(	unsigned val )
{
  unsigned i;
	unsigned mask 	= 1;
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

unsigned cw::math::nearPowerOfTwo( unsigned i )
{
  unsigned vh = nextPowerOfTwo(i);

  if( vh == 2 )
    return vh;

  unsigned vl = vh / 2;

  if( vh - i < i - vl )
    return vh;
  return vl;
}

bool     cw::math::isOddU(    unsigned v ) { return v % 2 == 1; }
bool     cw::math::isEvenU(   unsigned v ) { return !isOddU(v); }
unsigned cw::math::nextOddU(  unsigned v ) { return isOddU(v)  ? v : v+1; }
unsigned cw::math::prevOddU(  unsigned v ) { return isOddU(v)  ? v : v-1; }
unsigned cw::math::nextEvenU( unsigned v ) { return isEvenU(v) ? v : v+1; }
unsigned cw::math::prevEvenU( unsigned v ) { return isEvenU(v) ? v : v-1; }

unsigned cw::math::modIncr(int idx, int delta, int maxN )
{
  int sum = idx + delta;

  if( sum >= maxN )
    return sum - maxN;

  if( sum < 0 )
    return maxN + sum;

  return sum;
}


unsigned cw::math::hzToMidi( double hz )
{

  float midi = 12.0 * std::log2(hz/13.75) + 9;

  if( midi < 0 )
    midi = 0;
  if( midi > 127 )
    midi = 127;

  return (unsigned)lround(midi);
}

float    cw::math::midiToHz( unsigned midi )
{
  double m = midi <= 127 ? midi : 127;
  
  return (float)( 13.75 * pow(2.0,(m - 9.0)/12.0)); 
}



//=================================================================
// Random numbers

int      cw::math::randInt( int min, int max )
{
  assert( min <= max );
  int range = max - min;
  return min + std::max(0,std::min(range,(int)round(range * (double)rand() / RAND_MAX)));
}

unsigned cw::math::randUInt( unsigned min, unsigned max )
{
  assert( min <= max );
  unsigned range = max - min;
  unsigned val = (unsigned)round(range * (double)rand() / RAND_MAX);
  return min + std::max((unsigned)0,std::min(range,val));
}

float    cw::math::randFloat( float min, float max )
{
  assert( min <= max );
  float range = max - min;
  float val = (float)(range * (double)rand() / RAND_MAX);
  return min + std::max(0.0f,std::min(range,val));
}

double   cw::math::randDouble( double min, double max )
{
  assert( min <= max );
  double range = max - min;
  double val = range * (double)rand() / RAND_MAX;
  return min + std::max(0.0,std::min(range,val));
}


//=================================================================
// Base on: http://stackoverflow.com/questions/3874627/floating-point-comparison-functions-for-c-sharp

bool cw::math::isCloseD( double x0, double x1, double eps )
{
  double d = fabs(x0-x1);
  
  if( x0 == x1 )
    return true;

  if( x0==0 || x1==0 || d<DBL_MIN )
    return d < (eps * DBL_MIN);

  return (d / std::min( fabs(x0) + fabs(x1), DBL_MAX)) < eps;
}

bool cw::math::isCloseF( float  x0, float  x1, double  eps_d )
{
  float eps = (float)eps_d;
  float d = fabsf(x0-x1);
  
  if( x0 == x1 )
    return true;

  if( x0==0 || x1==0 || d<FLT_MIN )
    return d < (eps * FLT_MIN);

  return (d / std::min( fabsf(x0) + fabsf(x1), FLT_MAX)) < eps;
}

bool cw::math::isCloseI( int x0, int x1, double eps )
{
  if( x0 == x1 )
    return true;
  
  return abs(x0-x1)/(abs(x0)+abs(x1)) < eps;
}


bool cw::math::isCloseU( unsigned x0, unsigned x1, double eps )
{
  if( x0 == x1 )
    return true;
  if( x0 > x1 )
    return (x0-x1)/(x0+x1) < eps;
  else
    return (x1-x0)/(x0+x1) < eps;
}

//=================================================================

// lFSR() implementation based on note at bottom of:
// http://www.ece.u.edu/~koopman/lfsr/index.html
void cw::math::lFSR( unsigned lfsrN, unsigned tapMask, unsigned seed, unsigned* yV, unsigned yN )
{
  assert( 0 < lfsrN && lfsrN < 32 );
  
  unsigned i;
  for(i=0; i<yN; ++i)
  {
    if( (yV[i] = seed & 1)==1 )
      seed = (seed >> 1) ^ tapMask;
    else
      seed = (seed >> 1);

  }
}

namespace cw
{
  namespace math
  {
    bool mLS_IsBalanced( const unsigned* xV, int xN)
    {
      int      a = 0;
      int i;
      
      for(i=0; i<xN; ++i)
        if( xV[i] == 1 )
          ++a;
      
      return abs(a - (xN-a)) == 1;
    }
  }

  unsigned _genGoldCopy( int* y, unsigned yi, unsigned yN, unsigned* x, unsigned xN)
  {
    unsigned i;
    for(i=0; i<xN; ++i,++yi)
      y[yi] = x[i]==1 ? -1 : 1;

    assert(yi <= yN);
    return yi;
  }
  
}


bool cw::math::genGoldCodes( unsigned lfsrN, unsigned poly_coeff0, unsigned poly_coeff1, unsigned goldN, int* yM, unsigned mlsN  )
{
  bool      retFl = true;
  unsigned  yi    = 0;
  unsigned  yN    = goldN * mlsN;
  unsigned* mls0V = mem::allocZ<unsigned>(mlsN);
  unsigned* mls1V = mem::allocZ<unsigned>(mlsN);
  unsigned* xorV  = mem::allocZ<unsigned>(mlsN);
  
  unsigned  i,j;
  
  lFSR(lfsrN, poly_coeff0, 1 << (lfsrN-1), mls0V, mlsN);

  lFSR(lfsrN, poly_coeff1, 1 << (lfsrN-1), mls1V, mlsN);

  if( mLS_IsBalanced(mls0V,mlsN) )
    yi = _genGoldCopy(yM, yi, yN, mls0V, mlsN);

  if( yi<yN && mLS_IsBalanced(mls1V,mlsN) )
    yi = _genGoldCopy(yM, yi, yN, mls1V, mlsN);

  
  for(i=0;  yi < yN && i<mlsN-1; ++i )
  {
    for(j=0; j<mlsN; ++j)
      xorV[j] = (mls0V[j] + mls1V[ (i+j) % mlsN ]) % 2;
    
    if( mLS_IsBalanced(xorV,mlsN) )
      yi = _genGoldCopy(yM,yi,yN,xorV,mlsN);
  }

  if(yi < yN )
  {    
    //rc = errMsg(err,kOpFailAtRC,"Gold code generation failed.  Insuffient balanced pairs.");
    retFl = false;
  }
  
  mem::release(mls0V);
  mem::release(mls1V);
  mem::release(xorV);

  return retFl;

}

bool  cw::math::lFSR_Test()
{
  // lfsrN          = 5;   % 5    6    7;
  // poly_coeff0    = 0x12;  % 0x12 0x21 0x41;
  // poly_coeff1    = 0x1e;  % 0x1e 0x36 0x72;

  unsigned lfsrN = 7;
  unsigned pc0   = 0x41;
  unsigned pc1   = 0x72;
  unsigned mlsN    = (1 << lfsrN)-1;

  unsigned yN = mlsN*2;
  unsigned yV[ yN ];
  unsigned i;

  lFSR( lfsrN, pc0, 1 << (lfsrN-1), yV, yN );

  for(i=0; i<mlsN; ++i)
    if( yV[i] != yV[i+mlsN] )
      return false;

  //atVOU_PrintL(NULL,"0x12",yV,mlsN,2);

  lFSR( lfsrN, pc1, 1 << (lfsrN-1), yV, yN );

  //atVOU_PrintL(NULL,"0x17",yV,mlsN,2);

  for(i=0; i<mlsN; ++i)
    if( yV[i] != yV[i+mlsN] )
      return false;

  return true;
}





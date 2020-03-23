#ifndef cwSerialPortDecls_H
#define cwSerialPortDecls_H

namespace cw
{
  namespace serialPort
  {
    enum
    {
     kDataBits5Fl 	= 0x0001,
     kDataBits6Fl 	= 0x0002,
     kDataBits7Fl 	= 0x0004,
     kDataBits8Fl 	= 0x0008,
     kDataBitsMask	= 0x000f,
   
     k1StopBitFl		= 0x0010,
     k2StopBitFl 	  = 0x0020,
   
     kEvenParityFl	= 0x0040,
     kOddParityFl	  = 0x0080,
     kNoParityFl		= 0x0000,
     /*
       kCTS_OutFlowCtlFl	= 0x0100,
       kRTS_InFlowCtlFl	= 0x0200,
       kDTR_InFlowCtlFl	= 0x0400,
       kDSR_OutFlowCtlFl	= 0x0800,
       kDCD_OutFlowCtlFl	= 0x1000
     */

     kDefaultCfgFlags = kDataBits8Fl | k1StopBitFl | kNoParityFl
    };
  }
}

#endif

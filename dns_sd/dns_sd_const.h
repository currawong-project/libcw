//| Copyright: (C) 2020-2024 Kevin Larke <contact AT larke DOT org> 
//| License: GNU GPL version 3.0 or above. See the accompanying LICENSE file.
#ifndef dns_sd_const_h
#define dns_sd_const_h


enum
  {
   kA_DnsTId     = 1,
   kPTR_DnsTId   = 12,
   kTXT_DnsTId   = 16,
   kAAAA_DnsTId  = 28,
   kSRV_DnsTId   = 33,
   kOPT_DnsTId   = 41,
   kNSEC_DnsTId  = 47,
   kANY_DnsTId   = 255
  };

  enum
  {
   kReplyHdrDnsFl         = 0x8000,
   kAuthoritativeHdrDnsFl = 0x0400,
   kFlushClassDnsFl       = 0x8000,
   kInClassDnsFl          = 0x0001
  };
  
#endif

//
//  IOSDHostDriverUserConfigs.hpp
//  RealtekCardReader
//
//  Created by FireWolf on 8/20/21.
//

#ifndef IOSDHostDriverUserConfigs_hpp
#define IOSDHostDriverUserConfigs_hpp

#include <IOKit/IOTypes.h>

/// Boot arguments that customize the card initialization and the host driver behavior
namespace UserConfigs::Card
{
    /// `True` if the card should be initialized at 3.3V
    extern bool InitAt3v3;
    
    /// `True` if the card should be initialized at the default speed mode
    extern bool InitAtDefaultSpeed;
    
    /// `True` if the card should be initialized at the high speed mode
    extern bool InitAtHighSpeed;
    
    /// `True` if the driver should separate each CMD18/25 request into multiple CMD17/24 ones
    extern bool SeparateAccessBlocksRequest;
    
    /// `True` if the driver should not issue the ACMD23 command when processing CMD25 requests
    extern bool NoACMD23;
    
    /// Specify the maximum number of attempts to retry an application command
    extern UInt32 ACMDMaxNumAttempts;

    /// GA403UM: milliseconds to wait after each power-up step (upstream left the bus config's power delay at 0)
    extern UInt32 PowerDelay;

    /// GA403UM: maximum number of CMD8 attempts (each retry sends CMD0 first)
    extern UInt32 CMD8MaxNumAttempts;
}

#endif /* IOSDHostDriverUserConfigs_hpp */

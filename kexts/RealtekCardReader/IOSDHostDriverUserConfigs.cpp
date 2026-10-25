//
//  IOSDHostDriverUserConfigs.cpp
//  RealtekCardReader
//
//  Created by FireWolf on 8/20/21.
//

#include "IOSDHostDriverUserConfigs.hpp"
#include "Utilities.hpp"

/// Boot arguments that customize the card initialization
namespace UserConfigs::Card
{
    /// `True` if the card should be initialized at 3.3V
    bool InitAt3v3 = BootArgs::contains("-iosd3v3");
    
    /// `True` if the card should be initialized at the default speed mode
    bool InitAtDefaultSpeed = BootArgs::contains("-iosddsm");
    
    /// `True` if the card should be initialized at the high speed mode
    bool InitAtHighSpeed = BootArgs::contains("-iosdhsm");
    
    /// `True` if the driver should separate each CMD18/25 request into multiple CMD17/24 ones
    bool SeparateAccessBlocksRequest = BootArgs::contains("-iosdsabr");
    
    /// `True` if the driver should not issue the ACMD23 command when processing CMD25 requests
    bool NoACMD23 = BootArgs::contains("-iosdnoacmd23");
    
    /// Specify the maximum number of attempts to retry an application command
    UInt32 ACMDMaxNumAttempts = max(BootArgs::get("iosdamna", 2), 1);

    /// GA403UM: milliseconds to wait after each power-up step, as Linux's mmc core does (power_delay_ms, 10 ms there).
    /// Without it the RTS525A's card misses CMD8 right after power-up and never leaves the busy state (no HCS in ACMD41).
    UInt32 PowerDelay = min(max(BootArgs::get("iosdpwrd", 20), 1), 500);

    /// GA403UM: maximum number of CMD8 attempts (each retry sends CMD0 first)
    UInt32 CMD8MaxNumAttempts = min(max(BootArgs::get("iosdc8na", 3), 1), 10);
}

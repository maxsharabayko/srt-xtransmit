#include "pkt_base.hpp"

const char* srtx::ctrl_type_str(ctrl_type type)
{
    static const char* type_str[] = {
        "HANDSHAKE",  // 0
        "KEEPALIVE",  // 1
        "ACK",        // 2
        "LOSSREPORT", // 3
        "CGWARN",     // 4
        "SHUTDOWN",   // 5
        "ACKACK",     // 6
        "DROPREQ",    // 7
        "PEERERROR",  // 8
    };

    switch (type)
    {
    case ctrl_type::INVALID:
        return "INVALID";
    case ctrl_type::USERDEFINED:
        return "USERDEFINED";
    case ctrl_type::HANDSHAKE:
    case ctrl_type::KEEPALIVE:
    case ctrl_type::ACK:
    case ctrl_type::LOSSREPORT:
    case ctrl_type::CGWARNING:
    case ctrl_type::SHUTDOWN:
    case ctrl_type::ACKACK:
    case ctrl_type::DROPREQ:
    case ctrl_type::PEERERROR:
        return type_str[(int)type];
    };

    return "ERROR";
}

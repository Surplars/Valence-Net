#include "TcpPipelinedTimerHarness.h"
#include "tcp_timer_peer.h"
using Test=TimerPeer<STcpPipelinedTimerHarness,true>;
int main(){return runTimerScenario<Test>();}

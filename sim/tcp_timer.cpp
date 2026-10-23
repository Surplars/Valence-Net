#include "TcpTimerHarness.h"
#include "tcp_timer_peer.h"
using Test=TimerPeer<STcpTimerHarness>;
int main(){return runTimerScenario<Test>();}

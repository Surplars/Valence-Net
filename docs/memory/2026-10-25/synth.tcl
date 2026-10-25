set root [lindex $argv 0]
set_param general.maxThreads 8
read_verilog -sv [glob $root/queue/*.sv]
synth_design -top MemoryQueueHarness -part xczu15eg-ffvb1156-2-i
report_utilization -file $root/queue_utilization.rpt
write_verilog -force -mode funcsim $root/queue_netlist.v
close_design
read_verilog -sv [glob $root/ultra/*.sv]
synth_design -top UltraStoreProbe -part xczu15eg-ffvb1156-2-i
report_utilization -file $root/ultra_utilization.rpt
write_verilog -force -mode funcsim $root/ultra_netlist.v
close_design

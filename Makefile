MILL ?= mill
.PHONY: compile test-app test-wire rtl snapshot
compile:
	$(MILL) -i hw.compile
test-app:
	python3 sim/build.py app
	python3 sim/test_app.py
	python3 sim/test_protocol_negative.py
test-wire:
	python3 sim/build.py gmii
	sudo python3 sim/test_tap.py
rtl:
	$(MILL) -i hw.runMain iperf.Emit board build/board
snapshot:
	python3 fpga/prepare.py

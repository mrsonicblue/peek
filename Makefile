.PHONY: all release

all:
	$(MAKE) -C src
	$(MAKE) -C fs

clean:
	$(MAKE) -C src clean
	$(MAKE) -C fs clean
	$(Q)rm -rf release

release:
	$(Q)mkdir -p release/peek
	$(Q)cp src/peek fs/peekfs S99peek import.txt release/peek/
	$(Q)cd release && $(Q)tar cvfz peek.tgz peek
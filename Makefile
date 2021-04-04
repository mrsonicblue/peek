.PHONY: all

all:
	$(MAKE) -C src
	$(MAKE) -C fs

clean:
	$(MAKE) -C src clean
	$(MAKE) -C fs clean

release:
	$(Q)mkdir -p release
	$(Q)cp src/peek release/
	$(Q)cp fs/peekfs release/
	$(Q)cp S99peek release/
	$(Q)cp import.txt release/
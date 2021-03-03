.PHONY: all

all:
	$(MAKE) -C src
	$(MAKE) -C fs

clean:
	$(MAKE) -C src clean
	$(MAKE) -C fs clean
.PHONY: all debug release clean

all: release

debug:
	@mkdir -p build-debug
	cd build-debug && cmake -DCMAKE_BUILD_TYPE=Debug .. && $(MAKE)

release:
	@mkdir -p build-release
	cd build-release && cmake -DCMAKE_BUILD_TYPE=Release .. && $(MAKE)

clean:
	rm -rf build-debug build-release

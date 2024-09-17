NINJA := $(shell command -v ninja 2> /dev/null)
ifdef NINJA
    NINJAGEN := "-G Ninja"
else
    NINJAGEN := ""
endif

all: clangnosan clangsan gccnosan gccsan # clangmodules

buildgcc/nosan/:
	CXXFLAGS="" CXX=g++ cmake $(NINJAGEN) -S test -B buildgcc/nosan/ -DCMAKE_BUILD_TYPE=Debug

buildgcc/test/:
	CXXFLAGS="" CXX=g++ cmake $(NINJAGEN) -S test -B buildgcc/test/ -DCMAKE_BUILD_TYPE=Debug -DUSE_SANITIZER='Address;Undefined' -DPOLYMATHNOEXPLICITSIMDARRAY=OFF

buildclang/nosan/:
	CXXFLAGS="" CXX=clang++ cmake $(NINJAGEN) -S test -B buildclang/nosan/ -DCMAKE_BUILD_TYPE=Debug

buildclang/test/:
	CXXFLAGS="" CXX=clang++ cmake $(NINJAGEN) -S test -B buildclang/test/ -DCMAKE_BUILD_TYPE=Debug -DUSE_SANITIZER='Address;Undefined'
	
buildclang/modules/:
	CXXFLAGS="" CXX=clang++ cmake $(NINJAGEN) -S test -B buildclang/modules/ -DCMAKE_BUILD_TYPE=Debug -DUSE_MODULES=ON

buildgcc/modules/:
	CXXFLAGS="" CXX=g++ cmake $(NINJAGEN) -S test -B buildgcc/modules/ -DCMAKE_BUILD_TYPE=Debug -DUSE_MODULES=ON

gccnosan: buildgcc/nosan/
	cmake --build buildgcc/nosan/
	cmake --build buildgcc/nosan/ --target test

gccsan: buildgcc/test/
	cmake --build buildgcc/test/ 
	cmake --build buildgcc/test/ --target test

clangnosan: buildclang/nosan/
	cmake --build buildclang/nosan/
	cmake --build buildclang/nosan/ --target test

clangsan: buildclang/test/
	cmake --build buildclang/test/ 
	cmake --build buildclang/test/ --target test

clangmodules: buildclang/modules/
	cmake --build buildclang/modules/
	cmake --build buildclang/modules/ --target test

gccmodules: buildgcc/modules/
	cmake --build buildgcc/modules/
	cmake --build buildgcc/modules/ --target test


